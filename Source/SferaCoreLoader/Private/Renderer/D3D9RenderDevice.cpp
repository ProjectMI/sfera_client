#include "Renderer/D3D9RenderDevice.h"
#include "FileSystem/PathUtils.h"
#include "Core/NumericParse.h"
#include "ResourceLoader/ResourceTypes.h"
#include "UI/LoadingScreen.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

namespace Sfera {
namespace {
constexpr unsigned int D3DX_DEFAULT_VALUE = static_cast<unsigned int>(-1);
constexpr unsigned long D3DX_FILTER_DEFAULT_VALUE = static_cast<unsigned long>(-1);
constexpr unsigned long D3DX_FILTER_NONE_VALUE = 1ul;

std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }); return s; }
bool EqualsNoCase(std::string_view a, std::string_view b) { return Lower(std::string(a)) == Lower(std::string(b)); }
std::string StemLower(const FPath& path) { return Lower(path.stem().string()); }

struct FUiVertex {
    float X;
    float Y;
    float Z;
    float Rhw;
    unsigned long Color;
    float U;
    float V;
};
constexpr unsigned long FVF_UI = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

unsigned long Argb(unsigned char a, unsigned char r, unsigned char g, unsigned char b) { return (static_cast<unsigned long>(a) << 24) | (static_cast<unsigned long>(r) << 16) | (static_cast<unsigned long>(g) << 8) | static_cast<unsigned long>(b); }
float SnapPixel(float v) { return std::floor(v + 0.5f) - 0.5f; }
float SnapSize(float v) { return std::max(1.0f, std::floor(v + 0.5f)); }

const FUiSprite* FindSprite(const FUiWindow& window, std::string_view name) {
    for (const auto& sprite : window.Sprites) { if (EqualsNoCase(sprite.Name, name)) { return &sprite; } }
    return nullptr;
}

const FUiProperty* FindProperty(const FUiControl& control, std::string_view key) {
    for (const auto& prop : control.Properties) { if (EqualsNoCase(prop.Key, key)) { return &prop; } }
    return nullptr;
}

const FUiProperty* FindWindowProperty(const FUiWindow& window, std::string_view key) {
    for (const auto& prop : window.Properties) { if (EqualsNoCase(prop.Key, key)) { return &prop; } }
    return nullptr;
}

std::string FirstPropertyValue(const FUiControl& control, std::string_view key) {
    const FUiProperty* prop = FindProperty(control, key);
    if (!prop || prop->Values.empty()) { return {}; }
    return prop->Values[0];
}

unsigned long ParseTextColor(const FUiControl& control, unsigned long fallback) {
    const FUiProperty* prop = FindProperty(control, "textcolor");
    if (!prop || prop->Values.size() < 3) { return fallback; }
    int32 r = 255, g = 255, b = 255, a = 255;
    NumericParse::TryParseInt32Strict(prop->Values[0], r);
    NumericParse::TryParseInt32Strict(prop->Values[1], g);
    NumericParse::TryParseInt32Strict(prop->Values[2], b);
    if (prop->Values.size() > 3) { NumericParse::TryParseInt32Strict(prop->Values[3], a); }
    return Argb(static_cast<unsigned char>(a), static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b));
}

std::string LocalizedUiText(std::string token) {
    if (token == "UISTR_OK") { return "OK"; }
    if (token == "UISTR_CANCEL") { return "Отмена"; }
    if (token == "UISTR_CONNECTION_LOGIN") { return "Имя пользователя:"; }
    if (token == "UISTR_CONNECTION_PASSWORD") { return "Пароль:"; }
    if (token == "UISTR_CONNECTION_SAVEPASSWORD") { return "Сохранить логин"; }
    if (token == "UISTR_CONNECTION_REGISTRATION") { return "Регистрация"; }
    if (token == "connection") { return "Подключение"; }
    return token;
}

std::string ControlText(const FUiControl& control) {
    const FUiProperty* prop = FindProperty(control, "windowText");
    if (!prop || prop->Values.empty()) { return {}; }
    std::string text;
    for (size_t i = 0; i < prop->Values.size(); ++i) { if (i) { text += ' '; } text += LocalizedUiText(prop->Values[i]); }
    return text;
}

