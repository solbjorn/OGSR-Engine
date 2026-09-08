// dx10HW.cpp: implementation of the DX10 specialisation of CHW.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "../xrRender/HW.h"

#include "../../xr_3da/XR_IOConsole.h"
#include "../../xr_3da/xr_input.h"
#include "../../Include/xrAPI/xrAPI.h"

#include "StateManager/dx10SamplerStateCache.h"
#include "StateManager/dx10StateCache.h"

#include "sleep.h"

#include <dxgi1_6.h>

namespace
{
void fill_vid_mode_list(CHW* _hw);
void free_vid_mode_list();

[[nodiscard]] constexpr u32 get_swapchain_flags(bool wnd, bool vrr)
{
    return DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | (DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT * !!wnd) | (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING * !!vrr);
}
} // namespace

CHW HW;

CHW::CHW()
{
    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);

    DEVMODE dmi{};
    EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dmi);
    psCurrentVidMode[0] = dmi.dmPelsWidth;
    psCurrentVidMode[1] = dmi.dmPelsHeight;
}

CHW::~CHW()
{
    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void CHW::CreateD3D()
{
    XR_ASSERT(xr::hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_pFactory))));

    Microsoft::WRL::ComPtr<::IDXGIAdapter1> adapter;
    UINT i = 0;

    while (m_pFactory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC desc{};
        adapter->GetDesc(&desc);

        Msg("* Avail GPU [vendor:{:X}]-[device:{:X}]: {}", desc.VendorId, desc.DeviceId, sf::String{desc.Description});
        ++i;
    }

    // In the Windows 10 April 2018 Update, there is now a new IDXGIFactory6 interface that supports
    // a new EnumAdapterByGpuPreference method which lets you enumerate adapters by 'max performance' or 'minimum power'
    IDXGIFactory6* pFactory6 = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory6))))
    {
        pFactory6->EnumAdapterByGpuPreference(0, ::DXGI_GPU_PREFERENCE::DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
        _RELEASE(pFactory6);

        XR_LOG_NOTICE("Using DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE");
    }
    else
    {
        m_pFactory->EnumAdapters1(0, adapter.ReleaseAndGetAddressOf());
        XR_LOG_NOTICE("Using EnumAdapters1(0)");
    }

    XR_ASSERT(xr::hr(adapter.As(&m_pAdapter)));

    // when using FLIP_* present modes, to disable DWM vsync we have to use DXGI_PRESENT_ALLOW_TEARING with ->Present()
    // when vsync is off (PresentInterval = 0) and only when in window mode
    // store whether we can use the flag for later use (swapchain creation, buffer resize, present call)

    // TODO: On some PC configurations (versions of Windows, graphics drivers, currently unknown what exactly) this isn't
    // sufficient to disable the DWM vsync when the game is launched in a windowed mode, however if the user switches from
    // borderless/windowed -> exclusive fullscreen -> borderless/windowed then it seems to work correctly.
    // Worth investigating why this is occurring
    //
    // Configuration where this occurs:
    // - Windows 10 Enterprise LTSC, 21H2, 19044.4894
    // - NVIDIA driver version 560.94
    IDXGIFactory5* factory5{};
    auto hr = m_pFactory->QueryInterface(IID_PPV_ARGS(&factory5));

    if (SUCCEEDED(hr) && factory5 != nullptr)
    {
        BOOL supports_vrr{};

        hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supports_vrr, sizeof(supports_vrr));
        m_SupportsVRR = SUCCEEDED(hr) && supports_vrr;

        _RELEASE(factory5);
    }
    else
    {
        m_SupportsVRR = false;
    }
}

void CHW::DestroyD3D()
{
    _SHOW_REF("refCount:m_pAdapter", m_pAdapter);
    m_pAdapter.Reset();

    _SHOW_REF("refCount:m_pFactory", m_pFactory);
    _RELEASE(m_pFactory);
}

