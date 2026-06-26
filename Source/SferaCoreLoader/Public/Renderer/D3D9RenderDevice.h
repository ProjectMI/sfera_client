#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "Renderer/D3D9BitmapFont.h"
#include "Renderer/D3D9CharacterScene.h"
#include "Renderer/D3D9GameWorldScene.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiRuntime.h"

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct _D3DPRESENT_PARAMETERS_;
struct FDdsImage;

struct FD3D9ShaderInventory 
{
    size_t VertexShaders = 0;
    size_t PixelShaders = 0;
    std::vector<std::string> Samples;
};

struct FD3D9TextureEntry 
{
    IDirect3DTexture9* Texture = nullptr;
    int32 Width = 0;
    int32 Height = 0;
    bool Tried = false;
    std::string ResourceName;
    std::string Error;
};

class FD3D9RenderDevice 
{
public:
    FD3D9RenderDevice();
    ~FD3D9RenderDevice();
    FStatus Initialize(HWND hwnd, int32 width, int32 height, FLogger* logger);
    void Shutdown();
    bool IsInitialized() const { return Device != nullptr; }
    FD3D9ShaderInventory InspectShaderResources(const FResourceManager& resources, FLogger* logger) const;
    void SetServerGameTime(float dayFraction);
    void SetInitialGameWorldPosition(std::optional<FGameWorldPosition> position);
    void ApplyServerGameWorldPosition(const FGameWorldPosition& position);
    FStatus RenderUiDesktop(const FResourceManager& resources, const FWorldScene* worldScene, const FUiRuntime& ui, const RECT& rect, float deltaSeconds, const FGameMovementInput& gameInput, float lookDeltaX, float lookDeltaY, bool jumpRequested, FLogger* logger);
    void PreloadUiTextures(const FResourceManager& resources, const FUiRuntime& ui, FLogger* logger);
private:
    struct FDrawContext;
    using FD3DXCreateTextureFromFileInMemoryExPtr = long (__stdcall *)(IDirect3DDevice9*, const void*, unsigned int, unsigned int, unsigned int, unsigned int, unsigned long, int, int, unsigned long, unsigned long, unsigned long, void*, void*, IDirect3DTexture9**);
    FStatus EnsureD3DX(FLogger* logger);
    bool EnsureDeviceReady(int32 width, int32 height, FLogger* logger);
    void ConfigureUiRenderState();
    FD3D9TextureEntry* LoadTextureByName(const FResourceManager& resources, std::string_view textureName, FLogger* logger);
    IDirect3DTexture9* CreateTextureFromDdsImage(const FDdsImage& image, FLogger* logger);
    std::string ResolveTextureResourceName(const FResourceManager& resources, std::string_view textureName) const;
    bool DrawTextureResource(FDrawContext& ctx, std::string_view textureName, const FUiRectF& dst, float alpha = 1.0f);
    bool DrawSprite(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, float alpha = 1.0f);
    bool DrawSpriteTinted(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, unsigned long color);
    bool DrawWindow(FDrawContext& ctx, const FUiWindowDef& window, const FUiRectF& dst, float alpha = 1.0f);
    void DrawControl(FDrawContext& ctx, const FUiWindowDef& window, const FUiControlDef& control, const FUiRectF& windowRect, float alpha = 1.0f);
    struct FFrameStats
    {
        bool Initialized = false;
        uint64 FrameCounter = 0;
        double LastMilliseconds = 0.0;
        double AverageMilliseconds = 0.0;
        double MinMilliseconds = 0.0;
        double MaxMilliseconds = 0.0;
        double P95Milliseconds = 0.0;
        double LowFps = 0.0;
        double SecondAccumulator = 0.0;
        uint32 CurrentFps = 0;
        uint32 SecondFrames = 0;
        uint32 DropFrames = 0;
        uint32 HitchFrames = 0;
        std::array<double, 120> History{};
        size_t HistoryHead = 0;
        size_t HistoryCount = 0;
    };
    void DrawModalDialog(FDrawContext& ctx, const RECT& rect);
    void DrawTextRect(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex);
    void DrawStatusOverlay(FDrawContext& ctx, const FUiRuntime& ui, const FUiRectF& designRect);
    void DrawRenderStatsOverlay(FDrawContext& ctx, const RECT& clientRect, const FD3D9GameWorldRenderStats* worldStats);
    void UpdateFrameStats(double frameMilliseconds);
    void DrawSolidRect(float x, float y, float w, float h, unsigned long color, float alpha = 1.0f);
    void DrawTexturePiece(IDirect3DTexture9* texture, const FUiSpritePiece& piece, const FUiRectF& spriteRect, int32 textureWidth, int32 textureHeight, unsigned long color);
    void DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color);
    void DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTexCoord* coords, int32 textureWidth, int32 textureHeight, unsigned long color);
    void ReleaseTextures();
    IDirect3D9* D3D = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HINSTANCE D3DXModule = nullptr;
    FD3DXCreateTextureFromFileInMemoryExPtr D3DXCreateTextureFromFileInMemoryExFn = nullptr;
    std::unordered_map<std::string, FD3D9TextureEntry> TextureCache;
    FD3D9BitmapFontCatalog FontCache;
    FFrameStats Stats;
    FD3D9CharacterScene CharacterScene;
    FD3D9GameWorldScene GameWorldScene;
    const FWorldScene* ActiveWorldScene = nullptr;
    const FWorldScene* FailedWorldScene = nullptr;
    float ServerGameTime = 0.0f;
    bool HasServerGameTime = false;
    bool ServerGameTimePending = false;
    std::optional<FGameWorldPosition> InitialGameWorldPosition;
    int32 BackBufferWidth = 0;
    int32 BackBufferHeight = 0;
    HWND DeviceWindow = nullptr;
    bool ReportedD3DXMissing = false;
};