bool IsCentered(const FUiControl& control) {
    const FUiProperty* prop = FindProperty(control, "textFormat");
    if (!prop) { return false; }
    for (const auto& v : prop->Values) { if (Lower(v).find("center") != std::string::npos) { return true; } }
    return false;
}


std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) { return {}; }
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        std::wstring fallback;
        fallback.reserve(text.size());
        for (unsigned char ch : text) { fallback.push_back(static_cast<wchar_t>(ch)); }
        return fallback;
    }
    std::wstring wide(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
    return wide;
}

int ColorR(unsigned long color) { return static_cast<int>((color >> 16) & 0xff); }
int ColorG(unsigned long color) { return static_cast<int>((color >> 8) & 0xff); }
int ColorB(unsigned long color) { return static_cast<int>(color & 0xff); }
int ColorA(unsigned long color) { return static_cast<int>((color >> 24) & 0xff); }
}

struct FD3D9RenderDevice::FDrawRect {
    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
};

struct FD3D9RenderDevice::FUiDrawContext {
    const FResourceManager& Resources;
    FLogger* Logger = nullptr;
    const FUiInteractionState* Interaction = nullptr;
    FDrawRect DesignRect;
    float Scale = 1.0f;
    float Progress = 0.0f;
};

FD3D9RenderDevice::FD3D9RenderDevice() = default;
FD3D9RenderDevice::~FD3D9RenderDevice() { Shutdown(); }

FStatus FD3D9RenderDevice::Initialize(HWND__* hwnd, int width, int height, FLogger* logger) {
    Shutdown();
    D3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!D3D) { return FStatus::Error(EStatusCode::RuntimeError, "Direct3DCreate9 failed; D3D9 frontend disabled"); }
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
    if (logger) { logger->Info("D3D9 device initialized: windowed=1, backbuffer=" + std::to_string(width) + "x" + std::to_string(height)); }
    EnsureD3DX(logger);
    return FStatus::Ok();
}

void FD3D9RenderDevice::ReleaseTextures() {
    for (auto& item : TextureCache) { if (item.second.Texture) { item.second.Texture->Release(); item.second.Texture = nullptr; } }
    TextureCache.clear();
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
        if (proc) {
            D3DXCreateTextureFromFileInMemoryExFn = proc;
            if (logger) { logger->Info(std::string("D3D9 UI texture loader: ") + dll); }
            return FStatus::Ok();
        }
        FreeLibrary(D3DXModule);
        D3DXModule = nullptr;
    }
    if (!ReportedD3DXMissing && logger) { logger->Warning("D3D9 UI texture loader unavailable: d3dx9_43.dll not found; sprite renderer will use vector fallback"); ReportedD3DXMissing = true; }
    return FStatus::Error(EStatusCode::RuntimeError, "D3DXCreateTextureFromFileInMemoryEx unavailable");
}

std::string FD3D9RenderDevice::ResolveTextureResourceName(const FResourceManager& resources, std::string_view textureName) const {
    std::string name(textureName);
    if (name.empty()) { return {}; }
    std::vector<std::string> names;
    std::string lower = Lower(name);
    bool hasExt = lower.find('.') != std::string::npos;
    std::vector<std::string> bases = {name, "textures/" + name, "Textures/" + name, "effects/" + name, "Effects/" + name, "interface/" + name, "Interface/" + name};
    std::vector<std::string> exts = hasExt ? std::vector<std::string>{""} : std::vector<std::string>{".dds", ".tga", ".bmp", ".png", ".jpg", ".jpeg"};
    for (const auto& base : bases) { for (const auto& ext : exts) { names.push_back(base + ext); } }
    for (const auto& candidate : names) { if (auto record = resources.Catalog().FindByLogicalName(candidate)) { return record->RelativePath.generic_string(); } }
    for (const auto& record : resources.Catalog().All()) {
        EResourceKind kind = GuessResourceKind(record.RelativePath);
        if (kind != EResourceKind::Texture && Lower(record.RelativePath.extension().string()) != ".dds") { continue; }
        if (StemLower(record.RelativePath) == lower) { return record.RelativePath.generic_string(); }
    }
    return {};
}