tmc::task<void> CHW::CreateDevice(HWND wnd, u32& dwWidth, u32& dwHeight)
{
    m_hWnd = wnd;
    CreateD3D();

    // General - select adapter and device
    BOOL bWindowed = (g_screenmode != 2);

    // Display the name of video board
    DXGI_ADAPTER_DESC1 Desc{};
    XR_ASSERT(xr::hr(m_pAdapter->GetDesc1(&Desc)));

    DumpVideoMemoryUsage();

    //	Warning: Desc.Description is wide string
    Msg("* Selected GPU [vendor:{:X}]-[device:{:X}]: {}", Desc.VendorId, Desc.DeviceId, sf::String{Desc.Description});

    Caps.id_vendor = Desc.VendorId;
    Caps.id_device = Desc.DeviceId;

    // Select back-buffer & depth-stencil format
    D3DFORMAT& fTarget = Caps.fTarget;
    D3DFORMAT& fDepth = Caps.fDepth;

    //	HACK: DX10: Embed hard target format.
    fTarget = D3DFMT_A8R8G8B8;
    fDepth = D3DFMT_D24S8;

    // Set up the presentation parameters
    DXGI_SWAP_CHAIN_DESC1& sd = m_ChainDesc;
    std::memset(&sd, 0, sizeof(sd));

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC& sd_fullscreen = m_ChainDescFullscreen;
    std::memset(&sd_fullscreen, 0, sizeof(sd_fullscreen));

    selectResolution(sd.Width, sd.Height, bWindowed);
    sd_fullscreen.Windowed = bWindowed;

    // Back buffer
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferCount = psDeviceFlags.test(rs_triple_buffering) ? 3 : 2;

    // With the DXGI_SWAP_EFFECT_FLIP_DISCARD mode, only the 0th back buffer
    // is accessible to the application.
    BackBufferCount = 1;

    // Multisample
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    // Required for HDR, provides better performance in windowed/borderless mode
    // https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // Refresh rate
    if (bWindowed)
    {
        sd_fullscreen.RefreshRate.Numerator = 60;
        sd_fullscreen.RefreshRate.Denominator = 1;
    }
    else
    {
        sd_fullscreen.RefreshRate = selectRefresh(sd.Width, sd.Height, sd.Format);
    }

    //	Additional set up
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.Flags = get_swapchain_flags(bWindowed, m_SupportsVRR);

    UINT create_device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef DEBUG
    if (xr::is_debugger_present())
        // enables d3d11 debug layer validation and output
        // viewable in VS debugger `Output > Debug` view or using a tool like Sysinternals DebugView
        create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // create device
    Microsoft::WRL::ComPtr<::ID3D11Device> device;
    Microsoft::WRL::ComPtr<::ID3D11DeviceContext> context;

    // Если мы выбираем конкретный адаптер, то мы обязаны использовать D3D_DRIVER_TYPE_UNKNOWN.
    XR_ASSERT(xr::hr(::D3D11CreateDevice(m_pAdapter.Get(), ::D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_UNKNOWN, nullptr, create_device_flags, nullptr, 0,
                                         D3D11_SDK_VERSION, &device, &FeatureLevel, &context)));
    // На всякий случай
    XR_ASSERT(FeatureLevel >= D3D_FEATURE_LEVEL_11_0);

    XR_ASSERT(xr::hr(device.As(&pDevice)));
    XR_ASSERT(xr::hr(context.As(&contexts_pool[R__IMM_CTX_ID])));

    D3D11_FEATURE_DATA_D3D11_OPTIONS options;
    pDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));

    D3D11_FEATURE_DATA_DOUBLES doubles;
    pDevice->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles));

    DoublePrecisionFloatShaderOps = doubles.DoublePrecisionFloatShaderOps;
    SAD4ShaderInstructions = options.SAD4ShaderInstructions;
    ExtendedDoublesShaderInstructions = options.ExtendedDoublesShaderInstructions;

    for (auto& ctx : contexts_pool | std::views::drop(R__PARALLEL_CTX_ID))
        XR_ASSERT(xr::hr(pDevice->CreateDeferredContext1(0, &ctx)));

    // create swapchain
    auto scope = co_await tmc::enter(xr::tmc_cpu_st_executor());
    XR_ASSERT(xr::hr(m_pFactory->CreateSwapChainForHwnd(pDevice.Get(), m_hWnd, &sd, &sd_fullscreen, nullptr, &m_pSwapChain)));
    co_await scope.exit();

    IDXGISwapChain3* swapchain3;
    XR_ASSERT(xr::hr(m_pSwapChain->QueryInterface(IID_PPV_ARGS(&swapchain3))));

    // setup colorspace
    XR_ASSERT(xr::hr(swapchain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)));

    if (bWindowed)
    {
        XR_ASSERT(xr::hr(swapchain3->SetMaximumFrameLatency(sd.BufferCount - 1)));
        waitable = swapchain3->GetFrameLatencyWaitableObject();
    }
    else
    {
        Microsoft::WRL::ComPtr<::IDXGIDevice1> dxgi;
        XR_ASSERT(xr::hr(pDevice.As(&dxgi)));
        XR_ASSERT(xr::hr(dxgi->SetMaximumFrameLatency(sd.BufferCount - 1)));
    }

    _RELEASE(swapchain3);
    _SHOW_REF("* CREATE: DeviceREF:", pDevice);

    //	Create render target and depth-stencil views here

    // NOTE: this seems required to get the default render target to match the swap chain resolution
    // probably the sequence ResizeTarget, and ResizeBuffers is important
    co_await Reset(wnd, dwWidth, dwHeight);
    fill_vid_mode_list(this);

    Device.update_dpi_scale();
    imgui_init();
}

