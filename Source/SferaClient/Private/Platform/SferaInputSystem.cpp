#include "SferaInputSystem.h"

bool SferaInputSystem::Initialize(HWND InWindowHandle)
{
    WindowHandle = InWindowHandle;
    return WindowHandle != nullptr;
}

void SferaInputSystem::Shutdown()
{
    WindowHandle = nullptr;
}

void SferaInputSystem::Tick()
{
}
