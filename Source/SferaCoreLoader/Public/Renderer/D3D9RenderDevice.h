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
    float GameWorldCameraFacing() const;
    void SetInitialGameWorldPosition(std::optional<FGameWorldPosition> position);
    void ApplyServerGameWorldPosition(const FGameWorldPosition& position);
    FStatus RenderUiDesktop(const FResourceManager& resources, const FWorldScene* worldScene, const FUiRuntime& ui, const RECT& rect, float deltaSeconds, const FGameMovementInput& gameInput, float lookDeltaX, float lookDeltaY, bool jumpRequested, FLogger* logger);
    void PreloadUiTextures(const FResourceManager& resources, const FUiRuntime& ui, FLogger* logger);
    bool HasPendingUiTexturePreload() const { return !UiTextureUrgentQueue.empty() || UiTexturePreloadHead < UiTexturePreloadQueue.size(); }
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
    bool DrawSpriteRotated(FDrawContext& ctx, const FUiWindowDef& window, std::string_view spriteName, const FUiRectF& dst, float degrees, float alpha = 1.0f);
    bool DrawWindow(FDrawContext& ctx, const FUiWindowDef& window, const FUiRectF& dst, float alpha = 1.0f, int32 gameWindowIndex = -1);
    void DrawControl(FDrawContext& ctx, const FUiWindowDef& window, const FUiControlDef& control, const FUiRectF& windowRect, float alpha = 1.0f, int32 gameWindowIndex = -1);
    struct FCachedEncodedText
    {
        std::string Text;
        int32 FontIndex = 0;
        std::vector<uint8> Bytes;
        int32 Width = 0;
    };
    struct FCachedWrappedText
    {
        std::string Text;
        int32 FontIndex = 0;
        int32 WidthQuarterPixels = 0;
        int32 ScaleMilli = 0;
        std::vector<std::string> Lines;
    };
    const FCachedEncodedText& GetEncodedText(FDrawContext& ctx, std::string_view text, int32 fontIndex);
    uint64 BuildTextCacheKey(std::string_view text, int32 fontIndex, int32 extraA = 0, int32 extraB = 0) const;

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
    const std::vector<std::string>& WrapTextLines(FDrawContext& ctx, std::string_view text, float width, int32 fontIndex);
    void DrawTextBlock(FDrawContext& ctx, const FUiRectF& rect, const std::string& text, unsigned long color, bool center, int32 fontIndex);
    void DrawStatusOverlay(FDrawContext& ctx, const FUiRuntime& ui, const FUiRectF& designRect);
    void DrawRenderStatsOverlay(FDrawContext& ctx, const RECT& clientRect, const FD3D9GameWorldRenderStats* worldStats);
    void UpdateFrameStats(double frameMilliseconds);
    void DrawSolidRect(float x, float y, float w, float h, unsigned long color, float alpha = 1.0f);
    void DrawTexturePiece(IDirect3DTexture9* texture, const FUiSpritePiece& piece, const FUiRectF& spriteRect, int32 textureWidth, int32 textureHeight, unsigned long color);
    void DrawTextureQuad(IDirect3DTexture9* texture, float x, float y, float w, float h, float u1, float v1, float u2, float v2, unsigned long color, bool premultiplied = false);
    void DrawTextureQuadRotated(IDirect3DTexture9* texture, float x, float y, float w, float h, float centerX, float centerY, float degrees, float u1, float v1, float u2, float v2, unsigned long color);
    struct FUiBatchVertex
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Rhw = 1.0f;
        unsigned long Color = 0xfffffffful;
        float U = 0.0f;
        float V = 0.0f;
    };
    void BeginUiBatch();
    void FlushUiBatch();
    void QueueUiQuad(IDirect3DTexture9* texture, const std::array<FUiBatchVertex, 4>& strip, bool premultiplied = false);
    void DrawTextureQuadUv(IDirect3DTexture9* texture, float x, float y, float w, float h, const FUiTexCoord* coords, int32 textureWidth, int32 textureHeight, unsigned long color);
    void QueueWindowTextures(const FUiWindowDef& window, bool highPriority = false);
    void PumpUiTexturePreload(const FResourceManager& resources, FLogger* logger, double budgetMilliseconds, size_t maxTextures);
    void ReleaseTextures();
    IDirect3D9* D3D = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HINSTANCE D3DXModule = nullptr;
    FD3DXCreateTextureFromFileInMemoryExPtr D3DXCreateTextureFromFileInMemoryExFn = nullptr;
    std::unordered_map<std::string, FD3D9TextureEntry> TextureCache;
    std::vector<FUiBatchVertex> UiBatchVertices;
    IDirect3DTexture9* UiBatchTexture = nullptr;
    bool UiBatchPremultiplied = false;
    bool UiBatchActive = false;
    std::vector<std::string> UiTexturePreloadQueue;
    std::vector<std::string> UiTextureUrgentQueue;
    std::unordered_set<std::string> UiTexturePreloadKnown;
    std::unordered_set<std::string> UiTextureUrgentKnown;
    size_t UiTexturePreloadHead = 0;
    size_t UiQueuedWindowCount = 0;
    std::vector<bool> UiWindowWasVisible;
    std::unordered_map<uint64, FCachedEncodedText> EncodedTextCache;
    std::unordered_map<uint64, FCachedWrappedText> WrappedTextCache;
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