tmc::task<void> CHW::DestroyDevice()
{
    imgui_shutdown();

    //	Destroy state managers
    RSManager.ClearStateArray();
    DSSManager.ClearStateArray();
    BSManager.ClearStateArray();
    SSManager.ClearStateArray();

    if (waitable != nullptr)
        CloseHandle(waitable);

    // Must switch to windowed mode to release swap chain
    if (!m_ChainDescFullscreen.Windowed)
    {
        auto scope = co_await tmc::enter(xr::tmc_cpu_st_executor());
        m_pSwapChain->SetFullscreenState(false, nullptr);
        co_await scope.exit();
    }

    _SHOW_REF("refCount:m_pSwapChain", m_pSwapChain);
    _RELEASE(m_pSwapChain);

    for (auto& ctx : std::views::reverse(contexts_pool))
        ctx.Reset();

    _SHOW_REF("DeviceREF:", pDevice);
    pDevice.Reset();

    DestroyD3D();
    free_vid_mode_list();
}

//////////////////////////////////////////////////////////////////////
// Resetting device
//////////////////////////////////////////////////////////////////////

tmc::task<void> CHW::Reset(HWND wnd, u32& dwWidth, u32& dwHeight)
{
    imgui_reset();

    DXGI_SWAP_CHAIN_DESC1& cd = m_ChainDesc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC& cd_fs = m_ChainDescFullscreen;

    BOOL bWindowed = (g_screenmode != 2);
    cd_fs.Windowed = bWindowed;

    if (!bWindowed)
    {
        auto scope = co_await tmc::enter(xr::tmc_cpu_st_executor());
        m_pSwapChain->SetFullscreenState(true, nullptr);
        co_await scope.exit();
    }

    selectResolution(cd.Width, cd.Height, bWindowed);

    if (bWindowed)
    {
        cd_fs.RefreshRate.Numerator = 60;
        cd_fs.RefreshRate.Denominator = 1;
    }
    else
    {
        cd_fs.RefreshRate = selectRefresh(cd.Width, cd.Height, cd.Format);
    }

    co_await tmc::resume_on(xr::tmc_cpu_st_executor());

    DXGI_MODE_DESC mode{};
    mode.Width = cd.Width;
    mode.Height = cd.Height;
    mode.Format = cd.Format;
    mode.RefreshRate = cd_fs.RefreshRate;

    XR_ASSERT(xr::hr(m_pSwapChain->ResizeTarget(&mode)));
    XR_ASSERT(xr::hr(m_pSwapChain->ResizeBuffers(cd.BufferCount, cd.Width, cd.Height, cd.Format, get_swapchain_flags(cd_fs.Windowed, m_SupportsVRR))));

    updateWindowProps(wnd);
    dwWidth = cd.Width;
    dwHeight = cd.Height;
}

