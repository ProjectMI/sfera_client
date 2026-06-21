#include "Renderer/D3D9BitmapFont.h"
#include "Core/NumericParse.h"
#include "FileSystem/PathUtils.h"
#include <Windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <bit>
#include <sstream>
#include <utility>

namespace
{
    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return s;
    }
    uint16 U16Le(const FByteArray& bytes, size_t offset) { return static_cast<uint16>(bytes[offset] | (static_cast<uint16>(bytes[offset + 1]) << 8)); }
    int16 I16Le(const FByteArray& bytes, size_t offset) { return static_cast<int16>(U16Le(bytes, offset)); }
    uint32 U32Le(const FByteArray& bytes, size_t offset) { return static_cast<uint32>(bytes[offset]) | (static_cast<uint32>(bytes[offset + 1]) << 8) | (static_cast<uint32>(bytes[offset + 2]) << 16) | (static_cast<uint32>(bytes[offset + 3]) << 24); }
    float F32Le(const FByteArray& bytes, size_t offset)
    {
        uint32 raw = U32Le(bytes, offset);
        return std::bit_cast<float>(raw);
    }
    bool HasRange(const FByteArray& bytes, size_t offset, size_t size) { return offset <= bytes.size() && size <= bytes.size() - offset; }
    std::string StripLineComment(std::string_view line)
    {
        std::string out;
        bool quoted = false;

        for (size_t i = 0; i < line.size(); ++i)
        {
            char ch = line[i];

            if (ch == '"')
            {
                quoted = !quoted;
                out.push_back(ch);
                continue;
            }

            if (!quoted && ch == '/' && i + 1 < line.size() && line[i + 1] == '/')
            {
                break;
            }

            out.push_back(ch);
        }

        return out;
    }
    std::vector<std::string> TokenizeConfigLine(std::string_view line)
    {
        std::vector<std::string> out;
        std::string cur;
        bool quoted = false;

        for (char ch : line)
        {
            if (quoted)
            {
                if (ch == '"')
                {
                    out.push_back(cur);
                    cur.clear();
                    quoted = false;
                }
                else
                {
                    cur.push_back(ch);
                }

                continue;
            }

            if (ch == '"')
            {
                if (!cur.empty())
                {
                    out.push_back(cur);
                    cur.clear();
                }

                quoted = true;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                if (!cur.empty())
                {
                    out.push_back(cur);
                    cur.clear();
                }

                continue;
            }

            cur.push_back(ch);
        }

        if (!cur.empty())
        {
            out.push_back(cur);
        }

        return out;
    }
    std::string BytesToText(const FByteArray& bytes)
    {
        std::string text;
        text.reserve(bytes.size());

        for (uint8 b : bytes)
        {
            if (b != 0)
            {
                text.push_back(static_cast<char>(b));
            }
        }

        return text;
    }
    std::wstring Utf8ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

        if (count <= 0)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
        return wide;
    }
    std::string ResolveLogicalName(const FResourceManager& resources, const std::vector<std::string>& candidates)
    {
        for (const auto& candidate : candidates)
        {
            if (auto record = resources.Catalog().FindByLogicalName(candidate))
            {
                return record->RelativePath.generic_string();
            }
        }

        return {};
    }
    std::string ResolveSfnName(const FResourceManager& resources, std::string_view fontName)
    {
        std::string name(fontName);
        return ResolveLogicalName(resources, {"effects/" + name + ".sfn", "Effects/" + name + ".sfn", name + ".sfn"});
    }
    std::string ResolveDdsName(const FResourceManager& resources, std::string_view fontName)
    {
        std::string name(fontName);
        return ResolveLogicalName(resources, {"xadd/" + name + ".dds", "XAdd/" + name + ".dds", "xAdd/" + name + ".dds", name + ".dds"});
    }
    bool SkipCString(const FByteArray& bytes, size_t& offset)
    {
        while (offset < bytes.size())
        {
            if (bytes[offset++] == 0)
            {
                return true;
            }
        }

        return false;
    }
    FStatus LoadSfnGlyphs(const FByteArray& bytes, const std::string& sourceName, int32& lineHeight, int32& baseline, std::array<FD3D9BitmapGlyph, 224>& glyphs, int32 textureWidth, int32 textureHeight)
    {
        if (bytes.size() < 40 || bytes[0] != 'S' || bytes[1] != 'F' || bytes[2] != 'N' || bytes[3] != 'T') { return FStatus::Error(EStatusCode::InvalidData, "bad SFN file: " + sourceName); }

        size_t offset = 4;

        if (!SkipCString(bytes, offset) || !SkipCString(bytes, offset) || !HasRange(bytes, offset, 8)) { return FStatus::Error(EStatusCode::InvalidData, "truncated SFN header: " + sourceName); }

        lineHeight = static_cast<int32>(U32Le(bytes, offset));
        baseline = static_cast<int32>(U32Le(bytes, offset + 4));
        offset += 8;

        if (!HasRange(bytes, offset, glyphs.size() * 28)) { return FStatus::Error(EStatusCode::InvalidData, "truncated SFN glyph table: " + sourceName); }

        for (size_t i = 0; i < glyphs.size(); ++i)
        {
            size_t rec = offset + i * 28;
            FD3D9BitmapGlyph glyph;
            int32 cellWidth = static_cast<int32>(I16Le(bytes, rec));
            int32 visibleWidth = static_cast<int32>(I16Le(bytes, rec + 8));
            glyph.Advance = visibleWidth > 0 ? visibleWidth : cellWidth;
            glyph.Height = static_cast<int32>(I16Le(bytes, rec + 2));
            glyph.XOffset = static_cast<int32>(I16Le(bytes, rec + 4));
            glyph.YOffset = static_cast<int32>(I16Le(bytes, rec + 6));
            float u1 = F32Le(bytes, rec + 12);
            float v1 = F32Le(bytes, rec + 16);
            float u2 = F32Le(bytes, rec + 20);
            float v2 = F32Le(bytes, rec + 24);
            glyph.SourceX = static_cast<int32>(std::lround(u1 * textureWidth));
            glyph.SourceY = static_cast<int32>(std::lround(v1 * textureHeight));
            glyph.SourceW = static_cast<int32>(std::lround((u2 - u1) * textureWidth));
            glyph.SourceH = static_cast<int32>(std::lround((v2 - v1) * textureHeight));
            glyphs[i] = glyph;
        }

        return FStatus::Ok();
    }
    TResult<IDirect3DTexture9*> LoadFontDds(IDirect3DDevice9* device, const FByteArray& bytes, const std::string& sourceName, int32& outWidth, int32& outHeight)
    {
        if (!device) { return FStatus::Error(EStatusCode::RuntimeError, "D3D device is not initialized"); }

        if (bytes.size() < 128 || bytes[0] != 'D' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != ' ') { return FStatus::Error(EStatusCode::InvalidData, "not a DDS file: " + sourceName); }

        uint32 headerSize = U32Le(bytes, 4);
        int32 height = static_cast<int32>(U32Le(bytes, 12));
        int32 width = static_cast<int32>(U32Le(bytes, 16));
        uint32 pfSize = U32Le(bytes, 76);
        uint32 pfFlags = U32Le(bytes, 80);
        uint32 bits = U32Le(bytes, 88);
        uint32 rMask = U32Le(bytes, 92);
        uint32 gMask = U32Le(bytes, 96);
        uint32 bMask = U32Le(bytes, 100);
        uint32 aMask = U32Le(bytes, 104);

        if (headerSize != 124 || pfSize != 32 || width <= 0 || height <= 0) { return FStatus::Error(EStatusCode::Unsupported, "unsupported DDS header: " + sourceName); }

        if ((pfFlags & 0x40U) == 0 || rMask != 0x00FF0000U || gMask != 0x0000FF00U || bMask != 0x000000FFU) { return FStatus::Error(EStatusCode::Unsupported, "unsupported DDS pixel format: " + sourceName); }

        if (bits != 24 && bits != 32) { return FStatus::Error(EStatusCode::Unsupported, "only RGB24/RGB32 DDS font atlas is supported: " + sourceName); }

        bool hasAlpha = bits == 32 && (pfFlags & 0x1U) != 0 && aMask == 0xFF000000U;
        size_t bpp = bits / 8;
        size_t sourceStride = static_cast<size_t>(width) * bpp;
        size_t imageSize = sourceStride * static_cast<size_t>(height);

        if (!HasRange(bytes, 128, imageSize)) { return FStatus::Error(EStatusCode::InvalidData, "truncated DDS pixels: " + sourceName); }

        IDirect3DTexture9* texture = nullptr;
        HRESULT hr = device->CreateTexture(static_cast<UINT>(width), static_cast<UINT>(height), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);

        if (FAILED(hr) || !texture) { return FStatus::Error(EStatusCode::RuntimeError, "CreateTexture failed for font DDS: " + sourceName + ", hr=" + std::to_string(static_cast<long>(hr))); }

        D3DLOCKED_RECT locked{};

        if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) { texture->Release(); return FStatus::Error(EStatusCode::RuntimeError, "LockRect failed for font DDS: " + sourceName); }

        const uint8* src = bytes.data() + 128;

        for (int32 y = 0; y < height; ++y)
        {
            const uint8* row = src + static_cast<size_t>(y) * sourceStride;
            auto* dst = std::bit_cast<uint32*>(static_cast<uint8*>(locked.pBits) + static_cast<size_t>(locked.Pitch) * static_cast<size_t>(y));

            for (int32 x = 0; x < width; ++x)
            {
                const uint8* p = row + static_cast<size_t>(x) * bpp;
                uint8 alpha = 0;
                uint8 red = p[2];
                uint8 green = p[1];
                uint8 blue = p[0];

                if (hasAlpha)
                {
                    alpha = p[3];
                    red = static_cast<uint8>((static_cast<unsigned>(red) * static_cast<unsigned>(alpha)) / 255U);
                    green = static_cast<uint8>((static_cast<unsigned>(green) * static_cast<unsigned>(alpha)) / 255U);
                    blue = static_cast<uint8>((static_cast<unsigned>(blue) * static_cast<unsigned>(alpha)) / 255U);
                }
                else
                {
                    alpha = static_cast<uint8>(std::max(p[0], std::max(p[1], p[2])));
                }

                dst[x] = (static_cast<uint32>(alpha) << 24) | (static_cast<uint32>(red) << 16) | (static_cast<uint32>(green) << 8) | static_cast<uint32>(blue);
            }
        }

        texture->UnlockRect(0);
        outWidth = width;
        outHeight = height;
        return texture;
    }
}

