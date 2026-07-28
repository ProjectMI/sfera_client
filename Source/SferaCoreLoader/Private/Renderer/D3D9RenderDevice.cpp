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

    constexpr unsigned long FVF_UI = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    std::string StemLower(const FPath& path) { return Common::ToLower(path.stem().string()); }
    unsigned long Argb(unsigned char a, unsigned char r, unsigned char g, unsigned char b) { return (static_cast<unsigned long>(a) << 24) | (static_cast<unsigned long>(r) << 16) | (static_cast<unsigned long>(g) << 8) | static_cast<unsigned long>(b); }
    unsigned long ColorToArgb(const FUiColor& color) { return Argb(static_cast<unsigned char>(std::clamp(color.A, 0, 255)), static_cast<unsigned char>(std::clamp(color.R, 0, 255)), static_cast<unsigned char>(std::clamp(color.G, 0, 255)), static_cast<unsigned char>(std::clamp(color.B, 0, 255))); }
    unsigned long ApplyAlpha(unsigned long color, float alpha) { const int a = std::clamp(static_cast<int>(static_cast<float>((color >> 24) & 0xff) * std::clamp(alpha, 0.0f, 1.0f)), 0, 255); return (color & 0x00fffffful) | (static_cast<unsigned long>(a) << 24); }
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
        auto it = window.Sprites.find(name);
        return it == window.Sprites.end() ? nullptr : &it->second;
    }

    int32 SpriteExtentX(const FUiSpriteDef& sprite) { return std::max(1, sprite.ExtentX); }
    int32 SpriteExtentY(const FUiSpriteDef& sprite) { return std::max(1, sprite.ExtentY); }

    std::string_view SelectButtonSprite(const FUiControlDef& control, bool disabled, bool hovered, bool pressed)
    {
        if (disabled && !control.DisabledImage.empty()) { return control.DisabledImage; }
        if (pressed && !control.CheckedImage.empty()) { return control.CheckedImage; }
        if (hovered && !control.FocusedImage.empty()) { return control.FocusedImage; }
        if (!control.UncheckedImage.empty()) { return control.UncheckedImage; }
        return control.DrawSpriteName;
    }

    std::string_view SelectSubButtonSprite(const FUiSubButtonDef& button, bool disabled, bool hot, bool pressed, std::string_view normalFallback, std::string_view focusFallback, std::string_view pressedFallback, std::string_view disabledFallback)
    {
        if (disabled) { return !button.DisabledImage.empty() ? std::string_view(button.DisabledImage) : disabledFallback; }
        if (pressed) { return !button.CheckedImage.empty() ? std::string_view(button.CheckedImage) : pressedFallback; }
        if (hot) { return !button.FocusedImage.empty() ? std::string_view(button.FocusedImage) : focusFallback; }
        return !button.UncheckedImage.empty() ? std::string_view(button.UncheckedImage) : normalFallback;
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

    bool IsTextLikeControl(const FUiControlDef& control) { return control.Class == EUiControlClass::Text; }
    std::string TextForControl(const FUiRuntime& ui, const FUiWindowDef& window, const FUiControlDef& control)
    {
        if (ui.Mode() == EUiRuntimeMode::CharacterSelect && Common::EqualsNoCase(window.Name, "pick_person")) { return ui.Character().CharacterControlText(control); }

        if (ui.HasModalDialog() && Common::EqualsNoCase(window.Name, ui.ActiveModalWindow().Name)) { return ui.Character().ModalControlText(control); }

        if (ui.Mode() == EUiRuntimeMode::Game) { return ui.GameControlText(window.Name, control); }
        return control.TextKey.empty() ? std::string{} : ui.ResolveText(control.TextKey);
    }

    bool ShouldPrewarmGameWindow(const FUiWindowDef& window)
    {
        static constexpr std::array<std::string_view, 19> names
        {
            "system_left", "system_leftmin", "system_right", "system_rightmin", "chat", "chat_st2", "chat_sys",
            "inventory", "statinfo", "statinfo_n", "mapbook", "minimap", "bigmap", "new_bigmap", "journal_mini",
            "options", "control_options", "interface_options", "sound_options"
        };
        const std::string_view key = window.NameKey.empty() ? std::string_view(window.Name) : std::string_view(window.NameKey);
        return std::find_if(names.begin(), names.end(), [&](std::string_view name) { return Common::EqualsNoCase(key, name); }) != names.end();
    }

    const FUiControlDef* FindGameControl(const FUiRuntime& ui, std::string_view windowName, int32 controlId)
    {
        const auto index = ui.FindGameWindowIndex(windowName);
        if (!index || *index >= ui.GameWindows().size()) { return nullptr; }
        const auto& controls = ui.GameWindows()[*index].Controls;
        const auto it = std::find_if(controls.begin(), controls.end(), [&](const FUiControlDef& control) { return control.Id == controlId; });
        return it == controls.end() ? nullptr : &*it;
    }
}

struct FD3D9RenderDevice::FDrawContext
{
    const FResourceManager& Resources;
    const FUiRuntime& Ui;
    FLogger* Logger = nullptr;
    float Scale = 1.0f;
};

FD3D9RenderDevice::FD3D9RenderDevice()
{
    RemoteGamePlayers.reserve(256);
    RemoteGameActors.reserve(1024);
}
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

void FD3D9RenderDevice::ClearServerGameTime()
{
    ServerGameTime = 0.0f;
    HasServerGameTime = false;
    ServerGameTimePending = false;
}

float FD3D9RenderDevice::GameWorldCameraFacing() const
{
    return GameWorldScene.CameraFacing();
}

void FD3D9RenderDevice::SetInitialGameWorldPosition(std::optional<FGameWorldPosition> position)
{
    InitialGameWorldPosition = position;
    if (GameWorldScene.IsValid())
    {
        GameWorldScene.Shutdown();
        ActiveWorldScene = nullptr;
        FailedWorldScene = nullptr;
    }
}

void FD3D9RenderDevice::ApplyServerGameWorldPosition(const FGameWorldPosition& position)
{
    InitialGameWorldPosition = position;
    if (GameWorldScene.IsValid())
    {
        GameWorldScene.SetPlayerWorldPosition(position);
    }
}

std::optional<FGameWorldPosition> FD3D9RenderDevice::CurrentGameWorldPosition() const
{
    return GameWorldScene.CurrentPlayerWorldPosition();
}

void FD3D9RenderDevice::UpsertRemoteGamePlayer(const FRemoteGamePlayer& player)
{
    if (player.EntityId == 0) { return; }
    if (RemoteGameActors.erase(player.EntityId) != 0 && GameWorldScene.IsValid()) { GameWorldScene.RemoveRemoteActor(player.EntityId); }
    FRemoteGamePlayer& target = RemoteGamePlayers[player.EntityId];
    target.EntityId = player.EntityId;
    if (!player.Name.empty()) { target.Name = player.Name; }
    if (player.Appearance) { target.Appearance = player.Appearance; }
    target.Position = player.Position;
    if (GameWorldScene.IsValid()) { GameWorldScene.UpsertRemotePlayer(target); }
}

