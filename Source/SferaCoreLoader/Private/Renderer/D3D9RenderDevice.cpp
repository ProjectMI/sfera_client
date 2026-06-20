#include "Renderer/D3D9RenderDevice.h"
#include "FileSystem/PathUtils.h"
#include "ResourceLoader/ResourceTypes.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>

namespace Sfera {
namespace {
constexpr unsigned int D3DX_DEFAULT_VALUE = static_cast<unsigned int>(-1);
constexpr unsigned long D3DX_FILTER_NONE_VALUE = 1ul;

struct FUiVertex { float X; float Y; float Z; float Rhw; unsigned long Color; float U; float V; };
constexpr unsigned long FVF_UI = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }); return value; }
std::string StemLower(const FPath& path) { return Lower(path.stem().string()); }
unsigned long Argb(unsigned char a, unsigned char r, unsigned char g, unsigned char b) { return (static_cast<unsigned long>(a) << 24) | (static_cast<unsigned long>(r) << 16) | (static_cast<unsigned long>(g) << 8) | static_cast<unsigned long>(b); }
unsigned long ColorToArgb(const FUiColor& color) { return Argb(static_cast<unsigned char>(std::clamp(color.A, 0, 255)), static_cast<unsigned char>(std::clamp(color.R, 0, 255)), static_cast<unsigned char>(std::clamp(color.G, 0, 255)), static_cast<unsigned char>(std::clamp(color.B, 0, 255))); }
int ColorR(unsigned long color) { return static_cast<int>((color >> 16) & 0xff); }
int ColorG(unsigned long color) { return static_cast<int>((color >> 8) & 0xff); }
int ColorB(unsigned long color) { return static_cast<int>(color & 0xff); }
int ColorA(unsigned long color) { return static_cast<int>((color >> 24) & 0xff); }
unsigned long PremultiplyDiffuse(unsigned long color) { unsigned int a = static_cast<unsigned int>(ColorA(color)); return Argb(static_cast<unsigned char>(a), static_cast<unsigned char>((static_cast<unsigned int>(ColorR(color)) * a) / 255U), static_cast<unsigned char>((static_cast<unsigned int>(ColorG(color)) * a) / 255U), static_cast<unsigned char>((static_cast<unsigned int>(ColorB(color)) * a) / 255U)); }
float SnapPixel(float v) { return std::floor(v + 0.5f) - 0.5f; }
float SnapSize(float v) { return std::max(1.0f, std::floor(v + 0.5f)); }
bool EqualsNoCase(std::string_view a, std::string_view b) { return Lower(std::string(a)) == Lower(std::string(b)); }

const FUiSpriteDef* FindSprite(const FUiWindowDef& window, std::string_view name) {
    auto it = window.Sprites.find(Lower(std::string(name)));
    if (it == window.Sprites.end()) { return nullptr; }
    return &it->second;
}

std::string SelectButtonSprite(const FUiControlDef& control, const FUiActionState& state) {
    if (control.Disabled && !control.LeftButton.DisabledImage.empty()) { return control.LeftButton.DisabledImage; }
    if (state.PressedControlId == control.Id && !control.CheckedImage.empty()) { return control.CheckedImage; }
    if (state.HoverControlId == control.Id && !control.FocusedImage.empty()) { return control.FocusedImage; }
    if (!control.UncheckedImage.empty()) { return control.UncheckedImage; }
    return control.DrawSpriteName;
}

std::string TextForControl(const FUiRuntime& ui, const FUiControlDef& control) { return control.TextKey.empty() ? std::string{} : ui.ResolveText(control.TextKey); }
}

struct FD3D9RenderDevice::FDrawContext {
    const FResourceManager& Resources;
    const FUiRuntime& Ui;
    FLogger* Logger = nullptr;
    float Scale = 1.0f;
};

FD3D9RenderDevice::FD3D9RenderDevice() = default;
FD3D9RenderDevice::~FD3D9RenderDevice() { Shutdown(); }