FD3D9TextureEntry* FD3D9RenderDevice::LoadTextureByName(const FResourceManager& resources, std::string_view textureName, FLogger* logger) {
    std::string key = Lower(std::string(textureName));
    if (key.empty()) { return nullptr; }
    auto it = TextureCache.find(key);
    if (it != TextureCache.end()) { return it->second.Texture ? &it->second : nullptr; }
    FD3D9TextureEntry entry;
    entry.Tried = true;
    if (!Device || !EnsureD3DX(logger).IsOk()) { entry.Error = "D3DX unavailable"; TextureCache.emplace(key, entry); return nullptr; }
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
    entry.Width = static_cast<int>(desc.Width);
    entry.Height = static_cast<int>(desc.Height);
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

void FD3D9RenderDevice::DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTextureCoord* coords, int textureWidth, int textureHeight, unsigned long color) {
    if (!Device || !texture || !coords || textureWidth <= 0 || textureHeight <= 0 || w <= 0.0f || h <= 0.0f) { return; }
    float x1 = SnapPixel(x);
    float y1 = SnapPixel(y);
    float x2 = SnapPixel(x + SnapSize(w));
    float y2 = SnapPixel(y + SnapSize(h));
    auto u = [textureWidth](int32 value) { return static_cast<float>(value) / static_cast<float>(textureWidth); };
    auto v = [textureHeight](int32 value) { return static_cast<float>(value) / static_cast<float>(textureHeight); };
    FUiVertex verts[4] = {
        {x1, y1, 0.0f, 1.0f, color, u(coords[0].X), v(coords[0].Y)},
        {x2, y1, 0.0f, 1.0f, color, u(coords[1].X), v(coords[1].Y)},
        {x1, y2, 0.0f, 1.0f, color, u(coords[3].X), v(coords[3].Y)},
        {x2, y2, 0.0f, 1.0f, color, u(coords[2].X), v(coords[2].Y)}};
    Device->SetTexture(0, texture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(FUiVertex));
}

void FD3D9RenderDevice::DrawTextureSlice(IDirect3DTexture9* texture, const FUiTextureSlice& slice, const FDrawRect& spriteRect, int textureWidth, int textureHeight, float alpha) {
    if (!Device || !texture || textureWidth <= 0 || textureHeight <= 0) { return; }
    if ((slice.Source.W == 0 || slice.Source.H == 0) && !slice.HasCustomTexCoords) { return; }
    float dx = spriteRect.X + static_cast<float>(slice.Dest.X) * spriteRect.W;
    float dy = spriteRect.Y + static_cast<float>(slice.Dest.Y) * spriteRect.H;
    float dw = static_cast<float>(slice.Dest.W) * spriteRect.W;
    float dh = static_cast<float>(slice.Dest.H) * spriteRect.H;
    if (dw < 0.0f) { dx += dw; dw = -dw; }
    if (dh < 0.0f) { dy += dh; dh = -dh; }
    if (dw <= 0.0f || dh <= 0.0f) { return; }
    unsigned char a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    unsigned long color = Argb(a, 255, 255, 255);
    if (slice.HasCustomTexCoords) {
        DrawTextureQuadUv(texture, dx, dy, dw, dh, slice.TexCoords, textureWidth, textureHeight, color);
        return;
    }
    float u1 = static_cast<float>(slice.Source.X) / static_cast<float>(textureWidth);
    float v1 = static_cast<float>(slice.Source.Y) / static_cast<float>(textureHeight);
    float u2 = static_cast<float>(slice.Source.X + slice.Source.W) / static_cast<float>(textureWidth);
    float v2 = static_cast<float>(slice.Source.Y + slice.Source.H) / static_cast<float>(textureHeight);
    DrawTextureQuad(texture, dx, dy, dw, dh, u1, v1, u2, v2, color);
}

