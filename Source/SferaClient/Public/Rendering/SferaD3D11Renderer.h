#pragma once
#include "SferaBase.h"
#include <d3d11.h>
#include <dxgi.h>

class SferaD3D11Renderer
{
public:
    bool Initialize(HWND WindowHandle);
    void Shutdown();
    bool BeginFrame(float R, float G, float B, float A);
    void Present();
    bool IsInitialized() const { return bInitialized; }

private:
    void ReleaseBackBuffer();
    bool CreateBackBuffer();

private:
    HWND WindowHandle = nullptr;
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* DeviceContext = nullptr;
    IDXGISwapChain* SwapChain = nullptr;
    ID3D11RenderTargetView* BackBufferView = nullptr;
    bool bInitialized = false;
};