FD3D9BitmapFont::FD3D9BitmapFont(FD3D9BitmapFont&& other) noexcept
{
    *this = std::move(other);
}
FD3D9BitmapFont& FD3D9BitmapFont::operator=(FD3D9BitmapFont&& other) noexcept
{
    if (this != &other)
    {
        Release();
        LineHeightValue = other.LineHeightValue;
        BaselineValue = other.BaselineValue;
        TextureWidth = other.TextureWidth;
        TextureHeight = other.TextureHeight;
        Texture = other.Texture;
        Glyphs = other.Glyphs;
        other.LineHeightValue = 0;
        other.BaselineValue = 0;
        other.TextureWidth = 0;
        other.TextureHeight = 0;
        other.Texture = nullptr;
    }

    return *this;
}
FD3D9BitmapFont::~FD3D9BitmapFont()
{
    Release();
}
void FD3D9BitmapFont::Release()
{
    if (Texture)
    {
        Texture->Release();
        Texture = nullptr;
    }

    TextureWidth = 0;
    TextureHeight = 0;
    LineHeightValue = 0;
    BaselineValue = 0;
    Glyphs = {};
}
int32 FD3D9BitmapFont::MeasureCodepageText(const std::vector<uint8>& text) const
{
    int32 width = 0;

    for (uint8 ch : text)
    {
        if (ch >= 32)
        {
            width += Glyphs[static_cast<size_t>(ch - 32)].Advance;
        }
    }

    return width;
}
std::vector<uint8> FD3D9BitmapFont::EncodeUtf8ToCp1251(std::string_view text) const
{
    std::wstring wide = Utf8ToWide(text);

    if (wide.empty())
    {
        return {};
    }

    int needed = WideCharToMultiByte(1251, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, "?", nullptr);
    std::vector<uint8> out(static_cast<size_t>(std::max(0, needed)));

    if (needed > 0)
    {
        WideCharToMultiByte(1251, 0, wide.c_str(), static_cast<int>(wide.size()), std::bit_cast<char*>(out.data()), needed, "?", nullptr);
    }

    return out;
}
TResult<FD3D9BitmapFont> FD3D9BitmapFont::Load(IDirect3DDevice9* device, const FResourceManager& resources, std::string_view fontName)
{
    std::string name(fontName);
    std::string sfnName = ResolveSfnName(resources, name);
    std::string ddsName = ResolveDdsName(resources, name);

    if (sfnName.empty()) { return FStatus::Error(EStatusCode::NotFound, "SFN font resource not found: " + name); }

    if (ddsName.empty()) { return FStatus::Error(EStatusCode::NotFound, "DDS font atlas resource not found: " + name); }

    auto sfnBlob = resources.Load(sfnName); if (!sfnBlob.IsOk()) { return sfnBlob.Status(); }
    auto ddsBlob = resources.Load(ddsName); if (!ddsBlob.IsOk()) { return ddsBlob.Status(); }
    FD3D9BitmapFont font; int32 texW = 0; int32 texH = 0; auto texture = LoadFontDds(device, ddsBlob.Value().Bytes, ddsName, texW, texH); if (!texture.IsOk()) { return texture.Status(); }
    font.Texture = texture.Value();
    font.TextureWidth = texW;
    font.TextureHeight = texH;
    FStatus glyphStatus = LoadSfnGlyphs(sfnBlob.Value().Bytes, sfnName, font.LineHeightValue, font.BaselineValue, font.Glyphs, texW, texH);

    if (!glyphStatus.IsOk()) { font.Release(); return glyphStatus; }

    return std::move(font);
}

