#include "Renderer/D3D9RenderDevice.h"
#include "Renderer/D3D9Utils.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"
#include "Renderer/DdsImage.h"
#include "FileSystem/PathUtils.h"
#include "ResourceLoader/ResourceTypes.h"

namespace
{
    constexpr unsigned int D3DX_DEFAULT_VALUE = static_cast<unsigned int>(-1);
    constexpr unsigned long D3DX_FILTER_NONE_VALUE = 1ul;

    struct FUiVertex
    {
        float X;
        float Y;
        float Z;
        float Rhw;
        unsigned long Color;
        float U;
        float V;
    };
    constexpr unsigned long FVF_UI = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    std::string StemLower(const FPath& path) { return Common::ToLower(path.stem().string()); }
    unsigned long Argb(unsigned char a, unsigned char r, unsigned char g, unsigned char b) { return (static_cast<unsigned long>(a) << 24) | (static_cast<unsigned long>(r) << 16) | (static_cast<unsigned long>(g) << 8) | static_cast<unsigned long>(b); }
    unsigned long ColorToArgb(const FUiColor& color) { return Argb(static_cast<unsigned char>(std::clamp(color.A, 0, 255)), static_cast<unsigned char>(std::clamp(color.R, 0, 255)), static_cast<unsigned char>(std::clamp(color.G, 0, 255)), static_cast<unsigned char>(std::clamp(color.B, 0, 255))); }
    int ColorR(unsigned long color) { return static_cast<int>((color >> 16) & 0xff); }
    int ColorG(unsigned long color) { return static_cast<int>((color >> 8) & 0xff); }
    int ColorB(unsigned long color) { return static_cast<int>(color & 0xff); }
    int ColorA(unsigned long color) { return static_cast<int>((color >> 24) & 0xff); }
    int AbsInt(int value) { return value < 0 ? -value : value; }
    unsigned long PremultiplyDiffuse(unsigned long color)
    {
        unsigned int a = static_cast<unsigned int>(ColorA(color));
        return Argb(static_cast<unsigned char>(a), static_cast<unsigned char>((static_cast<unsigned int>(ColorR(color)) * a) / 255U), static_cast<unsigned char>((static_cast<unsigned int>(ColorG(color)) * a) / 255U), static_cast<unsigned char>((static_cast<unsigned int>(ColorB(color)) * a) / 255U));
    }
    float SnapPixel(float v) { return std::floor(v + 0.5f) - 0.5f; }
    float SnapSize(float v) { return std::max(1.0f, std::floor(v + 0.5f)); }
    const FUiSpriteDef* FindSprite(const FUiWindowDef& window, std::string_view name)
    {
        auto it = window.Sprites.find(Common::ToLower(std::string(name)));

        if (it == window.Sprites.end()) { return nullptr; }

        return &it->second;
    }

    std::string SelectButtonSprite(const FUiControlDef& control, const FUiActionState& state)
    {
        if (control.Disabled && !control.LeftButton.DisabledImage.empty()) { return control.LeftButton.DisabledImage; }

        if (state.PressedControlId == control.Id && !control.CheckedImage.empty()) { return control.CheckedImage; }

        if (state.HoverControlId == control.Id && !control.FocusedImage.empty()) { return control.FocusedImage; }

        if (!control.UncheckedImage.empty()) { return control.UncheckedImage; }

        return control.DrawSpriteName;
    }

    std::string SelectSubButtonSprite(const FUiSubButtonDef& button, bool disabled, bool hot, bool pressed, std::string_view normalFallback, std::string_view focusFallback, std::string_view pressedFallback, std::string_view disabledFallback)
    {
        if (disabled) { return !button.DisabledImage.empty() ? button.DisabledImage : std::string(disabledFallback); }

        if (pressed) { return !button.CheckedImage.empty() ? button.CheckedImage : std::string(pressedFallback); }

        if (hot) { return !button.FocusedImage.empty() ? button.FocusedImage : std::string(focusFallback); }

        return !button.UncheckedImage.empty() ? button.UncheckedImage : std::string(normalFallback);
    }

    std::string FormatOneDecimal(double value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        return buffer;
    }

    std::string FormatCompactCount(uint64 value)
    {
        char buffer[32]{};
        if (value >= 1000000ULL)
        {
            std::snprintf(buffer, sizeof(buffer), "%.1fM", static_cast<double>(value) / 1000000.0);
        }
        else if (value >= 1000ULL)
        {
            std::snprintf(buffer, sizeof(buffer), "%.1fk", static_cast<double>(value) / 1000.0);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        }
        return buffer;
    }

    bool IsTextLikeControl(const FUiControlDef& control) { return Common::EqualsNoCase(control.ClassId, "TEXT") || Common::EqualsNoCase(control.ClassId, "TEXTLIST") || Common::EqualsNoCase(control.ClassId, "HYPER_TEXT"); }
    std::string TextForControl(const FUiRuntime& ui, const FUiWindowDef& window, const FUiControlDef& control)
    {
        if (ui.Mode() == EUiRuntimeMode::CharacterSelect && Common::EqualsNoCase(window.Name, "pick_person")) { return ui.CharacterControlText(control); }

        if (ui.HasModalDialog() && Common::EqualsNoCase(window.Name, ui.ActiveModalWindow().Name)) { return ui.ModalControlText(control); }

        return control.TextKey.empty() ? std::string{} : ui.ResolveText(control.TextKey);
    }
}

struct FD3D9RenderDevice::FDrawContext
{
    const FResourceManager& Resources;
    const FUiRuntime& Ui;
    FLogger* Logger = nullptr;
    float Scale = 1.0f;
};

FD3D9RenderDevice::FD3D9RenderDevice() = default;
FD3D9RenderDevice::~FD3D9RenderDevice()
{
    Shutdown();
}

void FD3D9RenderDevice::SetServerGameTime(float dayFraction)
{
    ServerGameTime = dayFraction - std::floor(dayFraction);
    HasServerGameTime = true;
    ServerGameTimePending = true;
    if (GameWorldScene.IsValid())
    {
        GameWorldScene.SetGameTime(ServerGameTime);
        ServerGameTimePending = false;
    }
}

