#include "stdafx.h"

#include "device.h"

#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "Render.h"
#include "XR_IOConsole.h"
#include "x_ray.h"
#include "xr_input.h"

#include "sleep.h"

#include <mmsystem.h>
#include <winternl.h>

CRenderDevice Device;
CLoadScreenRenderer load_screen_renderer;

BOOL g_bRendering = FALSE;

// На каком кадре с момента прошлого рендера во второй вьюпорт мы начнём новый (не может быть меньше 2 - каждый второй кадр, чем больше тем более низкий FPS во
// втором вьюпорте)
int g_3dscopes_fps_factor = 2;

BOOL g_bLoaded = FALSE;
float refresh_rate = 0;

tmc::task<bool> CRenderDevice::RenderBegin()
{
    switch (co_await m_pRender->GetDeviceState())
    {
    case DeviceState::Normal: break;
    case DeviceState::Lost:
        // If the device was lost, do not render until we get it back
        co_await tmc::spawn_clang(xr::sleep(std::chrono::milliseconds{33}), xr::tmc_cpu_st_executor());
        co_return false;
    case DeviceState::NeedReset:
        // Check if the device is ready to be reset
        co_await Reset();
        co_return false;
    default: xr::unreachable();
    }

    m_pRender->Begin();
    g_bRendering = true;

    co_return true;
}

void CRenderDevice::Clear() { m_pRender->Clear(); }

tmc::task<void> CRenderDevice::RenderEnd()
{
    if (dwPrecacheFrame)
    {
        ::Sound->set_master_volume(0.f);
        dwPrecacheFrame--;

        if (!load_screen_renderer.b_registered)
            m_pRender->ClearTarget();

        if (0 == dwPrecacheFrame)
        {
            m_pRender->updateGamma();
            ::Sound->set_master_volume(1.f);

            Memory.mem_compact();

            Msg("* MEMORY USAGE: {} K", Memory.mem_usage() / 1024);
            Msg("* End of synchronization A[{}] R[{}]", b_is_Active, b_is_Ready);
        }
    }

    g_bRendering = false;
    co_await m_pRender->End();

    vCameraDirectionSaved = vCameraDirection;
}

void CRenderDevice::PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input)
{
    if (m_pRender->GetForceGPU_REF())
        amount = 0;

    dwPrecacheFrame = dwPrecacheTotal = amount;

    if (amount && b_draw_loadscreen && load_screen_renderer.b_registered == false)
    {
        load_screen_renderer.start(b_wait_user_input);
    }
}

void GetMonitorResolution(u32& horizontal, u32& vertical)
{
    HMONITOR hMonitor = MonitorFromWindow(Device.m_hWnd, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(hMonitor, &mi))
    {
        horizontal = mi.rcMonitor.right - mi.rcMonitor.left;
        vertical = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    else
    {
        RECT desktop;
        const HWND hDesktop = GetDesktopWindow();
        GetWindowRect(hDesktop, &desktop);
        horizontal = desktop.right - desktop.left;
        vertical = desktop.bottom - desktop.top;
    }
}

namespace
{
float GetMonitorRefresh()
{
    DEVMODE lpDevMode;
    memset(&lpDevMode, 0, sizeof(DEVMODE));
    lpDevMode.dmSize = sizeof(DEVMODE);
    lpDevMode.dmDriverExtra = 0;

    if (!EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &lpDevMode))
        return 60.f;

    return lpDevMode.dmDisplayFrequency;
}
} // namespace

void CRenderDevice::update_dpi_scale()
{
    u32 w, h;
    GetMonitorResolution(w, h);

    const auto base = gsl::narrow_cast<f32>(GetDpiForWindow(m_hWnd)) / f32{USER_DEFAULT_SCREEN_DPI};
    if (dwWidth == w && dwHeight == h)
    {
        dpi_scale = base;
        return;
    }

    dpi_scale = base * (gsl::narrow_cast<f32>(dwWidth) / gsl::narrow_cast<f32>(w) + gsl::narrow_cast<f32>(dwHeight) / gsl::narrow_cast<f32>(h)) / 2.0f;
}

