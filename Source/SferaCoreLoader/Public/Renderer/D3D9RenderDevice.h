#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "Renderer/D3D9BitmapFont.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiRuntime.h"
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
struct FD3D9ShaderInventory {
    size_t VertexShaders = 0;
    size_t PixelShaders = 0;
    std::vector<std::string> Samples;
};

struct FD3D9TextureEntry {
    IDirect3DTexture9* Texture = nullptr;
    int32 Width = 0;
    int32 Height = 0;
    bool Tried = false;
    std::string ResourceName;
    std::string Error;
};

class FD3D9RenderDevice {
public:
    FD3D9RenderDevice();
    ~FD3D9RenderDevice();
    FStatus Initialize(HWND__* hwnd, int32 width, int32 height, FLogger* logger);
    void Shutdown();
    bool IsInitialized() const { return Device != nullptr; }
    FD3D9ShaderInventory InspectShaderResources(const FResourceManager& resources, FLogger* logger) const;
    FStatus RenderUiDesktop(const FResourceManager& resources, const FUiRuntime& ui, const tagRECT& rect, FLogger* logger);
    void PreloadUiTextures(const FResourceManager& resources, const FUiRuntime& ui, FLogger* logger);
private:
    struct FDrawContext;
    using FD3DXCreateTextureFromFileInMemoryExPtr = long (__stdcall *)(IDirect3DDevice9*, const void*, unsigned int, unsigned int, unsigned int, unsigned int, unsigned long, int, int, unsigned long, unsigned long, unsigned long, void*, void*, IDirect3DTexture9**);
    FStatus EnsureD3DX(FLogger* logger);
    FD3D9TextureEntry* LoadTextureByName(const FResourceManager& resources, std::string_view textureName, FLogger* logger);
    std::string ResolveTextureResourceName(const FResourceManager& resources, std::string_view textureName) const;
    bool DrawTextureResource(FDrawContext& ctx, std::string_view textureName, const FUiRectF& dst, float alpha = 1.0f);
    bool DrawSprite(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, float alpha = 1.0f);
    bool DrawWindow(FDrawContext& ctx, const FUiWindowDef& window, const FUiRectF& dst);
    void DrawControl(FDrawContext& ctx, const FUiWindowDef& window, const FUiControlDef& control, const FUiRectF& windowRect);
    void DrawTextRect(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex);
    void DrawStatusOverlay(FDrawContext& ctx, const FUiRuntime& ui, const FUiRectF& designRect);
    void DrawSolidRect(float x, float y, float w, float h, unsigned long color);
    void DrawTexturePiece(IDirect3DTexture9* texture, const FUiSpritePiece& piece, const FUiRectF& spriteRect, int32 textureWidth, int32 textureHeight, float alpha);
    void DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color);
    void DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTexCoord* coords, int32 textureWidth, int32 textureHeight, unsigned long color);
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
