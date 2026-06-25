#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

static bool IsTerrainLndPath(const std::filesystem::path& Path)
{
    return Common::NormalizePathKey(Path.extension().string()) == ".lnd";
}

static std::string TerrainStemKey(const std::string& TerrainStem)
{
    return Common::NormalizePathKey(Common::StripExtension(TerrainStem));
}

FD3D9GameWorldScene::Impl::~Impl()
{
    Release();
}

void FD3D9GameWorldScene::Impl::Release()
{
    DrainTerrainCpuPreloadJobs(true);
    DrainStaticModelCpuPreloadJobs(true);
    PendingTerrainCpuPreloads.clear();
    PendingStaticModelCpuPreloads.clear();
    QueuedTerrainCpuPreloads.clear();
    QueuedStaticModelCpuPreloads.clear();
    QueuedTerrainCpuPreloadCells.clear();

    SafeRelease(OverlayTexture);
    SafeRelease(TerrainMicrotexture);
    SafeRelease(SkyTexture);
    SafeRelease(WaterTexture);
    ReflectionTextureReady = false;
    ReflectionWarmupFrames = 0;
    ReflectionUpdateCountdown = 0;
    SafeRelease(ReflectionDepth);
    SafeRelease(ReflectionSurface);
    SafeRelease(ReflectionTexture);
    SafeRelease(BaseVS);
    SafeRelease(BasePS);
    SafeRelease(GrassVS);
    SafeRelease(GrassPS);
    SafeRelease(WorldDecl);
    WorldShadersReady = false;
    for (auto& batch : PlayerBatches)
    {
        SafeRelease(batch.Texture);
    }
    PlayerBatches.clear();
    SafeRelease(PlayerIndexBuffer);
    SafeRelease(PlayerVertexBuffer);
    for (auto& [_, resource] : TerrainResources)
    {
        SafeRelease(resource->texture);
        SafeRelease(resource->IndexBuffer);
        SafeRelease(resource->VertexBuffer);
        SafeRelease(resource->WaterIndexBuffer);
        SafeRelease(resource->WaterVertexBuffer);
    }
    for (auto& [_, resource] : StaticResources)
    {
        for (auto& batch : resource->Batches)
        {
            SafeRelease(batch.Texture);
        }
        SafeRelease(resource->IndexBuffer);
        SafeRelease(resource->VertexBuffer);
    }
    ClearStaticRenderBatches();
    for (auto& [_, texture] : DdsTextureCache)
    {
        SafeRelease(texture);
    }
    DdsTextureCache.clear();
    ClearGrassRenderBatches();
    GrassInstances.clear();
    GrassCells.clear();
    GrassRefreshIncomplete = false;
    GrassInitialBlockingLoad = false;
    GrassMaps.clear();
    StaticInstances.clear();
    StaticPlacementModels.clear();
    StaticPlacements.clear();
    StaticPlacementIndicesByRenderCell.clear();
    VisibleStaticPlacementIndices.clear();
    VisibleStaticRenderCells.clear();
    StaticVisibilityPlanReady = false;
    StaticResources.clear();
    TerrainInstances.clear();
    TerrainInstanceLookup.clear();
    TerrainResources.clear();
    StreamingGuardRow = (std::numeric_limits<int>::min)();
    StreamingGuardColumn = (std::numeric_limits<int>::min)();
    StreamingGuardRowStep = (std::numeric_limits<int>::min)();
    StreamingGuardColumnStep = (std::numeric_limits<int>::min)();
    NextStreamingGuardTime = {};
    QueuedTerrainCpuPreloadCells.clear();
    OptionalPathCache.clear();
    TerrainLndPathByRelativeKey.clear();
    TerrainLndPathByStemKey.clear();
    TerrainPathIndexReady = false;
    TerrainStemPathCache.clear();
    TerrainRelativePathCache.clear();
    ModelPathCache.clear();
    ModelPathIndex.clear();
    ModelPathIndexReady = false;
    ModelTexturePathCache.clear();
    SafeRelease(Device);
    Initialized = false;
}