FStatus FD3D9RenderDevice::Initialize(HWND hwnd, int32 width, int32 height, FLogger* logger)
{
    Shutdown();
    D3D = Direct3DCreate9(D3D_SDK_VERSION);

    if (!D3D) { return FStatus::Error(EStatusCode::RuntimeError, "Direct3DCreate9 failed"); }

    DeviceWindow = hwnd;
    BackBufferWidth = std::max<int32>(1, width);
    BackBufferHeight = std::max<int32>(1, height);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.hDeviceWindow = hwnd;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = static_cast<UINT>(BackBufferWidth);
    pp.BackBufferHeight = static_cast<UINT>(BackBufferHeight);
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    auto CreateDeviceWithCurrentPresentation = [&]()
    {
        pp.AutoDepthStencilFormat = D3DFMT_D24S8;
        HRESULT result = D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &Device);
        if (FAILED(result))
        {
            pp.AutoDepthStencilFormat = D3DFMT_D16;
            result = D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &Device);
        }
        if (FAILED(result))
        {
            result = D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &Device);
        }
        return result;
    };

    const HRESULT hr = CreateDeviceWithCurrentPresentation();

    if (FAILED(hr)) { Shutdown(); return FStatus::Error(EStatusCode::RuntimeError, "IDirect3D9::CreateDevice failed: hr=" + std::to_string(static_cast<long>(hr))); }

    if (logger)
    {
        logger->Info("D3D9 device initialized borderless windowed: backbuffer=" + std::to_string(BackBufferWidth) + "x" + std::to_string(BackBufferHeight) + ", present=display-paced, pacing=present");
    }

    FStatus d3dx = EnsureD3DX(logger);

    if (!d3dx.IsOk() && logger)
    {
        logger->Warning("D3D9 texture path will use built-in DDS decoder where possible: " + d3dx.Message());
    }

    return FStatus::Ok();
}

void FD3D9RenderDevice::ReleaseTextures()
{
    for (auto& item : TextureCache)
    {
        SafeRelease(item.second.Texture);
    }

    TextureCache.clear();
    FontCache.Release();
}

void FD3D9RenderDevice::Shutdown()
{
    GameWorldScene.Shutdown();
    CharacterScene.Shutdown();
    ReleaseTextures();

    SafeRelease(Device);
    SafeRelease(D3D);

    DeviceWindow = nullptr;
    ActiveWorldScene = nullptr;
    FailedWorldScene = nullptr;
    BackBufferWidth = 0;
    BackBufferHeight = 0;

    if (D3DXModule)
    {
        FreeLibrary(D3DXModule);
        D3DXModule = nullptr;
    }

    D3DXCreateTextureFromFileInMemoryExFn = nullptr;
    ReportedD3DXMissing = false;
}

FStatus FD3D9RenderDevice::EnsureD3DX(FLogger* logger)
{
    if (D3DXCreateTextureFromFileInMemoryExFn) { return FStatus::Ok(); }

    constexpr std::array<std::string_view, 8> dlls = {
        "d3dx9_43.dll", "d3dx9_42.dll", "d3dx9_41.dll", "d3dx9_40.dll", "d3dx9_39.dll", "d3dx9_38.dll", "d3dx9_37.dll", "d3dx9_36.dll"
    };

    for (std::string_view dll : dlls)
    {
        D3DXModule = LoadLibraryA(std::string(dll).c_str());

        if (!D3DXModule) { continue; }

        auto* proc = std::bit_cast<FD3DXCreateTextureFromFileInMemoryExPtr>(GetProcAddress(D3DXModule, "D3DXCreateTextureFromFileInMemoryEx"));

        if (proc)
        {
            D3DXCreateTextureFromFileInMemoryExFn = proc;

            if (logger)
            {
                std::string message = "D3D9 texture loader: ";
                message.append(dll);
                logger->Info(message);
            }

            return FStatus::Ok();
        }

        FreeLibrary(D3DXModule);
        D3DXModule = nullptr;
    }

    if (!ReportedD3DXMissing && logger)
    {
        logger->Warning("D3D9 texture loader unavailable: d3dx9_xx.dll is required for non-DDS textures");
        ReportedD3DXMissing = true;
    }

    return FStatus::Error(EStatusCode::RuntimeError, "D3DXCreateTextureFromFileInMemoryEx unavailable");
}

std::string FD3D9RenderDevice::ResolveTextureResourceName(const FResourceManager& resources, std::string_view textureName) const
{
    std::string name(textureName);

    if (name.empty()) { return {}; }

    std::string lower = Common::ToLower(name);
    bool hasExt = lower.find('.') != std::string::npos;
    std::vector<std::string> bases =
    {
        name, "textures/" + name, "Textures/" + name, "effects/" + name, "Effects/" + name, "interface/" + name, "Interface/" + name, "xadd/" + name, "XADD/" + name
    };
    std::vector<std::string> exts = hasExt ? std::vector<std::string>
    {
        ""
    }
    : std::vector<std::string>
    {
        ".dds", ".tga", ".bmp", ".png", ".jpg", ".jpeg"
    };

    for (const auto& base : bases)
    {
        for (const auto& ext : exts)
        {
            if (auto record = resources.Catalog().FindByLogicalName(base + ext))
            {
                return record->RelativePath.generic_string();
            }
        }
    }

    for (const auto& record : resources.Catalog().All())
    {
        EResourceKind kind = GuessResourceKind(record.RelativePath);

        if (kind != EResourceKind::Texture && Common::ToLower(record.RelativePath.extension().string()) != ".dds")
        {
            continue;
        }

        if (StemLower(record.RelativePath) == lower || Common::ToLower(record.RelativePath.filename().string()) == lower)
        {
            return record.RelativePath.generic_string();
        }
    }

    return {};
}

IDirect3DTexture9* FD3D9RenderDevice::CreateTextureFromDdsImage(const FDdsImage& image, FLogger* logger)
{
    if (!Device || !image) { return nullptr; }

    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = Device->CreateTexture(static_cast<UINT>(image.Width), static_cast<UINT>(image.Height), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);

    if (FAILED(hr) || !texture)
    {
        if (logger)
        {
            logger->Warning("D3D9 DDS texture allocation failed: hr=" + std::to_string(static_cast<long>(hr)));
        }

        return nullptr;
    }

    D3DLOCKED_RECT locked{};
    hr = texture->LockRect(0, &locked, nullptr, 0);

    if (FAILED(hr))
    {
        if (logger)
        {
            logger->Warning("D3D9 DDS texture lock failed: hr=" + std::to_string(static_cast<long>(hr)));
        }

        SafeRelease(texture);
        return nullptr;
    }

    const auto* src = image.BgraPixels.data();
    auto* dst = static_cast<uint8*>(locked.pBits);
    const size_t copyStride = static_cast<size_t>(image.Width) * 4;

    for (int32 y = 0; y < image.Height; ++y)
    {
        std::copy_n(src + static_cast<size_t>(y) * static_cast<size_t>(image.Stride), copyStride, dst + static_cast<size_t>(y) * static_cast<size_t>(locked.Pitch));
    }

    texture->UnlockRect(0);
    return texture;
}