bool FD3D9RenderDevice::DrawSprite(FUiDrawContext& ctx, const FUiWindow& window, std::string_view spriteName, const FDrawRect& dst, float alpha) {
    const FUiSprite* sprite = FindSprite(window, spriteName);
    if (!sprite) { return false; }
    bool drew = false;
    for (const auto& slice : sprite->TextureSlices) {
        FD3D9TextureEntry* texture = LoadTextureByName(ctx.Resources, slice.TextureName, ctx.Logger);
        if (!texture || !texture->Texture) { continue; }
        FDrawRect spriteRect;
        spriteRect.X = dst.X;
        spriteRect.Y = dst.Y;
        spriteRect.W = sprite->Size.X > 0 ? dst.W / static_cast<float>(sprite->Size.X) : 1.0f;
        spriteRect.H = sprite->Size.Y > 0 ? dst.H / static_cast<float>(sprite->Size.Y) : 1.0f;
        DrawTextureSlice(texture->Texture, slice, spriteRect, texture->Width, texture->Height, alpha);
        drew = true;
    }
    if (!drew) { DrawSolidRect(dst.X, dst.Y, dst.W, dst.H, Argb(90, 80, 80, 92)); }
    return true;
}

void FD3D9RenderDevice::DrawTextRect(FUiDrawContext& ctx, const FDrawRect& rect, const std::string& text, unsigned long color, bool center, int fontId) {
    if (!Device || text.empty() || rect.W <= 1.0f || rect.H <= 1.0f) { return; }
    int w = std::max(2, static_cast<int>(std::ceil(rect.W)));
    int h = std::max(2, static_cast<int>(std::ceil(rect.H)));
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memdc = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!memdc || !dib || !bits) {
        if (dib) { DeleteObject(dib); }
        if (memdc) { DeleteDC(memdc); }
        return;
    }
    std::memset(bits, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    HGDIOBJ oldBmp = SelectObject(memdc, dib);
    SetBkMode(memdc, TRANSPARENT);
    SetTextColor(memdc, RGB(ColorR(color), ColorG(color), ColorB(color)));
    int fontHeight = -std::max(8, std::min(18, static_cast<int>(rect.H * 0.72f)));
    const FUiFontFace* uiFont = ctx.Resources.Catalog().Count() > 0 ? FontCatalog.Find(fontId) : nullptr;
    const wchar_t* faceName = L"Arial";
    if (uiFont) {
        switch (uiFont->Index) {
        case 0: faceName = L"Tahoma"; break;
        case 1: faceName = L"Tahoma"; break;
        case 2: faceName = L"Tahoma"; break;
        case 3: faceName = L"Arial"; break;
        case 4: faceName = L"Calibri"; break;
        case 5: faceName = L"Century"; break;
        case 6: faceName = L"Consolas"; break;
        case 7: faceName = L"Garamond"; break;
        case 8: faceName = L"Georgia"; break;
        case 9: faceName = L"Microsoft Sans Serif"; break;
        case 10: faceName = L"Times New Roman"; break;
        case 11: faceName = L"Verdana"; break;
        default: break;
        }
    }
    HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, faceName);
    HGDIOBJ oldFont = SelectObject(memdc, font);
    RECT textRect{0, 0, w, h};
    std::wstring wide = Utf8ToWide(text);
    unsigned int flags = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    flags |= center ? DT_CENTER : DT_LEFT;
    DrawTextW(memdc, wide.c_str(), static_cast<int>(wide.size()), &textRect, flags);
    SelectObject(memdc, oldFont);
    DeleteObject(font);
    SelectObject(memdc, oldBmp);
    uint32_t* pixels = static_cast<uint32_t*>(bits);
    int baseA = std::max(0, std::min(255, ColorA(color)));
    for (int i = 0; i < w * h; ++i) {
        uint32_t p = pixels[i];
        uint32_t r = (p >> 16) & 0xff;
        uint32_t g = (p >> 8) & 0xff;
        uint32_t b = p & 0xff;
        uint32_t coverage = std::max(r, std::max(g, b));
        if (!coverage) { pixels[i] = 0; continue; }
        uint32_t a = static_cast<uint32_t>((coverage * static_cast<uint32_t>(baseA)) / 255u);
        pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = Device->CreateTexture(static_cast<UINT>(w), static_cast<UINT>(h), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    if (SUCCEEDED(hr) && texture) {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(texture->LockRect(0, &locked, nullptr, 0))) {
            for (int y = 0; y < h; ++y) {
                std::memcpy(static_cast<unsigned char*>(locked.pBits) + static_cast<size_t>(locked.Pitch) * static_cast<size_t>(y), pixels + static_cast<size_t>(w) * static_cast<size_t>(y), static_cast<size_t>(w) * 4);
            }
            texture->UnlockRect(0);
            DrawTextureQuad(texture, rect.X, rect.Y, static_cast<float>(w), static_cast<float>(h), 0.0f, 0.0f, 1.0f, 1.0f, Argb(255, 255, 255, 255));
        }
        texture->Release();
    }
    DeleteObject(dib);
    DeleteDC(memdc);
}

