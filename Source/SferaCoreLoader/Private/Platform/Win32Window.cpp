#include "Platform/Win32Window.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <sstream>

namespace Sfera {
FWin32Window::FWin32Window() = default;
FWin32Window::~FWin32Window() { Destroy(); }

FStatus FWin32Window::Create(const FWindowDesc& desc, FLogger* logger) {
    Desc = desc;
    Log = logger;
    Desc.Width = Desc.Width < 800 ? 800 : Desc.Width;
    Desc.Height = Desc.Height < 600 ? 600 : Desc.Height;
    Instance = GetModuleHandleA(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &FWin32Window::StaticWndProc;
    wc.hInstance = Instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = Desc.ClassName.c_str();
    ATOM classAtom = RegisterClassExA(&wc);
    if (!classAtom) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return FStatus::Error(EStatusCode::RuntimeError, "RegisterClassExA failed for Sphere frontend window, error=" + std::to_string(err));
        }
    }
    DWORD style = Desc.Windowed ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) : WS_POPUP;
    RECT rect{0, 0, Desc.Width, Desc.Height};
    AdjustWindowRect(&rect, style, FALSE);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    SetLastError(0);
    Hwnd = CreateWindowExA(0, Desc.ClassName.c_str(), Desc.Title.c_str(), style, x, y, width, height, nullptr, nullptr, Instance, this);
    if (!Hwnd) {
        DWORD err = GetLastError();
        return FStatus::Error(EStatusCode::RuntimeError, "CreateWindowExA failed for Sphere frontend window, error=" + std::to_string(err));
    }
    if (Log) { Log->Info("Window created: class=" + Desc.ClassName + ", title=" + Desc.Title + ", size=" + std::to_string(Desc.Width) + "x" + std::to_string(Desc.Height)); }
    return FStatus::Ok();
}

void FWin32Window::Show() {
    if (!Hwnd) { return; }
    ShowWindow(Hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(Hwnd);
    UpdateWindow(Hwnd);
}

bool FWin32Window::PumpMessages() {
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { InputState.CloseRequested = true; return false; }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return !InputState.CloseRequested;
}

void FWin32Window::RequestRepaint() {
    if (Hwnd) { InvalidateRect(Hwnd, nullptr, FALSE); }
}

void FWin32Window::Destroy() {
    if (Hwnd) { DestroyWindow(Hwnd); Hwnd = nullptr; }
}

void FWin32Window::SetPaintCallback(FPaintCallback callback) { PaintCallback = std::move(callback); }

FInputSnapshot FWin32Window::ConsumeInputFrame() {
    FInputSnapshot snapshot = InputState;
    InputState.LeftPressed = false;
    InputState.LeftReleased = false;
    InputState.BackspacePressed = false;
    InputState.EnterPressed = false;
    InputState.TabPressed = false;
    InputState.TypedText.clear();
    return snapshot;
}

long long SFERA_WINAPI_CALL FWin32Window::StaticWndProc(HWND__* hwnd, unsigned int message, unsigned long long wparam, long long lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lparam);
        auto* self = create ? static_cast<FWin32Window*>(create->lpCreateParams) : nullptr;
        if (!self) { return FALSE; }
        self->Hwnd = hwnd;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    auto* self = reinterpret_cast<FWin32Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (self) { return self->WndProc(hwnd, message, wparam, lparam); }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

long long FWin32Window::WndProc(HWND__* hwnd, unsigned int message, unsigned long long wparam, long long lparam) {
    switch (message) {
    case WM_CLOSE:
        InputState.CloseRequested = true;
        DestroyWindow(hwnd);
        if (Hwnd == hwnd) { Hwnd = nullptr; }
        return 0;
    case WM_DESTROY:
        InputState.CloseRequested = true;
        if (Hwnd == hwnd) { Hwnd = nullptr; }
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
        if (wparam >= 32 && wparam < 127) { InputState.TypedText.push_back(static_cast<char>(wparam)); }
        return 0;
    case WM_RBUTTONDOWN:
        InputState.RightButton = true;
        return 0;
    case WM_RBUTTONUP:
        InputState.RightButton = false;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        if (PaintCallback) { PaintCallback(dc, rect); }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }
}
}
