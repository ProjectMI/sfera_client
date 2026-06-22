#include "Platform/Win64Window.h"
#include "Common/TextEncoding.h"
#include <windowsx.h>

#include <array>
#include <bit>
#include <sstream>
#include <string>

namespace
{
std::wstring Utf8ToWideOrByteExpanded(std::string_view text)
{
    std::wstring wide = Common::Utf8ToWide(text);
    return !wide.empty() || text.empty() ? wide : std::wstring(text.begin(), text.end());
}

std::string WideCharToUtf8(wchar_t ch)
{
    return Common::WideToUtf8(std::wstring_view(&ch, 1));
}
}

FWin64Window::FWin64Window() = default;
FWin64Window::~FWin64Window()
{
    Destroy();
}

FStatus FWin64Window::Create(const FWindowDesc& desc, FLogger* logger)
{
    Desc = desc;
    Log = logger;

    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (GetMonitorInfoW(monitor, &monitorInfo))
    {
        Desc.Width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        Desc.Height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    }
    else
    {
        Desc.Width = GetSystemMetrics(SM_CXSCREEN);
        Desc.Height = GetSystemMetrics(SM_CYSCREEN);
    }

    Instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    const std::wstring classNameW = Utf8ToWideOrByteExpanded(Desc.ClassName);
    const std::wstring titleW = Utf8ToWideOrByteExpanded(Desc.Title);

    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = &FWin64Window::StaticWndProc;
    wc.hInstance = Instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = classNameW.c_str();

    ATOM classAtom = RegisterClassExW(&wc);

    if (!classAtom)
    {
        const DWORD err = GetLastError();

        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            return FStatus::Error(EStatusCode::RuntimeError, "RegisterClassExW failed for Sphere frontend window, error=" + std::to_string(err));
        }
    }

    const DWORD style = WS_POPUP;
    const DWORD exStyle = WS_EX_APPWINDOW;
    const int width = Desc.Width;
    const int height = Desc.Height;
    const int x = monitorInfo.rcMonitor.left;
    const int y = monitorInfo.rcMonitor.top;

    SetLastError(0);
    Hwnd = CreateWindowExW(exStyle, classNameW.c_str(), titleW.c_str(), style, x, y, width, height, nullptr, nullptr, Instance, this);

    if (!Hwnd)
    {
        const DWORD err = GetLastError();
        return FStatus::Error(EStatusCode::RuntimeError, "CreateWindowExW failed for Sphere frontend window, error=" + std::to_string(err));
    }

    SetWindowLongPtrW(Hwnd, GWL_STYLE, static_cast<LONG_PTR>(style));
    SetWindowLongPtrW(Hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(exStyle));
    SetWindowPos(Hwnd, HWND_TOP, x, y, Desc.Width, Desc.Height, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    if (Log)
    {
        Log->Info("Window created borderless: class=" + Desc.ClassName + ", title=" + Desc.Title + ", bounds=" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(Desc.Width) + "x" + std::to_string(Desc.Height));
    }

    return FStatus::Ok();
}

void FWin64Window::Show()
{
    if (!Hwnd) { return; }

    ShowWindow(Hwnd, SW_SHOW);
    SetForegroundWindow(Hwnd);
    UpdateWindow(Hwnd);
}

bool FWin64Window::PumpMessages()
{
    MSG msg{};

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            InputState.CloseRequested = true;
            return false;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return !InputState.CloseRequested;
}

void FWin64Window::RequestRepaint()
{
    if (Hwnd)
    {
        InvalidateRect(Hwnd, nullptr, FALSE);
    }
}

void FWin64Window::Destroy()
{
    if (Hwnd)
    {
        DestroyWindow(Hwnd);
        Hwnd = nullptr;
    }
}

void FWin64Window::ClearCloseRequest()
{
    InputState.CloseRequested = false;
}

void FWin64Window::SetPaintCallback(FPaintCallback callback)
{
    PaintCallback = std::move(callback);
}

FInputSnapshot FWin64Window::ConsumeInputFrame()
{
    FInputSnapshot snapshot = InputState;
    InputState.LeftPressed = false;
    InputState.LeftReleased = false;
    InputState.BackspacePressed = false;
    InputState.EnterPressed = false;
    InputState.TabPressed = false;
    InputState.TypedText.clear();
    return snapshot;
}

LRESULT CALLBACK FWin64Window::StaticWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = std::bit_cast<const CREATESTRUCTW*>(lparam);
        auto* self = create ? static_cast<FWin64Window*>(create->lpCreateParams) : nullptr;

        if (!self) { return FALSE; }

        self->Hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, std::bit_cast<LONG_PTR>(self));
        return TRUE;
    }

    auto* self = std::bit_cast<FWin64Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self) { return self->WndProc(hwnd, message, wparam, lparam); }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT FWin64Window::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CLOSE:
        InputState.CloseRequested = true;
        DestroyWindow(hwnd);

        if (Hwnd == hwnd)
        {
            Hwnd = nullptr;
        }

        return 0;
    case WM_DESTROY:
        InputState.CloseRequested = true;

        if (Hwnd == hwnd)
        {
            Hwnd = nullptr;
        }

        PostQuitMessage(0);
        return 0;
    case WM_SETFOCUS:
        InputState.HasFocus = true;
        return 0;
    case WM_KILLFOCUS:
        InputState.HasFocus = false;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        InputState.MouseX = GET_X_LPARAM(lparam);
        InputState.MouseY = GET_Y_LPARAM(lparam);
        return 0;
    case WM_LBUTTONDOWN:
        InputState.MouseX = GET_X_LPARAM(lparam);
        InputState.MouseY = GET_Y_LPARAM(lparam);
        InputState.LeftButton = true;
        InputState.LeftPressed = true;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        InputState.MouseX = GET_X_LPARAM(lparam);
        InputState.MouseY = GET_Y_LPARAM(lparam);
        InputState.LeftButton = false;
        InputState.LeftReleased = true;
        ReleaseCapture();
        return 0;
    case WM_CHAR:
        if (wparam == 8) { InputState.BackspacePressed = true; return 0; }

        if (wparam == 9) { InputState.TabPressed = true; return 0; }

        if (wparam == 13) { InputState.EnterPressed = true; return 0; }

        if (wparam >= 32 && wparam <= 0xffff)
        {
            InputState.TypedText += WideCharToUtf8(static_cast<wchar_t>(wparam));
        }

        return 0;
    case WM_RBUTTONDOWN:
        InputState.RightButton = true;
        return 0;
    case WM_RBUTTONUP:
        InputState.RightButton = false;
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rect{};
            GetClientRect(hwnd, &rect);

            if (PaintCallback)
            {
                PaintCallback(dc, rect);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