void FD3D9RenderDevice::SetRemoteGamePlayerAppearance(uint64 entityId, const FCharacterCreationAppearance& appearance)
{
    const auto iterator = RemoteGamePlayers.find(entityId);
    if (iterator == RemoteGamePlayers.end()) { return; }
    iterator->second.Appearance = appearance;
    if (GameWorldScene.IsValid()) { GameWorldScene.SetRemotePlayerAppearance(entityId, appearance); }
}

void FD3D9RenderDevice::RemoveRemoteGamePlayer(uint64 entityId)
{
    RemoteGamePlayers.erase(entityId);
    if (GameWorldScene.IsValid()) { GameWorldScene.RemoveRemotePlayer(entityId); }
}

void FD3D9RenderDevice::ClearRemoteGamePlayers()
{
    RemoteGamePlayers.clear();
    if (GameWorldScene.IsValid()) { GameWorldScene.ClearRemotePlayers(); }
}

void FD3D9RenderDevice::UpsertRemoteGameActor(const FRemoteGameActor& actor)
{
    if (actor.EntityId == 0) { return; }
    if (RemoteGamePlayers.erase(actor.EntityId) != 0 && GameWorldScene.IsValid()) { GameWorldScene.RemoveRemotePlayer(actor.EntityId); }
    FRemoteGameActor& target = RemoteGameActors[actor.EntityId];
    target = actor;
    if (GameWorldScene.IsValid()) { GameWorldScene.UpsertRemoteActor(target); }
}

void FD3D9RenderDevice::UpdateRemoteGameEntityPosition(uint64 entityId, const FGameWorldPosition& position)
{
    if (auto actor = RemoteGameActors.find(entityId); actor != RemoteGameActors.end())
    {
        FGameWorldPosition resolved = position;
        const double deltaX = position.X - actor->second.Position.X;
        const double deltaZ = position.Z - actor->second.Position.Z;
        if (deltaX * deltaX + deltaZ * deltaZ > 0.0001) { resolved.Angle = std::atan2(-deltaX, deltaZ); }
        else { resolved.Angle = actor->second.Position.Angle; }
        actor->second.Position = resolved;
        if (GameWorldScene.IsValid()) { GameWorldScene.UpdateRemoteActorPosition(entityId, resolved); }
        return;
    }
    if (auto player = RemoteGamePlayers.find(entityId); player != RemoteGamePlayers.end())
    {
        player->second.Position = position;
        if (GameWorldScene.IsValid()) { GameWorldScene.UpsertRemotePlayer(player->second); }
    }
}

void FD3D9RenderDevice::RemoveRemoteGameEntity(uint64 entityId)
{
    RemoteGamePlayers.erase(entityId);
    RemoteGameActors.erase(entityId);
    if (GameWorldScene.IsValid())
    {
        GameWorldScene.RemoveRemotePlayer(entityId);
        GameWorldScene.RemoveRemoteActor(entityId);
    }
}

void FD3D9RenderDevice::ClearRemoteGameActors()
{
    RemoteGameActors.clear();
    if (GameWorldScene.IsValid()) { GameWorldScene.ClearRemoteActors(); }
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
    UiBatchVertices.clear();
    UiBatchTexture = nullptr;
    UiBatchActive = false;
    UiTexturePreloadQueue.clear();
    UiTextureUrgentQueue.clear();
    UiTexturePreloadKnown.clear();
    UiTextureUrgentKnown.clear();
    UiTexturePreloadHead = 0;
    UiQueuedWindowCount = 0;
    UiWindowWasVisible.clear();
    EncodedTextCache.clear();
    WrappedTextCache.clear();
    FontCache.Release();
}

