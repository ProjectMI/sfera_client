#pragma once
#include "Renderer/GameWorld/GameWorldConfig.h"
#include "Renderer/GameWorld/SkinnedCharacterModel.h"
#include "Renderer/D3D9GameWorldScene.h"
#include "Renderer/GameWorld/GameWorldTypes.h"
#include "Renderer/GameWorld/GameWorldSupport.h"
#include <atomic>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

enum class EGameWorldDrawBucket
{
    Sky,
    Terrain,
    StaticObjects,
    Grass,
    Player,
    Water,
    Overlay
};

struct FD3D9GameWorldScene::Impl
{
    HWND Hwnd = nullptr;
    IDirect3D9* D3D = nullptr;
    IDirect3DDevice9* Device = nullptr;
    IDirect3DTexture9* OverlayTexture = nullptr;
    IDirect3DTexture9* TerrainMicrotexture = nullptr;
    IDirect3DTexture9* SkyTexture = nullptr;
    IDirect3DTexture9* WaterTexture = nullptr;
    static constexpr UINT kReflectionSize = 256;
    IDirect3DTexture9* ReflectionTexture = nullptr;
    IDirect3DSurface9* ReflectionSurface = nullptr;
    IDirect3DSurface9* ReflectionDepth = nullptr;
    bool RenderingReflection = false;
    bool ReflectionTextureReady = false;
    int ReflectionWarmupFrames = 0;
    int ReflectionUpdateCountdown = 0;
    IDirect3DVertexBuffer9* PlayerVertexBuffer = nullptr;
    IDirect3DIndexBuffer9* PlayerIndexBuffer = nullptr;
    IDirect3DVertexDeclaration9* WorldDecl = nullptr;
    IDirect3DVertexShader9* BaseVS = nullptr;
    IDirect3DPixelShader9* BasePS = nullptr;
    IDirect3DPixelShader9* DebugPS = nullptr;
    std::unordered_map<std::string, int> BaseVSConsts;
    bool WorldShadersReady = false;
    D3DMATRIX ViewMatrix{};
    D3DMATRIX ProjectionMatrix{};
    D3DMATRIX ViewProjectionMatrix{};
    float ViewAspect = 1.333333f;
    float FrustumTanHalfX = 1.0f;
    float FrustumTanHalfY = 1.0f;
    D3DPRESENT_PARAMETERS Present{};
    const FResourceManager* AssetResources = nullptr;
    const FWorldScene* WorldScene = nullptr;
    FLogger* Logger = nullptr;
    FGameWorldConfig Config;
    std::unordered_map<std::wstring, std::unique_ptr<TerrainResource>> TerrainResources;
    std::vector<TerrainInstance> TerrainInstances;
    std::unordered_map<uint64, TerrainInstance> TerrainInstanceLookup;
    std::unordered_map<std::string, std::unique_ptr<StaticModelResource>> StaticResources;
    std::vector<StaticPlacementModel> StaticPlacementModels;
    std::vector<StaticPlacement> StaticPlacements;
    std::vector<StaticInstance> StaticInstances;
    std::vector<std::size_t> VisibleStaticPlacementIndices;
    std::vector<uint32> PendingStaticModelLoads;
    std::thread StaticPlacementWorker;
    std::atomic_bool StaticPlacementWorkerReady{false};
    std::mutex StaticPlacementWorkerMutex;
    std::unique_ptr<StaticPlacementLoadResult> StaticPlacementWorkerResult;
    std::exception_ptr StaticPlacementWorkerException;
    bool StaticPlacementWorkerStarted = false;
    bool StaticVisibilityPlanReady = false;
    float StaticVisibilityAnchorX = 0.0f;
    float StaticVisibilityAnchorZ = 0.0f;
    std::vector<GrassInstance> GrassInstances;
    std::vector<WorldVertex> SkyVertices;
    std::vector<uint16> SkyIndices;
    std::unordered_map<int, std::vector<uint8>> GrassMaps;
    std::unordered_set<uint64> GrassCells;
    std::vector<FSceneBatch> PlayerBatches;
    UINT PlayerVertexCount = 0;
    FSkinnedCharacterModel PlayerModel;
    std::vector<float> PlayerSkinScratch;
    std::vector<WorldVertex> PlayerVertexScratch;
    std::size_t PlayerAction = kPlayerIdleAction;
    float PlayerAnimTime = 0.0f;
    int PlayerHeadBone = -1;
    bool PlayerEyeValid = false;
    bool PlayerEyeInitialized = false;
    bool PlayerWalking = false;
    float PlayerLiveCrownY = 0.0f;
    float PlayerLockedCrownY = 0.0f;
    float PlayerBodyShift = 0.0f;
    float PlayerEyeLocalX = 0.0f;
    float PlayerEyeLocalY = 0.0f;
    float PlayerEyeLocalZ = 0.0f;
    float SpawnX = 0.0f;
    float SpawnY = 0.0f;
    float VelocityY = 0.0f;
    float VelocityX = 0.0f;
    float VelocityZ = 0.0f;
    bool Grounded = true;
    float SpawnZ = 0.0f;
    float SpawnAngle = 0.0f;
    float CameraYaw = 0.0f;
    float CameraPitch = 0.0f;
    FVector3 CameraEye{};
    FVector3 CameraTarget{};
    int TerrainCenterRow = -1;
    int TerrainCenterColumn = -1;
    int GrassCenterX = (std::numeric_limits<int>::min)();
    int GrassCenterZ = (std::numeric_limits<int>::min)();
    float GrassAnchorX = 0.0f;
    float GrassAnchorZ = 0.0f;
    float ElapsedSeconds = 0.0f;
    float GameTimeFraction = 0.0f;
    int EnvironmentClearRed = 0;
    int EnvironmentClearGreen = 0;
    int EnvironmentClearBlue = 0;
    int EnvironmentAmbientRed = 110;
    int EnvironmentAmbientGreen = 110;
    int EnvironmentAmbientBlue = 110;
    int EnvironmentSunRed = 255;
    int EnvironmentSunGreen = 245;
    int EnvironmentSunBlue = 224;
    int EnvironmentCloudRed = 200;
    int EnvironmentCloudGreen = 200;
    int EnvironmentCloudBlue = 200;
    bool GrassAnchorValid = false;
    int OverlayWidth = 0;
    int OverlayHeight = 0;
    bool Initialized = false;
    bool TerrainStreamingPending = false;
    bool DeferredGrassLoadPending = false;
    bool DeferredStaticPlacementsPending = false;
    bool DeferredStaticInstancesPending = false;
    bool DeferredReflectionTargetPending = false;
    bool WorldEntryLoadPending = false;
    int WorldEntryLoadStage = 0;
    FSkinnedCharacterModel PendingPlayerModel;
    std::deque<std::wstring> TerrainGpuUploadQueue;
    mutable std::unordered_map<std::string, std::filesystem::path> OptionalPathCache;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> TerrainStemPathCache;
    mutable std::unordered_map<std::string, std::filesystem::path> ModelPathCache;
    mutable std::unordered_map<std::string, std::filesystem::path> ModelPathIndex;
    mutable bool ModelPathIndexReady = false;
    mutable std::unordered_map<std::string, std::filesystem::path> ModelTexturePathCache;
    std::unordered_map<std::wstring, IDirect3DTexture9*> DdsTextureCache;
    FD3D9GameWorldRenderStats LastRenderStats;
    uint64 RenderStatsFrameCounter = 0;

