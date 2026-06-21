#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include <Windows.h>
#include <functional>
#include <string>
#include <vector>

struct FWindowDesc
{
    std::string ClassName = "SphereWclName";
    std::string Title = "Sphere";
    int Width = 800;
    int Height = 600;
    bool Borderless = true;
};

struct FInputSnapshot
{
    int MouseX = 0;
    int MouseY = 0;
    bool LeftButton = false;
    bool RightButton = false;
    bool HasFocus = false;
    bool CloseRequested = false;
    bool LeftPressed = false;
    bool LeftReleased = false;
    bool BackspacePressed = false;
    bool EnterPressed = false;
    bool TabPressed = false;
    std::string TypedText;
};

class FWin64Window
{
public:
    using FPaintCallback = std::function<void(HDC, const RECT&)>;

    FWin64Window();
    ~FWin64Window();

    FStatus Create(const FWindowDesc& desc, FLogger* logger);
    void Show();
    bool PumpMessages();
    void RequestRepaint();
    void Destroy();
    void SetPaintCallback(FPaintCallback callback);
    void ClearCloseRequest();

    HWND Handle() const { return Hwnd; }
    const FInputSnapshot& Input() const { return InputState; }
    FInputSnapshot ConsumeInputFrame();
    bool IsOpen() const { return Hwnd != nullptr && !InputState.CloseRequested; }
    int Width() const { return Desc.Width; }
    int Height() const { return Desc.Height; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    FWindowDesc Desc;
    HWND Hwnd = nullptr;
    HINSTANCE Instance = nullptr;
    FLogger* Log = nullptr;
    FPaintCallback PaintCallback;
    FInputSnapshot InputState;
};