void CHW::selectResolution(u32& dwWidth, u32& dwHeight, BOOL bWindowed)
{
    fill_vid_mode_list(this);

    if (psCurrentVidMode[0] == 0 || psCurrentVidMode[1] == 0)
        GetMonitorResolution(psCurrentVidMode[0], psCurrentVidMode[1]);

    if (bWindowed)
    {
        dwWidth = psCurrentVidMode[0];
        dwHeight = psCurrentVidMode[1];
        return;
    }

    string64 buff;
    xr_sprintf(buff, "%dx%d", psCurrentVidMode[0], psCurrentVidMode[1]);

    if (_ParseItem(buff, vid_mode_token) == std::numeric_limits<u32>::max()) // not found
    {
        // select safe
        xr_sprintf(buff, "vid_mode %s", vid_mode_token[0].name);
        Console->Execute(buff);
    }

    dwWidth = psCurrentVidMode[0];
    dwHeight = psCurrentVidMode[1];
}

DXGI_RATIONAL CHW::selectRefresh(u32 dwWidth, u32 dwHeight, DXGI_FORMAT fmt)
{
    Microsoft::WRL::ComPtr<IDXGIOutput> pOutput;
    XR_ASSERT(xr::hr(m_pAdapter->EnumOutputs(0, &pOutput)));

    // Get the number of display modes available
    u32 num;
    pOutput->GetDisplayModeList(fmt, 0, &num, nullptr);

    // Get the list of display modes
    xr_vector<DXGI_MODE_DESC> modes(num);
    pOutput->GetDisplayModeList(fmt, 0, &num, modes.data());

    DXGI_RATIONAL res{.Numerator = 60, .Denominator = 1};
    refresh_rate = 60.0f;

    for (const auto& desc : modes)
    {
        if (desc.Width != dwWidth || desc.Height != dwHeight)
            continue;

        if (const auto freq = gsl::narrow_cast<f32>(desc.RefreshRate.Numerator) / gsl::narrow_cast<f32>(XR_ASSERT_VAL(desc.RefreshRate.Denominator > 0));
            freq > refresh_rate)
        {
            res = desc.RefreshRate;
            refresh_rate = freq;
        }
    }

    return res;
}

tmc::task<void> CHW::OnAppActivate()
{
    if (m_pSwapChain == nullptr || m_ChainDescFullscreen.Windowed)
        co_return;

    co_await tmc::resume_on(xr::tmc_cpu_st_executor());

    ShowWindow(m_hWnd, SW_RESTORE);
    m_pSwapChain->SetFullscreenState(true, nullptr);

    const auto& cd = m_ChainDesc;
    m_pSwapChain->ResizeBuffers(cd.BufferCount, cd.Width, cd.Height, cd.Format, get_swapchain_flags(false, m_SupportsVRR));
}

tmc::task<void> CHW::OnAppDeactivate()
{
    if (m_pSwapChain == nullptr || m_ChainDescFullscreen.Windowed)
        co_return;

    co_await tmc::resume_on(xr::tmc_cpu_st_executor());

    m_pSwapChain->SetFullscreenState(false, nullptr);

    const auto& cd = m_ChainDesc;
    m_pSwapChain->ResizeBuffers(cd.BufferCount, cd.Width, cd.Height, cd.Format, get_swapchain_flags(false, m_SupportsVRR));

    ShowWindow(m_hWnd, SW_MINIMIZE);
}

void CHW::DumpVideoMemoryUsage() const
{
    DXGI_ADAPTER_DESC1 Desc{};
    XR_ASSERT(xr::hr(m_pAdapter->GetDesc1(&Desc)));

    DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo{};

    if (xr::hr(m_pAdapter->QueryVideoMemoryInfo(0, ::DXGI_MEMORY_SEGMENT_GROUP::DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo)))
    {
        Msg("\tDedicated VRAM: {} MB ({} bytes)\n\tDedicated Memory: {} MB ({} bytes)\n\tShared Memory: {} MB ({} bytes)\n\tCurrentUsage: {} MB ({} "
            "bytes)\n\tBudget: {} MB ({} bytes)",
            Desc.DedicatedVideoMemory / 1024 / 1024, Desc.DedicatedVideoMemory, Desc.DedicatedSystemMemory / 1024 / 1024, Desc.DedicatedSystemMemory,
            Desc.SharedSystemMemory / 1024 / 1024, Desc.SharedSystemMemory, videoMemoryInfo.CurrentUsage / 1024 / 1024, videoMemoryInfo.CurrentUsage,
            videoMemoryInfo.Budget / 1024 / 1024, videoMemoryInfo.Budget);
    }
    else
    {
        Msg("\tDedicated VRAM: {} MB ({} bytes)\n\tDedicated Memory: {} MB ({} bytes)\n\tShared Memory: {} MB ({} bytes)",
            Desc.DedicatedVideoMemory / 1024 / 1024, Desc.DedicatedVideoMemory, Desc.DedicatedSystemMemory / 1024 / 1024, Desc.DedicatedSystemMemory,
            Desc.SharedSystemMemory / 1024 / 1024, Desc.SharedSystemMemory);
    }
}