void FD3D9RenderDevice::DrawControl(FUiDrawContext& ctx, const FUiWindow& window, const FUiControl& control, const FDrawRect& windowRect) {
    if (control.Hidden) { return; }
    FDrawRect r{windowRect.X + control.Position.X * ctx.Scale, windowRect.Y + control.Position.Y * ctx.Scale, control.Size.X * ctx.Scale, control.Size.Y * ctx.Scale};
    bool hovered = ctx.Interaction && ctx.Interaction->HoverControlId == control.Id;
    bool pressed = ctx.Interaction && ctx.Interaction->PressedControlId == control.Id;
    bool focused = ctx.Interaction && ctx.Interaction->FocusedControlId == control.Id;
    if (EqualsNoCase(control.ClassId, "IMAGE")) {
        std::string sprite = control.Image;
        if (sprite == "black") { return; }
        if (!sprite.empty()) { DrawSprite(ctx, window, sprite, r.W > 0 && r.H > 0 ? r : FDrawRect{windowRect.X, windowRect.Y, windowRect.W, windowRect.H}); }
        return;
    }
    if (EqualsNoCase(control.ClassId, "PROGRESS_BAR")) {
        float progress = std::clamp(ctx.Progress, 0.0f, 1.0f);
        FDrawRect border{r.X - 2.0f, r.Y - 2.0f, r.W + 4.0f, r.H + 4.0f};
        DrawSolidRect(border.X, border.Y, border.W, border.H, Argb(180, 54, 52, 42));
        DrawSolidRect(r.X, r.Y, r.W, r.H, Argb(220, 28, 38, 14));
        FDrawRect fill = r;
        fill.W = std::max(1.0f, r.W * progress);
        std::string sprite = !control.DrawSprite.empty() ? control.DrawSprite : "yellow1";
        if (!DrawSprite(ctx, window, sprite, fill)) { DrawSolidRect(fill.X, fill.Y, fill.W, fill.H, Argb(240, 210, 220, 20)); }
        return;
    }
    if (EqualsNoCase(control.ClassId, "BUTTON")) {
        std::string sprite;
        if (pressed) { sprite = FirstPropertyValue(control, "checkedImage"); }
        if (sprite.empty() && hovered) { sprite = FirstPropertyValue(control, "focusedImage"); }
        if (sprite.empty()) { sprite = FirstPropertyValue(control, "uncheckedImage"); }
        if (sprite.empty()) { sprite = control.DrawSprite; }
        if (!sprite.empty()) { DrawSprite(ctx, window, sprite, r); }
        std::string text = ControlText(control);
        if (!text.empty()) { DrawTextRect(ctx, r, text, hovered ? Argb(255, 255, 239, 212) : ParseTextColor(control, Argb(255, 255, 255, 255)), true, control.Font); }
        return;
    }
    if (EqualsNoCase(control.ClassId, "CHECKBOX")) {
        bool checked = ctx.Interaction && ctx.Interaction->SaveLogin;
        std::string sprite;
        if (pressed || checked) { sprite = FirstPropertyValue(control, "checkedImage"); }
        if (sprite.empty() && hovered) { sprite = FirstPropertyValue(control, "focusedImage"); }
        if (sprite.empty()) { sprite = FirstPropertyValue(control, "uncheckedImage"); }
        if (!sprite.empty()) { DrawSprite(ctx, window, sprite, r); }
        return;
    }
    if (EqualsNoCase(control.ClassId, "EDIT")) {
        unsigned long border = focused ? Argb(190, 237, 208, 161) : Argb(120, 170, 150, 120);
        DrawSolidRect(r.X - 1.0f, r.Y - 1.0f, r.W + 2.0f, 1.0f, border);
        DrawSolidRect(r.X - 1.0f, r.Y + r.H, r.W + 2.0f, 1.0f, border);
        DrawSolidRect(r.X - 1.0f, r.Y - 1.0f, 1.0f, r.H + 2.0f, border);
        DrawSolidRect(r.X + r.W, r.Y - 1.0f, 1.0f, r.H + 2.0f, border);
        std::string text;
        if (ctx.Interaction) {
            if (control.Id == 7) { text = ctx.Interaction->LoginText; }
            else if (control.Id == 8) { text.assign(ctx.Interaction->PasswordText.size(), '*'); }
        }
        if (!text.empty()) { DrawTextRect(ctx, r, text, ParseTextColor(control, Argb(255, 237, 208, 161)), IsCentered(control), control.Font); }
        return;
    }
    if (EqualsNoCase(control.ClassId, "TEXT")) { DrawTextRect(ctx, r, ControlText(control), ParseTextColor(control, Argb(255, 255, 255, 255)), IsCentered(control), control.Font); return; }
}

