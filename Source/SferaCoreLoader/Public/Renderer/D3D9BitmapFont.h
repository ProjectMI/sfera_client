#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace Sfera {
struct FD3D9BitmapGlyph {
    int32 Advance = 0;
    int32 Height = 0;
    int32 XOffset = 0;
    int32 YOffset = 0;
    int32 SourceX = 0;
    int32 SourceY = 0;
    int32 SourceW = 0;
    int32 SourceH = 0;
};

class FD3D9BitmapFont {
public:
    FD3D9BitmapFont() = default;
    FD3D9BitmapFont(const FD3D9BitmapFont&) = delete;
    FD3D9BitmapFont& operator=(const FD3D9BitmapFont&) = delete;
    FD3D9BitmapFont(FD3D9BitmapFont&& other) noexcept;
    FD3D9BitmapFont& operator=(FD3D9BitmapFont&& other) noexcept;
    ~FD3D9BitmapFont();
    void Release();
    bool IsValid() const { return Texture != nullptr && TextureWidth > 0 && TextureHeight > 0 && LineHeightValue > 0; }
    int32 LineHeight() const { return LineHeightValue; }
    int32 Baseline() const { return BaselineValue; }
    int32 Width() const { return TextureWidth; }
    int32 Height() const { return TextureHeight; }
    IDirect3DTexture9* AtlasTexture() const { return Texture; }
    const FD3D9BitmapGlyph& Glyph(uint8 codepageByte) const { return Glyphs[static_cast<size_t>(codepageByte - 32)]; }
    int32 MeasureCodepageText(const std::vector<uint8>& text) const;
    std::vector<uint8> EncodeUtf8ToCp1251(std::string_view text) const;
    static TResult<FD3D9BitmapFont> Load(IDirect3DDevice9* device, const FResourceManager& resources, std::string_view fontName);
private:
    int32 LineHeightValue = 0;
    int32 BaselineValue = 0;
    int32 TextureWidth = 0;
    int32 TextureHeight = 0;
    IDirect3DTexture9* Texture = nullptr;
    std::array<FD3D9BitmapGlyph, 224> Glyphs{};
};

class FD3D9BitmapFontCatalog {
public:
    void Release();
    void LoadConfig(const FResourceManager& resources, FLogger* logger);
    void Preload(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger);
    const FD3D9BitmapFont* GetFont(IDirect3DDevice9* device, const FResourceManager& resources, int32 fontIndex, FLogger* logger);
private:
    void EnsureConfig(const FResourceManager& resources, FLogger* logger);
    std::vector<std::string> FontNames;
    std::unordered_map<std::string, std::unique_ptr<FD3D9BitmapFont>> LoadedFonts;
    bool ConfigLoaded = false;
};
}
