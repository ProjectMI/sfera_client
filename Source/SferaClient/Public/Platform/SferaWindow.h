#pragma once
#include "SferaBase.h"

class SferaWindow
{
public:
    bool Register(HINSTANCE Instance);
    bool Create(HINSTANCE Instance, const SferaSize2D& DesiredClientSize, int ShowCommand);
    void Destroy();

    HWND GetHandle() const { return WindowHandle; }
    bool IsValid() const { return WindowHandle != nullptr; }

    static const char* GetClassName();
    static const char* GetDefaultTitle();

private:
    static LRESULT CALLBACK StaticWndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam);
    LRESULT WndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam);

private:
    HWND WindowHandle = nullptr;
};
