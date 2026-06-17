#pragma once
#include "SferaBase.h"
#include "SferaD3D11Renderer.h"

class SferaResourceManager;
class SferaInterfaceResourceManager;

class SferaRenderer
{
public:
    bool Initialize(HWND WindowHandle, const SferaResourceManager* Resources, const SferaInterfaceResourceManager* InterfaceResources);
    void Shutdown();
    void Tick();
    bool IsHardwareRendererActive() const { return bHardwareRendererActive; }

private:
    void DrawBootstrapOverlay(HDC Dc, const RECT& Rect);
    void DrawGdiFallbackFrame();

private:
    HWND WindowHandle = nullptr;
    const SferaResourceManager* ResourceManager = nullptr;
    const SferaInterfaceResourceManager* InterfaceResources = nullptr;
    SferaD3D11Renderer D3D11Renderer;
    bool bHardwareRendererActive = false;
};