void FD3D9RenderDevice::Shutdown()
{
    GameWorldScene.Shutdown();
    RemoteGamePlayers.clear();
    RemoteGameActors.clear();
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

    if (UiBatchActive)
    {
        if (UiTextureUrgentKnown.insert(key).second) { UiTextureUrgentQueue.push_back(key); }
        return nullptr;
    }

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

void FD3D9RenderDevice::BeginUiBatch()
{
    UiBatchActive = true;
    UiBatchTexture = nullptr;
    UiBatchPremultiplied = false;
    UiBatchVertices.clear();
    if (UiBatchVertices.capacity() < 24576) { UiBatchVertices.reserve(24576); }
}

void FD3D9RenderDevice::FlushUiBatch()
{
    if (!Device || UiBatchVertices.empty()) { return; }
    Device->SetRenderState(D3DRS_SRCBLEND, UiBatchPremultiplied ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Device->SetTexture(0, UiBatchTexture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<unsigned int>(UiBatchVertices.size() / 3), UiBatchVertices.data(), sizeof(FUiBatchVertex));
    UiBatchVertices.clear();
}

void FD3D9RenderDevice::QueueUiQuad(IDirect3DTexture9* texture, const std::array<FUiBatchVertex, 4>& strip, bool premultiplied)
{
    if (!UiBatchActive)
    {
        BeginUiBatch();
    }
    if ((!UiBatchVertices.empty() && (UiBatchTexture != texture || UiBatchPremultiplied != premultiplied)) || UiBatchVertices.size() + 6 > 24576)
    {
        FlushUiBatch();
    }
    UiBatchTexture = texture;
    UiBatchPremultiplied = premultiplied;
    UiBatchVertices.push_back(strip[0]);
    UiBatchVertices.push_back(strip[1]);
    UiBatchVertices.push_back(strip[2]);
    UiBatchVertices.push_back(strip[2]);
    UiBatchVertices.push_back(strip[1]);
    UiBatchVertices.push_back(strip[3]);
}

void FD3D9RenderDevice::DrawSolidRect(float x, float y, float w, float h, unsigned long color, float alpha)
{
    if (!Device || w <= 0.0f || h <= 0.0f) { return; }
    const float x1 = SnapPixel(x);
    const float y1 = SnapPixel(y);
    const float x2 = SnapPixel(x + SnapSize(w));
    const float y2 = SnapPixel(y + SnapSize(h));
    color = ApplyAlpha(color, alpha);
    const std::array<FUiBatchVertex, 4> vertices{{
        {x1, y1, 0.0f, 1.0f, color, 0.0f, 0.0f},
        {x2, y1, 0.0f, 1.0f, color, 1.0f, 0.0f},
        {x1, y2, 0.0f, 1.0f, color, 0.0f, 1.0f},
        {x2, y2, 0.0f, 1.0f, color, 1.0f, 1.0f}
    }};
    QueueUiQuad(nullptr, vertices, false);
}

void FD3D9RenderDevice::DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color, bool premultiplied)
{
    if (!Device || !texture || w <= 0.0f || h <= 0.0f) { return; }
    const float x1 = SnapPixel(x);
    const float y1 = SnapPixel(y);
    const float x2 = SnapPixel(x + SnapSize(w));
    const float y2 = SnapPixel(y + SnapSize(h));
    const std::array<FUiBatchVertex, 4> vertices{{
        {x1, y1, 0.0f, 1.0f, color, u1, v1},
        {x2, y1, 0.0f, 1.0f, color, u2, v1},
        {x1, y2, 0.0f, 1.0f, color, u1, v2},
        {x2, y2, 0.0f, 1.0f, color, u2, v2}
    }};
    QueueUiQuad(texture, vertices, premultiplied);
}

void FD3D9RenderDevice::DrawTextureQuadRotated(IDirect3DTexture9* texture, float x, float y, float w, float h, float centerX, float centerY, float degrees, float u1, float v1, float u2, float v2, unsigned long color)
{
    if (!Device || !texture || w <= 0.0f || h <= 0.0f) { return; }
    const float radians = degrees * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(radians);
    const float sn = std::sin(radians);
    auto rotate = [&](float px, float py)
    {
        const float dx = px - centerX;
        const float dy = py - centerY;
        return std::pair<float, float>{SnapPixel(centerX + dx * c - dy * sn), SnapPixel(centerY + dx * sn + dy * c)};
    };
    const auto p0 = rotate(x, y);
    const auto p1 = rotate(x + w, y);
    const auto p2 = rotate(x, y + h);
    const auto p3 = rotate(x + w, y + h);
    const std::array<FUiBatchVertex, 4> vertices{{
        {p0.first, p0.second, 0.0f, 1.0f, color, u1, v1},
        {p1.first, p1.second, 0.0f, 1.0f, color, u2, v1},
        {p2.first, p2.second, 0.0f, 1.0f, color, u1, v2},
        {p3.first, p3.second, 0.0f, 1.0f, color, u2, v2}
    }};
    QueueUiQuad(texture, vertices, false);
}

void FD3D9RenderDevice::DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTexCoord* coords, int32 textureWidth, int32 textureHeight, unsigned long color)
{
    if (!Device || !texture || !coords || textureWidth <= 0 || textureHeight <= 0 || w <= 0.0f || h <= 0.0f) { return; }
    auto u = [textureWidth](int32 value) { return static_cast<float>(value) / static_cast<float>(textureWidth); };
    auto v = [textureHeight](int32 value) { return static_cast<float>(value) / static_cast<float>(textureHeight); };
    const std::array<FUiBatchVertex, 4> vertices{{
        {SnapPixel(x), SnapPixel(y), 0.0f, 1.0f, color, u(coords[0].U), v(coords[0].V)},
        {SnapPixel(x + w), SnapPixel(y), 0.0f, 1.0f, color, u(coords[1].U), v(coords[1].V)},
        {SnapPixel(x), SnapPixel(y + h), 0.0f, 1.0f, color, u(coords[3].U), v(coords[3].V)},
        {SnapPixel(x + w), SnapPixel(y + h), 0.0f, 1.0f, color, u(coords[2].U), v(coords[2].V)}
    }};
    QueueUiQuad(texture, vertices, false);
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

    const float sx = dst.W / static_cast<float>(SpriteExtentX(*sprite));
    const float sy = dst.H / static_cast<float>(SpriteExtentY(*sprite));
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

bool FD3D9RenderDevice::DrawSpriteRotated(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, float degrees, float alpha)
{
    const FUiSpriteDef* sprite = FindSprite(window, spriteName);
    if (!sprite) { return false; }
    const float sx = dst.W / static_cast<float>(SpriteExtentX(*sprite));
    const float sy = dst.H / static_cast<float>(SpriteExtentY(*sprite));
    const float centerX = dst.X + dst.W * 0.5f;
    const float centerY = dst.Y + dst.H * 0.5f;
    const unsigned long color = Argb(static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f), 255, 255, 255);
    bool drew = false;
    for (const FUiSpritePiece& piece : sprite->Pieces)
    {
        FD3D9TextureEntry* texture = LoadTextureByName(ctx.Resources, piece.TextureName, ctx.Logger);
        if (!texture || !texture->Texture) { continue; }
        const float x = dst.X + static_cast<float>(std::min(piece.DstLeft, piece.DstRight)) * sx;
        const float y = dst.Y + static_cast<float>(std::min(piece.DstTop, piece.DstBottom)) * sy;
        const float w = static_cast<float>(AbsInt(piece.DstRight - piece.DstLeft)) * sx;
        const float h = static_cast<float>(AbsInt(piece.DstBottom - piece.DstTop)) * sy;
        int32 left = piece.SrcLeft;
        int32 top = piece.SrcTop;
        int32 right = piece.SrcRight;
        int32 bottom = piece.SrcBottom;
        if (piece.DstRight < piece.DstLeft) { std::swap(left, right); }
        if (piece.DstBottom < piece.DstTop) { std::swap(top, bottom); }
        DrawTextureQuadRotated(texture->Texture, x, y, w, h, centerX, centerY, degrees, static_cast<float>(left) / texture->Width, static_cast<float>(top) / texture->Height, static_cast<float>(right) / texture->Width, static_cast<float>(bottom) / texture->Height, color);
        drew = true;
    }
    return drew;
}

uint64 FD3D9RenderDevice::BuildTextCacheKey(std::string_view text, int32 fontIndex, int32 extraA, int32 extraB) const
{
    uint64 hash = 1469598103934665603ull;
    for (unsigned char ch : text) { hash = (hash ^ static_cast<uint64>(ch)) * 1099511628211ull; }
    const auto mix = [&hash](uint32 value)
    {
        for (int shift = 0; shift < 32; shift += 8) { hash = (hash ^ static_cast<uint64>((value >> shift) & 0xffu)) * 1099511628211ull; }
    };
    mix(static_cast<uint32>(fontIndex));
    mix(static_cast<uint32>(extraA));
    mix(static_cast<uint32>(extraB));
    return hash;
}

const FD3D9RenderDevice::FCachedEncodedText& FD3D9RenderDevice::GetEncodedText(FDrawContext& ctx, std::string_view text, int32 fontIndex)
{
    constexpr size_t MaxEncodedEntries = 2048;
    const uint64 key = BuildTextCacheKey(text, fontIndex);
    auto it = EncodedTextCache.find(key);
    if (it != EncodedTextCache.end() && it->second.FontIndex == fontIndex && it->second.Text == text) { return it->second; }
    if (EncodedTextCache.size() >= MaxEncodedEntries) { EncodedTextCache.clear(); }

    FCachedEncodedText value;
    value.Text.assign(text);
    value.FontIndex = fontIndex;
    const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
    if (font && font->IsValid())
    {
        value.Bytes = font->EncodeUtf8ToCp1251(text);
        value.Width = font->MeasureCodepageText(value.Bytes);
    }
    return EncodedTextCache.insert_or_assign(key, std::move(value)).first->second;
}

