#pragma once
#include "Renderer/GameWorld/GameWorldConfig.h"
#include "Renderer/GameWorld/SkinnedCharacterModel.h"
#include "Renderer/D3D9GameWorldScene.h"
#include "Renderer/GameWorld/GameWorldTypes.h"
#include "Renderer/GameWorld/GameWorldSupport.h"

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
    IDirect3DVertexShader9* GrassVS = nullptr;
    IDirect3DPixelShader9* GrassPS = nullptr;
    std::unordered_map<std::string, int> BaseVSConsts;
    bool WorldShadersReady = false;
    D3DMATRIX ViewMatrix{};
    D3DMATRIX ProjectionMatrix{};
    D3DMATRIX ViewProjectionMatrix{};
    D3DPRESENT_PARAMETERS Present{};
    const FResourceManager* AssetResources = nullptr;
    const FWorldScene* WorldScene = nullptr;
    FLogger* Logger = nullptr;
    FGameWorldConfig Config;
    struct TerrainCpuPreloadJob
    {
        std::vector<std::wstring> Keys;
        std::future<void> Future;
    };

    struct StaticModelCpuPreloadTarget
    {
        std::string ModelName;
        std::filesystem::path ModelPath;
        std::string Key;
    };

    struct StaticModelCpuPreloadJob
    {
        std::vector<std::string> Keys;
        std::future<void> Future;
    };

    std::unordered_map<std::wstring, std::unique_ptr<TerrainResource>> TerrainResources;
    std::vector<TerrainCpuPreloadJob> TerrainCpuPreloadJobs;
    std::vector<std::filesystem::path> PendingTerrainCpuPreloads;
    std::unordered_set<std::wstring> QueuedTerrainCpuPreloads;
    std::thread TerrainCpuPreloadThread;
    std::mutex TerrainCpuPreloadMutex;
    std::condition_variable TerrainCpuPreloadCv;
    bool TerrainCpuPreloadStop = false;
    bool TerrainCpuPreloadWorkerStarted = false;
    std::vector<TerrainInstance> TerrainInstances;
    std::unordered_map<uint64, TerrainInstance> TerrainInstanceLookup;
    std::unordered_map<std::string, std::unique_ptr<StaticModelResource>> StaticResources;
    std::vector<StaticModelCpuPreloadJob> StaticModelCpuPreloadJobs;
    std::vector<StaticModelCpuPreloadTarget> PendingStaticModelCpuPreloads;
    std::unordered_set<std::string> QueuedStaticModelCpuPreloads;
    std::thread StaticModelCpuPreloadThread;
    std::mutex StaticModelCpuPreloadMutex;
    std::condition_variable StaticModelCpuPreloadCv;
    bool StaticModelCpuPreloadStop = false;
    bool StaticModelCpuPreloadWorkerStarted = false;
    std::vector<StaticPlacementModel> StaticPlacementModels;
    std::vector<StaticPlacement> StaticPlacements;
    std::vector<StaticInstance> StaticInstances;
    std::vector<ModelCollisionProxy> ModelCollisionProxies;
    std::unordered_map<uint64, std::vector<std::size_t>> ModelCollisionProxyCells;
    uint64 ModelCollisionSourceGeneration = 0;
    std::thread CollisionWorkerThread;
    std::mutex CollisionWorkerMutex;
    std::condition_variable CollisionWorkerCv;
    bool CollisionWorkerStop = false;
    bool CollisionWorkerStarted = false;
    bool CollisionWorkerRequestPending = false;
    bool CollisionWorkerRebuildSource = false;
    uint64 CollisionWorkerPendingGeneration = 0;
    float CollisionWorkerPendingFocusX = 0.0f;
    float CollisionWorkerPendingFocusZ = 0.0f;
    std::vector<ModelCollisionWorkerInstance> CollisionWorkerPendingInstances;
    mutable std::mutex ActiveCollisionSnapshotMutex;
    std::shared_ptr<const PreparedModelCollisionSnapshot> ActiveCollisionSnapshot;
    std::vector<std::size_t> VisibleStaticPlacementIndices;
    std::vector<uint64> VisibleStaticRenderCells;
    std::unordered_map<uint64, std::vector<std::size_t>> StaticPlacementIndicesByRenderCell;
    std::unordered_map<uint64, std::vector<WorldRenderBatch>> StaticCellRenderBatches;
    bool StaticVisibilityPlanReady = false;
    float StaticVisibilityAnchorX = 0.0f;
    float StaticVisibilityAnchorZ = 0.0f;
    std::vector<GrassInstance> GrassInstances;
    std::unordered_map<uint64, std::vector<WorldRenderBatch>> GrassCellRenderBatches;
    std::unordered_map<int, std::vector<uint8>> GrassMaps;
    std::unordered_set<uint64> GrassCells;
    bool GrassRefreshIncomplete = false;
    bool GrassInitialBlockingLoad = false;
    std::vector<FSceneBatch> PlayerBatches;
    UINT PlayerVertexCount = 0;
    FSkinnedCharacterModel PlayerModel;
    std::vector<float> PlayerSkinScratch;
    std::vector<WorldVertex> PlayerVertexScratch;
    std::size_t PlayerAction = kPlayerIdleAction;
    std::size_t PlayerLastSkinnedFrame = (std::numeric_limits<std::size_t>::max)();
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
    int StreamingGuardRow = (std::numeric_limits<int>::min)();
    int StreamingGuardColumn = (std::numeric_limits<int>::min)();
    int StreamingGuardRowStep = (std::numeric_limits<int>::min)();
    int StreamingGuardColumnStep = (std::numeric_limits<int>::min)();
    std::chrono::steady_clock::time_point NextStreamingGuardTime{};
    std::unordered_set<uint64> QueuedTerrainCpuPreloadCells;
    int GrassCenterX = (std::numeric_limits<int>::min)();
    int GrassCenterZ = (std::numeric_limits<int>::min)();
    float GrassAnchorX = 0.0f;
    float GrassAnchorZ = 0.0f;
    float ElapsedSeconds = 0.0f;
    float GameTimeFraction = 0.0f;
    FGameWorldSkyState Environment{0.0f, 0, 0, 0, 110, 110, 110, 255, 245, 224, 200, 200, 200};
    bool GrassAnchorValid = false;
    int OverlayWidth = 0;
    int OverlayHeight = 0;
    bool Initialized = false;
    mutable std::unordered_map<std::string, std::filesystem::path> OptionalPathCache;
    mutable std::unordered_map<std::string, std::filesystem::path> TerrainLndPathByRelativeKey;
    mutable std::unordered_map<std::string, std::filesystem::path> TerrainLndPathByStemKey;
    mutable bool TerrainPathIndexReady = false;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> TerrainStemPathCache;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> TerrainRelativePathCache;
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
    void CreateReflectionTarget();
    std::filesystem::path ResolveOptionalPath(std::string LogicalName) const;
    std::filesystem::path ResolveConfiguredPath(const std::string& LogicalName) const;
    void BuildTerrainPathIndex() const;
    std::optional<std::filesystem::path> TryResolveTerrainRelativePath(const std::filesystem::path& RelativePath) const;
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
    StaticModelResource* EnsureStaticModelResource(const std::string& ModelName);
    std::unique_ptr<StaticModelResource> LoadStaticModelResource(
        const std::string& ModelName,
        const std::filesystem::path& ModelPath);
    void StartStaticModelCpuPreloadWorker();
    void StopStaticModelCpuPreloadWorker();
    void StaticModelCpuPreloadWorkerMain();
    void QueueStaticModelCpuPreload(const std::string& ModelName, const std::filesystem::path& ModelPath);
    void DrainStaticModelCpuPreloadJobs(bool Wait);
    const std::vector<uint8>& LoadGrassMap(int ChunkX, int ChunkZ);
    uint8 GrassTypeAt(float WorldX, float WorldZ);
    void LoadVisibleStaticObjects();
    void UpdateNpcAnimation(float DeltaSeconds);
    void ClearStaticRenderBatches();
    void BakeStaticRenderCell(uint64 CellKey);
    void BuildVisibleStaticRenderBatches();
    bool BeginAlphaWorldPass(const D3DMATRIX& World);
    void EndAlphaWorldPass(bool UsedShader);
    void DrawWorldRenderBatches(std::vector<const WorldRenderBatch*>& DrawList, EGameWorldDrawBucket Bucket, float CullingMargin);
    void PreloadStaticResourcesAround(float CenterX, float CenterZ, float Radius);
    void LoadVisibleGrass();
    void ClearGrassRenderBatches();
    void BakeGrassCell(uint64 CellKey, const std::vector<GrassInstance>& Instances);
    std::unique_ptr<TerrainResource> LoadTerrainResource(const std::filesystem::path& LNDPath);
    void StartTerrainCpuPreloadWorker();
    void StopTerrainCpuPreloadWorker();
    void TerrainCpuPreloadWorkerMain();
    void QueueTerrainCpuPreload(const std::filesystem::path& LNDPath);
    void DrainTerrainCpuPreloadJobs(bool Wait);
    void LoadVisibleTerrain();
    void PreloadTerrainForCenter(int CenterRow, int CenterColumn, int Radius);
    void PreloadStreamingGuard();
    void UploadWaterMesh(TerrainResource& resource, const std::vector<uint16>& indices);
    void LoadWorldShaders();
    void SetVsConst(const char* name, const float* data, int Vec4Count);
    void SetBaseLightConstants();
    void ComputeWindCircles(float Out[12]) const;
    DWORD TerrainColorAt(float WorldX, float WorldZ) const;
    bool IsBoundsVisibleToCamera(const FBox3& Bounds, float ExtraMargin = 0.0f) const;
    void SetBaseWorld(const D3DMATRIX& world);
    void BeginBaseShader();
    void EndBaseShader();
    void ConfigureRenderState();
    bool TerrainHeightAt(float WorldX, float WorldZ, float ReferenceY, float& OutHeight) const;
    bool TerrainSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const;
    bool FlatGrassSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const;
    void SnapToGround();
    void RebuildModelCollisionProxies();
    void StartCollisionWorker();
    void StopCollisionWorker();
    void CollisionWorkerMain();
    void QueueCollisionWorkerRebuild();
    void RequestCollisionSnapshotAround(float CenterX, float CenterZ);
    std::shared_ptr<const PreparedModelCollisionSnapshot> GetActiveCollisionSnapshot(FBox2 area) const;
    std::vector<std::size_t> QueryModelCollisionProxies(FBox2 area) const;
    bool CollidesWithModelContacts(float fromX, float fromZ, float x, float y, float z) const;
    bool CollidesWithModelContactsSwept(float fromX, float fromY, float fromZ, float toX, float toY, float toZ, bool includeWalkableSurfaces) const;
    bool CollidesWithStatic(float x, float y, float z) const;
    bool HasContourCollision() const;
    bool CollidesWithContours(float fromX, float fromZ, float toX, float toZ, float radius) const;
    static float PointSegmentDistanceSquared2D(float px, float pz, FVector2 a, FVector2 b);
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
    bool TryMoveTo(float x, float z, float fromY, float toY);
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
    void SetPlayerWorldPosition(const FGameWorldPosition& Position);
    bool Update(float DeltaSeconds, const FGameMovementInput& input, std::wstring& error);
    void RotateView(float MouseDx, float MouseDy);
    void Resize();
    void RenderInsideScene(const RECT&);
};