    ~Impl();
    void Release();
    void SkinPlayerFrame();
    void LoadPlayerModel(const FSkinnedCharacterModel& model);
    void UpdatePlayerAnimation(float DeltaSeconds, bool moving, bool running);
    RECT ClientRect() const;
    void FillPresentParameters();
    bool CreateDevice(std::wstring& error);
    void CreateReflectionTarget();
    std::filesystem::path ResolveOptionalPath(std::string LogicalName) const;
    std::filesystem::path ResolveOptionalPath(const std::wstring& LogicalName) const;
    std::filesystem::path ResolveConfiguredPath(const std::wstring& LogicalName) const;
    std::filesystem::path ResolveConfiguredPath(const std::string& LogicalName) const;
    std::optional<std::filesystem::path> TryResolveTerrainStemPath(const std::string& TerrainStem) const;
    std::optional<std::filesystem::path> TryResolveTerrainPathFromPatch(const FWorldPatchRecord& PatchRecord) const;
    std::filesystem::path ResolveTerrainPath(const FWorldMapCell& cell) const;
    void BuildModelPathIndex() const;
    std::filesystem::path ResolveModelPath(const std::string& ModelName) const;
    std::filesystem::path ResolveModelTexturePath(
        const std::filesystem::path& ModelPath,
        const std::string& MaterialName) const;
    IDirect3DTexture9* LoadCachedDdsTexture(const std::filesystem::path& Path);
    void LoadStaticPlacements();
    void BeginStaticPlacementLoadAsync();
    bool PollStaticPlacementLoad();
    void JoinStaticPlacementWorker();
    void ApplyStaticPlacementLoadResult(StaticPlacementLoadResult&& result);
    StaticModelResource* EnsureStaticModelResource(const std::string& ModelName);
    std::unique_ptr<StaticModelResource> LoadStaticModelResource(
        const std::string& ModelName,
        const std::filesystem::path& ModelPath);
    bool LoadVisibleStaticObjects(int MaxNewModels = (std::numeric_limits<int>::max)());
    const std::vector<uint8>& LoadGrassMap(int ChunkX, int ChunkZ);
    uint8 GrassTypeAt(float WorldX, float WorldZ);
    bool LoadVisibleGrass(int MaxNewCells = (std::numeric_limits<int>::max)());
    std::unique_ptr<TerrainResource> LoadTerrainResource(const std::filesystem::path& LNDPath);
    void UploadWaterMesh(TerrainResource& resource);
    bool UploadTerrainResourceStep(TerrainResource& resource);
    bool PumpTerrainGpuUploads(int MaxSteps);
    bool PumpWorldEntryLoad(std::wstring& error);
    bool LoadVisibleTerrain(int MaxNewResources = (std::numeric_limits<int>::max)());
    void LoadWorldShaders();
    void SetVsConst(const char* name, const float* data, int Vec4Count);
    void SetBaseLightConstants();
    bool IsBoundsVisibleToCamera(const FBox3& Bounds, float ExtraMargin = 0.0f) const;
    bool IsPointVisibleToCamera(float x, float y, float z, float Radius) const;
    void SetBaseWorld(const D3DMATRIX& world);
    void BeginBaseShader();
    void EndBaseShader();
    void ConfigureRenderState();
    bool TerrainHeightAt(float WorldX, float WorldZ, float ReferenceY, float& OutHeight) const;
    bool TerrainSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const;
    bool FlatGrassSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const;
    void SnapToGround();
    bool CollidesWithStatic(float x, float y, float z) const;
    static bool PointInTriangleXz(float px, float pz, const FVector3& a, const FVector3& b, const FVector3& c);
    bool StaticFloorHeightAt(
        float x,
        float z,
        float MinY,
        float MaxY,
        float& OutY,
        FVector3* OutNormal = nullptr) const;
    bool SupportHeightAt(float x, float z, float FeetY, float& OutY, FVector3* OutNormal = nullptr) const;
    bool TryMoveTo(float x, float z);
    void Jump();
    void ApplySlopeSlide(float DeltaSeconds);
    void UpdateVertical(float DeltaSeconds);
    void UpdateViewProjection();
    void DrawTerrain();
    void DrawStaticObjects();
    void DrawGrass();
    void DrawSky();
    bool WaterPlane(float& OutY) const;
    float WaterReflectCoeff() const;
    void RenderReflection();
    void ResetRenderStats();
    void RecordWorldDraw(uint32 triangles, EGameWorldDrawBucket bucket);
    FD3D9GameWorldRenderStats RenderStats() const;
    void UpdateWaterWaves(TerrainResource* resource, float OriginX, float OriginZ);
    void DrawWater();
    void DrawPlayer();
    void DrawOverlay();
    bool Initialize(
        HWND window,
        IDirect3DDevice9* ExternalDevice,
        const FResourceManager& ResourceManager,
        const FWorldScene& world,
        const FGameWorldConfig& WorldConfig,
        double x,
        double y,
        double z,
        double Angle,
        std::wstring& error,
        FLogger* InLogger,
        const FSkinnedCharacterModel* PlayerModelIn);
    bool SetOverlayBitmap(int width, int height, std::vector<uint8> pixels, std::wstring& error);
    void SetFog(float start, float end);
    bool SetGrassQuality(int quality, std::wstring& error);
    void SetGameTime(float DayFraction);
    bool Update(float DeltaSeconds, const FGameMovementInput& input, std::wstring& error);
    void RotateView(float MouseDx, float MouseDy);
    FGameWorldPosition Position() const;
    void Resize();
    void RenderInsideScene(const RECT&);
};