bool FD3D9RenderDevice::DrawWindow(FUiDrawContext& ctx, const FUiWindow& window, const FDrawRect& overrideRect, bool forceConnectionTitle) {
    FDrawRect wr = overrideRect;
    if (wr.W <= 0.0f || wr.H <= 0.0f) { wr = {ctx.DesignRect.X + window.Position.X * ctx.Scale, ctx.DesignRect.Y + window.Position.Y * ctx.Scale, window.Size.X * ctx.Scale, window.Size.Y * ctx.Scale}; }
    bool drewBackground = false;
    if (!window.DrawSprite.empty()) { drewBackground = DrawSprite(ctx, window, window.DrawSprite, wr); }
    if (!drewBackground) { DrawSolidRect(wr.X, wr.Y, wr.W, wr.H, Argb(210, 31, 25, 20)); }
    std::string title = LocalizedUiText(window.Text.empty() ? window.Name : window.Text);
    if (forceConnectionTitle) { title = "Подключение"; }
    if (!title.empty()) {
        FDrawRect tr{wr.X + 10.0f * ctx.Scale, wr.Y + 8.0f * ctx.Scale, std::max(1.0f, wr.W - 34.0f * ctx.Scale), 20.0f * ctx.Scale};
        if (const FUiProperty* prop = FindWindowProperty(window, "rectTitle"); prop && prop->Values.size() >= 4) {
            int32 x1 = 10, y1 = 8, x2 = static_cast<int32>(window.Size.X) - 24, y2 = 28;
            NumericParse::TryParseInt32Strict(prop->Values[0], x1);
            NumericParse::TryParseInt32Strict(prop->Values[1], y1);
            NumericParse::TryParseInt32Strict(prop->Values[2], x2);
            NumericParse::TryParseInt32Strict(prop->Values[3], y2);
            tr = {wr.X + static_cast<float>(x1) * ctx.Scale, wr.Y + static_cast<float>(y1) * ctx.Scale, std::max(1.0f, std::min(static_cast<float>(x2 - x1) * ctx.Scale, wr.W - static_cast<float>(x1) * ctx.Scale)), std::max(1.0f, static_cast<float>(y2 - y1) * ctx.Scale)};
        }
        DrawTextRect(ctx, tr, title, Argb(255, 237, 208, 161), false, window.Font);
    }
    for (const auto& control : window.Controls) { DrawControl(ctx, window, control, wr); }
    return true;
}