void CHW::updateWindowProps(HWND m_hWnd) const
{
    BOOL bWindowed = g_screenmode != 2;
    LONG_PTR dwWindowStyle = 0;
    // Set window properties depending on what mode were in.
    if (bWindowed)
    {
        dwWindowStyle = WS_BORDER | WS_VISIBLE;
        if (!strstr(Core.Params, "-no_dialog_header"))
            dwWindowStyle |= WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX;
        SetWindowLongPtr(m_hWnd, GWL_STYLE, dwWindowStyle);
        // When moving from fullscreen to windowed mode, it is important to
        // adjust the window size after recreating the device rather than
        // beforehand to ensure that you get the window size you want.  For
        // example, when switching from 640x480 fullscreen to windowed with
        // a 1000x600 window on a 1024x768 desktop, it is impossible to set
        // the window size to 1000x600 until after the display mode has
        // changed to 1024x768, because windows cannot be larger than the
        // desktop.

        RECT m_rcWindowBounds{};
        int fYOffset = 0;

        static const bool bCenter = !!strstr(Core.Params, "-center_screen");
        if (bCenter)
        {
            RECT DesktopRect;

            GetClientRect(GetDesktopWindow(), &DesktopRect);
            SetRect(&m_rcWindowBounds, (DesktopRect.right - m_ChainDesc.Width) / 2, (DesktopRect.bottom - m_ChainDesc.Height) / 2,
                    (DesktopRect.right + m_ChainDesc.Width) / 2, (DesktopRect.bottom + m_ChainDesc.Height) / 2);
        }
        else
        {
            if (dwWindowStyle & WS_DLGFRAME)
                fYOffset = GetSystemMetrics(SM_CYCAPTION); // size of the window title bar

            SetRect(&m_rcWindowBounds, 0, 0, m_ChainDesc.Width, m_ChainDesc.Height);
        }

        AdjustWindowRect(&m_rcWindowBounds, DWORD(dwWindowStyle), FALSE);

        SetWindowPos(m_hWnd, HWND_NOTOPMOST, m_rcWindowBounds.left, m_rcWindowBounds.top + fYOffset, m_rcWindowBounds.right - m_rcWindowBounds.left,
                     m_rcWindowBounds.bottom - m_rcWindowBounds.top, SWP_SHOWWINDOW | SWP_NOCOPYBITS | SWP_DRAWFRAME);
    }
    else
    {
        SetWindowLongPtr(m_hWnd, GWL_STYLE, dwWindowStyle = (WS_POPUP | WS_VISIBLE));
    }

    SetForegroundWindow(m_hWnd);
    pInput->clip_cursor(true);
}

namespace
{
struct _uniq_mode final
{
    gsl::czstring _val;

    constexpr explicit _uniq_mode(gsl::czstring v) : _val{v} {}

