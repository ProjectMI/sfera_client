#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include "Renderer/D3D9BitmapFont.h"
#include "UI/UiResourceDocument.h"
#include <string>
#include <unordered_map>
#include <vector>

struct HWND__;
struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct HINSTANCE__;
struct tagRECT;

namespace Sfera {
class FLoadingScreenModel;

struct FD3D9ShaderInventory {
    size_t VertexShaders = 0;
    size_t PixelShaders = 0;
    std::vector<std::string> Samples;
};

struct FD3D9TextureEntry {
    IDirect3DTexture9* Texture = nullptr;
    int Width = 0;
    int Height = 0;
    bool Tried = false;
    std::string ResourceName;
    std::string Error;
};

class FD3D9RenderDevice {
public:
    FD3D9RenderDevice();
    ~FD3D9RenderDevice();
    FStatus Initialize(HWND__* hwnd, int width, int height, FLogger* logger);
    void Shutdown();
    bool IsInitialized() const { return Device != nullptr; }
    FD3D9ShaderInventory InspectShaderResources(const FResourceManager& resources, FLogger* logger) const;
    FStatus RenderLoadingScreen(const FResourceManager& resources, const FLoadingScreenModel& model, const tagRECT& rect, FLogger* logger);
    void PreloadLoadingScreenTextures(const FResourceManager& resources, const FLoadingScreenModel& model, FLogger* logger);
private:
    struct FUiDrawContext;
    struct FDrawRect;
    using FD3DXCreateTextureFromFileInMemoryExPtr = long (__stdcall *)(IDirect3DDevice9*, const void*, unsigned int, unsigned int, unsigned int, unsigned int, unsigned long, int, int, unsigned long, unsigned long, unsigned long, void*, void*, IDirect3DTexture9**);
    FStatus EnsureD3DX(FLogger* logger);
    FD3D9TextureEntry* LoadTextureByName(const FResourceManager& resources, std::string_view textureName, FLogger* logger);
    std::string ResolveTextureResourceName(const FResourceManager& resources, std::string_view textureName) const;
    bool DrawSprite(FUiDrawContext& ctx, const FUiWindow& window, std::string_view spriteName, const FDrawRect& dst, float alpha = 1.0f);
    bool DrawWindow(FUiDrawContext& ctx, const FUiWindow& window, const FDrawRect& overrideRect, bool forceConnectionTitle);
    void DrawControl(FUiDrawContext& ctx, const FUiWindow& window, const FUiControl& control, const FDrawRect& windowRect);
    void DrawTextRect(FUiDrawContext& ctx, const FDrawRect& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex);
    void DrawGdiTextRect(FUiDrawContext& ctx, const FDrawRect& rect, const std::string& text, unsigned long color, bool center);
    void DrawSolidRect(float x, float y, float w, float h, unsigned long color);
    void DrawTextureSlice(IDirect3DTexture9* texture, const FUiTextureSlice& slice, const FDrawRect& spriteRect, int textureWidth, int textureHeight, float alpha);
    void DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color);
    void DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTextureCoord* coords, int textureWidth, int textureHeight, unsigned long color);
    void ReleaseTextures();
    IDirect3D9* D3D = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HINSTANCE__* D3DXModule = nullptr;
    FD3DXCreateTextureFromFileInMemoryExPtr D3DXCreateTextureFromFileInMemoryExFn = nullptr;
    std::unordered_map<std::string, FD3D9TextureEntry> TextureCache;
    FD3D9BitmapFontCatalog FontCache;
    bool ReportedD3DXMissing = false;
};
}
