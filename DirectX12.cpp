#include <windows.h>
#include <iostream>
#include <string>

class Window
{
public:
    Window(const std::wstring& title, int width, int height)
        : m_title(title), m_width(width), m_height(height)
    {
    }

    ~Window()
    {
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
        }
        UnregisterClassW(L"GameEngineWindow", GetModuleHandle(nullptr));
    }

    bool Initialize(bool startFullscreen = false)
    {
        HINSTANCE hInst = GetModuleHandle(nullptr);

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"GameEngineWindow";

        if (!RegisterClassExW(&wc))
        {
            std::cerr << "[Error] Failed to register window class." << std::endl;
            return false;
        }

        RECT rc = { 0, 0, m_width, m_height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        m_hwnd = CreateWindowExW(
            0,
            wc.lpszClassName,
            m_title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            hInst,
            this
        );

        if (!m_hwnd)
        {
            std::cerr << "[Error] Failed to create window." << std::endl;
            return false;
        }

        if (startFullscreen)
        {
            SetFullscreen(true);
        }

        ShowWindow(m_hwnd, SW_SHOW);
        return true;
    }

    void SetFullscreen(bool fullscreen)
    {
        if (m_isFullscreen == fullscreen) return;
        m_isFullscreen = fullscreen;

        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));

        if (m_isFullscreen)
        {
            MONITORINFO mi = { sizeof(mi) };
            if (GetWindowPlacement(m_hwnd, &m_prevPlacement) &&
                GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            {
                SetWindowLongPtrW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);

                int monitorWidth  = mi.rcMonitor.right - mi.rcMonitor.left;
                int monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

                SetWindowPos(m_hwnd, HWND_TOP,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    monitorWidth,
                    monitorHeight,
                    SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

                std::cout << "[Window] Switched to fullscreen mode ("
                          << monitorWidth << "x" << monitorHeight << ")" << std::endl;
            }
        }
        else
        {
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(m_hwnd, &m_prevPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

            std::cout << "[Window] Switched to windowed mode." << std::endl;
        }

        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void ToggleFullscreen()
    {
        SetFullscreen(!m_isFullscreen);
    }

    bool ProcessMessages()
    {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return true;
    }

    HWND GetHwnd() const { return m_hwnd; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        Window* pWindow = nullptr;
        if (uMsg == WM_CREATE)
        {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pWindow = reinterpret_cast<Window*>(pCreate->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
        }
        else
        {
            pWindow = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        return pWindow ? pWindow->WndProc(hwnd, uMsg, wParam, lParam) : DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                m_width = LOWORD(lParam);
                m_height = HIWORD(lParam);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_F11)
            {
                ToggleFullscreen();
                return 0;
            }
            if (wParam == VK_ESCAPE && m_isFullscreen)
            {
                SetFullscreen(false);
                return 0;
            }
            break;

        case WM_SYSKEYDOWN:
            if (wParam == VK_RETURN && (lParam & (1 << 29)))
            {
                ToggleFullscreen();
                return 0;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    std::wstring m_title;
    int m_width = 0;
    int m_height = 0;
    HWND m_hwnd = nullptr;
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_prevPlacement{ sizeof(WINDOWPLACEMENT) };
};

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Window window(L"Game Engine", 1280, 720);
    if (!window.Initialize())
    {
        return -1;
    }

    std::cout << "[Info] Window initialized successfully. ([F11] or [Alt+Enter] to toggle fullscreen)" << std::endl;

    while (window.ProcessMessages())
    {
    }

    std::cout << "[Info] Application exiting cleanly." << std::endl;
    return 0;
}
