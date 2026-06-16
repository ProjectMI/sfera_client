#pragma once
#include "SferaBase.h"

class SferaInputSystem
{
public:
    bool Initialize(HWND WindowHandle);
    void Shutdown();
    void Tick();

private:
    HWND WindowHandle = nullptr;
};