FStatus FD3D9RenderDevice::Initialize(HWND__* hwnd, int32 width, int32 height, FLogger* logger) {
    Shutdown();
    D3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!D3D) { return FStatus::Error(EStatusCode::RuntimeError, "Direct3DCreate9 failed"); }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = static_cast<UINT>(width);
    pp.BackBufferHeight = static_cast<UINT>(height);
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &Device);
    if (FAILED(hr)) { hr = D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &Device); }
    if (FAILED(hr)) { Shutdown(); return FStatus::Error(EStatusCode::RuntimeError, "IDirect3D9::CreateDevice failed: hr=" + std::to_string(static_cast<long>(hr))); }
    if (logger) { logger->Info("D3D9 device initialized: backbuffer=" + std::to_string(width) + "x" + std::to_string(height)); }
    return EnsureD3DX(logger);
}

void FD3D9RenderDevice::ReleaseTextures() {
    for (auto& item : TextureCache) { if (item.second.Texture) { item.second.Texture->Release(); item.second.Texture = nullptr; } }
    TextureCache.clear();
    FontCache.Release();
}

void FD3D9RenderDevice::Shutdown() {
    ReleaseTextures();
    if (Device) { Device->Release(); Device = nullptr; }
    if (D3D) { D3D->Release(); D3D = nullptr; }
    if (D3DXModule) { FreeLibrary(D3DXModule); D3DXModule = nullptr; }
    D3DXCreateTextureFromFileInMemoryExFn = nullptr;
    ReportedD3DXMissing = false;
}

FStatus FD3D9RenderDevice::EnsureD3DX(FLogger* logger) {
    if (D3DXCreateTextureFromFileInMemoryExFn) { return FStatus::Ok(); }
    const char* dlls[] = {"d3dx9_43.dll", "d3dx9_42.dll", "d3dx9_41.dll", "d3dx9_40.dll", "d3dx9_39.dll", "d3dx9_38.dll", "d3dx9_37.dll", "d3dx9_36.dll"};
    for (const char* dll : dlls) {
        D3DXModule = LoadLibraryA(dll);
        if (!D3DXModule) { continue; }
        auto* proc = reinterpret_cast<FD3DXCreateTextureFromFileInMemoryExPtr>(GetProcAddress(D3DXModule, "D3DXCreateTextureFromFileInMemoryEx"));
        if (proc) { D3DXCreateTextureFromFileInMemoryExFn = proc; if (logger) { logger->Info(std::string("D3D9 texture loader: ") + dll); } return FStatus::Ok(); }
        FreeLibrary(D3DXModule);
        D3DXModule = nullptr;
    }
    if (!ReportedD3DXMissing && logger) { logger->Error("D3D9 texture loader unavailable: d3dx9_xx.dll is required"); ReportedD3DXMissing = true; }
    return FStatus::Error(EStatusCode::RuntimeError, "D3DXCreateTextureFromFileInMemoryEx unavailable");
}

std::string FD3D9RenderDevice::ResolveTextureResourceName(const FResourceManager& resources, std::string_view textureName) const {
    std::string name(textureName);
    if (name.empty()) { return {}; }
    std::string lower = Lower(name);
    bool hasExt = lower.find('.') != std::string::npos;
    std::vector<std::string> bases = {name, "textures/" + name, "Textures/" + name, "effects/" + name, "Effects/" + name, "interface/" + name, "Interface/" + name, "xadd/" + name, "XADD/" + name};
    std::vector<std::string> exts = hasExt ? std::vector<std::string>{""} : std::vector<std::string>{".dds", ".tga", ".bmp", ".png", ".jpg", ".jpeg"};
    for (const auto& base : bases) { for (const auto& ext : exts) { if (auto record = resources.Catalog().FindByLogicalName(base + ext)) { return record->RelativePath.generic_string(); } } }
    for (const auto& record : resources.Catalog().All()) { EResourceKind kind = GuessResourceKind(record.RelativePath); if (kind != EResourceKind::Texture && Lower(record.RelativePath.extension().string()) != ".dds") { continue; } if (StemLower(record.RelativePath) == lower || Lower(record.RelativePath.filename().string()) == lower) { return record.RelativePath.generic_string(); } }
    return {};
}