void FD3D9BitmapFontCatalog::Release()
{
    LoadedFonts.clear();
    FontNames.clear();
    ConfigLoaded = false;
}
void FD3D9BitmapFontCatalog::LoadConfig(const FResourceManager& resources, FLogger* logger)
{
    FontNames.clear();
    ConfigLoaded = true;
    auto blob = resources.Load("fonts.cfg");

    if (!blob.IsOk())
    {
        if (logger)
        {
            logger->Error("UI fonts.cfg is required: " + blob.Status().Message());
        }

        return;
    }

    std::string text = BytesToText(blob.Value().Bytes);
    std::istringstream stream(text);
    std::string line;
    int32 declaredCount = -1;
    std::unordered_map<int32, std::string> indexed;

    while (std::getline(stream, line))
    {
        auto tokens = TokenizeConfigLine(StripLineComment(line));

        if (tokens.empty())
        {
            continue;
        }

        std::string key = Lower(tokens[0]);

        if (key == "new_fonts_number" && tokens.size() >= 2)
        {
            NumericParse::TryParseInt32Strict(tokens[1], declaredCount);
            continue;
        }

        constexpr std::string_view prefix = "new_font_";

        if (key.rfind(prefix.data(), 0) == 0 && tokens.size() >= 2)
        {
            int32 index = -1;

            if (NumericParse::TryParseInt32Strict(key.substr(prefix.size()), index) && index >= 0)
            {
                indexed[index] = tokens[1];
            }
        }
    }

    int32 count = declaredCount > 0 ? declaredCount : static_cast<int32>(indexed.size());

    if (count > 0)
    {
        FontNames.assign(static_cast<size_t>(count), {});

        for (int32 i = 0; i < count; ++i)
        {
            auto it = indexed.find(i);

            if (it != indexed.end())
            {
                FontNames[static_cast<size_t>(i)] = it->second;
            }
        }
    }

    if (logger)
    {
        logger->Info("UI font config loaded: fonts=" + std::to_string(FontNames.size()));
    }
}
void FD3D9BitmapFontCatalog::EnsureConfig(const FResourceManager& resources, FLogger* logger)
{
    if (!ConfigLoaded)
    {
        LoadConfig(resources, logger);
    }
}
void FD3D9BitmapFontCatalog::Preload(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger)
{
    EnsureConfig(resources, logger);

    for (int32 i = 0; i < static_cast<int32>(FontNames.size()); ++i)
    {
        const std::string& name = FontNames[static_cast<size_t>(i)];
        std::string key = Lower(name);

        if (key.empty() || LoadedFonts.find(key) != LoadedFonts.end())
        {
            continue;
        }

        auto loaded = FD3D9BitmapFont::Load(device, resources, name);

        if (!loaded.IsOk())
        {
            if (logger)
            {
                logger->Warning("UI bitmap font preload failed: index=" + std::to_string(i) + " name=" + name + " - " + loaded.Status().Message());
            }

            LoadedFonts.emplace(key, nullptr);
            continue;
        }

        LoadedFonts.emplace(key, std::make_unique<FD3D9BitmapFont>(std::move(loaded.Value())));

        if (logger)
        {
            logger->Info("UI bitmap font loaded: index=" + std::to_string(i) + " name=" + name);
        }
    }

    if (logger)
    {
        logger->Info("D3D9 UI preload: bitmap_fonts=" + std::to_string(LoadedFonts.size()));
    }
}
const FD3D9BitmapFont* FD3D9BitmapFontCatalog::GetFont(IDirect3DDevice9* device, const FResourceManager& resources, int32 fontIndex, FLogger* logger)
{
    EnsureConfig(resources, logger);

    if (FontNames.empty())
    {
        return nullptr;
    }

    int32 requestedIndex = fontIndex;
    int32 mappedIndex = fontIndex >= 2 ? fontIndex - 2 : fontIndex;

    if (mappedIndex < 0 || mappedIndex >= static_cast<int32>(FontNames.size()))
    {
        return nullptr;
    }

    const std::string& name = FontNames[static_cast<size_t>(mappedIndex)]; std::string key = Lower(name); if (key.empty()) { return nullptr; }
    auto existing = LoadedFonts.find(key); if (existing != LoadedFonts.end()) { return existing->second ? existing->second.get() : nullptr; }
    auto loaded = FD3D9BitmapFont::Load(device, resources, name);

    if (!loaded.IsOk())
    {
        if (logger)
        {
            logger->Warning("UI bitmap font load failed: requested=" + std::to_string(requestedIndex) + " mapped=" + std::to_string(mappedIndex) + " name=" + name + " - " + loaded.Status().Message());
        }

        LoadedFonts.emplace(key, nullptr);
        return nullptr;
    }

    auto font = std::make_unique<FD3D9BitmapFont>(std::move(loaded.Value()));
    const FD3D9BitmapFont* out = font.get();
    LoadedFonts.emplace(key, std::move(font));

    if (logger)
    {
        logger->Info("UI bitmap font loaded: requested=" + std::to_string(requestedIndex) + " mapped=" + std::to_string(mappedIndex) + " name=" + name);
    }

    return out;
}
