#pragma once
#include "Core/Types.h"
#include "Renderer/GameWorld/GameWorldConfig.h"
#include "Renderer/GameWorld/SkinnedCharacterModel.h"
#include "ResourceLoader/ResourceManager.h"
#include "WorldScene/WorldScene.h"

struct IDirect3DDevice9;
class FLogger;

struct FD3D9GameWorldRenderStats
{
    uint64 FrameId = 0;
    uint32 DrawCalls = 0;
    uint64 Triangles = 0;
    uint32 SkyDrawCalls = 0;
    uint32 TerrainDrawCalls = 0;
    uint32 StaticDrawCalls = 0;
    uint32 GrassDrawCalls = 0;
    uint32 PlayerDrawCalls = 0;
    uint32 WaterDrawCalls = 0;
    uint32 OverlayDrawCalls = 0;
    uint32 ReflectionDrawCalls = 0;
    uint64 ReflectionTriangles = 0;
    size_t TerrainResources = 0;
    size_t TerrainInstances = 0;
    size_t StaticResources = 0;
    size_t StaticInstances = 0;
    size_t GrassInstances = 0;
    size_t GrassMaps = 0;
    size_t GrassCells = 0;
};

class FD3D9GameWorldScene
{
public:
    FD3D9GameWorldScene();
    ~FD3D9GameWorldScene();
    FD3D9GameWorldScene(const FD3D9GameWorldScene&) = delete;
    FD3D9GameWorldScene& operator=(const FD3D9GameWorldScene&) = delete;

    bool Initialize(
        HWND hwnd,
        IDirect3DDevice9* device,
        const FResourceManager& terrainResources,
        const FWorldScene& world,
        const FGameWorldConfig& config,
        double spawnX,
        double spawnY,
        double spawnZ,
        double spawnAngle,
        std::wstring& error,
        FLogger* logger = nullptr,
        const FSkinnedCharacterModel* playerModel = nullptr);

    void Shutdown();
    bool SetOverlayBitmap(int width, int height, std::vector<uint8> bgraPixels, std::wstring& error);
    bool SetGrassQuality(int quality, std::wstring& error);
    void SetFog(float start, float end);
    void SetGameTime(float dayFraction);
    float CurrentGameTime() const;
    float CameraFacing() const;
    bool Update(float deltaSeconds, const FGameMovementInput& input, std::wstring& error);
    void RotateView(float mouseDx, float mouseDy);
    void Jump();
    FGameWorldPosition Position() const;
    void Resize();
    void RenderInsideScene(const RECT& viewport);
    FD3D9GameWorldRenderStats RenderStats() const;
    bool IsValid() const;

    static FGameWorldConfig DefaultConfig();
    static void PrewarmGrassModelCpuCache(const FResourceManager& resources, const FGameWorldConfig& config, FLogger* logger = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> ImplPtr;
};
