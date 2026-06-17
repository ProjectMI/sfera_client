#include "SferaWindow.h"
#include "SferaInterfaceResourceManager.h"
#include <algorithm>

std::string_view SferaWindow::GetClassName()
{
    // Original string: 0x005601B4 'SphereWclName'.
    return "SphereWclName";
}

std::string_view SferaWindow::GetDefaultTitle()
{
    // Original string: 0x00541EF8 'Sphere'.
    return "Sphere";
}

bool SferaWindow::Register(HINSTANCE Instance)
{
    // Port of original init_main_window_class candidate sub_4A2390.
    WNDCLASSEXA WindowClass = {};
    WindowClass.cbSize = sizeof(WindowClass);
    WindowClass.style = CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc = &SferaWindow::StaticWndProc;
    WindowClass.cbClsExtra = 0;
    WindowClass.cbWndExtra = 0;
    WindowClass.hInstance = Instance;
    WindowClass.hIcon = LoadIconA(Instance, MAKEINTRESOURCEA(0x71));
    if (!WindowClass.hIcon)
    {
        WindowClass.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    }
    WindowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    WindowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    WindowClass.lpszMenuName = nullptr;
    WindowClass.lpszClassName = GetClassName().data();
    WindowClass.hIconSm = WindowClass.hIcon;

    ATOM Result = RegisterClassExA(&WindowClass);
    if (Result == 0)
    {
        const DWORD Error = GetLastError();
        if (Error != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxA(nullptr, "RegisterClassEx() failed! => init_main_window_class()", "Error", MB_ICONERROR | MB_OK);
            return false;
        }
    }

    return true;
}

bool SferaWindow::Create(HINSTANCE Instance, const SferaSize2D& DesiredClientSize, int ShowCommand)
{
    // Port of original init_main_window candidate sub_499B60.
    const int ClientWidth = (std::max)(DesiredClientSize.Width, 800);
    const int ClientHeight = (std::max)(DesiredClientSize.Height, 600);

    RECT Rect = { 0, 0, ClientWidth, ClientHeight };
    const DWORD Style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&Rect, Style, FALSE);

    const int WindowWidth = Rect.right - Rect.left;
    const int WindowHeight = Rect.bottom - Rect.top;
    const int ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int X = (std::max)(0, (ScreenWidth - WindowWidth) / 2);
    const int Y = (std::max)(0, (ScreenHeight - WindowHeight) / 2);

    WindowHandle = CreateWindowExA(
        0,
        GetClassName().data(),
        GetDefaultTitle().data(),
        Style,
        X,
        Y,
        WindowWidth,
        WindowHeight,
        nullptr,
        nullptr,
        Instance,
        this);

    if (!WindowHandle)
    {
        MessageBoxA(nullptr, "CreateWindowEx() failed! => init_main_window()", "Error", MB_ICONERROR | MB_OK);
        return false;
    }

    ShowWindow(WindowHandle, ShowCommand);
    UpdateWindow(WindowHandle);
    return true;
}

void SferaWindow::SetInterfaceResources(const SferaInterfaceResourceManager* Resources)
{
    InterfaceResources = Resources;
    if (WindowHandle && InterfaceResources)
    {
        SetWindowTextA(WindowHandle, InterfaceResources->GetStartupTitle().c_str());
        InvalidateRect(WindowHandle, nullptr, TRUE);
    }
}

void SferaWindow::Destroy()
{
    if (WindowHandle)
    {
        DestroyWindow(WindowHandle);
        WindowHandle = nullptr;
    }
}

LRESULT CALLBACK SferaWindow::StaticWndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    SferaWindow* Window = nullptr;

    if (Message == WM_NCCREATE)
    {
        CREATESTRUCTA* CreateStruct = reinterpret_cast<CREATESTRUCTA*>(LParam);
        Window = reinterpret_cast<SferaWindow*>(CreateStruct->lpCreateParams);
        SetWindowLongPtrA(Hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Window));
    }
    else
    {
        Window = reinterpret_cast<SferaWindow*>(GetWindowLongPtrA(Hwnd, GWLP_USERDATA));
    }

    if (Window)
    {
        return Window->WndProc(Hwnd, Message, WParam, LParam);
    }

    return DefWindowProcA(Hwnd, Message, WParam, LParam);
}

LRESULT SferaWindow::WndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    // Original WndProc candidate: sub_4A2100.
    switch (Message)
    {
    case WM_CLOSE:
        DestroyWindow(Hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        PaintStartupUi(Hwnd);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        return TRUE;
    default:
        return DefWindowProcA(Hwnd, Message, WParam, LParam);
    }
}

void SferaWindow::PaintStartupUi(HWND Hwnd)
{
    PAINTSTRUCT Paint = {};
    HDC DeviceContext = BeginPaint(Hwnd, &Paint);
    if (!DeviceContext)
    {
        return;
    }

    RECT ClientRect = {};
    GetClientRect(Hwnd, &ClientRect);
    HBRUSH Background = CreateSolidBrush(RGB(10, 12, 18));
    FillRect(DeviceContext, &ClientRect, Background);
    DeleteObject(Background);

    SetBkMode(DeviceContext, TRANSPARENT);
    SetTextColor(DeviceContext, RGB(210, 220, 235));
    RECT TextRect = { 24, 24, ClientRect.right - 24, ClientRect.bottom - 24 };
    const char* Header = InterfaceResources ? InterfaceResources->GetStartupStatusText().c_str() : "Interface resources are not attached";
    DrawTextA(DeviceContext, Header, -1, &TextRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

    if (InterfaceResources)
    {
        int Y = 56;
        for (const SferaResourceRecord* Record : InterfaceResources->GetStartupUiFiles())
        {
            if (!Record)
            {
                continue;
            }
            RECT RowRect = { 40, Y, ClientRect.right - 40, Y + 22 };
            DrawTextA(DeviceContext, Record->LogicalPath.c_str(), -1, &RowRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            Y += 24;
        }

        Y += 12;
        SetTextColor(DeviceContext, RGB(160, 190, 230));
        for (const SferaUiControlFamily& Family : InterfaceResources->GetControlCatalog().GetFamilies())
        {
            RECT RowRect = { 40, Y, ClientRect.right - 40, Y + 22 };
            const std::string Row = Family.Name + " - " + Family.SourceFile + " (" + std::to_string(Family.FunctionCount) + " funcs)";
            DrawTextA(DeviceContext, Row.c_str(), -1, &RowRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            Y += 20;
            if (Y > ClientRect.bottom - 32)
            {
                break;
            }
        }
    }

    EndPaint(Hwnd, &Paint);
}