FD3D9TextureEntry* FD3D9RenderDevice::LoadTextureByName(const FResourceManager& resources, std::string_view textureName, FLogger* logger) {
    std::string key = Lower(std::string(textureName));
    if (key.empty()) { return nullptr; }
    auto it = TextureCache.find(key);
    if (it != TextureCache.end()) { return it->second.Texture ? &it->second : nullptr; }
    FD3D9TextureEntry entry;
    entry.Tried = true;
    if (!Device) { entry.Error = "device not initialized"; TextureCache.emplace(key, entry); return nullptr; }
    FStatus d3dx = EnsureD3DX(logger);
    if (!d3dx.IsOk()) { entry.Error = d3dx.Message(); TextureCache.emplace(key, entry); return nullptr; }
    std::string logical = ResolveTextureResourceName(resources, textureName);
    if (logical.empty()) { entry.Error = "resource not found"; TextureCache.emplace(key, entry); if (logger) { logger->Warning("UI texture not found: " + std::string(textureName)); } return nullptr; }
    auto blob = resources.Load(logical);
    if (!blob.IsOk()) { entry.Error = blob.Status().Message(); TextureCache.emplace(key, entry); if (logger) { logger->Warning("UI texture load failed: " + logical + " - " + entry.Error); } return nullptr; }
    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryExFn(Device, blob.Value().Bytes.data(), static_cast<unsigned int>(blob.Value().Bytes.size()), D3DX_DEFAULT_VALUE, D3DX_DEFAULT_VALUE, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_FILTER_NONE_VALUE, D3DX_FILTER_NONE_VALUE, 0, nullptr, nullptr, &texture);
    if (FAILED(hr) || !texture) { entry.Error = "D3DX load failed hr=" + std::to_string(static_cast<long>(hr)); TextureCache.emplace(key, entry); if (logger) { logger->Warning("UI texture decode failed: " + logical + " - " + entry.Error); } return nullptr; }
    D3DSURFACE_DESC desc{};
    texture->GetLevelDesc(0, &desc);
    entry.Texture = texture;
    entry.Width = static_cast<int32>(desc.Width);
    entry.Height = static_cast<int32>(desc.Height);
    entry.ResourceName = logical;
    auto result = TextureCache.emplace(key, entry);
    if (logger) { logger->Info("UI texture loaded: " + std::string(textureName) + " -> " + logical + ", size=" + std::to_string(entry.Width) + "x" + std::to_string(entry.Height)); }
    return &result.first->second;
}