void FD3D9RenderDevice::DrawTextRect(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex)
{
    if (!Device || text.empty() || rect.W <= 1.0f || rect.H <= 1.0f) { return; }
    const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
    if (!font || !font->IsValid()) { return; }
    const FCachedEncodedText& encoded = GetEncodedText(ctx, text, fontIndex);
    if (encoded.Bytes.empty()) { return; }

    const float scale = std::max(0.5f, ctx.Scale);
    float x = rect.X;
    if (center) { x += std::max(0.0f, (rect.W - static_cast<float>(encoded.Width) * scale) * 0.5f); }
    const float lineHeight = static_cast<float>(font->LineHeight()) * scale;
    const float y = rect.Y + std::max(0.0f, (rect.H - lineHeight) * 0.5f);
    const unsigned long fontColor = PremultiplyDiffuse(color);
    for (uint8 ch : encoded.Bytes)
    {
        if (ch < 32) { continue; }
        const FD3D9BitmapGlyph& glyph = font->Glyph(ch);
        if (x > rect.X + rect.W) { break; }
        if (glyph.SourceW > 0 && glyph.SourceH > 0 && ch != 32)
        {
            const float dx = x + static_cast<float>(glyph.XOffset) * scale;
            const float dy = y + static_cast<float>(font->Baseline() - glyph.YOffset) * scale;
            const float dw = static_cast<float>(glyph.SourceW) * scale;
            const float dh = static_cast<float>(glyph.SourceH) * scale;
            const float u1 = static_cast<float>(glyph.SourceX) / static_cast<float>(font->Width());
            const float v1 = static_cast<float>(glyph.SourceY) / static_cast<float>(font->Height());
            const float u2 = static_cast<float>(glyph.SourceX + glyph.SourceW) / static_cast<float>(font->Width());
            const float v2 = static_cast<float>(glyph.SourceY + glyph.SourceH) / static_cast<float>(font->Height());
            DrawTextureQuad(font->AtlasTexture(), dx, dy, dw, dh, u1, v1, u2, v2, fontColor, true);
        }
        x += static_cast<float>(glyph.Advance) * scale;
    }
}

const std::vector<std::string>& FD3D9RenderDevice::WrapTextLines(FDrawContext& ctx, std::string_view text, float width, int32 fontIndex)
{
    constexpr size_t MaxWrappedEntries = 1024;
    const float scale = std::max(0.5f, ctx.Scale);
    const int32 widthQuarterPixels = static_cast<int32>(std::lround(std::max(0.0f, width) * 4.0f));
    const int32 scaleMilli = static_cast<int32>(std::lround(scale * 1000.0f));
    const uint64 key = BuildTextCacheKey(text, fontIndex, widthQuarterPixels, scaleMilli);
    auto it = WrappedTextCache.find(key);
    if (it != WrappedTextCache.end() && it->second.FontIndex == fontIndex && it->second.WidthQuarterPixels == widthQuarterPixels && it->second.ScaleMilli == scaleMilli && it->second.Text == text) { return it->second.Lines; }
    if (WrappedTextCache.size() >= MaxWrappedEntries) { WrappedTextCache.clear(); }

    FCachedWrappedText value;
    value.Text.assign(text);
    value.FontIndex = fontIndex;
    value.WidthQuarterPixels = widthQuarterPixels;
    value.ScaleMilli = scaleMilli;
    std::vector<std::string>& lines = value.Lines;
    const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
    if (!font || !font->IsValid() || width <= 1.0f)
    {
        lines.emplace_back(text);
        return WrappedTextCache.insert_or_assign(key, std::move(value)).first->second.Lines;
    }

    const auto fits = [&](std::string_view candidate)
    {
        const std::vector<uint8> bytes = font->EncodeUtf8ToCp1251(candidate);
        return static_cast<float>(font->MeasureCodepageText(bytes)) * scale <= width;
    };
    size_t paragraphStart = 0;
    while (paragraphStart <= text.size())
    {
        const size_t paragraphEnd = text.find('\n', paragraphStart);
        const std::string_view paragraph = text.substr(paragraphStart, paragraphEnd == std::string_view::npos ? text.size() - paragraphStart : paragraphEnd - paragraphStart);
        if (paragraph.empty()) { lines.emplace_back(); }
        else
        {
            std::string line;
            size_t cursor = 0;
            while (cursor < paragraph.size())
            {
                while (cursor < paragraph.size() && (paragraph[cursor] == ' ' || paragraph[cursor] == '\t' || paragraph[cursor] == '\r')) { ++cursor; }
                if (cursor >= paragraph.size()) { break; }
                const size_t wordStart = cursor;
                while (cursor < paragraph.size() && paragraph[cursor] != ' ' && paragraph[cursor] != '\t' && paragraph[cursor] != '\r') { ++cursor; }
                const std::string_view word = paragraph.substr(wordStart, cursor - wordStart);
                if (line.empty()) { line.assign(word); continue; }
                std::string candidate;
                candidate.reserve(line.size() + 1 + word.size());
                candidate = line;
                candidate.push_back(' ');
                candidate.append(word);
                if (fits(candidate)) { line = std::move(candidate); }
                else { lines.push_back(std::move(line)); line.assign(word); }
            }
            if (!line.empty()) { lines.push_back(std::move(line)); }
        }
        if (paragraphEnd == std::string_view::npos) { break; }
        paragraphStart = paragraphEnd + 1;
    }
    if (lines.empty()) { lines.emplace_back(); }
    return WrappedTextCache.insert_or_assign(key, std::move(value)).first->second.Lines;
}

void FD3D9RenderDevice::DrawTextBlock(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex)
{
    const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
    const float lineHeight = font && font->IsValid() ? static_cast<float>(font->LineHeight()) * std::max(0.5f, ctx.Scale) + 2.0f : 14.0f * ctx.Scale;
    const std::vector<std::string>& lines = WrapTextLines(ctx, text, rect.W, fontIndex);
    if (lines.size() == 1)
    {
        DrawTextRect(ctx, rect, lines.front(), color, center, fontIndex);
        return;
    }
    const size_t visibleLines = static_cast<size_t>(std::max(1.0f, std::floor((rect.H + 0.5f) / lineHeight)));
    float y = rect.Y;
    for (size_t index = 0; index < std::min(lines.size(), visibleLines); ++index)
    {
        const float remainingHeight = std::max(1.0f, rect.Y + rect.H - y);
        DrawTextRect(ctx, FUiRectF{rect.X, y, rect.W, std::min(lineHeight, remainingHeight)}, lines[index], color, center, fontIndex);
        y += lineHeight;
    }
}

