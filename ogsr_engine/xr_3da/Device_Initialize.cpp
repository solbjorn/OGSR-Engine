#include "stdafx.h"

#include "device.h"

#include "resource.h"

tmc::task<void> CRenderDevice::Initialize()
{
    XR_LOG_NOTICE("Initializing Engine...");

    TimerGlobal.Start();
    TimerMM.Start();

    co_await tmc::resume_on(xr::tmc_cpu_st_executor());

    // Unless a substitute hWnd has been specified, create a window to render into
    if (!m_hWnd)
    {
        static constexpr std::wstring_view wndclass{L"_XRAY_1.5"};

        // Register the windows class
        HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(nullptr);
        WNDCLASSW wndClass{0,
                           &CRenderDevice::wnd_proc,
                           0,
                           0,
                           hInstance,
                           LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)),
                           LoadCursor(nullptr, IDC_ARROW),
                           (HBRUSH)GetStockObject(BLACK_BRUSH),
                           nullptr,
                           wndclass.data()};
        RegisterClassW(&wndClass);

        // Set the window's initial style
        m_dwWindowStyle = WS_BORDER | WS_DLGFRAME;

        // Set the window's initial width
        RECT rc;
        SetRect(&rc, 0, 0, 640, 480);
        AdjustWindowRect(&rc, m_dwWindowStyle, FALSE);

        // Create the render window
        m_hWnd = CreateWindowW(wndclass.data(), L"ReapeR Engine", m_dwWindowStyle, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                               nullptr, nullptr, hInstance, nullptr);
        gGameWindow = m_hWnd;
    }

    // Save window properties
    m_dwWindowStyle = u32(GetWindowLongPtr(m_hWnd, GWL_STYLE));
    GetWindowRect(m_hWnd, &m_rcWindowBounds);
    GetClientRect(m_hWnd, &m_rcWindowClient);
}