void FD3D9RenderDevice::DrawSolidRect(float x, float y, float w, float h, unsigned long color) {
    if (!Device || w <= 0.0f || h <= 0.0f) { return; }
    float x1 = SnapPixel(x);
    float y1 = SnapPixel(y);
    float x2 = SnapPixel(x + SnapSize(w));
    float y2 = SnapPixel(y + SnapSize(h));
    FUiVertex v[4] = {{x1, y1, 0.0f, 1.0f, color, 0.0f, 0.0f}, {x2, y1, 0.0f, 1.0f, color, 1.0f, 0.0f}, {x1, y2, 0.0f, 1.0f, color, 0.0f, 1.0f}, {x2, y2, 0.0f, 1.0f, color, 1.0f, 1.0f}};
    Device->SetTexture(0, nullptr);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color) {
    if (!Device || !texture || w <= 0.0f || h <= 0.0f) { return; }
    float x1 = SnapPixel(x);
    float y1 = SnapPixel(y);
    float x2 = SnapPixel(x + SnapSize(w));
    float y2 = SnapPixel(y + SnapSize(h));
    FUiVertex v[4] = {{x1, y1, 0.0f, 1.0f, color, u1, v1}, {x2, y1, 0.0f, 1.0f, color, u2, v1}, {x1, y2, 0.0f, 1.0f, color, u1, v2}, {x2, y2, 0.0f, 1.0f, color, u2, v2}};
    Device->SetTexture(0, texture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTexCoord* coords, int32 textureWidth, int32 textureHeight, unsigned long color) {
    if (!Device || !texture || !coords || textureWidth <= 0 || textureHeight <= 0 || w <= 0.0f || h <= 0.0f) { return; }
    auto u = [textureWidth](int32 value) { return static_cast<float>(value) / static_cast<float>(textureWidth); };
    auto v = [textureHeight](int32 value) { return static_cast<float>(value) / static_cast<float>(textureHeight); };
    FUiVertex verts[4] = {{SnapPixel(x), SnapPixel(y), 0.0f, 1.0f, color, u(coords[0].U), v(coords[0].V)}, {SnapPixel(x + w), SnapPixel(y), 0.0f, 1.0f, color, u(coords[1].U), v(coords[1].V)}, {SnapPixel(x), SnapPixel(y + h), 0.0f, 1.0f, color, u(coords[3].U), v(coords[3].V)}, {SnapPixel(x + w), SnapPixel(y + h), 0.0f, 1.0f, color, u(coords[2].U), v(coords[2].V)}};
    Device->SetTexture(0, texture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTexturePiece(IDirect3DTexture9* texture, const FUiSpritePiece& piece, const FUiRectF& spriteRect, int32 textureWidth, int32 textureHeight, float alpha) {
    if (!Device || !texture || textureWidth <= 0 || textureHeight <= 0) { return; }
    float dx = spriteRect.X + static_cast<float>(piece.DstLeft) * spriteRect.W;
    float dy = spriteRect.Y + static_cast<float>(piece.DstTop) * spriteRect.H;
    float dw = static_cast<float>(piece.DstRight - piece.DstLeft) * spriteRect.W;
    float dh = static_cast<float>(piece.DstBottom - piece.DstTop) * spriteRect.H;
    if (dw < 0.0f) { dx += dw; dw = -dw; }
    if (dh < 0.0f) { dy += dh; dh = -dh; }
    if (dw <= 0.0f || dh <= 0.0f) { return; }
    unsigned char a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    unsigned long color = Argb(a, 255, 255, 255);
    if (piece.HasTexCoords) { DrawTextureQuadUv(texture, dx, dy, dw, dh, piece.TexCoords.data(), textureWidth, textureHeight, color); return; }
    float u1 = static_cast<float>(piece.SrcLeft) / static_cast<float>(textureWidth);
    float v1 = static_cast<float>(piece.SrcTop) / static_cast<float>(textureHeight);
    float u2 = static_cast<float>(piece.SrcRight) / static_cast<float>(textureWidth);
    float v2 = static_cast<float>(piece.SrcBottom) / static_cast<float>(textureHeight);
    DrawTextureQuad(texture, dx, dy, dw, dh, u1, v1, u2, v2, color);
}

bool FD3D9RenderDevice::DrawTextureResource(FDrawContext& ctx, std::string_view textureName, const FUiRectF& dst, float alpha) {
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

bool FD3D9RenderDevice::DrawSprite(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, float alpha) {
    const FUiSpriteDef* sprite = FindSprite(window, spriteName);
    if (!sprite) { return false; }
    const float sx = dst.W / static_cast<float>(std::max(1, sprite->Width));
    const float sy = dst.H / static_cast<float>(std::max(1, sprite->Height));
    FUiRectF spriteRect{dst.X, dst.Y, sx, sy};
    bool drew = false;
    for (const auto& piece : sprite->Pieces) { FD3D9TextureEntry* texture = LoadTextureByName(ctx.Resources, piece.TextureName, ctx.Logger); if (!texture || !texture->Texture) { continue; } DrawTexturePiece(texture->Texture, piece, spriteRect, texture->Width, texture->Height, alpha); drew = true; }
    return drew;
}

void FD3D9RenderDevice::DrawTextRect(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex) {
    if (!Device || text.empty() || rect.W <= 1.0f || rect.H <= 1.0f) { return; }
    const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
    if (!font || !font->IsValid()) { return; }
    std::vector<uint8> bytes = font->EncodeUtf8ToCp1251(text);
    if (bytes.empty()) { return; }
    const float scale = std::max(0.5f, ctx.Scale);
    const int32 textWidth = font->MeasureCodepageText(bytes);
    float x = rect.X;
    if (center) { x += std::max(0.0f, (rect.W - static_cast<float>(textWidth) * scale) * 0.5f); }
    const float lineHeight = static_cast<float>(font->LineHeight()) * scale;
    const float y = rect.Y + std::max(0.0f, (rect.H - lineHeight) * 0.5f);
    const unsigned long fontColor = PremultiplyDiffuse(color);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    for (uint8 ch : bytes) {
        if (ch < 32) { continue; }
        const FD3D9BitmapGlyph& glyph = font->Glyph(ch);
        if (x > rect.X + rect.W) { break; }
        if (glyph.SourceW > 0 && glyph.SourceH > 0 && ch != 32) { float dx = x + static_cast<float>(glyph.XOffset) * scale; float dy = y + static_cast<float>(font->Baseline() - glyph.YOffset) * scale; float dw = static_cast<float>(glyph.SourceW) * scale; float dh = static_cast<float>(glyph.SourceH) * scale; float u1 = static_cast<float>(glyph.SourceX) / static_cast<float>(font->Width()); float v1 = static_cast<float>(glyph.SourceY) / static_cast<float>(font->Height()); float u2 = static_cast<float>(glyph.SourceX + glyph.SourceW) / static_cast<float>(font->Width()); float v2 = static_cast<float>(glyph.SourceY + glyph.SourceH) / static_cast<float>(font->Height()); DrawTextureQuad(font->AtlasTexture(), dx, dy, dw, dh, u1, v1, u2, v2, fontColor); }
        x += static_cast<float>(glyph.Advance) * scale;
    }
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

void FD3D9RenderDevice::DrawControl(FDrawContext& ctx, const FUiWindowDef& window, const FUiControlDef& control, const FUiRectF& windowRect) {
    if (control.Hidden) { return; }
    const FUiActionState& state = ctx.Ui.ActionState();
    FUiRectF r{windowRect.X + control.Rect.X * ctx.Scale, windowRect.Y + control.Rect.Y * ctx.Scale, control.Rect.W * ctx.Scale, control.Rect.H * ctx.Scale};
    if (EqualsNoCase(control.ClassId, "IMAGE")) { if (!EqualsNoCase(control.ImageName, "black") && !control.ImageName.empty()) { DrawSprite(ctx, window, control.ImageName, r.W > 0.0f && r.H > 0.0f ? r : windowRect); } return; }
    if (EqualsNoCase(control.ClassId, "BUTTON")) { std::string sprite = SelectButtonSprite(control, state); if (!sprite.empty()) { DrawSprite(ctx, window, sprite, r); } std::string text = TextForControl(ctx.Ui, control); if (!text.empty()) { DrawTextRect(ctx, r, text, state.HoverControlId == control.Id ? ColorToArgb(control.FocusColor) : ColorToArgb(control.TextColor), true, control.Font >= 0 ? control.Font : window.Font); } return; }
    if (EqualsNoCase(control.ClassId, "CHECKBOX")) { std::string sprite; if (state.SaveLogin && !control.CheckedImage.empty()) { sprite = control.CheckedImage; } else if (state.HoverControlId == control.Id && !control.FocusedImage.empty()) { sprite = control.FocusedImage; } else { sprite = control.UncheckedImage; } if (!sprite.empty()) { DrawSprite(ctx, window, sprite, r); } return; }
    if (EqualsNoCase(control.ClassId, "EDIT")) { std::string text; if (control.Id == 7) { text = state.LoginText; } else if (control.Id == 8 || control.Password) { text.assign(state.PasswordText.size(), '*'); } if (!text.empty()) { DrawTextRect(ctx, r, text, ColorToArgb(control.TextColor), control.TextCenter, control.Font >= 0 ? control.Font : window.Font); } if (state.FocusedControlId == control.Id) { DrawSolidRect(r.X, r.Y + r.H - 1.0f, r.W, 1.0f, Argb(190, 237, 208, 161)); } return; }
    if (EqualsNoCase(control.ClassId, "TEXT")) { std::string text = TextForControl(ctx.Ui, control); if (!text.empty()) { DrawTextRect(ctx, r, text, ColorToArgb(control.TextColor), control.TextCenter, control.Font >= 0 ? control.Font : window.Font); } return; }
}

bool FD3D9RenderDevice::DrawWindow(FDrawContext& ctx, const FUiWindowDef& window, const FUiRectF& dst) {
    if (!window.DrawNone && !window.DrawSpriteName.empty()) { DrawSprite(ctx, window, window.DrawSpriteName, dst); }
    std::string title = window.TextKey.empty() ? std::string{} : ctx.Ui.ResolveText(window.TextKey);
    if (!title.empty()) {
        FUiRectF tr{dst.X + 10.0f * ctx.Scale, dst.Y + 8.0f * ctx.Scale, std::max(1.0f, dst.W - 34.0f * ctx.Scale), 20.0f * ctx.Scale};
        if (window.TitleRect.W > window.TitleRect.X && window.TitleRect.H > window.TitleRect.Y) { tr = FUiRectF{dst.X + window.TitleRect.X * ctx.Scale, dst.Y + window.TitleRect.Y * ctx.Scale, static_cast<float>(window.TitleRect.W - window.TitleRect.X) * ctx.Scale, static_cast<float>(window.TitleRect.H - window.TitleRect.Y) * ctx.Scale}; }
        DrawTextRect(ctx, tr, title, ColorToArgb(window.TextColor), false, window.Font);
    }
    for (const auto& control : window.Controls) { DrawControl(ctx, window, control, dst); }
    return true;
}

void FD3D9RenderDevice::DrawStatusOverlay(FDrawContext& ctx, const FUiRuntime& ui, const FUiRectF& designRect) {
    const float margin = 18.0f * ctx.Scale;
    const float rowH = 18.0f * ctx.Scale;
    const float panelH = (2.0f + static_cast<float>(ui.StatusLines().size())) * rowH + margin;
    FUiRectF panel{designRect.X + margin, designRect.Y + designRect.H - panelH - margin, designRect.W - margin * 2.0f, panelH};
    DrawSolidRect(panel.X, panel.Y, panel.W, panel.H, Argb(110, 0, 0, 0));
    DrawSolidRect(panel.X + margin, panel.Y + margin * 0.5f, std::max(1.0f, (panel.W - margin * 2.0f) * ui.Progress()), 3.0f * ctx.Scale, Argb(220, 237, 208, 161));
    DrawTextRect(ctx, FUiRectF{panel.X + margin, panel.Y + rowH, panel.W - margin * 2.0f, rowH}, ui.Stage(), Argb(230, 237, 208, 161), false, 0);
    float y = panel.Y + rowH * 2.0f;
    for (const auto& line : ui.StatusLines()) { DrawTextRect(ctx, FUiRectF{panel.X + margin, y, panel.W - margin * 2.0f, rowH}, line, Argb(210, 255, 255, 255), false, 0); y += rowH; }
}

void FD3D9RenderDevice::PreloadUiTextures(const FResourceManager& resources, const FUiRuntime& ui, FLogger* logger) {
    LoadTextureByName(resources, ui.LoginBackgroundTexture(), logger);
    for (const auto& [name, sprite] : ui.ConnectionWindow().Sprites) { (void)name; for (const auto& piece : sprite.Pieces) { LoadTextureByName(resources, piece.TextureName, logger); } }
    FontCache.Preload(Device, resources, logger);
    if (logger) { logger->Info("D3D9 UI preload: texture_cache=" + std::to_string(TextureCache.size())); }
}

FStatus FD3D9RenderDevice::RenderUiDesktop(const FResourceManager& resources, const FUiRuntime& ui, const tagRECT& rect, FLogger* logger) {
    if (!Device) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 device is not initialized"); }
    if (!ui.IsReady()) { return FStatus::Error(EStatusCode::RuntimeError, "UI runtime is not initialized"); }
    const FUiRectF design = ui.BuildDesignRect(rect);
    const float scale = design.W / static_cast<float>(std::max(1, ui.DesignWidth()));
    FDrawContext ctx{resources, ui, logger, scale};
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    Device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    Device->SetFVF(FVF_UI);
    HRESULT hr = Device->BeginScene();
    if (FAILED(hr)) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 BeginScene failed: hr=" + std::to_string(static_cast<long>(hr))); }
    Device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    const bool backgroundDrawn = DrawTextureResource(ctx, ui.LoginBackgroundTexture(), design);
    if (ui.IsConnectionPage()) {
        DrawWindow(ctx, ui.ConnectionWindow(), ui.BuildConnectionRect(rect));
    } else {
        const bool connected = ui.Page() == EUiPage::ConnectedPage;
        const unsigned long panelColor = connected ? Argb(145, 12, 40, 22) : Argb(145, 24, 24, 34);
        FUiRectF panel{design.X + design.W * 0.25f, design.Y + design.H * 0.36f, design.W * 0.50f, design.H * 0.22f};
        DrawSolidRect(panel.X, panel.Y, panel.W, panel.H, panelColor);
        DrawSolidRect(panel.X, panel.Y, panel.W, 2.0f * ctx.Scale, Argb(230, 237, 208, 161));
        DrawTextRect(ctx, FUiRectF{panel.X + 24.0f * ctx.Scale, panel.Y + 34.0f * ctx.Scale, panel.W - 48.0f * ctx.Scale, 28.0f * ctx.Scale}, connected ? "Сервер подключен" : "Подключение к серверу", Argb(240, 237, 208, 161), true, 0);
        DrawTextRect(ctx, FUiRectF{panel.X + 24.0f * ctx.Scale, panel.Y + 78.0f * ctx.Scale, panel.W - 48.0f * ctx.Scale, 24.0f * ctx.Scale}, ui.Stage(), Argb(220, 255, 255, 255), true, 0);
    }
    DrawStatusOverlay(ctx, ui, design);
    Device->EndScene();
    hr = Device->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 Present failed: hr=" + std::to_string(static_cast<long>(hr))); }
    if (!backgroundDrawn) { return FStatus::Error(EStatusCode::NotFound, "login background texture was not rendered: " + ui.LoginBackgroundTexture()); }
    return FStatus::Ok();
}

FD3D9ShaderInventory FD3D9RenderDevice::InspectShaderResources(const FResourceManager& resources, FLogger* logger) const {
    FD3D9ShaderInventory inventory;
    for (const auto& record : resources.Catalog().All()) {
        std::string path = record.RelativePath.generic_string();
        std::string lower = Lower(path);
        bool isShader = lower.find("shader") != std::string::npos || lower.find("shaders/") != std::string::npos || lower.find("shaders\\") != std::string::npos;
        if (!isShader) { continue; }
        if (lower.find("vertex") != std::string::npos || lower.find("/vs") != std::string::npos) { ++inventory.VertexShaders; }
        else if (lower.find("pixel") != std::string::npos || lower.find("/ps") != std::string::npos) { ++inventory.PixelShaders; }
        if (inventory.Samples.size() < 8) { inventory.Samples.push_back(path); }
    }
    if (logger) { logger->Info("D3D9 shader resource inventory: vertex=" + std::to_string(inventory.VertexShaders) + ", pixel=" + std::to_string(inventory.PixelShaders) + ", samples=" + std::to_string(inventory.Samples.size())); for (size_t i = 0; i < inventory.Samples.size(); ++i) { logger->Info("D3D9 shader sample[" + std::to_string(i) + "]: " + inventory.Samples[i]); } }
    return inventory;
}
}