void FD3D9RenderDevice::DrawControl(FDrawContext& ctx, const FUiWindowDef& window, const FUiControlDef& control, const FUiRectF& windowRect, float alpha, int32 gameWindowIndex)
{
    const bool gameMode = ctx.Ui.Mode() == EUiRuntimeMode::Game;
    const bool runtimeVisibility = gameMode && ctx.Ui.UsesRuntimeVisibility(window.Name, control.Id);
    if ((control.Hidden && !runtimeVisibility) || (gameMode && ctx.Ui.IsGameControlHidden(window.Name, control.Id))) { return; }

    const bool runtimeDisabled = gameMode && ctx.Ui.IsGameControlDisabled(window.Name, control.Id);
    const FUiActionState& state = ctx.Ui.ActionState();
    const bool hovered = state.HoverControlId == control.Id && (gameWindowIndex < 0 || state.HoverWindowIndex == gameWindowIndex);
    const bool pressed = state.PressedControlId == control.Id && (gameWindowIndex < 0 || state.PressedWindowIndex == gameWindowIndex);
    const bool focused = state.FocusedControlId == control.Id && (gameWindowIndex < 0 || state.FocusedWindowIndex == gameWindowIndex);
    FUiRectF r
    {
        windowRect.X + control.Rect.X * ctx.Scale, windowRect.Y + control.Rect.Y * ctx.Scale, control.Rect.W * ctx.Scale, control.Rect.H * ctx.Scale
    };

    if (gameMode && ctx.Ui.IsMapPlayerControl(window.Name, control.Id))
    {
        const auto mapControl = std::find_if(window.Controls.begin(), window.Controls.end(), [](const FUiControlDef& item) { return item.Id == 1; });
        const std::optional<std::pair<float, float>> uv = ctx.Ui.GameMapPlayerUv();
        if (mapControl != window.Controls.end() && uv)
        {
            const float mapX = windowRect.X + static_cast<float>(mapControl->Rect.X) * ctx.Scale;
            const float mapY = windowRect.Y + static_cast<float>(mapControl->Rect.Y) * ctx.Scale;
            const float mapW = static_cast<float>(mapControl->Rect.W) * ctx.Scale;
            const float mapH = static_cast<float>(mapControl->Rect.H) * ctx.Scale;
            r.X = mapX + uv->first * mapW - r.W * 0.5f;
            r.Y = mapY + uv->second * mapH - r.H * 0.5f;
        }
    }

    if (control.Class == EUiControlClass::Image)
    {
        const std::string_view imageName = gameMode ? ctx.Ui.GameControlImage(window.Name, control) : std::string_view(control.ImageName);
        if (!Common::EqualsNoCase(imageName, "black") && !imageName.empty())
        {
            const FUiRectF imageRect = r.W > 0.0f && r.H > 0.0f ? r : windowRect;
            const float rotation = gameMode ? ctx.Ui.GameControlRotation(window.Name, control) : control.RotationDegrees;
            if (std::abs(rotation) > 0.001f) { DrawSpriteRotated(ctx, window, imageName, imageRect, rotation, alpha); }
            else { DrawSprite(ctx, window, imageName, imageRect, alpha); }
        }
        return;
    }

    if (control.Class == EUiControlClass::Button)
    {
        const bool modalDisabled = ctx.Ui.HasModalDialog() && Common::EqualsNoCase(window.Name, ctx.Ui.ActiveModalWindow().Name) && !ctx.Ui.Character().IsModalActionAllowed(control);
        const bool effectiveDisabled = control.Disabled || runtimeDisabled;
        const bool runtimeChecked = ctx.Ui.Mode() == EUiRuntimeMode::Game && ctx.Ui.IsGameControlChecked(window.Name, control.Id);
        const std::string_view sprite = runtimeChecked && !control.CheckedImage.empty() && !pressed ? std::string_view(control.CheckedImage) : SelectButtonSprite(control, effectiveDisabled, hovered, pressed);
        const FUiRectF visualRect{r.X + static_cast<float>(control.ImageOffset.X) * ctx.Scale, r.Y + static_cast<float>(control.ImageOffset.Y) * ctx.Scale, r.W, r.H};

        if (!sprite.empty())
        {
            DrawSprite(ctx, window, sprite, visualRect, alpha);
        }
        else if (runtimeDisabled)
        {
            DrawSolidRect(r.X, r.Y, r.W, r.H, Argb(135, 16, 14, 12), alpha);
        }

        std::string text = TextForControl(ctx.Ui, window, control);

        if (!text.empty())
        {
            unsigned long color = ColorToArgb((control.Disabled || runtimeDisabled || modalDisabled) ? control.DisabledColor : (hovered ? control.FocusColor : control.TextColor));
            DrawTextRect(ctx, r, text, ApplyAlpha(color, alpha), true, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }

    if (control.Class == EUiControlClass::CheckBox)
    {
        std::string_view sprite;

        const bool checked = ctx.Ui.Mode() == EUiRuntimeMode::Game ? ctx.Ui.IsGameControlChecked(window.Name, control.Id) : state.SaveLogin;
        if (checked && !control.CheckedImage.empty())
        {
            sprite = control.CheckedImage;
        }
        else if (hovered && !control.FocusedImage.empty())
        {
            sprite = control.FocusedImage;
        }
        else
        {
            sprite = control.UncheckedImage;
        }

        if (!sprite.empty())
        {
            DrawSprite(ctx, window, sprite, r, alpha);
        }

        return;
    }

    if (control.Class == EUiControlClass::RadioButton)
    {
        const bool checked = ctx.Ui.Mode() == EUiRuntimeMode::Game ? ctx.Ui.IsGameControlChecked(window.Name, control.Id) : ctx.Ui.Character().SelectedSlotIndex() == control.Id - 63;
        const std::string_view sprite = checked ? std::string_view(control.CheckedImage) : std::string_view(control.UncheckedImage);

        if (!sprite.empty())
        {
            DrawSprite(ctx, window, sprite, FUiRectF{r.X + static_cast<float>(control.ImageOffset.X) * ctx.Scale, r.Y + static_cast<float>(control.ImageOffset.Y) * ctx.Scale, r.W, r.H}, alpha);
        }

        std::string text = TextForControl(ctx.Ui, window, control);

        if (!text.empty())
        {
            unsigned long color = ColorToArgb((control.Disabled || runtimeDisabled) ? control.DisabledColor : (hovered ? control.FocusColor : control.TextColor));
            DrawTextRect(ctx, r, text, ApplyAlpha(color, alpha), false, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }

    if (control.Class == EUiControlClass::SpinButton)
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
        const bool effectiveDisabled = (control.Disabled && !ctx.Ui.OverridesStaticDisabled(window.Name, control.Id)) || runtimeDisabled;
        const bool hotLeft = hovered && state.SpinHoverDirection < 0;
        const bool hotRight = hovered && state.SpinHoverDirection > 0;
        const bool pressedLeft = pressed && state.SpinPressedDirection < 0;
        const bool pressedRight = pressed && state.SpinPressedDirection > 0;
        DrawSprite(ctx, window, SelectSubButtonSprite(leftButton, effectiveDisabled, hotLeft, pressedLeft, "sl_normal", "sl_focus", "sl_push", "sl_disabled"), left, alpha);
        DrawSprite(ctx, window, SelectSubButtonSprite(rightButton, effectiveDisabled, hotRight, pressedRight, "sr_normal", "sr_focus", "sr_push", "sr_disabled"), right, alpha);
        return;
    }

    if (control.Class == EUiControlClass::ScrollBar)
    {
        DrawSolidRect(r.X, r.Y + r.H * 0.35f, r.W, std::max(2.0f, r.H * 0.3f), Argb(150, 35, 30, 25), alpha);
        const float range = static_cast<float>(std::max(1, control.RangeMax - control.RangeMin));
        const float ratio = std::clamp((ctx.Ui.GameControlValue(window.Name, control) - static_cast<float>(control.RangeMin)) / range, 0.0f, 1.0f);
        const float thumbW = static_cast<float>(control.ScrollSpriteWidth > 0 ? control.ScrollSpriteWidth : 16) * ctx.Scale;
        const FUiRectF thumb{r.X + ratio * std::max(0.0f, r.W - thumbW), r.Y + (r.H - static_cast<float>(control.ScrollSpriteHeight > 0 ? control.ScrollSpriteHeight : control.Rect.H) * ctx.Scale) * 0.5f, thumbW, static_cast<float>(control.ScrollSpriteHeight > 0 ? control.ScrollSpriteHeight : control.Rect.H) * ctx.Scale};
        if (!control.ScrollSpriteName.empty()) { DrawSprite(ctx, window, control.ScrollSpriteName, thumb, alpha); }
        else { DrawSolidRect(thumb.X, thumb.Y, thumb.W, thumb.H, Argb(220, 190, 145, 82), alpha); }
        return;
    }

    if (control.Class == EUiControlClass::Slot)
    {
        const std::string_view fill = !control.SlotFullImage.empty() ? std::string_view(control.SlotFullImage) : std::string_view(control.SlotEmptyImage);

        if (!fill.empty())
        {
            DrawSpriteTinted(ctx, window, fill, r, ApplyAlpha(Argb(128, 0x14, 0x14, 0x14), alpha));
        }

        if (!control.SlotBorderImage.empty())
        {
            DrawSpriteTinted(ctx, window, control.SlotBorderImage, r, ApplyAlpha(Argb(255, 0x9e, 0x7c, 0x6a), alpha));
        }

        return;
    }

    if (control.Class == EUiControlClass::ProgressBar)
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

        const float ratio = ctx.Ui.Mode() == EUiRuntimeMode::CharacterSelect ? ctx.Ui.Character().CharacterProgressRatio(control.Id) : ctx.Ui.Mode() == EUiRuntimeMode::Game ? ctx.Ui.GameProgressRatio(window.Name, control.Id) : 1.0f;
        DrawSolidRect(r.X, r.Y, r.W, r.H, Argb(210, 30, 28, 24), alpha);
        DrawSolidRect(r.X, r.Y, r.W * std::clamp(ratio, 0.0f, 1.0f), r.H, color, alpha);
        std::string status = ctx.Ui.Mode() == EUiRuntimeMode::CharacterSelect ? ctx.Ui.Character().CharacterProgressText(control) : ctx.Ui.Mode() == EUiRuntimeMode::Game && Common::EqualsNoCase(window.Name, "statinfo") ? ctx.Ui.GameControlText(window.Name, control) : std::string{};

        if (!status.empty() && !control.StatusShow.empty())
        {
            FUiRectF sr
            {
                r.X + static_cast<float>(control.StatusPos.X) * ctx.Scale, r.Y + static_cast<float>(control.StatusPos.Y) * ctx.Scale, std::max(44.0f * ctx.Scale, r.W), 12.0f * ctx.Scale
            };
            DrawTextRect(ctx, sr, status, ApplyAlpha(ColorToArgb(control.TextColor), alpha), (control.Id == 41 || control.Id == 42) ? true : control.TextCenter, control.Font >= 0 ? control.Font : window.Font);
        }

        return;
    }

    if (control.Class == EUiControlClass::Edit)
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
        else if (ctx.Ui.Mode() == EUiRuntimeMode::Game)
        {
            text = ctx.Ui.GameEditText(window.Name, control.Id);
        }
        else if (control.Id == 7)
        {
            text = state.LoginText;
        }
        else if (control.Id == 8 || control.Password)
        {
            text.assign(state.PasswordText.size(), '*');
        }

        const int32 fontIndex = control.Font >= 0 ? control.Font : window.Font;
        const unsigned long editColor = gameMode && Common::EqualsNoCase(window.Name, "chat_st2") && control.Id == 3 ? ColorToArgb(ctx.Ui.GameChatModeColor()) : ColorToArgb((control.Disabled || runtimeDisabled) ? control.DisabledColor : control.TextColor);
        if (!text.empty()) { DrawTextRect(ctx, r, text, ApplyAlpha(editColor, alpha), control.TextCenter, fontIndex); }
        if (focused)
        {
            if (gameMode && ctx.Ui.IsTextCaretVisible())
            {
                const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
                const FCachedEncodedText& encoded = GetEncodedText(ctx, text, fontIndex);
                const float textWidth = font && font->IsValid() ? static_cast<float>(encoded.Width) * std::max(0.5f, ctx.Scale) : 0.0f;
                const float caretX = std::min(r.X + r.W - 1.0f, r.X + textWidth + 1.0f);
                DrawSolidRect(caretX, r.Y + 2.0f * ctx.Scale, std::max(1.0f, ctx.Scale), std::max(2.0f, r.H - 4.0f * ctx.Scale), ApplyAlpha(editColor, alpha));
            }
            DrawSolidRect(r.X, r.Y + r.H - 1.0f, r.W, 1.0f, Argb(190, 237, 208, 161), alpha);
        }
        return;
    }

    if (IsTextLikeControl(control))
    {
        const int32 fontIndex = control.Font >= 0 ? control.Font : window.Font;
        if (gameMode && Common::EqualsNoCase(window.Name, "chat_st2") && control.Id == 1)
        {
            const FD3D9BitmapFont* font = FontCache.GetFont(Device, ctx.Resources, fontIndex, ctx.Logger);
            const float lineHeight = font && font->IsValid() ? static_cast<float>(font->LineHeight()) * std::max(0.5f, ctx.Scale) + 2.0f : 14.0f * ctx.Scale;
            const size_t visibleCount = static_cast<size_t>(std::max(1.0f, std::floor(r.H / lineHeight)));
            std::vector<std::pair<std::string, unsigned long>> lines;
            lines.reserve(visibleCount);
            const auto& history = ctx.Ui.GameChatHistory();
            for (auto chatIt = history.rbegin(); chatIt != history.rend() && lines.size() < visibleCount; ++chatIt)
            {
                const std::vector<std::string>& wrapped = WrapTextLines(ctx, chatIt->Text, r.W, fontIndex);
                const unsigned long lineColor = ColorToArgb(chatIt->Color);
                for (auto lineIt = wrapped.rbegin(); lineIt != wrapped.rend() && lines.size() < visibleCount; ++lineIt) { lines.emplace_back(*lineIt, lineColor); }
            }
            std::reverse(lines.begin(), lines.end());
            float y = r.Y;
            for (const auto& line : lines)
            {
                if (y + lineHeight > r.Y + r.H + 0.5f) { break; }
                DrawTextRect(ctx, FUiRectF{r.X, y, r.W, lineHeight}, line.first, ApplyAlpha(line.second, alpha), false, fontIndex);
                y += lineHeight;
            }
            return;
        }
        const std::string text = TextForControl(ctx.Ui, window, control);
        if (!text.empty()) { DrawTextBlock(ctx, r, text, ApplyAlpha(ColorToArgb((control.Disabled || runtimeDisabled) ? control.DisabledColor : control.TextColor), alpha), control.TextCenter, fontIndex); }
        return;
    }
}

bool FD3D9RenderDevice::DrawWindow(FDrawContext& ctx, const FUiWindowDef& window, const FUiRectF& dst, float alpha, int32 gameWindowIndex)
{
    if (!window.DrawNone && !window.DrawSpriteName.empty())
    {
        DrawSprite(ctx, window, window.DrawSpriteName, dst, alpha);
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

        DrawTextRect(ctx, tr, title, ApplyAlpha(ColorToArgb(window.TextColor), alpha), false, window.Font);
    }

    for (const auto& control : window.Controls)
    {
        DrawControl(ctx, window, control, dst, alpha, gameWindowIndex);
    }

    return true;
}

void FD3D9RenderDevice::DrawModalDialog(FDrawContext& ctx, const RECT& rect)
{
    if (!ctx.Ui.HasModalDialog()) { return; }

    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    const float alpha = ctx.Ui.ModalAnimationAlpha();
    DrawSolidRect(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(width), static_cast<float>(height), Argb(120, 0, 0, 0), alpha);
    const FUiWindowDef& window = ctx.Ui.ActiveModalWindow();

    if (window.Name.empty()) { return; }

    FUiRectF wr = ctx.Ui.BuildAnimatedModalRect(rect);
    DrawWindow(ctx, window, wr, alpha);

    if (!ctx.Ui.ModalMessage().empty() && window.Controls.empty())
    {
        DrawTextRect(ctx, FUiRectF{wr.X + 18.0f * ctx.Scale, wr.Y + wr.H * 0.38f, wr.W - 36.0f * ctx.Scale, 44.0f * ctx.Scale}, ctx.Ui.ModalMessage(), ApplyAlpha(Argb(235, 237, 208, 161), alpha), true, window.Font);
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

void FD3D9RenderDevice::QueueWindowTextures(const FUiWindowDef& window, bool highPriority)
{
    auto queueTexture = [&](std::string_view textureName)
    {
        std::string key = Common::ToLower(textureName);
        if (key.empty() || key == "black" || TextureCache.contains(key)) { return; }
        if (highPriority)
        {
            if (UiTextureUrgentKnown.insert(key).second) { UiTextureUrgentQueue.push_back(std::move(key)); }
        }
        else if (UiTexturePreloadKnown.insert(key).second)
        {
            UiTexturePreloadQueue.push_back(std::move(key));
        }
    };
    auto queueSprite = [&](std::string_view spriteName)
    {
        if (spriteName.empty() || Common::EqualsNoCase(spriteName, "black")) { return; }
        const FUiSpriteDef* sprite = FindSprite(window, spriteName);
        if (!sprite) { return; }
        for (const FUiSpritePiece& piece : sprite->Pieces) { queueTexture(piece.TextureName); }
    };
    auto queueSubButton = [&](const FUiSubButtonDef& button)
    {
        queueSprite(button.CheckedImage);
        queueSprite(button.FocusedImage);
        queueSprite(button.DisabledImage);
        queueSprite(button.UncheckedImage);
    };

    queueSprite(window.DrawSpriteName);
    for (const FUiControlDef& control : window.Controls)
    {
        queueSprite(control.CheckedImage);
        queueSprite(control.UncheckedImage);
        queueSprite(control.FocusedImage);
        queueSprite(control.DisabledImage);
        queueSprite(control.ImageName);
        queueSprite(control.DrawSpriteName);
        queueSprite(control.SlotEmptyImage);
        queueSprite(control.SlotFullImage);
        queueSprite(control.SlotBorderImage);
        queueSprite(control.ScrollSpriteName);
        queueSprite(control.StatusShow);
        queueSubButton(control.LeftButton);
        queueSubButton(control.RightButton);
        if (control.Class == EUiControlClass::SpinButton)
        {
            queueSprite("sl_normal"); queueSprite("sl_focus"); queueSprite("sl_push"); queueSprite("sl_disabled");
            queueSprite("sr_normal"); queueSprite("sr_focus"); queueSprite("sr_push"); queueSprite("sr_disabled");
        }
    }
}

void FD3D9RenderDevice::PumpUiTexturePreload(const FResourceManager& resources, FLogger* logger, double budgetMilliseconds, size_t maxTextures)
{
    if (!Device || maxTextures == 0) { return; }
    const auto start = std::chrono::steady_clock::now();
    size_t loaded = 0;
    while (loaded < maxTextures)
    {
        std::string textureName;
        if (!UiTextureUrgentQueue.empty())
        {
            textureName = std::move(UiTextureUrgentQueue.back());
            UiTextureUrgentQueue.pop_back();
            UiTextureUrgentKnown.erase(textureName);
        }
        else
        {
            while (UiTexturePreloadHead < UiTexturePreloadQueue.size() && TextureCache.contains(UiTexturePreloadQueue[UiTexturePreloadHead])) { ++UiTexturePreloadHead; }
            if (UiTexturePreloadHead >= UiTexturePreloadQueue.size()) { break; }
            textureName = UiTexturePreloadQueue[UiTexturePreloadHead++];
        }
        if (!TextureCache.contains(textureName)) { LoadTextureByName(resources, textureName, logger); }
        ++loaded;
        if (budgetMilliseconds > 0.0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= budgetMilliseconds) { break; }
    }
    if (UiTexturePreloadHead >= UiTexturePreloadQueue.size())
    {
        UiTexturePreloadQueue.clear();
        UiTexturePreloadKnown.clear();
        UiTexturePreloadHead = 0;
    }
}

void FD3D9RenderDevice::PreloadUiTextures(const FResourceManager& resources, const FUiRuntime& ui, FLogger* logger)
{
    LoadTextureByName(resources, ui.LoginBackgroundTexture(), logger);
    QueueWindowTextures(ui.ConnectionWindow(), true);
    PumpUiTexturePreload(resources, logger, 0.0, std::numeric_limits<size_t>::max());
    QueueWindowTextures(ui.PickPersonWindow());
    QueueWindowTextures(ui.CreatePersonWindow());
    QueueWindowTextures(ui.DeleteCharacterWindow());
    QueueWindowTextures(ui.ConnectMessageWindow());
    QueueWindowTextures(ui.MessageWindow());
    const auto& windows = ui.GameWindows();
    const auto& visibility = ui.GameWindowVisibility();
    UiWindowWasVisible.assign(windows.size(), false);
    for (size_t index = 0; index < windows.size(); ++index)
    {
        const bool visible = index < visibility.size() && visibility[index];
        UiWindowWasVisible[index] = visible;
        if (visible || ShouldPrewarmGameWindow(windows[index])) { QueueWindowTextures(windows[index], visible); }
    }
    UiQueuedWindowCount = windows.size();
    FontCache.Preload(Device, resources, logger);
    if (logger) { logger->Info("D3D9 UI preload: texture_cache=" + std::to_string(TextureCache.size()) + ", background_queue=" + std::to_string(UiTexturePreloadQueue.size() - UiTexturePreloadHead)); }
}

FStatus FD3D9RenderDevice::RenderUiDesktop(const FResourceManager& resources, const FWorldScene* worldScene, const FUiRuntime& ui, const RECT& rect, float deltaSeconds, const FGameMovementInput& gameInput, float lookDeltaX, float lookDeltaY, bool jumpRequested, FLogger* logger)
{
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

            const FGameWorldPosition spawn = InitialGameWorldPosition.value_or(FGameWorldPosition{SferaProtocol::DefaultServerSpawnX, SferaProtocol::DefaultServerSpawnY, SferaProtocol::DefaultServerSpawnZ, SferaProtocol::DefaultServerSpawnAngle});
            if (GameWorldScene.Initialize(DeviceWindow, Device, resources, *worldScene, worldConfig, spawn.X, spawn.Y, spawn.Z, spawn.Angle, worldError, logger, playerModelPtr))
            {
                ActiveWorldScene = worldScene;
                FailedWorldScene = nullptr;
                for (const auto& [_, remote] : RemoteGamePlayers) { GameWorldScene.UpsertRemotePlayer(remote); }
                for (const auto& [_, remote] : RemoteGameActors) { GameWorldScene.UpsertRemoteActor(remote); }
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
            if (const FUiControlDef* fogControl = FindGameControl(ui, "gfx_options", 28))
            {
                const float fogAmount = std::clamp(ui.GameControlValue("gfx_options", *fogControl) / 100.0f, 0.0f, 1.0f);
                GameWorldScene.SetFog(110.0f - fogAmount * 70.0f, 260.0f - fogAmount * 140.0f);
            }
            if (const FUiControlDef* lodControl = FindGameControl(ui, "gfx_options", 43))
            {
                std::wstring settingError;
                const int quality = std::clamp(static_cast<int>(std::round(ui.GameControlValue("gfx_options", *lodControl))), 0, 2);
                if (!GameWorldScene.SetGrassQuality(quality, settingError) && logger && !settingError.empty()) { logger->Warning("D3D9 grass quality update failed: " + Common::WideToUtf8(settingError)); }
            }
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
            if (!GameWorldScene.Update(deltaSeconds, gameInput, worldUpdateError) && logger)
            {
                logger->Warning("D3D9 game world Update failed: " + Common::WideToUtf8(worldUpdateError));
            }
        }
    }

    const FUiRectF design = ui.Input().BuildDesignRect(rect);
    const float scale = design.W / static_cast<float>(std::max(1, ui.DesignWidth()));
    FDrawContext ctx
    {
        resources, ui, logger, scale
    };
    const auto& currentUiWindows = ui.GameWindows();
    const auto& currentUiVisibility = ui.GameWindowVisibility();
    while (UiQueuedWindowCount < currentUiWindows.size())
    {
        const size_t index = UiQueuedWindowCount++;
        const bool visible = index < currentUiVisibility.size() && currentUiVisibility[index];
        UiWindowWasVisible.push_back(visible);
        if (visible || ShouldPrewarmGameWindow(currentUiWindows[index])) { QueueWindowTextures(currentUiWindows[index], visible); }
    }
    if (UiWindowWasVisible.size() < currentUiWindows.size()) { UiWindowWasVisible.resize(currentUiWindows.size(), false); }
    for (size_t index = 0; index < currentUiWindows.size() && index < currentUiVisibility.size(); ++index)
    {
        const bool visible = currentUiVisibility[index];
        if (visible && !UiWindowWasVisible[index]) { QueueWindowTextures(currentUiWindows[index], true); }
        UiWindowWasVisible[index] = visible;
    }
    PumpUiTexturePreload(resources, logger, ui.Mode() == EUiRuntimeMode::Game ? 0.5 : 1.5, ui.Mode() == EUiRuntimeMode::Game ? 1 : 2);
    HRESULT hr = Device->BeginScene();

    if (FAILED(hr)) { return FStatus::Error(EStatusCode::RuntimeError, "D3D9 BeginScene failed: hr=" + std::to_string(static_cast<long>(hr))); }

    bool backgroundDrawn = true;

    if (ui.Mode() == EUiRuntimeMode::Game && worldScene && GameWorldScene.IsValid())
    {
        GameWorldScene.RenderInsideScene(rect);
    }
    else
    {
        Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        if (ui.Mode() == EUiRuntimeMode::CharacterSelect)
        {
            CharacterScene.Draw(Device, resources, ui.Character().SelectedCharacterSceneAppearance(), ui.Character().SceneAngle(), ui.Character().SceneCameraFocusId(), rect, deltaSeconds, logger);
        }
    }

    ConfigureUiRenderState();
    BeginUiBatch();

    if (ui.Mode() == EUiRuntimeMode::Login)
    {
        backgroundDrawn = DrawTextureResource(ctx, ui.LoginBackgroundTexture(), design);
        DrawWindow(ctx, ui.ConnectionWindow(), ui.Input().BuildConnectionRect(rect));
    }
    else if (ui.Mode() == EUiRuntimeMode::CharacterSelect)
    {
        if (!ui.PickPersonWindow().Name.empty())
        {
            DrawWindow(ctx, ui.PickPersonWindow(), ui.Input().BuildWindowRect(ui.PickPersonWindow(), rect));
        }
    }
    else
    {
        const auto& gameWindows = ui.GameWindows();
        const auto& gameVisibility = ui.GameWindowVisibility();
        for (size_t i : ui.GameWindowRenderOrder())
        {
            if (i >= gameWindows.size()) { continue; }
            if (i < gameVisibility.size() && !gameVisibility[i]) { continue; }
            DrawWindow(ctx, gameWindows[i], ui.BuildGameWindowRect(i, rect), 1.0f, static_cast<int32>(i));
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
    FlushUiBatch();
    UiBatchActive = false;
    {
        Device->EndScene();
    }
    hr = Device->Present(nullptr, nullptr, nullptr, nullptr);

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
