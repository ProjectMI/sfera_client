#pragma once
#include "SferaBase.h"

class SferaInterfaceResourceManager;

class SferaWindow
{
public:
    bool Register(HINSTANCE Instance);
    bool Create(HINSTANCE Instance, const SferaSize2D& DesiredClientSize, int ShowCommand);
    void Destroy();

    HWND GetHandle() const { return WindowHandle; }
    void SetInterfaceResources(const SferaInterfaceResourceManager* Resources);
    bool IsValid() const { return WindowHandle != nullptr; }

    static std::string_view GetClassName();
    static std::string_view GetDefaultTitle();

private:
    static LRESULT CALLBACK StaticWndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam);
    LRESULT WndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam);

private:
    HWND WindowHandle = nullptr;
    const SferaInterfaceResourceManager* InterfaceResources = nullptr;
};
