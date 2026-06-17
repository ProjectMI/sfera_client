#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include <functional>
#include <string>
#include <vector>

struct HDC__;
struct HWND__;
struct HINSTANCE__;
struct tagRECT;

#if defined(_MSC_VER)
#define SFERA_WINAPI_CALL __stdcall
#else
#define SFERA_WINAPI_CALL
#endif

namespace Sfera {
struct FWindowDesc {
    std::string ClassName = "SphereWclName";
    std::string Title = "Sphere";
    int Width = 800;
    int Height = 600;
    bool Windowed = true;
};

struct FInputSnapshot {
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

class FWin32Window {
public:
    using FPaintCallback = std::function<void(HDC__*, const tagRECT&)>;
    FWin32Window();
    ~FWin32Window();
    FStatus Create(const FWindowDesc& desc, FLogger* logger);
    void Show();
    bool PumpMessages();
    void RequestRepaint();
    void Destroy();
    void SetPaintCallback(FPaintCallback callback);
    HWND__* Handle() const { return Hwnd; }
    const FInputSnapshot& Input() const { return InputState; }
    FInputSnapshot ConsumeInputFrame();
    bool IsOpen() const { return Hwnd != nullptr && !InputState.CloseRequested; }
    int Width() const { return Desc.Width; }
    int Height() const { return Desc.Height; }
private:
    static long long SFERA_WINAPI_CALL StaticWndProc(HWND__* hwnd, unsigned int message, unsigned long long wparam, long long lparam);
    long long WndProc(HWND__* hwnd, unsigned int message, unsigned long long wparam, long long lparam);
    FWindowDesc Desc;
    HWND__* Hwnd = nullptr;
    HINSTANCE__* Instance = nullptr;
    FLogger* Log = nullptr;
    FPaintCallback PaintCallback;
    FInputSnapshot InputState;
};
}