FD3D9TextureEntry* FD3D9RenderDevice::LoadTextureByName(const FResourceManager& resources, std::string_view textureName, FLogger* logger)
{
    std::string key = Common::ToLower(std::string(textureName));

    if (key.empty()) { return nullptr; }

    auto it = TextureCache.find(key);

    if (it != TextureCache.end()) { return it->second.Texture ? &it->second : nullptr; }

    FD3D9TextureEntry entry;
    entry.Tried = true;

    if (!Device) { entry.Error = "device not initialized"; TextureCache.emplace(key, entry); return nullptr; }

    std::string logical = ResolveTextureResourceName(resources, textureName);

    if (logical.empty())
    {
        entry.Error = "resource not found";
        TextureCache.emplace(key, entry);

        if (logger)
        {
            logger->Warning("UI texture not found: " + std::string(textureName));
        }

        return nullptr;
    }

    auto blob = resources.Load(logical);

    if (!blob.IsOk())
    {
        entry.Error = blob.Status().Message();
        TextureCache.emplace(key, entry);

        if (logger)
        {
            logger->Warning("UI texture load failed: " + logical + " - " + entry.Error);
        }

        return nullptr;
    }

    IDirect3DTexture9* texture = nullptr;

    if (EnsureD3DX(nullptr).IsOk())
    {
        HRESULT hr = D3DXCreateTextureFromFileInMemoryExFn(Device, blob.Value().Bytes.data(), static_cast<unsigned int>(blob.Value().Bytes.size()), D3DX_DEFAULT_VALUE, D3DX_DEFAULT_VALUE, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_FILTER_NONE_VALUE, D3DX_FILTER_NONE_VALUE, 0, nullptr, nullptr, &texture);

        if (FAILED(hr) || !texture)
        {
            entry.Error = "D3DX load failed hr=" + std::to_string(static_cast<long>(hr));
        }
    }

    if (!texture && Common::ToLower(FPath(logical).extension().string()) == ".dds")
    {
        auto dds = DecodeDdsRgbImageFromBytes(blob.Value().Bytes, logical);

        if (dds.IsOk())
        {
            texture = CreateTextureFromDdsImage(dds.Value(), logger);
        }
        else if (entry.Error.empty())
        {
            entry.Error = dds.Status().Message();
        }
    }

    if (!texture)
    {
        if (entry.Error.empty())
        {
            entry.Error = "texture decode failed";
        }

        TextureCache.emplace(key, entry);

        if (logger)
        {
            logger->Warning("UI texture decode failed: " + logical + " - " + entry.Error);
        }

        return nullptr;
    }

    D3DSURFACE_DESC desc{};
    texture->GetLevelDesc(0, &desc);
    entry.Texture = texture;
    entry.Width = static_cast<int32>(desc.Width);
    entry.Height = static_cast<int32>(desc.Height);
    entry.ResourceName = logical;
    auto result = TextureCache.emplace(key, entry);

    if (logger)
    {
        logger->Info("UI texture loaded: " + std::string(textureName) + " -> " + logical + ", size=" + std::to_string(entry.Width) + "x" + std::to_string(entry.Height));
    }

    return &result.first->second;
}

