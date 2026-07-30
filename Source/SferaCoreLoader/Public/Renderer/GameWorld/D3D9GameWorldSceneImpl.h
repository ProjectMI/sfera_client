#pragma once
#include <deque>
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
    Weather,
    Overlay
};

struct FD3D9GameWorldScene::Impl
{
    HWND Hwnd = nullptr;
    IDirect3DDevice9* Device = nullptr;
    IDirect3DTexture9* OverlayTexture = nullptr;
    IDirect3DTexture9* TerrainMicrotexture = nullptr;
    IDirect3DTexture9* SkyTexture = nullptr;
    IDirect3DTexture9* WeatherSkyCurrent1 = nullptr;
    IDirect3DTexture9* WeatherSkyCurrent2 = nullptr;
    IDirect3DTexture9* WeatherSkyNext1 = nullptr;
    IDirect3DTexture9* WeatherSkyNext2 = nullptr;
    IDirect3DTexture9* RainTexture = nullptr;
    IDirect3DTexture9* WaterTexture = nullptr;
    static constexpr UINT kReflectionSize = 256;
    IDirect3DTexture9* ReflectionTexture = nullptr;
    IDirect3DSurface9* ReflectionSurface = nullptr;
    IDirect3DSurface9* ReflectionDepth = nullptr;
    bool RenderingReflection = false;
    bool ReflectionTextureReady = false;
    int ReflectionWarmupFrames = 0;
    IDirect3DVertexBuffer9* PlayerVertexBuffer = nullptr;
    IDirect3DIndexBuffer9* PlayerIndexBuffer = nullptr;
    IDirect3DVertexDeclaration9* WorldDecl = nullptr;
    IDirect3DVertexDeclaration9* AnimatedWorldDecl = nullptr;
    IDirect3DVertexShader9* BaseVS = nullptr;
    IDirect3DPixelShader9* BasePS = nullptr;
    IDirect3DVertexShader9* GrassVS = nullptr;
    IDirect3DPixelShader9* GrassPS = nullptr;
    std::unordered_map<std::string, int> BaseVSConsts;
    int BaseVsWorldViewProjection = -1;
    int BaseVsDirLightToLightDirL = -1;
    int BaseVsDirLightColor = -1;
    int BaseVsAmbientColor = -1;
    bool WorldShadersReady = false;
    D3DMATRIX ViewMatrix{};
    D3DMATRIX ProjectionMatrix{};
    D3DMATRIX ViewProjectionMatrix{};
    std::array<std::array<float, 4>, 6> ViewFrustumPlanes{};
    bool ViewFrustumReady = false;
    FVector3 CullingEye{};
    float CullingViewportHeight = 1.0f;
    float ReflectionPlaneY = 0.0f;
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
        bool HighPriority = false;
    };

    struct StaticModelCpuPreloadJob
    {
        std::vector<std::string> Keys;
        std::future<void> Future;
    };

    std::unordered_map<std::wstring, std::unique_ptr<TerrainResource>> TerrainResources;
    struct FTerrainCpuPreloadResult
    {
        std::filesystem::path Path;
        std::unique_ptr<TerrainResource> Resource;
    };
    std::vector<TerrainCpuPreloadJob> TerrainCpuPreloadJobs;
    std::vector<std::filesystem::path> PendingTerrainCpuPreloads;
    std::vector<FTerrainCpuPreloadResult> CompletedTerrainCpuPreloads;
    std::unordered_set<std::wstring> QueuedTerrainCpuPreloads;
    std::deque<std::filesystem::path> PendingTerrainGpuPromotions;
    std::unordered_set<std::wstring> QueuedTerrainGpuPromotions;
    std::thread TerrainCpuPreloadThread;
    std::mutex TerrainCpuPreloadMutex;
    std::condition_variable TerrainCpuPreloadCv;
    bool TerrainCpuPreloadStop = false;
    bool TerrainCpuPreloadWorkerStarted = false;
    std::vector<TerrainInstance> TerrainInstances;
    std::unordered_map<uint64, TerrainInstance> TerrainInstanceLookup;
    std::unordered_map<std::string, std::unique_ptr<StaticModelResource>> StaticResources;
    std::vector<StaticModelCpuPreloadJob> StaticModelCpuPreloadJobs;
    struct FStaticModelCpuPreloadResult
    {
        StaticModelCpuPreloadTarget Target;
        std::unique_ptr<StaticModelResource> Resource;
    };
    std::deque<StaticModelCpuPreloadTarget> PendingStaticModelCpuPreloads;
    std::vector<FStaticModelCpuPreloadResult> CompletedStaticModelCpuPreloads;
    std::unordered_set<std::string> QueuedStaticModelCpuPreloads;
    struct FStaticModelGpuPromotion
    {
        StaticModelCpuPreloadTarget Target;
        std::unique_ptr<StaticModelResource> Resource;
        std::size_t NextTexture = 0;
    };
    std::deque<FStaticModelGpuPromotion> PendingStaticGpuPromotions;
    std::unordered_set<std::string> QueuedStaticGpuPromotions;
    std::thread StaticModelCpuPreloadThread;
    std::mutex StaticModelCpuPreloadMutex;
    std::condition_variable StaticModelCpuPreloadCv;
    bool StaticModelCpuPreloadStop = false;
    bool StaticModelCpuPreloadWorkerStarted = false;
    std::vector<StaticPlacementModel> StaticPlacementModels;
    std::vector<StaticPlacement> StaticPlacements;
    std::vector<StaticInstance> StaticInstances;
    std::vector<StaticCollisionInstance> StaticCollisionInstances;
    std::unordered_map<uint64, std::vector<std::size_t>> StaticCollisionCells;
    std::vector<std::size_t> LargeStaticCollisionInstances;
    mutable std::vector<uint32> StaticCollisionVisitMarks;
    mutable uint32 StaticCollisionVisitGeneration = 0;
    mutable std::vector<std::size_t> StaticCollisionInstanceScratch;
    mutable std::vector<uint32> StaticCollisionTriangleScratch;
    std::vector<std::size_t> VisibleStaticPlacementIndices;
    std::vector<uint64> VisibleStaticRenderCells;
    std::vector<const WorldRenderBatch*> StaticDrawBatches;
    std::vector<std::size_t> DirectStaticInstanceIndices;
    bool StaticDrawBatchesDirty = true;
    bool DirectStaticInstancesDirty = true;
    std::unordered_map<uint64, std::vector<std::size_t>> StaticPlacementIndicesByRenderCell;
    std::unordered_map<uint64, std::vector<WorldRenderBatch>> StaticCellRenderBatches;
    struct FStaticRenderBakeSource
    {
        const StaticModelResource* Resource = nullptr;
        D3DMATRIX World{};
    };
    struct FStaticRenderBakeRequest
    {
        uint64 CellKey = 0;
        std::vector<FStaticRenderBakeSource> Sources;
    };
    struct FStaticRenderBakeResult
    {
        uint64 CellKey = 0;
        std::vector<WorldRenderCpuBatch> Batches;
    };
    struct FStaticRenderGpuUpload
    {
        FStaticRenderBakeResult Result;
        std::size_t NextBatch = 0;
        std::vector<WorldRenderBatch> GpuBatches;
    };
    std::vector<FStaticRenderBakeRequest> PendingStaticRenderBakes;
    std::vector<FStaticRenderBakeResult> CompletedStaticRenderBakes;
    std::deque<FStaticRenderGpuUpload> PendingStaticRenderGpuUploads;
    std::unordered_set<uint64> QueuedStaticRenderBakeCells;
    std::thread StaticRenderBakeThread;
    std::mutex StaticRenderBakeMutex;
    std::condition_variable StaticRenderBakeCv;
    bool StaticRenderBakeStop = false;
    bool StaticRenderBakeWorkerStarted = false;
    bool StaticRenderBakeBusy = false;
    bool StaticVisibilityPlanReady = false;
    float StaticVisibilityAnchorX = 0.0f;
    float StaticVisibilityAnchorY = 0.0f;
    float StaticVisibilityAnchorZ = 0.0f;
    std::unordered_map<uint64, std::vector<GrassInstance>> GrassInstancesByCell;
    std::size_t GrassInstanceCount = 0;
    std::unordered_map<uint64, std::vector<WorldRenderBatch>> GrassCellRenderBatches;
    std::vector<const WorldRenderBatch*> GrassDrawBatches;
    bool GrassDrawBatchesDirty = true;
    struct FGrassRenderBakeSource
    {
        const StaticModelResource* Resource = nullptr;
        D3DMATRIX World{};
        DWORD Tint = 0xfffffffful;
    };
    struct FGrassRenderBakeRequest
    {
        uint64 CellKey = 0;
        uint64 Epoch = 0;
        uint64 Revision = 0;
        std::vector<FGrassRenderBakeSource> Sources;
    };
    struct FGrassRenderBakeResult
    {
        uint64 CellKey = 0;
        uint64 Epoch = 0;
        uint64 Revision = 0;
        std::vector<WorldRenderCpuBatch> Batches;
    };
    struct FGrassRenderGpuUpload
    {
        FGrassRenderBakeResult Result;
        std::size_t NextBatch = 0;
        std::vector<WorldRenderBatch> GpuBatches;
    };
    std::vector<FGrassRenderBakeRequest> PendingGrassRenderBakes;
    std::vector<FGrassRenderBakeResult> CompletedGrassRenderBakes;
    std::deque<FGrassRenderGpuUpload> PendingGrassRenderGpuUploads;
    std::unordered_map<uint64, uint64> GrassCellBakeRevisions;
    std::unordered_map<uint64, uint32> GrassPendingCellsPerRenderGroup;
    std::unordered_set<uint64> GrassDirtyRenderGroups;
    std::thread GrassRenderBakeThread;
    std::mutex GrassRenderBakeMutex;
    std::condition_variable GrassRenderBakeCv;
    uint64 GrassRenderBakeEpoch = 1;
    uint64 GrassRenderBakeRevision = 0;
    bool GrassRenderBakeStop = false;
    bool GrassRenderBakeWorkerStarted = false;
    bool GrassRenderBakeBusy = false;
    std::unordered_map<int, std::vector<uint8>> GrassMaps;
    struct FGrassMapPreloadTarget
    {
        int ChunkX = 0;
        int ChunkZ = 0;
        int Key = 0;
    };
    std::vector<FGrassMapPreloadTarget> PendingGrassMapPreloads;
    std::unordered_set<int> QueuedGrassMapPreloads;
    std::thread GrassMapPreloadThread;
    std::mutex GrassMapMutex;
    std::mutex GrassMapPreloadMutex;
    std::condition_variable GrassMapPreloadCv;
    bool GrassMapPreloadStop = false;
    bool GrassMapPreloadWorkerStarted = false;
    std::unordered_set<uint64> GrassCells;
    std::unordered_set<uint64> GrassTargetCells;
    std::vector<uint64> GrassPendingCells;
    std::array<std::vector<StaticModelResource*>, 31> GrassPatternResources;
    std::array<std::vector<StaticModelResource*>, 31> GrassFlowerPatternResources;
    std::vector<StaticModelResource*> GrassDetailResources;
    bool GrassRefreshIncomplete = false;
    bool TerrainInitialBlockingLoad = false;
    bool StaticInitialBlockingLoad = false;
    bool StaticRefreshPending = false;
    bool GrassInitialBlockingLoad = false;
    std::vector<FSceneBatch> PlayerBatches;
    struct FRemotePlayerModelResource
    {
        FSkinnedCharacterModel Model;
        FCharacterPoseCache PoseCache;
        std::vector<FSceneBatch> Batches;
        IDirect3DIndexBuffer9* IndexBuffer = nullptr;
        UINT VertexCount = 0;
    };
    struct FRemotePlayerRenderState
    {
        FRemoteGamePlayer Player;
        IDirect3DVertexBuffer9* VertexBuffer = nullptr;
        std::vector<float> SkinScratch;
        std::vector<WorldVertex> VertexScratch;
        uint64 ModelKey = 0;
        std::size_t Action = kPlayerIdleAction;
        float AnimationTime = 0.0f;
        float MovementHold = 0.0f;
        float LastPacketTime = -1.0f;
        bool Running = false;
    };
    std::unordered_map<uint64, FRemotePlayerModelResource> RemotePlayerModels;
    std::unordered_map<uint64, FRemotePlayerRenderState> RemotePlayers;
    struct FRemoteActorRenderState
    {
        FRemoteGameActor Actor;
        StaticModelResource* Resource = nullptr;
        std::string ModelName;
        FBox3 Bounds{};
        bool BoundsValid = false;
    };
    std::unordered_map<uint64, FRemoteActorRenderState> RemoteActors;
    std::vector<StaticModelResource*> VisibleAnimatedResources;
    std::unordered_map<uint32, std::string> MonsterModelNames;
    bool MonsterModelIndexReady = false;
    UINT PlayerVertexCount = 0;
    FSkinnedCharacterModel PlayerModel;
    FCharacterPoseCache PlayerPoseCache;
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
    bool PlayerCollisionNeedsRecovery = false;
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
    std::unordered_set<uint64> QueuedTerrainCpuPreloadCells;
    int GrassCenterX = (std::numeric_limits<int>::min)();
    int GrassCenterZ = (std::numeric_limits<int>::min)();
    float GrassAnchorX = 0.0f;
    float GrassAnchorZ = 0.0f;
    struct WeatherKeyframe
    {
        float Time = 0.0f;
        float Rain = 0.0f;
        float Fog = 0.0f;
        float Wind = 0.0f;
        float Cloud = -1.0f;
    };

    struct WeatherScenario
    {
        std::string Name;
        std::string Sky1;
        std::string Sky2;
        float Duration = 120.0f;
        float CloudCover = 0.0f;
        float Rain = 0.0f;
        float Fog = 0.0f;
        float Wind = 0.35f;
        float SkyScrollScale = 1.0f;
        std::vector<WeatherKeyframe> Keyframes;
    };

    std::vector<WeatherScenario> WeatherScenarios;
    std::vector<std::size_t> WeatherSequence;
    std::size_t WeatherSequencePosition = 0;
    float WeatherScenarioElapsed = 0.0f;
    float WeatherTransitionBlend = 0.0f;
    float WeatherRain = 0.0f;
    float WeatherCloudCover = 0.0f;
    float WeatherFog = 0.0f;
    float WeatherWind = 0.35f;
    float WeatherSkyScrollScale = 1.0f;
    float WeatherFogStart = 70.0f;
    float WeatherFogEnd = 170.0f;
    float ElapsedSeconds = 0.0f;
    float GameTimeFraction = 0.0f;
    FGameWorldSkyState DayNightEnvironment{0.0f, 0, 0, 0, 110, 110, 110, 255, 245, 224, 200, 200, 200};
    FGameWorldSkyState Environment{0.0f, 0, 0, 0, 110, 110, 110, 255, 245, 224, 200, 200, 200};
    bool GrassAnchorValid = false;
    int OverlayWidth = 0;
    int OverlayHeight = 0;
    bool Initialized = false;
    mutable std::unordered_map<std::string, std::filesystem::path> OptionalPathCache;
    mutable std::mutex OptionalPathCacheMutex;
    mutable std::unordered_map<std::string, std::filesystem::path> TerrainLndPathByRelativeKey;
    mutable std::unordered_map<std::string, std::filesystem::path> TerrainLndPathByStemKey;
    mutable bool TerrainPathIndexReady = false;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> TerrainStemPathCache;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> TerrainRelativePathCache;
    mutable std::unordered_map<std::string, std::filesystem::path> ModelPathCache;
    mutable std::unordered_map<std::string, std::filesystem::path> ModelPathIndex;
    mutable bool ModelPathIndexReady = false;
    mutable std::unordered_map<std::string, std::filesystem::path> ModelTexturePathCache;
    mutable std::mutex ModelTexturePathCacheMutex;
    std::unordered_map<std::wstring, IDirect3DTexture9*> DdsTextureCache;
    FD3D9GameWorldRenderStats LastRenderStats;
    uint64 RenderStatsFrameCounter = 0;

    ~Impl();
    void Release();
    void SkinPlayerFrame();
    void LoadPlayerModel(const FSkinnedCharacterModel& model);
    void UpdatePlayerAnimation(float DeltaSeconds, bool moving, bool running);
    void UpdateRemotePlayerAnimations(float DeltaSeconds);
    FRemotePlayerModelResource* EnsureRemotePlayerModel(const FCharacterCreationAppearance& appearance);
    std::vector<FSceneBatch> LoadCharacterBatches(const FSkinnedCharacterModel& model);
    RECT ClientRect() const;
    void FillPresentParameters();
    void CreateReflectionTarget();
    std::filesystem::path ResolveOptionalPath(std::string LogicalName) const;
    std::filesystem::path ResolveConfiguredPath(const std::string& LogicalName) const;
    std::filesystem::path ResolveWeatherTexturePath(const std::string& TextureName) const;
    void BuildTerrainPathIndex() const;
    std::optional<std::filesystem::path> TryResolveTerrainRelativePath(const std::filesystem::path& RelativePath) const;
    std::optional<std::filesystem::path> TryResolveTerrainStemPath(const std::string& TerrainStem) const;
    std::optional<std::filesystem::path> TryResolveTerrainPathFromPatch(const FWorldPatchRecord& PatchRecord) const;
    std::filesystem::path ResolveTerrainPath(const FWorldMapCell& cell) const;
    void BuildModelPathIndex() const;
    void BuildMonsterModelIndex();
    std::optional<std::string> ResolveRemoteActorModelName(const FRemoteGameActor& Actor);
    void RefreshRemoteActorResource(FRemoteActorRenderState& Actor);
    std::filesystem::path ResolveModelPath(const std::string& ModelName) const;
    std::filesystem::path ResolveModelTexturePath(
        const std::filesystem::path& ModelPath,
        const std::string& MaterialName) const;
    IDirect3DTexture9* LoadCachedDdsTexture(const std::filesystem::path& Path);
    IDirect3DTexture9* LoadCachedDdsTextureFromBytes(const std::filesystem::path& Path, const FByteArray& Bytes);
    void LoadStaticPlacements();
    FWorldMusicRegionEvidence QueryMusicRegionEvidence(float WorldX, float WorldZ, float Radius) const;
    StaticModelResource* EnsureStaticModelResource(const std::string& ModelName);
    std::unique_ptr<StaticModelResource> LoadStaticModelCpuBackedResource(
        const std::string& ModelName,
        const std::filesystem::path& ModelPath);
    bool AdvanceStaticModelGpuPromotion(FStaticModelGpuPromotion& Promotion);
    std::unique_ptr<StaticModelResource> LoadStaticModelResource(
        const std::string& ModelName,
        const std::filesystem::path& ModelPath);
    void StartStaticModelCpuPreloadWorker();
    void StopStaticModelCpuPreloadWorker();
    void StaticModelCpuPreloadWorkerMain();
    void QueueStaticModelCpuPreload(const std::string& ModelName, const std::filesystem::path& ModelPath, bool HighPriority = false);
    void DrainStaticModelCpuPreloadJobs(bool Wait);
    void PumpStreamingGpuPromotions();
    const std::vector<uint8>& LoadGrassMap(int ChunkX, int ChunkZ);
    void StartGrassMapPreloadWorker();
    void StopGrassMapPreloadWorker();
    void GrassMapPreloadWorkerMain();
    void QueueGrassMapPreload(int ChunkX, int ChunkZ);
    void PreloadGrassMapsAround(float CenterX, float CenterZ, float Radius);
    bool TryGrassTypeAt(float WorldX, float WorldZ, bool AllowBlockingLoad, uint8& OutType);
    void LoadVisibleStaticObjects();
    void UpdateNpcAnimation(float DeltaSeconds);
    void ClearStaticRenderBatches();
    void BakeStaticRenderCell(uint64 CellKey);
    void StartStaticRenderBakeWorker();
    void StopStaticRenderBakeWorker();
    void StaticRenderBakeWorkerMain();
    void QueueStaticRenderCellBake(uint64 CellKey);
    void DrainStaticRenderCellBakeJobs(bool Wait);
    void RebuildStaticDrawBatchCache();
    void RebuildDirectStaticInstanceCache();
    void BuildVisibleStaticRenderBatches();
    bool BeginAlphaWorldPass(const D3DMATRIX& World);
    void EndAlphaWorldPass(bool UsedShader);
    void DrawWorldRenderBatches(std::vector<const WorldRenderBatch*>& DrawList, EGameWorldDrawBucket Bucket, float CullingMargin, bool AlreadySorted = false);
    void PreloadStaticResourcesAround(float CenterX, float CenterY, float CenterZ, float Radius);
    void LoadVisibleGrass();
    void ClearGrassRenderBatches();
    void BakeGrassCell(uint64 CellKey, std::vector<GrassInstance> Instances);
    void QueueGrassRenderGroupBake(uint64 GroupKey);
    void FlushReadyGrassRenderGroupBakes();
    void CompleteGrassPendingCell(uint64 CellKey);
    void StartGrassRenderBakeWorker();
    void StopGrassRenderBakeWorker();
    void GrassRenderBakeWorkerMain();
    void DrainGrassRenderBakeJobs(bool Wait);
    std::unique_ptr<TerrainResource> LoadTerrainResource(const std::filesystem::path& LNDPath);
    std::unique_ptr<TerrainResource> LoadTerrainCpuBackedResource(const std::filesystem::path& LNDPath);
    bool AdvanceTerrainGpuResource(TerrainResource& Resource);
    void UploadTerrainGpuResource(TerrainResource& Resource);
    void QueueTerrainGpuPromotion(const std::filesystem::path& LNDPath);
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
    void SetVsConst(int Register, const float* data, int Vec4Count);
    void SetBaseLightConstants();
    void ComputeWindCircles(float Out[12]) const;
    DWORD TerrainColorAt(float WorldX, float WorldZ) const;
    void UpdateFrustumPlanes();
    bool IsBoundsVisibleToCamera(const FBox3& Bounds, float ExtraMargin = 0.0f) const;
    void SetBaseWorld(const D3DMATRIX& world);
    void BeginBaseShader();
    void EndBaseShader();
    void ConfigureRenderState();
    void LoadWeatherSystem();
    void UpdateWeather(float DeltaSeconds);
    void ApplyWeatherEnvironment();
    void RefreshWeatherSkyTextures();
    bool TerrainHeightAt(float WorldX, float WorldZ, float ReferenceY, float& OutHeight) const;
    bool TerrainSurfaceNearAt(float WorldX, float WorldZ, float ReferenceY, float& OutHeight, FVector3& OutNormal) const;
    bool TerrainSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const;
    bool FlatGrassSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const;
    void SnapToGround();
    void RebuildStaticCollisionIndex();
    void QueryStaticCollisionInstances(const FBox3& Area, std::vector<std::size_t>& OutInstances) const;
    void QueryStaticCollisionTriangles(const StaticCollisionInstance& Instance, const FBox3& Area, std::vector<uint32>& OutTriangles) const;
    bool CapsuleOverlapsStatic(float X, float FeetY, float Z, FGameWorldCollisionHit* OutHit = nullptr, bool IgnoreSupportingFloor = true) const;
    bool RecoverFromPenetration();
    static bool PointInTriangleXz(float px, float pz, const FVector3& a, const FVector3& b, const FVector3& c);
    bool StaticFloorHeightAt(
        float x,
        float z,
        float MinY,
        float MaxY,
        float& OutY,
        FVector3* OutNormal = nullptr) const;
    bool SupportHeightAt(float x, float z, float FeetY, float& OutY, FVector3* OutNormal = nullptr) const;
    bool TryMoveTo(float x, float z, FGameWorldCollisionHit* OutHit = nullptr);
    void Jump();
    void ApplySlopeSlide(float DeltaSeconds);
    void UpdateVertical(float DeltaSeconds);
    void UpdateViewProjection();
    void DrawTerrain();
    void DrawStaticObjects();
    void DrawGrass();
    void DrawSky();
    void DrawRain();
    bool WaterPlane(float& OutY) const;
    float WaterReflectCoeff() const;
    void RenderReflection();
    void ResetRenderStats();
    void RecordWorldDraw(uint32 triangles, EGameWorldDrawBucket bucket);
    FD3D9GameWorldRenderStats RenderStats() const;
    void UpdateWaterWaves(TerrainResource* resource, float OriginX, float OriginZ);
    void DrawWater();
    void DrawPlayer();
    std::optional<FGameWorldPosition> CurrentPlayerWorldPosition() const;
    void UpsertRemotePlayer(const FRemoteGamePlayer& Player);
    void SetRemotePlayerAppearance(uint64 EntityId, const FCharacterCreationAppearance& Appearance);
    void RemoveRemotePlayer(uint64 EntityId);
    void ClearRemotePlayers();
    void UpsertRemoteActor(const FRemoteGameActor& Actor);
    void UpdateRemoteActorPosition(uint64 EntityId, const FGameWorldPosition& Position);
    void RemoveRemoteActor(uint64 EntityId);
    void ClearRemoteActors();
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