void FD3D9RenderDevice::PreloadLoadingScreenTextures(const FResourceManager& resources, const FLoadingScreenModel& model, FLogger* logger) {
    FontCatalog.Load(resources, logger);
    for (const auto& face : FontCatalog.Faces()) {
        if (!face.TextureResource.empty()) { LoadTextureByName(resources, face.TextureResource, logger); }
    }
    auto preloadWindow = [&](const FUiWindow* window) {
        if (!window) { return; }
        for (const auto& sprite : window->Sprites) {
            for (const auto& slice : sprite.TextureSlices) {
                LoadTextureByName(resources, slice.TextureName, logger);
            }
        }
    };
    preloadWindow(model.LayoutWindow());
    preloadWindow(model.ConnectionWindow());
    if (logger) { logger->Info("D3D9 UI preload: texture_cache=" + std::to_string(TextureCache.size())); }
}

FStatus FD3D9RenderDevice::RenderLoadingScreen(const FResourceManager& resources, const FLoadingScreenModel& model, const tagRECT& rect, FLogger* logger) {
    if (!Device) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 device is not initialized"); }
    FontCatalog.Load(resources, logger);
    const FUiWindow* loadscreen = model.LayoutWindow();
    if (!loadscreen) { return FStatus::Error(EStatusCode::RuntimeError, "loadscreen UI is not parsed"); }
    float clientW = static_cast<float>(rect.right - rect.left);
    float clientH = static_cast<float>(rect.bottom - rect.top);
    float designW = loadscreen->Size.X > 0 ? static_cast<float>(loadscreen->Size.X) : 1024.0f;
    float designH = loadscreen->Size.Y > 0 ? static_cast<float>(loadscreen->Size.Y) : 768.0f;
    float scale = std::min(clientW / designW, clientH / designH);
    FDrawRect design{(clientW - designW * scale) * 0.5f, (clientH - designH * scale) * 0.5f, designW * scale, designH * scale};
    FUiDrawContext ctx{resources, logger, &model.Interaction(), design, scale, model.Progress()};
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
    FDrawRect full{design.X, design.Y, design.W, design.H};
    if (!DrawSprite(ctx, *loadscreen, "russian_sphere1", full)) { DrawSprite(ctx, *loadscreen, "russian_free", full); }
    for (const auto& control : loadscreen->Controls) { DrawControl(ctx, *loadscreen, control, full); }
    if (const FUiWindow* connection = model.ConnectionWindow()) {
        FDrawRect conn{design.X + (design.W - connection->Size.X * scale) * 0.5f, design.Y + (design.H - connection->Size.Y * scale) * 0.5f - 10.0f * scale, connection->Size.X * scale, connection->Size.Y * scale};
        DrawWindow(ctx, *connection, conn, true);
    }
    Device->EndScene();
    hr = Device->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 Present failed: hr=" + std::to_string(static_cast<long>(hr))); }
    return FStatus::Ok();
}

FD3D9ShaderInventory FD3D9RenderDevice::InspectShaderResources(const FResourceManager& resources, FLogger* logger) const {
    FD3D9ShaderInventory inventory;
    for (const auto& record : resources.Catalog().All()) {
        std::string path = record.RelativePath.generic_string();
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool isShader = lower.find("shader") != std::string::npos || lower.find("shaders/") != std::string::npos || lower.find("shaders\\") != std::string::npos;
        if (!isShader) { continue; }
        if (lower.find("vertex") != std::string::npos || lower.find("/vs") != std::string::npos) { ++inventory.VertexShaders; }
        else if (lower.find("pixel") != std::string::npos || lower.find("/ps") != std::string::npos) { ++inventory.PixelShaders; }
        if (inventory.Samples.size() < 8) { inventory.Samples.push_back(path); }
    }
    if (logger) {
        logger->Info("D3D9 shader resource inventory: vertex=" + std::to_string(inventory.VertexShaders) + ", pixel=" + std::to_string(inventory.PixelShaders) + ", samples=" + std::to_string(inventory.Samples.size()));
        for (size_t i = 0; i < inventory.Samples.size(); ++i) { logger->Info("D3D9 shader sample[" + std::to_string(i) + "]: " + inventory.Samples[i]); }
    }
    return inventory;
}
}