std::filesystem::path FD3D9GameWorldScene::Impl::ResolveOptionalPath(std::string LogicalName) const
{
    if (!AssetResources || LogicalName.empty())
    {
        return {};
    }
    const std::string key = Common::NormalizePathKey(LogicalName);
    if (const auto cached = OptionalPathCache.find(key); cached != OptionalPathCache.end())
    {
        return cached->second;
    }
    std::filesystem::path resolved;
    auto direct = AssetResources->Catalog().FindByLogicalName(LogicalName);
    if (direct)
    {
        resolved = direct->AbsolutePath;
    }
    else
    {
        for (const auto& record : AssetResources->Catalog().All())
        {
            const std::string rel = Common::NormalizePathKey(record.RelativePath);
            if (rel == key || rel.ends_with("/" + key))
            {
                resolved = record.AbsolutePath;
                break;
            }
        }
    }
    OptionalPathCache.emplace(key, resolved);
    return resolved;
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveConfiguredPath(const std::string& LogicalName) const
{
    const auto path = ResolveOptionalPath(LogicalName);
    if (!path.empty())
    {
        return path;
    }
    throw std::runtime_error("required configured asset is missing: " + LogicalName);
}

void FD3D9GameWorldScene::Impl::BuildTerrainPathIndex() const
{
    if (TerrainPathIndexReady || !AssetResources)
    {
        return;
    }

    TerrainLndPathByRelativeKey.clear();
    TerrainLndPathByStemKey.clear();
    TerrainLndPathByRelativeKey.reserve(1024);
    TerrainLndPathByStemKey.reserve(1024);
    for (const auto& record : AssetResources->Catalog().All())
    {
        if (!IsTerrainLndPath(record.RelativePath))
        {
            continue;
        }
        const std::string relativeKey = Common::NormalizePathKey(record.RelativePath);
        const std::string stemKey = TerrainStemKey(record.RelativePath.stem().string());
        TerrainLndPathByRelativeKey.emplace(relativeKey, record.AbsolutePath);
        TerrainLndPathByStemKey.emplace(stemKey, record.AbsolutePath);
    }
    TerrainPathIndexReady = true;
}

std::optional<std::filesystem::path> FD3D9GameWorldScene::Impl::TryResolveTerrainRelativePath(const std::filesystem::path& RelativePath) const
{
    if (!AssetResources || RelativePath.empty())
    {
        return std::nullopt;
    }

    const std::string key = Common::NormalizePathKey(RelativePath);
    if (const auto cached = TerrainRelativePathCache.find(key); cached != TerrainRelativePathCache.end())
    {
        return cached->second;
    }

    BuildTerrainPathIndex();
    if (const auto it = TerrainLndPathByRelativeKey.find(key); it != TerrainLndPathByRelativeKey.end())
    {
        TerrainRelativePathCache.emplace(key, it->second);
        return it->second;
    }

    const auto direct = AssetResources->Catalog().FindByLogicalName(RelativePath.generic_string());
    if (direct && IsTerrainLndPath(direct->RelativePath))
    {
        TerrainRelativePathCache.emplace(key, direct->AbsolutePath);
        return direct->AbsolutePath;
    }

    TerrainRelativePathCache.emplace(key, std::nullopt);
    return std::nullopt;
}

std::optional<std::filesystem::path> FD3D9GameWorldScene::Impl::TryResolveTerrainStemPath(const std::string& TerrainStem) const
{
    if (!AssetResources || TerrainStem.empty())
    {
        return std::nullopt;
    }

    const std::string stemKey = TerrainStemKey(TerrainStem);
    if (const auto cached = TerrainStemPathCache.find(stemKey); cached != TerrainStemPathCache.end())
    {
        return cached->second;
    }

    BuildTerrainPathIndex();
    const std::array<std::string, 6> logicalCandidates
    {{
        "landscape/" + TerrainStem + ".lnd",
        "Landscape/" + TerrainStem + ".lnd",
        "Landscape_ph/" + TerrainStem + ".lnd",
        "Landscape_hr/" + TerrainStem + ".lnd",
        "Landscape_rd/" + TerrainStem + ".lnd",
        TerrainStem + ".lnd"
    }};

    for (const auto& logicalName : logicalCandidates)
    {
        const std::string key = Common::NormalizePathKey(logicalName);
        if (const auto it = TerrainLndPathByRelativeKey.find(key); it != TerrainLndPathByRelativeKey.end())
        {
            TerrainStemPathCache.emplace(stemKey, it->second);
            return it->second;
        }
    }

    if (const auto it = TerrainLndPathByStemKey.find(stemKey); it != TerrainLndPathByStemKey.end())
    {
        TerrainStemPathCache.emplace(stemKey, it->second);
        return it->second;
    }

    TerrainStemPathCache.emplace(stemKey, std::nullopt);
    return std::nullopt;
}

std::optional<std::filesystem::path> FD3D9GameWorldScene::Impl::TryResolveTerrainPathFromPatch(const FWorldPatchRecord& PatchRecord) const
{
    FPath candidate = PatchRecord.RelativePath;
    candidate.replace_extension(".lnd");
    if (const auto sibling = TryResolveTerrainRelativePath(candidate))
    {
        return sibling;
    }

    if (!PatchRecord.StemName.empty())
    {
        if (const auto byStem = TryResolveTerrainStemPath(PatchRecord.StemName))
        {
            return byStem;
        }
    }

    if (!PatchRecord.Name.empty())
    {
        if (const auto byName = TryResolveTerrainStemPath(Common::StripExtension(PatchRecord.Name)))
        {
            return byName;
        }
    }

    return std::nullopt;
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveTerrainPath(const FWorldMapCell& cell) const
{
    const std::string terrainStem = cell.TerrainStem();
    auto resolveTerrainSizePath = [this](const FWorldTerrainSizeRecord& terrain) -> std::filesystem::path
    {
        FPath lndPath = terrain.RelativePath;
        lndPath.replace_extension(".lnd");
        if (const auto terrainPath = TryResolveTerrainRelativePath(lndPath))
        {
            return *terrainPath;
        }
        return ResolveConfiguredPath(lndPath.generic_string());
    };

    if (WorldScene)
    {
        if (cell.ResolvedByTerrainSize && cell.TerrainSizeRecordIndex < WorldScene->TerrainSizeRecords().size())
        {
            return resolveTerrainSizePath(WorldScene->TerrainSizeRecords()[cell.TerrainSizeRecordIndex]);
        }

        if (!terrainStem.empty())
        {
            if (const FWorldTerrainSizeRecord* terrain = WorldScene->FindTerrainSizeByName(terrainStem))
            {
                return resolveTerrainSizePath(*terrain);
            }

            if (const auto terrainPath = TryResolveTerrainStemPath(terrainStem))
            {
                return *terrainPath;
            }

            if (const FWorldPatchRecord* patch = WorldScene->FindPatchByName(terrainStem))
            {
                if (const auto terrainPath = TryResolveTerrainPathFromPatch(*patch))
                {
                    return *terrainPath;
                }
            }
        }

        if (const FWorldTerrainSizeRecord* terrain = WorldScene->FindTerrainSizeByName(cell.TileName))
        {
            return resolveTerrainSizePath(*terrain);
        }

        if (const auto terrainPath = TryResolveTerrainStemPath(cell.TileName))
        {
            return *terrainPath;
        }

        if (cell.ResolvedByPatchCatalog && cell.TileRecordIndex < WorldScene->Patches().size())
        {
            if (const auto terrainPath = TryResolveTerrainPathFromPatch(WorldScene->Patches()[cell.TileRecordIndex]))
            {
                return *terrainPath;
            }
        }

        if (const FWorldPatchRecord* patch = WorldScene->FindPatchByName(cell.TileName))
        {
            if (const auto terrainPath = TryResolveTerrainPathFromPatch(*patch))
            {
                return *terrainPath;
            }
        }
    }

    if (!terrainStem.empty())
    {
        if (const auto terrainPath = TryResolveTerrainStemPath(terrainStem))
        {
            return *terrainPath;
        }
    }

    if (const auto terrainPath = TryResolveTerrainStemPath(cell.TileName))
    {
        return *terrainPath;
    }

    throw std::runtime_error("required landscape tile is missing: " + (terrainStem.empty() ? cell.TileName : terrainStem) + ".lnd");
}
void FD3D9GameWorldScene::Impl::BuildModelPathIndex() const
{
    if (ModelPathIndexReady || !AssetResources)
    {
        return;
    }
    ModelPathIndex.clear();
    std::vector<std::string> dirs;
    dirs.reserve(Config.ModelDirs.size());
    for (const auto& dir : Config.ModelDirs)
    {
        auto key = Common::NormalizePathKey(NarrowAscii(dir));
        if (!key.empty())
        {
            dirs.push_back(std::move(key));
        }
    }
    std::unordered_map<std::string, std::size_t> priorities;
    priorities.reserve(512);
    for (const auto& record : AssetResources->Catalog().All())
    {
        const std::string rel = Common::NormalizePathKey(record.RelativePath);
        if (!rel.ends_with(".mdl"))
        {
            continue;
        }
        std::size_t priority = 0;
        bool allowed = dirs.empty();
        for (std::size_t i = 0; i < dirs.size(); ++i)
        {
            const auto& dir = dirs[i];
            const std::string prefix = dir + "/";
            if (rel.starts_with(prefix) || rel.find("/" + prefix) != std::string::npos)
            {
                allowed = true;
                priority = i;
                break;
            }
        }
        if (!allowed)
        {
            continue;
        }
        const auto modelKey = Common::BaseNameWithoutExtension(rel);
        if (modelKey.empty())
        {
            continue;
        }
        const auto priorityIt = priorities.find(modelKey);
        if (priorityIt == priorities.end() || priority < priorityIt->second)
        {
            priorities[modelKey] = priority;
            ModelPathIndex[modelKey] = record.AbsolutePath;
        }
    }
    ModelPathIndexReady = true;
}


std::filesystem::path FD3D9GameWorldScene::Impl::ResolveModelPath(const std::string& ModelName) const
{
    const auto cacheKey = LowercaseAscii(ModelName);
    if (const auto cached = ModelPathCache.find(cacheKey); cached != ModelPathCache.end())
    {
        if (!cached->second.empty())
        {
            return cached->second;
        }
        throw std::runtime_error("required static model is missing: " + ModelName + ".mdl");
    }
    BuildModelPathIndex();
    if (const auto indexed = ModelPathIndex.find(cacheKey); indexed != ModelPathIndex.end())
    {
        ModelPathCache.emplace(cacheKey, indexed->second);
        return indexed->second;
    }
    for (const auto& dir : Config.ModelDirs)
    {
        const auto logical = NarrowAscii(dir) + "/" + ModelName + ".mdl";
        const auto path = ResolveOptionalPath(logical);
        if (!path.empty())
        {
            ModelPathCache.emplace(cacheKey, path);
            return path;
        }
    }
    ModelPathCache.emplace(cacheKey, std::filesystem::path{});
    throw std::runtime_error("required static model is missing: " + ModelName + ".mdl");
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveModelTexturePath(
    const std::filesystem::path& ModelPath,
    const std::string& MaterialName) const
{
    const auto TextureName = LowercaseAscii(MaterialName) + ".dds";
    const auto cacheKey = Common::NormalizePathKey(ModelPath.generic_string() + "|" + TextureName);
    if (const auto cached = ModelTexturePathCache.find(cacheKey); cached != ModelTexturePathCache.end())
    {
        if (!cached->second.empty())
        {
            return cached->second;
        }
        throw std::runtime_error("required static model texture is missing: " + MaterialName + ".dds for " + ModelPath.string());
    }
    const auto LocalPath = ModelPath.parent_path() / "textures" / TextureName;
    if (std::filesystem::exists(LocalPath))
    {
        ModelTexturePathCache.emplace(cacheKey, LocalPath);
        return LocalPath;
    }
    for (const auto& dir : Config.ModelDirs)
    {
        const auto logical = NarrowAscii(dir) + "/textures/" + TextureName;
        const auto path = ResolveOptionalPath(logical);
        if (!path.empty())
        {
            ModelTexturePathCache.emplace(cacheKey, path);
            return path;
        }
    }
    const auto fallback = ResolveOptionalPath(TextureName);
    if (!fallback.empty())
    {
        ModelTexturePathCache.emplace(cacheKey, fallback);
        return fallback;
    }
    ModelTexturePathCache.emplace(cacheKey, std::filesystem::path{});
    throw std::runtime_error("required static model texture is missing: " + MaterialName + ".dds for " + ModelPath.string());
}

IDirect3DTexture9* FD3D9GameWorldScene::Impl::LoadCachedDdsTexture(const std::filesystem::path& Path)
{
    const auto key = Path.lexically_normal().wstring();
    if (auto it = DdsTextureCache.find(key); it != DdsTextureCache.end())
    {
        it->second->AddRef();
        return it->second;
    }
    IDirect3DTexture9* texture = LoadDdsTexture(Device, Path);
    DdsTextureCache.emplace(key, texture);
    texture->AddRef();
    return texture;
}

bool FD3D9GameWorldScene::Impl::Initialize(
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
    const FSkinnedCharacterModel* PlayerModelIn)
{
    Release();
    Hwnd = window;
    Device = ExternalDevice;
    if (Device)
    {
        Device->AddRef();
    }
    AssetResources = &ResourceManager;
    WorldScene = &world;
    Logger = InLogger;
    Config = WorldConfig;
    QueuedTerrainCpuPreloadCells.reserve(2048);
    PendingTerrainCpuPreloads.reserve(256);
    StartTerrainCpuPreloadWorker();
    StartStaticModelCpuPreloadWorker();
    Environment = FGameWorldSkyState{0.0f, Config.ClearRed, Config.ClearGreen, Config.ClearBlue, 110, 110, 110, 255, 245, 224, Config.SkyRed, Config.SkyGreen, Config.SkyBlue};
    SpawnX = static_cast<float>(x);
    SpawnY = static_cast<float>(y);
    SpawnZ = static_cast<float>(z);
    SpawnAngle = static_cast<float>(Angle);
    CameraYaw = -SpawnAngle;
    if (!Device)
    {
        error = L"D3D9 game world received null device";
        return false;
    }
    FillPresentParameters();
    try
    {
        BuildTerrainPathIndex();
        LoadWorldShaders();
        TerrainMicrotexture = LoadMtxTexture(Device, ResolveConfiguredPath(NarrowAscii(Config.TerrainMicrotexture)));
        SkyTexture = LoadCachedDdsTexture(ResolveConfiguredPath(NarrowAscii(Config.SkyTexture)));
        const auto WaterPath = ResolveOptionalPath("landscape/river1a_00.dds");
        if (!WaterPath.empty())
        {
            WaterTexture = LoadCachedDdsTexture(WaterPath);
        }
        LoadVisibleTerrain();
        SnapToGround();
        LoadStaticPlacements();
        LoadVisibleStaticObjects();
        GrassInitialBlockingLoad = true;
        LoadVisibleGrass();
        GrassInitialBlockingLoad = false;
        PreloadStreamingGuard();
        if (PlayerModelIn && PlayerModelIn->IsValid())
        {
            LoadPlayerModel(*PlayerModelIn);
        }
        CreateReflectionTarget();
    }
    catch (const std::exception& ex)
    {
        GrassInitialBlockingLoad = false;
        AssignError(error, std::string("game world load failed: ") + ex.what());
        return false;
    }
    ConfigureRenderState();
    Initialized = true;
    return true;
}

bool FD3D9GameWorldScene::Impl::SetOverlayBitmap(int width, int height, std::vector<uint8> pixels, std::wstring& error)
{
    if (!Device)
    {
        error = L"SetOverlayBitmap called before Direct3D device creation";
        return false;
    }
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width) * height * 4)
    {
        error = L"invalid game overlay bitmap";
        return false;
    }
    HRESULT hr = S_OK;
    if (!OverlayTexture || OverlayWidth != width || OverlayHeight != height)
    {
        SafeRelease(OverlayTexture);
        hr = Device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &OverlayTexture, nullptr);
        if (FAILED(hr))
        {
            error = HResultText("CreateTexture game overlay", hr);
            return false;
        }
    }
    D3DLOCKED_RECT locked{};
    hr = OverlayTexture->LockRect(0, &locked, nullptr, 0);
    if (FAILED(hr))
    {
        error = HResultText("GameOverlayTexture::LockRect", hr);
        return false;
    }
    const std::size_t SourcePitch = static_cast<std::size_t>(width) * 4;
    for (int row = 0; row < height; ++row)
    {
        std::copy_n(
        pixels.data() + static_cast<std::size_t>(row) * SourcePitch,
        SourcePitch,
        static_cast<uint8*>(locked.pBits) + static_cast<std::size_t>(row) * locked.Pitch);
    }
    OverlayTexture->UnlockRect(0);
    OverlayWidth = width;
    OverlayHeight = height;
    return true;
}

