#include "SferaRenderer.h"
#include "SferaResourceManager.h"
#include "SferaInterfaceResourceManager.h"
#include <cstdio>

bool SferaRenderer::Initialize(HWND InWindowHandle, const SferaResourceManager* InResources, const SferaInterfaceResourceManager* InInterfaceResources)
{
    WindowHandle = InWindowHandle;
    ResourceManager = InResources;
    InterfaceResources = InInterfaceResources;
    if (!WindowHandle)
    {
        return false;
    }

    // S0003 x64 pivot: use platform D3D11, not legacy D3D8/D3D9/D3DX DLLs.
    bHardwareRendererActive = D3D11Renderer.Initialize(WindowHandle);
    return true;
}

void SferaRenderer::Shutdown()
{
    D3D11Renderer.Shutdown();
    bHardwareRendererActive = false;
    ResourceManager = nullptr;
    InterfaceResources = nullptr;
    WindowHandle = nullptr;
}

void SferaRenderer::Tick()
{
    if (!WindowHandle)
    {
        return;
    }

    if (bHardwareRendererActive && D3D11Renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f))
    {
        D3D11Renderer.Present();
        return;
    }

    DrawGdiFallbackFrame();
}

void SferaRenderer::DrawGdiFallbackFrame()
{
    HDC Dc = GetDC(WindowHandle);
    if (Dc)
    {
        RECT Rect = {};
        GetClientRect(WindowHandle, &Rect);
        HBRUSH Brush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        FillRect(Dc, &Rect, Brush);
        DrawBootstrapOverlay(Dc, Rect);
        ReleaseDC(WindowHandle, Dc);
    }
}

void SferaRenderer::DrawBootstrapOverlay(HDC Dc, const RECT& Rect)
{
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, RGB(180, 180, 180));

    RECT TextRect = Rect;
    TextRect.left += 24;
    TextRect.top += 24;

    char Buffer[768] = {};
    if (ResourceManager)
    {
        const SferaResourceStats& Stats = ResourceManager->GetStats();
        std::snprintf(Buffer, sizeof(Buffer),
            "SferaClientSnapshot S0003 x64\n"
            "renderer: %s | sound: NoOp\n"
            "resources: %d files | cfg %d | scripts %d | ui %d | tex %d | sound indexed %d | model %d | landscape %d | shaders %d\n"
            "root: %s%s%s",
            bHardwareRendererActive ? "D3D11" : "GDI fallback",
            Stats.TotalFiles,
            Stats.ConfigFiles,
            Stats.ScriptFiles,
            Stats.UserInterfaceFiles,
            Stats.TextureFiles,
            Stats.SoundFiles,
            Stats.ModelFiles,
            Stats.LandscapeFiles,
            Stats.ShaderFiles,
            ResourceManager->GetRootDirectory().c_str(),
            InterfaceResources ? "\n" : "",
            InterfaceResources ? InterfaceResources->GetStartupStatusText().c_str() : "");
    }
    else
    {
        std::snprintf(Buffer, sizeof(Buffer), "SferaClientSnapshot S0003 x64");
    }

    DrawTextA(Dc, Buffer, -1, &TextRect, DT_LEFT | DT_TOP | DT_WORDBREAK);
}