void CRenderDevice::CalcFrameStats()
{
    auto& stats = *Statistic;
    stats.RenderTOTAL.FrameEnd();

    // calc FPS & TPS
    if (fTimeDelta <= EPS_S)
    {
    out:
        stats.RenderTOTAL.FrameStart();
        return;
    }

    const float fps = 1.f / fTimeDelta;
    constexpr float fOne = 0.3f;
    constexpr float fInv = 1.f - fOne;
    stats.fFPS = fInv * stats.fFPS + fOne * fps;

    if (stats.RenderTOTAL.result > EPS_S)
    {
        const u32 renderedPolys = m_pRender->GetCacheStatPolys();
        stats.fTPS = fInv * stats.fTPS + fOne * float(renderedPolys) / (stats.RenderTOTAL.result * 1000.f);
        stats.fRFPS = fInv * stats.fRFPS + fOne * 1000.f / stats.RenderTOTAL.result;
    }

    goto out;
}

xr_list<CallMe::Delegate<tmc::task<bool>()>> g_loading_events;

tmc::task<bool> CRenderDevice::BeforeFrame()
{
    if (!b_is_Ready)
    {
        co_await tmc::spawn_clang(xr::sleep(std::chrono::milliseconds{100}), xr::tmc_cpu_st_executor());
        co_return false;
    }

    if (psDeviceFlags.test(rsStatistic))
        g_bEnableStatGather = TRUE;
    else
        g_bEnableStatGather = FALSE;

    if (!g_loading_events.empty())
    {
        if (co_await g_loading_events.front()())
            g_loading_events.pop_front();

        co_await process_frame_async();
        co_await pApp->LoadDraw();

        co_return false;
    }

    co_return true;
}

tmc::task<void> CRenderDevice::OnCameraUpdated()
{
    if (camFrame == dwFrame)
        co_return;

    XR_TRACY_ZONE_SCOPED();

    // Precache
    if (dwPrecacheFrame)
    {
        const float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
        float sin, cos;

        DirectX::XMScalarSinCos(&sin, &cos, PI_MUL_2 * factor);
        vCameraDirection.set(sin, 0, cos);
        vCameraDirection.normalize();
        vCameraTop.set(0, 1, 0);
        vCameraRight.crossproduct(vCameraTop, vCameraDirection);

        mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
    }

    // Matrices
    mFullTransform.mul(mProject, mView);

    mViewHud.build_camera_dir({}, vCameraDirection, vCameraTop);
    mProjectHud.build_projection(deg2rad(psHUD_FOV <= 1.f ? psHUD_FOV * fFOV : psHUD_FOV), fASPECT, HUD_VIEWPORT_NEAR,
                                 g_pGamePersistent->Environment().CurrentEnv->far_plane);

    mInvView.invert(mView);
    mInvFullTransform.invert_44(mFullTransform);

    co_await ::Render->OnCameraUpdated();
    m_pRender->SetCacheXform(mView, mProject);

    camFrame = dwFrame;
}

tmc::task<void> CRenderDevice::ProcessFrame(decltype(std::chrono::high_resolution_clock::now()) start)
{
    if (!co_await BeforeFrame())
        co_return;

    XR_TRACY_ZONE_SCOPED();

    const bool editor = xr::editor() && xr::editor()->opened();
    if (editor)
        m_pRender->editor_new();

    co_await FrameMove();

    if (editor)
    {
        xr::editor()->update();
        m_pRender->editor_end();
    }

    co_await process_frame_async();
    co_await OnCameraUpdated();

    auto second = co_await tmc::fork_clang(process_second(), tmc::current_executor(), xr::tmc_priority_any);

    {
        XR_TRACY_ZONE_SCOPED();

        const bool calc = g_bEnableStatGather;

        g_bEnableStatGather = true;
        Statistic->RenderTOTAL_Real.FrameStart();
        Statistic->RenderTOTAL_Real.Begin();
        g_bEnableStatGather = calc;

        if (b_is_Active && co_await RenderBegin())
        {
            co_await seqRender.process();

            if (editor)
                m_pRender->editor_render();

            CalcFrameStats();

            if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || !Statistic->errors.empty())
                Statistic->Show();
            if (psDeviceFlags.test(rsHWInfo))
                Statistic->Show_HW_Stats();

            co_await RenderEnd();
        }

        g_bEnableStatGather = true;
        Statistic->RenderTOTAL_Real.End();
        Statistic->RenderTOTAL_Real.FrameEnd();
        Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
        g_bEnableStatGather = calc;
    }