void FD3D9GameWorldScene::Impl::SetFog(float start, float end)
{
    Config.FogStart = start;
    Config.FogEnd = end;
    if (Device)
    {
        ConfigureRenderState();
    }
}

bool FD3D9GameWorldScene::Impl::SetGrassQuality(int quality, std::wstring& error)
{
    quality = std::clamp(quality, 0, 2);
    if (Config.GrassQuality == quality)
    {
        return true;
    }
    const bool VisibilityChanged = (Config.GrassQuality == 0) != (quality == 0);
    Config.GrassQuality = quality;
    if (!VisibilityChanged)
    {
        return true;
    }
    GrassCenterX = (std::numeric_limits<int>::min)();
    GrassCenterZ = (std::numeric_limits<int>::min)();
    GrassAnchorValid = false;
    try
    {
        LoadVisibleGrass();
        return true;
    }
    catch (const std::exception& ex)
    {
        AssignError(error, std::string("game world grass quality update failed: ") + ex.what());
        return false;
    }
}

void FD3D9GameWorldScene::Impl::SetGameTime(float DayFraction)
{
    GameTimeFraction = DayFraction - std::floor(DayFraction);
    const auto& states = Config.SkyStates;
    if (states.size() < 2)
    {
        return;
    }
    std::size_t next = 0;
    while (next < states.size() && GameTimeFraction >= states[next].Time)
    {
        ++next;
    }
    const FGameWorldSkyState* from = nullptr;
    const FGameWorldSkyState* to = nullptr;
    float FromTime = 0.0f;
    float ToTime = 0.0f;
    if (next == 0)
    {
        from = &states.back();
        to = &states.front();
        FromTime = states.back().Time - 1.0f;
        ToTime = states.front().Time;
    } else if (next == states.size())
    {
        from = &states.back();
        to = &states.front();
        FromTime = states.back().Time;
        ToTime = states.front().Time + 1.0f;
    } else
    {
        from = &states[next - 1];
        to = &states[next];
        FromTime = from->Time;
        ToTime = to->Time;
    }
    float SampleTime = GameTimeFraction;
    if (SampleTime < FromTime)
    {
        SampleTime += 1.0f;
    }
    const float blend = std::clamp((SampleTime - FromTime) / (ToTime - FromTime), 0.0f, 1.0f);
    auto channel = [blend](int a, int b)
    {
        return static_cast<int>(std::lround(static_cast<float>(a) + static_cast<float>(b - a) * blend));
    };
    for (const auto field : {&FGameWorldSkyState::ClearRed, &FGameWorldSkyState::ClearGreen, &FGameWorldSkyState::ClearBlue, &FGameWorldSkyState::AmbientRed, &FGameWorldSkyState::AmbientGreen, &FGameWorldSkyState::AmbientBlue, &FGameWorldSkyState::SunRed, &FGameWorldSkyState::SunGreen, &FGameWorldSkyState::SunBlue, &FGameWorldSkyState::CloudRed, &FGameWorldSkyState::CloudGreen, &FGameWorldSkyState::CloudBlue})
    {
        Environment.*field = channel(from->*field, to->*field);
    }
    if (Device)
    {
        ConfigureRenderState();
    }

}