void FD3D9RenderDevice::DrawSolidRect(float x, float y, float w, float h, unsigned long color)
{
    if (!Device || w <= 0.0f || h <= 0.0f) { return; }

    float x1 = SnapPixel(x);
    float y1 = SnapPixel(y);
    float x2 = SnapPixel(x + SnapSize(w));
    float y2 = SnapPixel(y + SnapSize(h));
    FUiVertex v[4] =
    {
        {
            x1, y1, 0.0f, 1.0f, color, 0.0f, 0.0f
        },
        {
            x2, y1, 0.0f, 1.0f, color, 1.0f, 0.0f
        },
        {
            x1, y2, 0.0f, 1.0f, color, 0.0f, 1.0f
        },
        {
            x2, y2, 0.0f, 1.0f, color, 1.0f, 1.0f
        }
    };
    Device->SetTexture(0, nullptr);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color)
{
    if (!Device || !texture || w <= 0.0f || h <= 0.0f) { return; }

    float x1 = SnapPixel(x);
    float y1 = SnapPixel(y);
    float x2 = SnapPixel(x + SnapSize(w));
    float y2 = SnapPixel(y + SnapSize(h));
    FUiVertex v[4] =
    {
        {
            x1, y1, 0.0f, 1.0f, color, u1, v1
        },
        {
            x2, y1, 0.0f, 1.0f, color, u2, v1
        },
        {
            x1, y2, 0.0f, 1.0f, color, u1, v2
        },
        {
            x2, y2, 0.0f, 1.0f, color, u2, v2
        }
    };
    Device->SetTexture(0, texture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTexCoord* coords, int32 textureWidth, int32 textureHeight, unsigned long color)
{
    if (!Device || !texture || !coords || textureWidth <= 0 || textureHeight <= 0 || w <= 0.0f || h <= 0.0f) { return; }

    auto u = [textureWidth](int32 value)
    {
        return static_cast<float>(value) / static_cast<float>(textureWidth);
    };
    auto v = [textureHeight](int32 value)
    {
        return static_cast<float>(value) / static_cast<float>(textureHeight);
    };
    FUiVertex verts[4] =
    {
        {
            SnapPixel(x), SnapPixel(y), 0.0f, 1.0f, color, u(coords[0].U), v(coords[0].V)
        },
        {
            SnapPixel(x + w), SnapPixel(y), 0.0f, 1.0f, color, u(coords[1].U), v(coords[1].V)
        },
        {
            SnapPixel(x), SnapPixel(y + h), 0.0f, 1.0f, color, u(coords[3].U), v(coords[3].V)
        },
        {
            SnapPixel(x + w), SnapPixel(y + h), 0.0f, 1.0f, color, u(coords[2].U), v(coords[2].V)
        }
    };
    Device->SetTexture(0, texture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTexturePiece(IDirect3DTexture9* texture, const FUiSpritePiece& piece, const FUiRectF& spriteRect, int32 textureWidth, int32 textureHeight, unsigned long color)
{
    if (!Device || !texture || textureWidth <= 0 || textureHeight <= 0) { return; }

    const float dx = spriteRect.X + static_cast<float>(std::min(piece.DstLeft, piece.DstRight)) * spriteRect.W;
    const float dy = spriteRect.Y + static_cast<float>(std::min(piece.DstTop, piece.DstBottom)) * spriteRect.H;
    const float dw = static_cast<float>(AbsInt(piece.DstRight - piece.DstLeft)) * spriteRect.W;
    const float dh = static_cast<float>(AbsInt(piece.DstBottom - piece.DstTop)) * spriteRect.H;

    if (dw <= 0.0f || dh <= 0.0f) { return; }

    if (piece.HasTexCoords) { DrawTextureQuadUv(texture, dx, dy, dw, dh, piece.TexCoords.data(), textureWidth, textureHeight, color); return; }

    int32 srcLeft = piece.SrcLeft;
    int32 srcTop = piece.SrcTop;
    int32 srcRight = piece.SrcRight;
    int32 srcBottom = piece.SrcBottom;

    if (piece.DstRight < piece.DstLeft)
    {
        std::swap(srcLeft, srcRight);
    }

    if (piece.DstBottom < piece.DstTop)
    {
        std::swap(srcTop, srcBottom);
    }

    float u1 = static_cast<float>(srcLeft) / static_cast<float>(textureWidth);
    float v1 = static_cast<float>(srcTop) / static_cast<float>(textureHeight);
    float u2 = static_cast<float>(srcRight) / static_cast<float>(textureWidth);
    float v2 = static_cast<float>(srcBottom) / static_cast<float>(textureHeight);
    DrawTextureQuad(texture, dx, dy, dw, dh, u1, v1, u2, v2, color);
}

bool FD3D9RenderDevice::DrawTextureResource(FDrawContext& ctx, std::string_view textureName, const FUiRectF& dst, float alpha)
{
    FD3D9TextureEntry* texture = LoadTextureByName(ctx.Resources, textureName, ctx.Logger);

    if (!texture || !texture->Texture) { return false; }

    const int32 designW = std::max(1, ctx.Ui.DesignWidth());
    const int32 designH = std::max(1, ctx.Ui.DesignHeight());
    const int32 srcW = std::min(texture->Width, designW);
    const int32 srcH = std::min(texture->Height, designH);
    const float u2 = static_cast<float>(srcW) / static_cast<float>(std::max(1, texture->Width));
    const float v2 = static_cast<float>(srcH) / static_cast<float>(std::max(1, texture->Height));
    unsigned char a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    DrawTextureQuad(texture->Texture, dst.X, dst.Y, dst.W, dst.H, 0.0f, 0.0f, u2, v2, Argb(a, 255, 255, 255));
    return true;
}

bool FD3D9RenderDevice::DrawSprite(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, float alpha)
{
    const unsigned char a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return DrawSpriteTinted(ctx, window, spriteName, dst, Argb(a, 255, 255, 255));
}

bool FD3D9RenderDevice::DrawSpriteTinted(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, unsigned long color)
{
    const FUiSpriteDef* sprite = FindSprite(window, spriteName);

    if (!sprite) { return false; }

    const float sx = dst.W / static_cast<float>(std::max(1, sprite->Width));
    const float sy = dst.H / static_cast<float>(std::max(1, sprite->Height));
    FUiRectF spriteRect
    {
        dst.X, dst.Y, sx, sy
    };
    bool drew = false;

    for (const auto& piece : sprite->Pieces)
    {
        FD3D9TextureEntry* texture = LoadTextureByName(ctx.Resources, piece.TextureName, ctx.Logger);

        if (!texture || !texture->Texture)
        {
            continue;
        }

        DrawTexturePiece(texture->Texture, piece, spriteRect, texture->Width, texture->Height, color);
        drew = true;
    }

    return drew;
}

void FD3D9RenderDevice::DrawTextRect(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex)
{
    if (!Device || text.empty() || rect.W <= 1.0f || rect.H <= 1.0f) { return; }

    const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);

    if (!font || !font->IsValid()) { return; }

    std::vector<uint8> bytes = font->EncodeUtf8ToCp1251(text);

    if (bytes.empty()) { return; }

    const float scale = std::max(0.5f, ctx.Scale);
    const int32 textWidth = font->MeasureCodepageText(bytes);
    float x = rect.X;

    if (center)
    {
        x += std::max(0.0f, (rect.W - static_cast<float>(textWidth) * scale) * 0.5f);
    }

    const float lineHeight = static_cast<float>(font->LineHeight()) * scale;
    const float y = rect.Y + std::max(0.0f, (rect.H - lineHeight) * 0.5f);
    const unsigned long fontColor = PremultiplyDiffuse(color);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    for (uint8 ch : bytes)
    {
        if (ch < 32) { continue; }

        const FD3D9BitmapGlyph& glyph = font->Glyph(ch);

        if (x > rect.X + rect.W) { break; }

        if (glyph.SourceW > 0 && glyph.SourceH > 0 && ch != 32)
        {
            float dx = x + static_cast<float>(glyph.XOffset) * scale;
            float dy = y + static_cast<float>(font->Baseline() - glyph.YOffset) * scale;
            float dw = static_cast<float>(glyph.SourceW) * scale;
            float dh = static_cast<float>(glyph.SourceH) * scale;
            float u1 = static_cast<float>(glyph.SourceX) / static_cast<float>(font->Width());
            float v1 = static_cast<float>(glyph.SourceY) / static_cast<float>(font->Height());
            float u2 = static_cast<float>(glyph.SourceX + glyph.SourceW) / static_cast<float>(font->Width());
            float v2 = static_cast<float>(glyph.SourceY + glyph.SourceH) / static_cast<float>(font->Height());
            DrawTextureQuad(font->AtlasTexture(), dx, dy, dw, dh, u1, v1, u2, v2, fontColor);
        }

        x += static_cast<float>(glyph.Advance) * scale;
    }

    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

void FD3D9RenderDevice::DrawControl(FDrawContext& ctx, const FUiWindowDef& window, const FUiControlDef& control, const FUiRectF& windowRect)
{
    if (control.Hidden) { return; }

    const FUiActionState& state = ctx.Ui.ActionState();
    FUiRectF r
    {
        windowRect.X + control.Rect.X * ctx.Scale, windowRect.Y + control.Rect.Y * ctx.Scale, control.Rect.W * ctx.Scale, control.Rect.H * ctx.Scale
    };

    if (Common::EqualsNoCase(control.ClassId, "IMAGE"))
    {
        if (!Common::EqualsNoCase(control.ImageName, "black") && !control.ImageName.empty())
        {
            DrawSprite(ctx, window, control.ImageName, r.W > 0.0f && r.H > 0.0f ? r : windowRect);
        }

        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "BUTTON"))
    {
        const bool modalDisabled = ctx.Ui.HasModalDialog() && Common::EqualsNoCase(window.Name, ctx.Ui.ActiveModalWindow().Name) && !ctx.Ui.IsModalActionAllowed(control);
        std::string sprite = SelectButtonSprite(control, state);

        if (!sprite.empty())
        {
            DrawSprite(ctx, window, sprite, r);
        }

        std::string text = TextForControl(ctx.Ui, window, control);

        if (!text.empty())
        {
            unsigned long color = ColorToArgb((control.Disabled || modalDisabled) ? control.DisabledColor : (state.HoverControlId == control.Id ? control.FocusColor : control.TextColor));
            DrawTextRect(ctx, r, text, color, true, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "CHECKBOX"))
    {
        std::string sprite;

        if (state.SaveLogin && !control.CheckedImage.empty())
        {
            sprite = control.CheckedImage;
        }
        else if (state.HoverControlId == control.Id && !control.FocusedImage.empty())
        {
            sprite = control.FocusedImage;
        }
        else
        {
            sprite = control.UncheckedImage;
        }

        if (!sprite.empty())
        {
            DrawSprite(ctx, window, sprite, r);
        }

        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "RADIOBUTTON"))
    {
        std::string sprite = ctx.Ui.SelectedCharacterSlot() == control.Id - 63 ? control.CheckedImage : control.UncheckedImage;

        if (!sprite.empty())
        {
            DrawSprite(ctx, window, sprite, FUiRectF{r.X + static_cast<float>(control.ImageOffset.X) * ctx.Scale, r.Y + static_cast<float>(control.ImageOffset.Y) * ctx.Scale, r.W, r.H});
        }

        std::string text = TextForControl(ctx.Ui, window, control);

        if (!text.empty())
        {
            unsigned long color = ColorToArgb(control.Disabled ? control.DisabledColor : (state.HoverControlId == control.Id ? control.FocusColor : control.TextColor));
            DrawTextRect(ctx, r, text, color, false, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "SPINBUTTON"))
    {
        FUiSubButtonDef leftButton = control.RightButton;
        FUiSubButtonDef rightButton = control.LeftButton;

        if (leftButton.W <= 0 || leftButton.H <= 0)
        {
            leftButton.X = 1;
            leftButton.Y = 4;
            leftButton.W = 18;
            leftButton.H = 18;
        }

        if (rightButton.W <= 0 || rightButton.H <= 0)
        {
            rightButton.X = 19;
            rightButton.Y = 4;
            rightButton.W = 18;
            rightButton.H = 18;
        }

        const FUiRectF left
        {
            r.X + static_cast<float>(leftButton.X) * ctx.Scale, r.Y + static_cast<float>(leftButton.Y) * ctx.Scale, static_cast<float>(leftButton.W) * ctx.Scale, static_cast<float>(leftButton.H) * ctx.Scale
        };
        const FUiRectF right
        {
            r.X + static_cast<float>(rightButton.X) * ctx.Scale, r.Y + static_cast<float>(rightButton.Y) * ctx.Scale, static_cast<float>(rightButton.W) * ctx.Scale, static_cast<float>(rightButton.H) * ctx.Scale
        };
        const bool hotLeft = state.HoverControlId == control.Id && state.SpinHoverDirection < 0;
        const bool hotRight = state.HoverControlId == control.Id && state.SpinHoverDirection > 0;
        const bool pressedLeft = state.PressedControlId == control.Id && state.SpinPressedDirection < 0;
        const bool pressedRight = state.PressedControlId == control.Id && state.SpinPressedDirection > 0;
        DrawSprite(ctx, window, SelectSubButtonSprite(leftButton, control.Disabled, hotLeft, pressedLeft, "sl_normal", "sl_focus", "sl_push", "sl_disabled"), left);
        DrawSprite(ctx, window, SelectSubButtonSprite(rightButton, control.Disabled, hotRight, pressedRight, "sr_normal", "sr_focus", "sr_push", "sr_disabled"), right);
        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "SLOT"))
    {
        const std::string fill = !control.SlotFullImage.empty() ? control.SlotFullImage : control.SlotEmptyImage;

        if (!fill.empty())
        {
            DrawSpriteTinted(ctx, window, fill, r, Argb(128, 0x14, 0x14, 0x14));
        }

        if (!control.SlotBorderImage.empty())
        {
            DrawSpriteTinted(ctx, window, control.SlotBorderImage, r, Argb(255, 0x9e, 0x7c, 0x6a));
        }

        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "PROGRESS_BAR") || Common::EqualsNoCase(control.ClassId, "PROGRESSBAR"))
    {
        unsigned long color = control.Id == 42 || control.Id == 46 ? Argb(210, 48, 109, 210) : control.Id == 47 ? Argb(210, 210, 190, 45) : Argb(210, 70, 170, 60);

        if (Common::EqualsNoCase(control.DrawSpriteName, "blue"))
        {
            color = Argb(210, 48, 109, 210);
        }
        else if (Common::EqualsNoCase(control.DrawSpriteName, "yellow"))
        {
            color = Argb(210, 210, 190, 45);
        }
        else if (Common::EqualsNoCase(control.DrawSpriteName, "green"))
        {
            color = Argb(210, 70, 170, 60);
        }

        const float ratio = ctx.Ui.Mode() == EUiRuntimeMode::CharacterSelect ? ctx.Ui.CharacterProgressRatio(control.Id) : 1.0f;
        DrawSolidRect(r.X, r.Y, r.W, r.H, Argb(210, 30, 28, 24));
        DrawSolidRect(r.X, r.Y, r.W * std::clamp(ratio, 0.0f, 1.0f), r.H, color);
        std::string status = ctx.Ui.Mode() == EUiRuntimeMode::CharacterSelect ? ctx.Ui.CharacterProgressText(control) : std::string{};

        if (!status.empty() && !control.StatusShow.empty())
        {
            FUiRectF sr
            {
                r.X + static_cast<float>(control.StatusPos.X) * ctx.Scale, r.Y + static_cast<float>(control.StatusPos.Y) * ctx.Scale, std::max(44.0f * ctx.Scale, r.W), 12.0f * ctx.Scale
            };
            DrawTextRect(ctx, sr, status, ColorToArgb(control.TextColor), (control.Id == 41 || control.Id == 42) ? true : control.TextCenter, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }

    if (Common::EqualsNoCase(control.ClassId, "EDIT"))
    {
        std::string text;

        if (ctx.Ui.HasModalDialog() && Common::EqualsNoCase(window.Name, ctx.Ui.ActiveModalWindow().Name))
        {
            text = TextForControl(ctx.Ui, window, control);
        }
        else if (ctx.Ui.Mode() == EUiRuntimeMode::CharacterSelect)
        {
            text = TextForControl(ctx.Ui, window, control);
        }
        else if (control.Id == 7)
        {
            text = state.LoginText;
        }
        else if (control.Id == 8 || control.Password)
        {
            text.assign(state.PasswordText.size(), '*');
        }

        if (!text.empty())
        {
            DrawTextRect(ctx, r, text, ColorToArgb(control.Disabled ? control.DisabledColor : control.TextColor), control.TextCenter, control.Font >= 0 ? control.Font : window.Font);
        }

        if (state.FocusedControlId == control.Id)
        {
            DrawSolidRect(r.X, r.Y + r.H - 1.0f, r.W, 1.0f, Argb(190, 237, 208, 161));
        }

        return;
    }

    if (IsTextLikeControl(control))
    {
        std::string text = TextForControl(ctx.Ui, window, control);

        if (!text.empty())
        {
            DrawTextRect(ctx, r, text, ColorToArgb(control.Disabled ? control.DisabledColor : control.TextColor), control.TextCenter, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }
}

bool FD3D9RenderDevice::DrawWindow(FDrawContext& ctx, const FUiWindowDef& window, const FUiRectF& dst)
{
    if (!window.DrawNone && !window.DrawSpriteName.empty())
    {
        DrawSprite(ctx, window, window.DrawSpriteName, dst);
    }

    std::string title = window.TextKey.empty() ? std::string{} : ctx.Ui.ResolveText(window.TextKey);

    if (!title.empty())
    {
        FUiRectF tr
        {
            dst.X + 10.0f * ctx.Scale, dst.Y + 8.0f * ctx.Scale, std::max(1.0f, dst.W - 34.0f * ctx.Scale), 20.0f * ctx.Scale
        };

        if (window.TitleRect.W > window.TitleRect.X && window.TitleRect.H > window.TitleRect.Y)
        {
            tr = FUiRectF
            {
                dst.X + window.TitleRect.X * ctx.Scale, dst.Y + window.TitleRect.Y * ctx.Scale, static_cast<float>(window.TitleRect.W - window.TitleRect.X) * ctx.Scale, static_cast<float>(window.TitleRect.H - window.TitleRect.Y) * ctx.Scale
            };
        }

        DrawTextRect(ctx, tr, title, ColorToArgb(window.TextColor), false, window.Font);
    }

    for (const auto& control : window.Controls)
    {
        DrawControl(ctx, window, control, dst);
    }

    return true;
}

void FD3D9RenderDevice::DrawModalDialog(FDrawContext& ctx, const RECT& rect)
{
    if (!ctx.Ui.HasModalDialog()) { return; }

    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    DrawSolidRect(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(width), static_cast<float>(height), Argb(120, 0, 0, 0));
    const FUiWindowDef& window = ctx.Ui.ActiveModalWindow();

    if (window.Name.empty()) { return; }

    FUiRectF wr = ctx.Ui.BuildWindowRect(window, rect);
    DrawWindow(ctx, window, wr);

    if (!ctx.Ui.ModalMessage().empty() && window.Controls.empty())
    {
        DrawTextRect(ctx, FUiRectF{wr.X + 18.0f * ctx.Scale, wr.Y + wr.H * 0.38f, wr.W - 36.0f * ctx.Scale, 44.0f * ctx.Scale}, ctx.Ui.ModalMessage(), Argb(235, 237, 208, 161), true, window.Font);
    }
}

void FD3D9RenderDevice::DrawStatusOverlay(FDrawContext& ctx, const FUiRuntime& ui, const FUiRectF& designRect)
{
    const float margin = 18.0f * ctx.Scale;
    const float rowH = 18.0f * ctx.Scale;
    const float panelH = (2.0f + static_cast<float>(ui.StatusLines().size())) * rowH + margin;
    FUiRectF panel
    {
        designRect.X + margin, designRect.Y + designRect.H - panelH - margin, designRect.W - margin * 2.0f, panelH
    };
    DrawSolidRect(panel.X, panel.Y, panel.W, panel.H, Argb(110, 0, 0, 0));
    DrawSolidRect(panel.X + margin, panel.Y + margin * 0.5f, std::max(1.0f, (panel.W - margin * 2.0f) * ui.Progress()), 3.0f * ctx.Scale, Argb(220, 237, 208, 161));
    DrawTextRect(ctx, FUiRectF{panel.X + margin, panel.Y + rowH, panel.W - margin * 2.0f, rowH}, ui.Stage(), Argb(230, 237, 208, 161), false, 0);
    float y = panel.Y + rowH * 2.0f;

    for (const auto& line : ui.StatusLines())
    {
        DrawTextRect(ctx, FUiRectF{panel.X + margin, y, panel.W - margin * 2.0f, rowH}, line, Argb(210, 255, 255, 255), false, 0);
        y += rowH;
    }
}


void FD3D9RenderDevice::UpdateFrameStats(double frameMilliseconds)
{
    if (frameMilliseconds <= 0.0)
    {
        return;
    }
    ++Stats.FrameCounter;
    Stats.LastMilliseconds = frameMilliseconds;
    if (!Stats.Initialized)
    {
        Stats.Initialized = true;
        Stats.AverageMilliseconds = frameMilliseconds;
        Stats.MinMilliseconds = frameMilliseconds;
        Stats.MaxMilliseconds = frameMilliseconds;
    }
    else
    {
        Stats.AverageMilliseconds = Stats.AverageMilliseconds * 0.92 + frameMilliseconds * 0.08;
    }
    Stats.History[Stats.HistoryHead] = frameMilliseconds;
    Stats.HistoryHead = (Stats.HistoryHead + 1) % Stats.History.size();
    Stats.HistoryCount = std::min(Stats.HistoryCount + 1, Stats.History.size());
    std::vector<double> samples;
    samples.reserve(Stats.HistoryCount);
    for (size_t i = 0; i < Stats.HistoryCount; ++i)
    {
        samples.push_back(Stats.History[i]);
    }
    std::sort(samples.begin(), samples.end());
    if (!samples.empty())
    {
        Stats.MinMilliseconds = samples.front();
        Stats.MaxMilliseconds = samples.back();
        const size_t p95Index = std::min(samples.size() - 1, static_cast<size_t>(static_cast<double>(samples.size() - 1) * 0.95));
        Stats.P95Milliseconds = samples[p95Index];
        Stats.LowFps = Stats.MaxMilliseconds > 0.0 ? 1000.0 / Stats.MaxMilliseconds : 0.0;
    }
    if (frameMilliseconds > 33.333)
    {
        ++Stats.DropFrames;
    }
    if (frameMilliseconds > 100.0)
    {
        ++Stats.HitchFrames;
    }
    Stats.SecondAccumulator += frameMilliseconds / 1000.0;
    ++Stats.SecondFrames;
    if (Stats.SecondAccumulator >= 0.5)
    {
        Stats.CurrentFps = static_cast<uint32>(std::max(0.0, static_cast<double>(Stats.SecondFrames) / Stats.SecondAccumulator + 0.5));
        Stats.SecondAccumulator = 0.0;
        Stats.SecondFrames = 0;
    }
}

void FD3D9RenderDevice::DrawRenderStatsOverlay(FDrawContext& ctx, const RECT& clientRect, const FD3D9GameWorldRenderStats* worldStats)
{
    if (!Stats.Initialized)
    {
        return;
    }
    std::vector<std::string> lines;
    const double instantFps = Stats.LastMilliseconds > 0.0 ? 1000.0 / Stats.LastMilliseconds : 0.0;
    const uint32 fps = Stats.CurrentFps != 0 ? Stats.CurrentFps : static_cast<uint32>(instantFps + 0.5);
    lines.push_back("FPS " + std::to_string(fps) + "   MS " + FormatOneDecimal(Stats.LastMilliseconds));
    lines.push_back("AVG " + FormatOneDecimal(Stats.AverageMilliseconds) + "  P95 " + FormatOneDecimal(Stats.P95Milliseconds));
    lines.push_back("LOW " + FormatOneDecimal(Stats.LowFps) + "  D " + std::to_string(Stats.DropFrames) + " H " + std::to_string(Stats.HitchFrames));
    if (worldStats)
    {
        lines.push_back("DRW " + std::to_string(worldStats->DrawCalls) + "  TRI " + FormatCompactCount(worldStats->Triangles));
        lines.push_back("T " + std::to_string(worldStats->TerrainInstances) + "/" + std::to_string(worldStats->TerrainResources) + "  S " + std::to_string(worldStats->StaticInstances) + "  G " + std::to_string(worldStats->GrassInstances));
    }
    const float scale = std::max(0.75f, ctx.Scale);
    const float margin = 10.0f * scale;
    const float rowH = 13.0f * scale;
    const float pad = 6.0f * scale;
    const float panelW = worldStats ? 214.0f * scale : 174.0f * scale;
    const float panelH = pad * 2.0f + rowH * static_cast<float>(lines.size());
    const int32 clientW = std::max<int32>(1, clientRect.right - clientRect.left);
    const int32 clientH = std::max<int32>(1, clientRect.bottom - clientRect.top);
    const float screenW = static_cast<float>(std::max<int32>(clientW, BackBufferWidth));
    const float screenH = static_cast<float>(std::max<int32>(clientH, BackBufferHeight));
    const float x = std::floor(std::max(0.0f, screenW - panelW - margin));
    const float y = std::floor(std::max(0.0f, screenH - panelH - margin));
    const unsigned long accent = Stats.LastMilliseconds > 33.333 ? Argb(225, 215, 82, 64) : Argb(225, 86, 180, 96);
    DrawSolidRect(x, y, panelW, panelH, Argb(145, 0, 0, 0));
    DrawSolidRect(x, y, 3.0f * scale, panelH, accent);
    float textY = y + pad - 1.0f * scale;
    for (const auto& line : lines)
    {
        DrawTextRect(ctx, FUiRectF{x + pad + 4.0f * scale, textY, panelW - pad * 2.0f, rowH}, line, Argb(235, 235, 235, 225), false, 0);
        textY += rowH;
    }
}

bool FD3D9RenderDevice::EnsureDeviceReady(int32 width, int32 height, FLogger* logger)
{
    if (!Device) { return false; }

    const HRESULT cooperative = Device->TestCooperativeLevel();

    if (cooperative == D3DERR_DEVICELOST) { return false; }

    const bool sizeChanged = width > 0 && height > 0 && (width != BackBufferWidth || height != BackBufferHeight);

    if (cooperative == D3DERR_DEVICENOTRESET || sizeChanged)
    {
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.hDeviceWindow = DeviceWindow;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth = static_cast<UINT>(std::max<int32>(1, width));
        pp.BackBufferHeight = static_cast<UINT>(std::max<int32>(1, height));
        pp.BackBufferCount = 1;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = D3DFMT_D24S8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        HRESULT hr = Device->Reset(&pp);

        if (FAILED(hr))
        {
            pp.AutoDepthStencilFormat = D3DFMT_D16;
            hr = Device->Reset(&pp);
        }

        if (FAILED(hr))
        {
            if (logger)
            {
                logger->Warning("D3D9 reset failed: hr=" + std::to_string(static_cast<long>(hr)));
            }

            return false;
        }

        BackBufferWidth = std::max<int32>(1, width);
        BackBufferHeight = std::max<int32>(1, height);

        if (logger)
        {
            logger->Info("D3D9 borderless backbuffer reset: " + std::to_string(BackBufferWidth) + "x" + std::to_string(BackBufferHeight));
        }
    }

    return true;
}

void FD3D9RenderDevice::ConfigureUiRenderState()
{
    if (!Device) { return; }

    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    Device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    Device->SetFVF(FVF_UI);
}

void FD3D9RenderDevice::PreloadUiTextures(const FResourceManager& resources, const FUiRuntime& ui, FLogger* logger)
{
    LoadTextureByName(resources, ui.LoginBackgroundTexture(), logger);

    for (const auto& [name, sprite] : ui.ConnectionWindow().Sprites)
    {
        (void)name;

        for (const auto& piece : sprite.Pieces)
        {
            LoadTextureByName(resources, piece.TextureName, logger);
        }
    }

    // Character-select, modal and game HUD textures are loaded lazily after login.

    FontCache.Preload(Device, resources, logger);

    if (logger)
    {
        logger->Info("D3D9 UI preload: texture_cache=" + std::to_string(TextureCache.size()));
    }
}

FStatus FD3D9RenderDevice::RenderUiDesktop(const FResourceManager& resources, const FWorldScene* worldScene, const FUiRuntime& ui, const RECT& rect, float deltaSeconds, const FGameMovementInput& gameInput, float lookDeltaX, float lookDeltaY, bool jumpRequested, FLogger* logger)
{
    FScopedDurationLog FrameProbe(logger, "frame.RenderUiDesktop", 18.0, "mode=" + std::to_string(static_cast<int>(ui.Mode())) + " delta=" + FormatDurationLogValue(static_cast<double>(deltaSeconds) * 1000.0));
    const auto frameStart = std::chrono::steady_clock::now();
    if (!Device) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 device is not initialized"); }

    if (!ui.IsReady()) { return FStatus::Error(EStatusCode::RuntimeError, "UI runtime is not initialized"); }

    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));

    if (!EnsureDeviceReady(width, height, logger)) { return FStatus::Ok(); }

    if (ui.Mode() != EUiRuntimeMode::Game)
    {
        if (ActiveWorldScene || FailedWorldScene || GameWorldScene.IsValid())
        {
            GameWorldScene.Shutdown();
            ActiveWorldScene = nullptr;
            FailedWorldScene = nullptr;
        }
    }
    else if (worldScene)
    {
        if (ActiveWorldScene != worldScene && GameWorldScene.IsValid())
        {
            GameWorldScene.Shutdown();
            ActiveWorldScene = nullptr;
        }

        if (ActiveWorldScene != worldScene && FailedWorldScene != worldScene)
        {
            std::wstring worldError;
            auto worldConfig = FD3D9GameWorldScene::DefaultConfig();
            const auto playerModel = CharacterScene.ExportSkinnedModel();
            const auto* playerModelPtr = playerModel.IsValid() ? &playerModel : nullptr;

            if (GameWorldScene.Initialize(DeviceWindow, Device, resources, *worldScene, worldConfig, 0.0, 0.0, 0.0, 0.0, worldError, logger, playerModelPtr))
            {
                ActiveWorldScene = worldScene;
                FailedWorldScene = nullptr;
                if (HasServerGameTime)
                {
                    GameWorldScene.SetGameTime(ServerGameTime);
                    ServerGameTimePending = false;
                }
            }
            else
            {
                GameWorldScene.Shutdown();
                ActiveWorldScene = nullptr;
                FailedWorldScene = worldScene;

                if (logger)
                {
                    logger->Warning("D3D9 game world scene initialization failed: " + Common::WideToUtf8(worldError));
                }
            }
        }

        if (GameWorldScene.IsValid())
        {
            if (ServerGameTimePending && HasServerGameTime)
            {
                GameWorldScene.SetGameTime(ServerGameTime);
                ServerGameTimePending = false;
            }
            if (lookDeltaX != 0.0f || lookDeltaY != 0.0f)
            {
                GameWorldScene.RotateView(lookDeltaX, lookDeltaY);
            }

            if (jumpRequested)
            {
                GameWorldScene.Jump();
            }

            std::wstring worldUpdateError;
            FScopedDurationLog Probe(logger, "frame.GameWorldScene.Update", 20.0);
            if (!GameWorldScene.Update(deltaSeconds, gameInput, worldUpdateError) && logger)
            {
                logger->Warning("D3D9 game world Update failed: " + Common::WideToUtf8(worldUpdateError));
            }
        }
    }

    const FUiRectF design = ui.BuildDesignRect(rect);
    const float scale = design.W / static_cast<float>(std::max(1, ui.DesignWidth()));
    FDrawContext ctx
    {
        resources, ui, logger, scale
    };
    const auto BeginSceneStart = std::chrono::steady_clock::now();
    HRESULT hr = Device->BeginScene();
    LogDurationProbe(logger, "d3d.BeginScene", DurationLogMillisecondsSince(BeginSceneStart), 5.0);

    if (FAILED(hr)) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 BeginScene failed: hr=" + std::to_string(static_cast<long>(hr))); }

    bool backgroundDrawn = true;

    if (ui.Mode() == EUiRuntimeMode::Game && worldScene && GameWorldScene.IsValid())
    {
        FScopedDurationLog Probe(logger, "frame.GameWorldScene.RenderInsideScene", 8.0);
        GameWorldScene.RenderInsideScene(rect);
    }
    else
    {
        Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        if (ui.Mode() == EUiRuntimeMode::CharacterSelect)
        {
            CharacterScene.Draw(Device, resources, ui.SelectedCharacterSceneAppearance(), ui.CharacterSceneAngle(), ui.CharacterCameraFocusId(), rect, deltaSeconds, logger);
        }
    }

    ConfigureUiRenderState();

    if (ui.Mode() == EUiRuntimeMode::Login)
    {
        backgroundDrawn = DrawTextureResource(ctx, ui.LoginBackgroundTexture(), design);
        DrawWindow(ctx, ui.ConnectionWindow(), ui.BuildConnectionRect(rect));
    }
    else if (ui.Mode() == EUiRuntimeMode::CharacterSelect)
    {
        if (!ui.PickPersonWindow().Name.empty())
        {
            DrawWindow(ctx, ui.PickPersonWindow(), ui.BuildWindowRect(ui.PickPersonWindow(), rect));
        }
    }
    else
    {
        const auto& gameWindows = ui.GameWindows();
        const auto& gameVisibility = ui.GameWindowVisibility();
        for (size_t i = 0; i < gameWindows.size(); ++i)
        {
            if (i < gameVisibility.size() && !gameVisibility[i]) { continue; }
            DrawWindow(ctx, gameWindows[i], ui.BuildWindowRect(gameWindows[i], rect));
        }

        if (gameWindows.empty())
        {
            DrawSolidRect(design.X + 24.0f, design.Y + design.H - 54.0f, design.W - 48.0f, 30.0f, Argb(150, 0, 0, 0));
            DrawTextRect(ctx, FUiRectF{design.X + 34.0f, design.Y + design.H - 50.0f, design.W - 68.0f, 22.0f}, ctx.Ui.GameChatDraft().empty() ? std::string("_") : ctx.Ui.GameChatDraft() + "_", Argb(230, 237, 208, 161), false, 0);
        }
    }

    DrawModalDialog(ctx, rect);
    FD3D9GameWorldRenderStats currentWorldStats;
    const FD3D9GameWorldRenderStats* currentWorldStatsPtr = nullptr;
    if (ui.Mode() == EUiRuntimeMode::Game && GameWorldScene.IsValid())
    {
        currentWorldStats = GameWorldScene.RenderStats();
        currentWorldStatsPtr = &currentWorldStats;
    }
    DrawRenderStatsOverlay(ctx, rect, currentWorldStatsPtr);
    {
        const auto EndSceneStart = std::chrono::steady_clock::now();
        Device->EndScene();
        LogDurationProbe(logger, "d3d.EndScene", DurationLogMillisecondsSince(EndSceneStart), 5.0);
    }
    const auto PresentStart = std::chrono::steady_clock::now();
    hr = Device->Present(nullptr, nullptr, nullptr, nullptr);
    LogDurationProbe(logger, "d3d.Present", DurationLogMillisecondsSince(PresentStart), 20.0);

    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) { return FStatus::Ok(); }

    if (FAILED(hr)) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 Present failed: hr=" + std::to_string(static_cast<long>(hr))); }

    const auto frameEnd = std::chrono::steady_clock::now();
    UpdateFrameStats(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count());

    if (!backgroundDrawn) { return FStatus::Error(EStatusCode::NotFound, "login background texture was not rendered: " + ui.LoginBackgroundTexture()); }

    return FStatus::Ok();
}

FD3D9ShaderInventory FD3D9RenderDevice::InspectShaderResources(const FResourceManager& resources, FLogger* logger) const
{
    FD3D9ShaderInventory inventory;

    for (const auto& record : resources.Catalog().All())
    {
        std::string path = record.RelativePath.generic_string();
        std::string lower = Common::ToLower(path);
        bool isShader = lower.find("shader") != std::string::npos || lower.find("shaders/") != std::string::npos || lower.find("shaders\\") != std::string::npos;

        if (!isShader) { continue; }

        if (lower.find("vertex") != std::string::npos || lower.find("/vs") != std::string::npos)
        {
            ++inventory.VertexShaders;
        }
        else if (lower.find("pixel") != std::string::npos || lower.find("/ps") != std::string::npos)
        {
            ++inventory.PixelShaders;
        }

        if (inventory.Samples.size() < 8)
        {
            inventory.Samples.push_back(path);
        }
    }

    if (logger)
    {
        logger->Info("D3D9 shader resource inventory: vertex=" + std::to_string(inventory.VertexShaders) + ", pixel=" + std::to_string(inventory.PixelShaders) + ", samples=" + std::to_string(inventory.Samples.size()));

        for (size_t i = 0; i < inventory.Samples.size(); ++i)
        {
            logger->Info("D3D9 shader sample[" + std::to_string(i) + "]: " + inventory.Samples[i]);
        }
    }

    return inventory;
}