    [[nodiscard]] constexpr bool operator()(gsl::czstring _other) { return std::is_eq(xr::strcasecmp(_val, _other)); }
};

void free_vid_mode_list()
{
    for (int i = 0; vid_mode_token[i].name; i++)
    {
        auto name = const_cast<gsl::zstring>(vid_mode_token[i].name);
        xr_free(name);
    }

    xr_free(vid_mode_token);
    vid_mode_token = nullptr;
}

void fill_vid_mode_list(CHW* _hw)
{
    if (vid_mode_token)
        return;

    xr_vector<LPCSTR> _tmp;
    xr_vector<DXGI_MODE_DESC> modes;

    IDXGIOutput* pOutput;
    XR_ASSERT(xr::hr(_hw->m_pAdapter->EnumOutputs(0, &pOutput)));

    UINT num = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT flags = 0;

    // Get the number of display modes available
    pOutput->GetDisplayModeList(format, flags, &num, nullptr);

    // Get the list of display modes
    modes.resize(num);
    pOutput->GetDisplayModeList(format, flags, &num, &modes.front());

    _RELEASE(pOutput);

    for (u32 i = 0; i < num; ++i)
    {
        DXGI_MODE_DESC& desc = modes[i];
        string32 str;

        if (desc.Width < 800)
            continue;

        xr_sprintf(str, sizeof(str), "%dx%d", desc.Width, desc.Height);

        if (_tmp.end() != std::find_if(_tmp.begin(), _tmp.end(), _uniq_mode(str)))
            continue;

        _tmp.emplace_back(nullptr);
        _tmp.back() = xr_strdup(str);
    }

    u32 _cnt = _tmp.size() + 1;

    vid_mode_token = xr_alloc<xr_token>(_cnt);

    vid_mode_token[_cnt - 1].id = -1;
    vid_mode_token[_cnt - 1].name = nullptr;

#ifdef DEBUG
    Msg("Available video modes[{}]:", _tmp.size());
#endif // DEBUG

    for (u32 i = 0; i < _tmp.size(); ++i)
    {
        vid_mode_token[i].id = i;
        vid_mode_token[i].name = _tmp[i];

#ifdef DEBUG
        Msg("[{}]", _tmp[i]);
#endif // DEBUG
    }
}

constexpr DXGI_PRESENT_PARAMETERS update_whole{};
} // namespace

tmc::task<void> CHW::Present()
{
    XR_TRACY_ZONE_SCOPED();

    u32 present_flags{DXGI_PRESENT_DO_NOT_WAIT};
    bool use_vsync = !!psDeviceFlags.test(rsVSync);
    UINT present_interval = (use_vsync) ? 1 : 0;

    // NOTE: https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
    BOOL is_windowed = m_ChainDescFullscreen.Windowed;
    if (is_windowed && !use_vsync && m_SupportsVRR)
        present_flags |= DXGI_PRESENT_ALLOW_TEARING;

    // --#SM+#-- +SecondVP+ Не выводим кадр из второго вьюпорта на экран (на практике у нас экранная картинка обновляется минимум
    // в два раза реже) [don't flush image into display for SecondVP-frame]
    if (Device.m_SecondViewport.IsSVPFrame() || Device.m_SecondViewport.m_bCamReady)
        co_return;

    HRESULT res;

    Device.Statistic->RenderDUMP_Wait_P.Begin();

    std::__libcpp_thread_poll_with_backoff(
        [this, &res, present_interval, present_flags] -> bool {
            res = m_pSwapChain->Present1(present_interval, present_flags, &update_whole);
            return res != DXGI_ERROR_WAS_STILL_DRAWING;
        },
        xr::backoff, std::chrono::milliseconds{ps_r2_wait_timeout});

    Device.Statistic->RenderDUMP_Wait_P.End();

    switch (res)
    {
    case DXGI_STATUS_OCCLUDED:
    case DXGI_ERROR_DEVICE_REMOVED: doPresentTest = true; break;
    }

    if (++CurrentBackBuffer == BackBufferCount)
        CurrentBackBuffer = 0;
}

tmc::task<DeviceState> CHW::GetDeviceState()
{
    XR_TRACY_ZONE_SCOPED();

    if (waitable == nullptr)
        goto out;

    Device.Statistic->RenderDUMP_Wait_W.Begin();
    std::__libcpp_thread_poll_with_backoff([this] -> bool { return WaitForSingleObjectEx(waitable, 0, true) == WAIT_OBJECT_0; }, xr::backoff,
                                           std::chrono::milliseconds{ps_r2_wait_timeout});
    Device.Statistic->RenderDUMP_Wait_W.End();

out:
    if (!doPresentTest)
        co_return DeviceState::Normal;

    switch (m_pSwapChain->Present1(0, DXGI_PRESENT_TEST, &update_whole))
    {
    case S_OK: doPresentTest = false; break;
    case DXGI_STATUS_OCCLUDED:
        // Do not render until we become visible again
        co_return DeviceState::Lost;
    case DXGI_ERROR_DEVICE_RESET: co_return DeviceState::NeedReset;
    case DXGI_ERROR_DEVICE_REMOVED: XR_PANIC("graphics driver was updated or GPU was physically removed");
    }

    co_return DeviceState::Normal;
}