#ifdef LOG_SECOND_THREAD_STATS
    const auto FrameEndTime = std::chrono::high_resolution_clock::now();
    const auto SecondThreadTasksElapsedTime = co_await std::move(second);
    const std::chrono::duration<f64, std::milli> FrameElapsedTime = FrameEndTime - start;

    if (SecondThreadTasksElapsedTime > FrameElapsedTime && dwPrecacheFrame == 0) [[unlikely]]
    {
        const std::chrono::duration<f64, std::milli> SecondThreadFreeTime = FrameElapsedTime - SecondThreadTasksElapsedTime;

        Msg("##[{}] Second thread work time is too long! Avail: [{}]ms, used: [{}]ms, free: [{}]ms", std::source_location::current().function_name(),
            FrameElapsedTime, SecondThreadTasksElapsedTime, SecondThreadFreeTime);
    }
#else
    co_await std::move(second);
#endif

    f32 curFPSLimit = gsl::narrow_cast<f32>(ps_framelimiter);
    if (g_pGamePersistent->IsMainMenuActive() || Paused())
    {
        if (fis_zero(refresh_rate)) [[unlikely]]
            refresh_rate = GetMonitorRefresh();

        curFPSLimit = ps_framelimiter > 0 ? std::min(curFPSLimit, refresh_rate) : refresh_rate;
    }

    co_await tmc::resume_on(xr::tmc_cpu_st_executor());

    if (curFPSLimit >= 1.0f && !m_SecondViewport.IsSVPFrame())
    {
        const std::chrono::duration<f64, std::milli> FpsLimitMs{1000.0 / curFPSLimit};
        const auto diff = std::chrono::floor<std::chrono::nanoseconds>(FpsLimitMs) - (std::chrono::high_resolution_clock::now() - start);

        if (diff > std::chrono::nanoseconds{100})
            co_await xr::sleep(diff);
    }

    if (!b_is_Active)
        co_await xr::sleep(std::chrono::milliseconds{1});
}

#ifdef LOG_SECOND_THREAD_STATS
tmc::task<std::chrono::duration<f64, std::milli>> CRenderDevice::process_second()
#else
tmc::task<void> CRenderDevice::process_second()
#endif
{
    XR_TRACY_ZONE_SCOPED();

#ifdef LOG_SECOND_THREAD_STATS
    const auto SecondThreadTasksStartTime = std::chrono::high_resolution_clock::now();
#endif

    auto size = seqParallel.size();
    while (size-- > 0)
    {
        co_await seqParallel.front()();
        seqParallel.pop_front();
    }

    co_await seqFrameMT.process();
    co_await process_frame_async();

#ifdef LOG_SECOND_THREAD_STATS
    co_return std::chrono::high_resolution_clock::now() - SecondThreadTasksStartTime;
#endif
}

