#include "Platform/Win32Window.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <sstream>
#include <string>

namespace
{
    std::wstring Utf8ToWideLocal(const std::string& text)
    {
        if (text.empty()) { return {}; }

        const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

        if (required <= 0) { return std::wstring(text.begin(), text.end()); }

        std::wstring out(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), required);
        return out;
    }

    void AppendWideCharAsUtf8(std::string& out, wchar_t ch)
    {
        char buffer[8]{};
        const int count = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buffer, static_cast<int>(sizeof(buffer)), nullptr, nullptr);

        if (count > 0)
        {
            out.append(buffer, buffer + count);
        }
    }
}
FWin32Window::FWin32Window() = default;
FWin32Window::~FWin32Window()
{
    Destroy();
}

FStatus FWin32Window::Create(const FWindowDesc& desc, FLogger* logger)
{
    Desc = desc;
    Log = logger;
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (GetMonitorInfoA(monitor, &monitorInfo))
    {
        Desc.Width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        Desc.Height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    }
    else
    {
        Desc.Width = GetSystemMetrics(SM_CXSCREEN);
        Desc.Height = GetSystemMetrics(SM_CYSCREEN);
    }

    Instance = GetModuleHandleA(nullptr);
    WNDCLASSEXW wc{};
    const std::wstring classNameW = Utf8ToWideLocal(Desc.ClassName);
    const std::wstring titleW = Utf8ToWideLocal(Desc.Title);
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = &FWin32Window::StaticWndProc;
    wc.hInstance = Instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = classNameW.c_str();
    ATOM classAtom = RegisterClassExW(&wc);

    if (!classAtom)
    {
        DWORD err = GetLastError();

        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            return FStatus::Error(EStatusCode::RuntimeError, "RegisterClassExW failed for Sphere frontend window, error=" + std::to_string(err));
        }
    }

    DWORD style = WS_POPUP;
    DWORD exStyle = WS_EX_APPWINDOW;
    int width = Desc.Width;
    int height = Desc.Height;
    int x = monitorInfo.rcMonitor.left;
    int y = monitorInfo.rcMonitor.top;
    SetLastError(0);
    Hwnd = CreateWindowExW(exStyle, classNameW.c_str(), titleW.c_str(), style, x, y, width, height, nullptr, nullptr, Instance, this);

    if (!Hwnd)
    {
        DWORD err = GetLastError();
        return FStatus::Error(EStatusCode::RuntimeError, "CreateWindowExW failed for Sphere frontend window, error=" + std::to_string(err));
    }

    SetWindowLongPtrA(Hwnd, GWL_STYLE, static_cast<LONG_PTR>(style));
    SetWindowLongPtrA(Hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(exStyle));
    SetWindowPos(Hwnd, HWND_TOP, x, y, Desc.Width, Desc.Height, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    if (Log)
    {
        Log->Info("Window created borderless: class=" + Desc.ClassName + ", title=" + Desc.Title + ", bounds=" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(Desc.Width) + "x" + std::to_string(Desc.Height));
    }

    return FStatus::Ok();
}

void FWin32Window::Show()
{
    if (!Hwnd) { return; }

    ShowWindow(Hwnd, SW_SHOW);
    SetForegroundWindow(Hwnd);
    UpdateWindow(Hwnd);
}

bool FWin32Window::PumpMessages()
{
    MSG msg{};

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) { InputState.CloseRequested = true; return false; }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return !InputState.CloseRequested;
}

void FWin32Window::RequestRepaint()
{
    if (Hwnd)
    {
        InvalidateRect(Hwnd, nullptr, FALSE);
    }
}

void FWin32Window::Destroy()
{
    if (Hwnd)
    {
        DestroyWindow(Hwnd);
        Hwnd = nullptr;
    }
}

void FWin32Window::ClearCloseRequest()
{
    InputState.CloseRequested = false;
}

void FWin32Window::SetPaintCallback(FPaintCallback callback)
{
    PaintCallback = std::move(callback);
}

FInputSnapshot FWin32Window::ConsumeInputFrame()
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

long long SFERA_WINAPI_CALL FWin32Window::StaticWndProc(HWND__* hwnd, unsigned int message, unsigned long long wparam, long long lparam)
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = create ? static_cast<FWin32Window*>(create->lpCreateParams) : nullptr;

        if (!self) { return FALSE; }

        self->Hwnd = hwnd;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    auto* self = reinterpret_cast<FWin32Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (self) { return self->WndProc(hwnd, message, wparam, lparam); }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

long long FWin32Window::WndProc(HWND__* hwnd, unsigned int message, unsigned long long wparam, long long lparam)
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
            AppendWideCharAsUtf8(InputState.TypedText, static_cast<wchar_t>(wparam));
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
