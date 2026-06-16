#pragma once
#include "SferaBase.h"
#include "SferaD3D11Renderer.h"

class SferaResourceManager;

class SferaRenderer
{
public:
    bool Initialize(HWND WindowHandle, const SferaResourceManager* Resources);
    void Shutdown();
    void Tick();
    bool IsHardwareRendererActive() const { return bHardwareRendererActive; }

private:
    void DrawBootstrapOverlay(HDC Dc, const RECT& Rect);
    void DrawGdiOverlayOnly();

private:
    HWND WindowHandle = nullptr;
    const SferaResourceManager* ResourceManager = nullptr;
    SferaD3D11Renderer D3D11Renderer;
    bool bHardwareRendererActive = false;
};