tmc::task<void> CRenderDevice::message_loop()
{
    while (true)
    {
        const auto start = std::chrono::high_resolution_clock::now();

        // Process everything before and after WM_INPUT in two passes, yeah MS who needs masks
        u32 min{0}, max{WM_INPUT - 1};
        ::MSG msg;

    again:
        while (::PeekMessageW(&msg, nullptr, min, max, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                co_return;

            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        if (min == 0)
        {
            min = WM_INPUT + 1;
            max = std::numeric_limits<u16>::max();

            goto again;
        }

        pInput->mouse_move();
        co_await tmc::spawn_clang(process_frame_async(), tmc::cpu_executor());

        if (xr::social())
            xr::social()->update();

        update_dpi_scale();

        co_await tmc::spawn_clang(ProcessFrame(start), tmc::cpu_executor());
        XR_TRACY_FRAME_MARK();
    }
}

static void LogOsVersion()
{
    static auto RtlGetVersion = reinterpret_cast<NTSTATUS(WINAPI*)(LPOSVERSIONINFOEXW)>(GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion"));

    if (RtlGetVersion)
    {
        OSVERSIONINFOEXW osInfo{};
        osInfo.dwOSVersionInfoSize = sizeof(osInfo);

        if (NT_SUCCESS(RtlGetVersion(&osInfo)))
        {
            Msg("--OS Version major: [{}] minor: [{}], build: [{}]. Server OS: [{}]", osInfo.dwMajorVersion, osInfo.dwMinorVersion, osInfo.dwBuildNumber,
                osInfo.wProductType != VER_NT_WORKSTATION ? "yes" : "no");
            return;
        }
    }

    Msg("!![{}] Can't get RtlGetVersion", std::source_location::current().function_name());
}

tmc::task<void> CRenderDevice::Run()
{
    //	DUMP_PHASE;
    g_bLoaded = FALSE;

    co_await process_frame_async();

    LogOsVersion();
    XR_LOG_NOTICE("Starting engine...");

    // Startup timers and calculate timer delta
    dwTimeGlobal = 0;
    Timer_MM_Delta = 0;

    {
        const s64 time_mm = timeGetTime();
        while (timeGetTime() == time_mm)
            ; // wait for next tick

        const s64 time_system = timeGetTime();
        const s64 time_local = TimerAsync();
        Timer_MM_Delta = time_system - time_local;
    }

    // Message cycle
    co_await seqAppStart.process();
    m_pRender->ClearTarget();

    co_await tmc::spawn_clang(message_loop(), xr::tmc_cpu_st_executor());

    co_await seqAppEnd.process();
    co_await process_frame_async();
}

namespace
{
u32 app_inactive_time{};
u32 app_inactive_time_start{};
} // namespace

tmc::task<void> CRenderDevice::FrameMove()
{
    XR_TRACY_ZONE_SCOPED();

    dwFrame++;
    dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;

    // Timer
    float fPreviousFrameTime = Timer.GetElapsed_sec();
    Timer.Start(); // previous frame
    fTimeDelta = 0.1f * fTimeDelta + 0.9f * fPreviousFrameTime; // smooth random system activity - worst case ~7% error

    if (fTimeDelta > .1f)
        fTimeDelta = .1f; // limit to 15fps minimum

    if (fTimeDelta <= 0.f)
        fTimeDelta = EPS_S + EPS_S; // limit to 15fps minimum

    if (Paused())
        fTimeDelta = 0.0f;

    fTimeGlobal = TimerGlobal.GetElapsed_sec();
    const u32 _old_global = dwTimeGlobal;
    dwTimeGlobal = TimerGlobal.GetElapsed_ms();
    dwTimeDelta = dwTimeGlobal - _old_global;

    // Frame move
    Statistic->EngineTOTAL.Begin();

    co_await seqFrame.process();
    g_bLoaded = TRUE;

    Statistic->EngineTOTAL.End();
}

BOOL bShowPauseString = TRUE;

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, [[maybe_unused]] LPCSTR reason)
{
    static int snd_emitters_ = -1;

    if (g_bBenchmark)
        return;

    if (bOn)
    {
        if (!Paused())
            bShowPauseString =
#ifdef DEBUG
                !xr_strcmp(reason, "li_pause_key_no_clip") ? FALSE :
#endif // DEBUG
                                                             TRUE;

        if (bTimer)
        {
            g_pauseMngr->Pause(TRUE);
#ifdef DEBUG
            if (!xr_strcmp(reason, "li_pause_key_no_clip"))
                TimerGlobal.Pause(FALSE);
#endif // DEBUG
        }

        if (bSound && ::Sound)
            snd_emitters_ = ::Sound->pause_emitters(true);
    }
    else
    {
        if (bTimer && g_pauseMngr->Paused())
        {
            fTimeDelta = EPS_S + EPS_S;
            g_pauseMngr->Pause(FALSE);
        }

        if (bSound)
        {
            if (snd_emitters_ > 0) // avoid crash
                snd_emitters_ = ::Sound->pause_emitters(false);
            else
                XR_LOG_DYNAMIC_DEBUG(xr::level::Error, "Sound->pause_emitters underflow");
        }
    }
}

bool CRenderDevice::Paused() { return g_pauseMngr->Paused(); }

tmc::task<void> CRenderDevice::execute_async(gsl::czstring cmd)
{
    const auto expected = std::ssize(seq_frame_async) + 1;

    Console->Execute(cmd);
    XR_ASSERT(std::ssize(seq_frame_async) == expected, "", cmd);

    auto& back = seq_frame_async.back();
    co_await back.first(back.second);

    seq_frame_async.pop_back();
}

tmc::task<void> CRenderDevice::OnWM_Activate(std::array<std::byte, 16>& arg)
{
    const auto wParam = *reinterpret_cast<u64*>(&arg);

    const u16 fActive = LOWORD(wParam);
    const BOOL fMinimized = (BOOL)HIWORD(wParam);
    const BOOL bActive = ((fActive != WA_INACTIVE) && (!fMinimized)) ? TRUE : FALSE;
    const BOOL isGameActive = ((psDeviceFlags.is(rsAlwaysActive) && g_screenmode != 2) || bActive) ? TRUE : FALSE;

    pInput->clip_cursor(fActive != WA_INACTIVE);

    if (isGameActive != b_is_Active)
    {
        b_is_Active = isGameActive;

        if (b_is_Active)
        {
            co_await seqAppActivate.process();
            app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;
        }
        else
        {
            app_inactive_time_start = TimerMM.GetElapsed_ms();
            co_await seqAppDeactivate.process();
        }
    }
}

CLoadScreenRenderer::CLoadScreenRenderer() : b_registered(false) {}

void CLoadScreenRenderer::start(bool b_user_input)
{
    Device.seqRender.Add(this, 0);
    b_registered = true;
    b_need_user_input = b_user_input;
}

void CLoadScreenRenderer::stop()
{
    if (!b_registered)
        return;
    Device.seqRender.Remove(this);
    pApp->DestroyLoadingScreen();
    b_registered = false;
    b_need_user_input = false;
}

tmc::task<void> CLoadScreenRenderer::OnRender()
{
    pApp->load_draw_internal();
    co_return;
}

void CRenderDevice::CSecondVPParams::SetSVPActive(bool bState) //--#SM+#-- +SecondVP+
{
    m_bIsActive = bState;

    if (g_pGamePersistent)
        g_pGamePersistent->m_pGShaderConstants.m_blender_mode.z = (m_bIsActive ? 1.0f : 0.0f);
}

bool CRenderDevice::CSecondVPParams::IsSVPFrame() //--#SM+#-- +SecondVP+
{
    const bool cond = IsSVPActive() && (Device.dwFrame % g_3dscopes_fps_factor) == 0;

    if (g_pGamePersistent)
        g_pGamePersistent->m_pGShaderConstants.m_blender_mode.y = cond ? 1.0f : 0.0f;

    return cond;
}

void CRenderDevice::time_factor(const float& time_factor)
{
    Timer.time_factor(time_factor);
    TimerGlobal.time_factor(time_factor);

    psSoundTimeFactor = time_factor; //--#SM+#--
}
