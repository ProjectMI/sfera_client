#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include "Model/ModelRepository.h"
#include <string_view>
#include <iterator>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>


namespace
{
struct StaticModelCpuBatch
{
    UINT StartIndex = 0;
    UINT IndexCount = 0;
    std::string MaterialName;
};

struct StaticModelCpuResource
{
    std::vector<WorldVertex> Vertices;
    std::vector<uint16> Indices;
    std::vector<FVector3> CollisionPositions;
    std::vector<uint16> CollisionIndices;
    std::vector<StaticModelCpuBatch> Batches;
    FBox3 Bounds;
};

std::mutex StaticModelCpuCacheMutex;
std::unordered_map<std::string, std::shared_ptr<const StaticModelCpuResource>> StaticModelCpuCache;

std::string StaticModelCpuCacheKey(const std::filesystem::path& path)
{
    return Common::NormalizePathKey(path.lexically_normal().generic_string());
}

std::shared_ptr<const StaticModelCpuResource> BuildStaticModelCpuResource(const std::string& ModelName, const std::filesystem::path& ModelPath)
{
    FMdlMesh MeshStorage;
    const FMdlMesh* MeshPtr = nullptr;
    const auto CacheKey = ModelPath.generic_string();
    if (auto CachedMesh = FindCachedMdlMesh(CacheKey))
    {
        MeshPtr = CachedMesh.get();
    }
    else if (auto CachedMesh = FindCachedMdlMesh(ModelName))
    {
        AliasCachedMdlMesh(CacheKey, ModelName);
        MeshPtr = CachedMesh.get();
    }
    else if (auto CachedMesh = FindCachedMdlMesh(ModelName + ".mdl"))
    {
        AliasCachedMdlMesh(CacheKey, ModelName + ".mdl");
        MeshPtr = CachedMesh.get();
    }
    else
    {
        auto MeshResult = LoadMdlMeshFromBytes(ReadGameWorldFileBytes(ModelPath), CacheKey);
        if (!MeshResult.IsOk())
        {
            throw std::runtime_error(MeshResult.Status().Message());
        }
        MeshStorage = std::move(MeshResult.Value());
        MeshPtr = &MeshStorage;
    }
    const FMdlMesh& mesh = *MeshPtr;
    if (mesh.Vertices.empty() || mesh.Triangles.empty() || mesh.Surfaces.empty() || mesh.Info.Materials.empty())
    {
        throw std::runtime_error("static model has no renderable geometry: " + ModelPath.string());
    }

    auto resource = std::make_shared<StaticModelCpuResource>();
    resource->Vertices.reserve(mesh.Vertices.size());
    for (const auto& source : mesh.Vertices)
    {
        const auto normal = NormalizeVector(FVector3{source.NX, source.NY, source.NZ});
        resource->Vertices.push_back(WorldVertex{source.X, source.Y, source.Z, normal.X, normal.Y, normal.Z, 0xffffffff, source.U, source.V, source.U, source.V});
    }

    std::vector<std::vector<uint16>> IndicesByMaterial(mesh.Info.Materials.size());
    for (const auto& surface : mesh.Surfaces)
    {
        if (surface.FirstTriangleIndex < 0 || surface.TriangleCount < 0 || surface.FirstVertexIndex < 0 || surface.VertexCount < 0)
        {
            throw std::runtime_error("static model has negative surface ranges: " + ModelPath.string());
        }
        const auto material = static_cast<std::size_t>(surface.TextureIndex);
        const auto FirstTriangle = static_cast<std::size_t>(surface.FirstTriangleIndex);
        const auto TriangleCount = static_cast<std::size_t>(surface.TriangleCount);
        const auto FirstVertex = static_cast<std::size_t>(surface.FirstVertexIndex);
        const auto VertexCount = static_cast<std::size_t>(surface.VertexCount);
        if (material >= IndicesByMaterial.size() || FirstTriangle > mesh.Triangles.size() || TriangleCount > mesh.Triangles.size() - FirstTriangle || FirstVertex > mesh.Vertices.size() || VertexCount > mesh.Vertices.size() - FirstVertex)
        {
            throw std::runtime_error("static model surface range is invalid: " + ModelPath.string());
        }
        auto& materialIndices = IndicesByMaterial[material];
        for (std::size_t i = 0; i < TriangleCount; ++i)
        {
            const auto& triangle = mesh.Triangles[FirstTriangle + i];
            if (triangle.A >= VertexCount || triangle.B >= VertexCount || triangle.C >= VertexCount)
            {
                throw std::runtime_error("static model triangle range is invalid: " + ModelPath.string());
            }
            const auto ia = static_cast<uint16>(FirstVertex + triangle.A);
            const auto ib = static_cast<uint16>(FirstVertex + triangle.B);
            const auto ic = static_cast<uint16>(FirstVertex + triangle.C);
            materialIndices.push_back(ia);
            materialIndices.push_back(ib);
            materialIndices.push_back(ic);
            if ((triangle.Flags & 0x100) == 0)
            {
                resource->CollisionIndices.push_back(ia);
                resource->CollisionIndices.push_back(ib);
                resource->CollisionIndices.push_back(ic);
            }
        }
    }

    resource->Bounds.Min = FVector3{mesh.Bounds.MinX, mesh.Bounds.MinY, mesh.Bounds.MinZ};
    resource->Bounds.Max = FVector3{mesh.Bounds.MaxX, mesh.Bounds.MaxY, mesh.Bounds.MaxZ};
    resource->CollisionPositions.reserve(mesh.Vertices.size());
    for (const auto& vertex : mesh.Vertices)
    {
        resource->CollisionPositions.push_back(FVector3{vertex.X, vertex.Y, vertex.Z});
    }
    for (std::size_t material = 0; material < IndicesByMaterial.size(); ++material)
    {
        auto& materialIndices = IndicesByMaterial[material];
        if (materialIndices.empty())
        {
            continue;
        }
        StaticModelCpuBatch batch;
        batch.StartIndex = static_cast<UINT>(resource->Indices.size());
        batch.IndexCount = static_cast<UINT>(materialIndices.size());
        batch.MaterialName = mesh.Info.Materials[material];
        resource->Indices.insert(resource->Indices.end(), materialIndices.begin(), materialIndices.end());
        resource->Batches.push_back(std::move(batch));
    }
    if (resource->Indices.empty() || resource->Batches.empty())
    {
        throw std::runtime_error("static model has no material batches: " + ModelName);
    }
    return resource;
}

std::shared_ptr<const StaticModelCpuResource> LoadStaticModelCpuResourceCached(const std::string& ModelName, const std::filesystem::path& ModelPath)
{
    const auto key = StaticModelCpuCacheKey(ModelPath);
    {
        std::lock_guard<std::mutex> lock(StaticModelCpuCacheMutex);
        if (auto it = StaticModelCpuCache.find(key); it != StaticModelCpuCache.end())
        {
            return it->second;
        }
    }
    auto built = BuildStaticModelCpuResource(ModelName, ModelPath);
    std::lock_guard<std::mutex> lock(StaticModelCpuCacheMutex);
    auto [it, inserted] = StaticModelCpuCache.emplace(key, built);
    return inserted ? built : it->second;
}

struct StaticPlacementFile
{
    std::filesystem::path AbsolutePath;
    std::string RelativeKey;
};

bool LowerPathEndsWith(std::string_view path, std::string_view suffix)
{
    return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
}

bool StaticPathAllowed(std::string_view rel, const std::vector<std::string>& configuredDirs)
{
    if (configuredDirs.empty())
    {
        return true;
    }
    for (const auto& dir : configuredDirs)
    {
        if (rel == dir || rel.starts_with(dir + "/"))
        {
            return true;
        }
    }
    return false;
}

struct ParsedStaticPlacement
{
    std::array<char, 20> Name{};
    std::array<char, 20> Key{};
    uint8 NameLength = 0;
    FVector3 Position;
    FVector3 Rotation;
    D3DMATRIX World{};
};

struct PlacementModelKey
{
    std::array<char, 20> Key{};
    uint8 Length = 0;

    bool operator==(const PlacementModelKey& other) const
    {
        if (Length != other.Length)
        {
            return false;
        }
        for (std::size_t i = 0; i < Length; ++i)
        {
            if (Key[i] != other.Key[i])
            {
                return false;
            }
        }
        return true;
    }
};

struct PlacementModelKeyHash
{
    std::size_t operator()(const PlacementModelKey& value) const
    {
        std::size_t hash = 1469598103934665603ull;
        for (std::size_t i = 0; i < value.Length; ++i)
        {
            hash ^= static_cast<unsigned char>(value.Key[i]);
            hash *= 1099511628211ull;
        }
        return hash ^ value.Length;
    }
};

bool FixedLowerEquals(const std::array<char, 20>& value, uint8 length, std::string_view text)
{
    if (length != text.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < length; ++i)
    {
        if (value[i] != text[i])
        {
            return false;
        }
    }
    return true;
}

bool ReadFixedAsciiName(const std::vector<uint8>& data, std::size_t offset, std::size_t limit, ParsedStaticPlacement& placement)
{
    std::size_t length = 0;
    while (length < limit && data[offset + length] != 0)
    {
        const auto ch = data[offset + length];
        placement.Name[length] = static_cast<char>(ch);
        placement.Key[length] = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        ++length;
    }
    placement.NameLength = static_cast<uint8>(length);
    return length != 0 && !FixedLowerEquals(placement.Key, placement.NameLength, "empty");
}

PlacementModelKey MakePlacementModelKey(const ParsedStaticPlacement& placement)
{
    PlacementModelKey key;
    key.Key = placement.Key;
    key.Length = placement.NameLength;
    return key;
}

std::string FixedAsciiString(const std::array<char, 20>& value, uint8 length)
{
    return std::string(value.data(), value.data() + static_cast<std::size_t>(length));
}

uint32 InternPlacementModel(const ParsedStaticPlacement& placement, std::unordered_map<PlacementModelKey, uint32, PlacementModelKeyHash>& ids, std::vector<StaticPlacementModel>& models)
{
    const auto key = MakePlacementModelKey(placement);
    if (auto it = ids.find(key); it != ids.end())
    {
        return it->second;
    }
    const auto id = static_cast<uint32>(models.size());
    StaticPlacementModel model;
    model.Name = FixedAsciiString(placement.Name, placement.NameLength);
    model.Key = FixedAsciiString(placement.Key, placement.NameLength);
    models.push_back(std::move(model));
    ids.emplace(key, id);
    return id;
}

std::vector<ParsedStaticPlacement> ParseStaticPlacementFile(const StaticPlacementFile& file)
{
    const auto data = ReadGameWorldFileBytes(file.AbsolutePath);
    Binary::RequireRange(data, 0, 4, "MBD header");
    const auto count = static_cast<std::size_t>(Binary::U16LE(data, 0));
    if (Binary::U16LE(data, 2) != 0)
    {
        throw std::runtime_error("invalid MBD header: " + file.RelativeKey);
    }
    constexpr std::size_t RecordBytes = 44;
    Binary::RequireRange(data, 4, count * RecordBytes, "MBD object records");
    std::vector<ParsedStaticPlacement> placements;
    placements.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t offset = 4 + i * RecordBytes;
        placements.emplace_back();
        auto& parsed = placements.back();
        if (!ReadFixedAsciiName(data, offset, 20, parsed))
        {
            placements.pop_back();
            continue;
        }
        parsed.Position = FVector3{Binary::F32LE(data, offset + 20), Binary::F32LE(data, offset + 24), Binary::F32LE(data, offset + 28)};
        parsed.Rotation = FVector3{Binary::F32LE(data, offset + 36), Binary::F32LE(data, offset + 32), Binary::F32LE(data, offset + 40)};
        StaticPlacement matrixSource;
        matrixSource.Position = parsed.Position;
        matrixSource.Rotation = parsed.Rotation;
        parsed.World = PlacementMatrix(matrixSource);
    }
    return placements;
}

std::vector<StaticPlacementFile> CollectStaticPlacementFiles(const FResourceManager* resources, const FGameWorldConfig& config)
{
    if (!resources)
    {
        throw std::runtime_error("static placement loading has no resource catalog");
    }
    std::vector<std::string> configuredDirs;
    configuredDirs.reserve(config.StaticObjectDirs.size());
    for (const auto& dir : config.StaticObjectDirs)
    {
        configuredDirs.push_back(Common::NormalizePathKey(NarrowAscii(dir)));
    }
    std::vector<StaticPlacementFile> files;
    for (const auto& record : resources->Catalog().All())
    {
        const std::string rel = Common::NormalizePathKey(record.RelativePath);
        if ((!LowerPathEndsWith(rel, ".mbd") && !LowerPathEndsWith(rel, ".mb")) || !StaticPathAllowed(rel, configuredDirs))
        {
            continue;
        }
        files.push_back(StaticPlacementFile{record.AbsolutePath, rel});
    }
    if (files.empty())
    {
        throw std::runtime_error("static object directories contain no MBD placement files");
    }
    return files;
}

StaticPlacementLoadResult BuildStaticPlacementLoadResult(const std::vector<StaticPlacementFile>& files)
{
    std::vector<std::vector<ParsedStaticPlacement>> parsed(files.size());
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        parsed[index] = ParseStaticPlacementFile(files[index]);
    }
    std::size_t total = 0;
    for (const auto& bucket : parsed)
    {
        total += bucket.size();
    }
    StaticPlacementLoadResult result;
    result.Placements.reserve(total);
    result.Models.reserve(128);
    std::unordered_map<PlacementModelKey, uint32, PlacementModelKeyHash> modelIds;
    modelIds.reserve(128);
    for (const auto& bucket : parsed)
    {
        for (const auto& source : bucket)
        {
            StaticPlacement placement;
            placement.ModelId = InternPlacementModel(source, modelIds, result.Models);
            placement.Position = source.Position;
            placement.Rotation = source.Rotation;
            placement.World = source.World;
            result.Placements.push_back(std::move(placement));
        }
    }
    if (result.Placements.empty())
    {
        throw std::runtime_error("static object directories contain no MBD placements");
    }
    return result;
}
}


void FD3D9GameWorldScene::Impl::ApplyStaticPlacementLoadResult(StaticPlacementLoadResult&& result)
{
    StaticInstances.clear();
    VisibleStaticPlacementIndices.clear();
    PendingStaticModelLoads.clear();
    StaticPlacementModels = std::move(result.Models);
    StaticPlacements = std::move(result.Placements);
    StaticVisibilityPlanReady = false;
}

void FD3D9GameWorldScene::Impl::LoadStaticPlacements()
{
    auto result = BuildStaticPlacementLoadResult(CollectStaticPlacementFiles(AssetResources, Config));
    ApplyStaticPlacementLoadResult(std::move(result));
}

void FD3D9GameWorldScene::Impl::BeginStaticPlacementLoadAsync()
{
    if (StaticPlacementWorkerStarted)
    {
        return;
    }
    JoinStaticPlacementWorker();
    StaticPlacementWorkerReady.store(false, std::memory_order_release);
    StaticPlacementWorkerResult.reset();
    StaticPlacementWorkerException = nullptr;
    auto files = CollectStaticPlacementFiles(AssetResources, Config);
    StaticPlacementWorkerStarted = true;
    StaticPlacementWorker = std::thread([this, files = std::move(files)]() mutable
    {
#if defined(_WIN32)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        try
        {
            auto result = std::make_unique<StaticPlacementLoadResult>(BuildStaticPlacementLoadResult(files));
            std::lock_guard<std::mutex> lock(StaticPlacementWorkerMutex);
            StaticPlacementWorkerResult = std::move(result);
            StaticPlacementWorkerException = nullptr;
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(StaticPlacementWorkerMutex);
            StaticPlacementWorkerResult.reset();
            StaticPlacementWorkerException = std::current_exception();
        }
        StaticPlacementWorkerReady.store(true, std::memory_order_release);
    });
}

bool FD3D9GameWorldScene::Impl::PollStaticPlacementLoad()
{
    if (!StaticPlacementWorkerStarted)
    {
        return false;
    }
    if (!StaticPlacementWorkerReady.load(std::memory_order_acquire))
    {
        return false;
    }
    if (StaticPlacementWorker.joinable())
    {
        StaticPlacementWorker.join();
    }
    std::unique_ptr<StaticPlacementLoadResult> result;
    std::exception_ptr failure;
    {
        std::lock_guard<std::mutex> lock(StaticPlacementWorkerMutex);
        result = std::move(StaticPlacementWorkerResult);
        failure = StaticPlacementWorkerException;
        StaticPlacementWorkerException = nullptr;
    }
    StaticPlacementWorkerStarted = false;
    StaticPlacementWorkerReady.store(false, std::memory_order_release);
    if (failure)
    {
        std::rethrow_exception(failure);
    }
    if (!result)
    {
        throw std::runtime_error("static placement async load finished without a result");
    }
    ApplyStaticPlacementLoadResult(std::move(*result));
    return true;
}

void FD3D9GameWorldScene::Impl::JoinStaticPlacementWorker()
{
    if (StaticPlacementWorker.joinable())
    {
        StaticPlacementWorker.join();
    }
    StaticPlacementWorkerStarted = false;
    StaticPlacementWorkerReady.store(false, std::memory_order_release);
    StaticPlacementWorkerResult.reset();
    StaticPlacementWorkerException = nullptr;
}

StaticModelResource* FD3D9GameWorldScene::Impl::EnsureStaticModelResource(const std::string& ModelName)
{
    const auto key = LowercaseAscii(ModelName);
    auto it = StaticResources.find(key);
    if (it != StaticResources.end())
    {
        return it->second.get();
    }
    const auto ModelPath = ResolveModelPath(ModelName);
    it = StaticResources.emplace(key, LoadStaticModelResource(ModelName, ModelPath)).first;
    return it->second.get();
}

std::unique_ptr<StaticModelResource> FD3D9GameWorldScene::Impl::LoadStaticModelResource(
    const std::string& ModelName,
    const std::filesystem::path& ModelPath)
{
    const auto cpu = LoadStaticModelCpuResourceCached(ModelName, ModelPath);
    auto resource = std::make_unique<StaticModelResource>();
    resource->VertexCount = static_cast<UINT>(cpu->Vertices.size());
    resource->Bounds = cpu->Bounds;
    resource->CollisionPositions = cpu->CollisionPositions;
    resource->CollisionIndices = cpu->CollisionIndices;

    for (const auto& cpuBatch : cpu->Batches)
    {
        FSceneBatch batch;
        batch.StartIndex = cpuBatch.StartIndex;
        batch.IndexCount = cpuBatch.IndexCount;
        batch.Texture = LoadCachedDdsTexture(ResolveModelTexturePath(ModelPath, cpuBatch.MaterialName));
        resource->Batches.push_back(batch);
    }

    const UINT VertexBytes = static_cast<UINT>(cpu->Vertices.size() * sizeof(WorldVertex));
    HRESULT hr = Device->CreateVertexBuffer(VertexBytes, D3DUSAGE_WRITEONLY, kWorldVertexFvf, D3DPOOL_MANAGED, &resource->VertexBuffer, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateVertexBuffer static model", hr));
    }
    void* VertexData = nullptr;
    hr = resource->VertexBuffer->Lock(0, VertexBytes, &VertexData, 0);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("StaticModelVertexBuffer::Lock", hr));
    }
    CopyVectorBytes(VertexData, cpu->Vertices, VertexBytes);
    resource->VertexBuffer->Unlock();

    const UINT IndexBytes = static_cast<UINT>(cpu->Indices.size() * sizeof(uint16));
    hr = Device->CreateIndexBuffer(IndexBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &resource->IndexBuffer, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateIndexBuffer static model", hr));
    }
    void* IndexData = nullptr;
    hr = resource->IndexBuffer->Lock(0, IndexBytes, &IndexData, 0);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("StaticModelIndexBuffer::Lock", hr));
    }
    CopyVectorBytes(IndexData, cpu->Indices, IndexBytes);
    resource->IndexBuffer->Unlock();
    return resource;
}

bool FD3D9GameWorldScene::Impl::LoadVisibleStaticObjects(int MaxNewModels)
{
    const float RadiusSquared = Config.StaticObjectRadius * Config.StaticObjectRadius;
    const bool needsPlan = !StaticVisibilityPlanReady || std::abs(SpawnX - StaticVisibilityAnchorX) > 1.0f || std::abs(SpawnZ - StaticVisibilityAnchorZ) > 1.0f;
    if (needsPlan)
    {
        VisibleStaticPlacementIndices.clear();
        PendingStaticModelLoads.clear();
        std::unordered_set<uint32> queuedModels;
        StaticVisibilityAnchorX = SpawnX;
        StaticVisibilityAnchorZ = SpawnZ;
        for (std::size_t index = 0; index < StaticPlacements.size(); ++index)
        {
            auto& placement = StaticPlacements[index];
            const float dx = placement.Position.X - SpawnX;
            const float dz = placement.Position.Z - SpawnZ;
            if (dx * dx + dz * dz > RadiusSquared)
            {
                continue;
            }
            if (placement.ModelId >= StaticPlacementModels.size())
            {
                continue;
            }
            const auto& model = StaticPlacementModels[placement.ModelId];
            VisibleStaticPlacementIndices.push_back(index);
            if (StaticResources.find(model.Key) == StaticResources.end() && queuedModels.insert(placement.ModelId).second)
            {
                PendingStaticModelLoads.push_back(placement.ModelId);
            }
        }
        StaticVisibilityPlanReady = true;
    }
    int LoadedThisCall = 0;
    while (LoadedThisCall < MaxNewModels && !PendingStaticModelLoads.empty())
    {
        const auto modelId = PendingStaticModelLoads.back();
        PendingStaticModelLoads.pop_back();
        if (modelId >= StaticPlacementModels.size())
        {
            continue;
        }
        const auto& model = StaticPlacementModels[modelId];
        if (model.Key.empty() || StaticResources.find(model.Key) != StaticResources.end())
        {
            continue;
        }
        const auto ModelPath = ResolveModelPath(model.Name);
        StaticResources.emplace(model.Key, LoadStaticModelResource(model.Name, ModelPath));
        ++LoadedThisCall;
    }
    StaticInstances.clear();
    StaticInstances.reserve((std::min<std::size_t>)(VisibleStaticPlacementIndices.size(), 4096));
    for (const auto placementIndex : VisibleStaticPlacementIndices)
    {
        auto& placement = StaticPlacements[placementIndex];
        if (placement.ModelId >= StaticPlacementModels.size())
        {
            continue;
        }
        auto it = StaticResources.find(StaticPlacementModels[placement.ModelId].Key);
        if (it == StaticResources.end())
        {
            continue;
        }
        if (!placement.BoundsValid)
        {
            placement.Bounds.Min = FVector3{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            placement.Bounds.Max = FVector3{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            for (int corner = 0; corner < 8; ++corner)
            {
                const FVector3 local{(corner & 1) ? it->second->Bounds.Max.X : it->second->Bounds.Min.X, (corner & 2) ? it->second->Bounds.Max.Y : it->second->Bounds.Min.Y, (corner & 4) ? it->second->Bounds.Max.Z : it->second->Bounds.Min.Z};
                const FVector3 point = TransformPoint(local, placement.World);
                placement.Bounds.Min.X = (std::min)(placement.Bounds.Min.X, point.X);
                placement.Bounds.Min.Y = (std::min)(placement.Bounds.Min.Y, point.Y);
                placement.Bounds.Min.Z = (std::min)(placement.Bounds.Min.Z, point.Z);
                placement.Bounds.Max.X = (std::max)(placement.Bounds.Max.X, point.X);
                placement.Bounds.Max.Y = (std::max)(placement.Bounds.Max.Y, point.Y);
                placement.Bounds.Max.Z = (std::max)(placement.Bounds.Max.Z, point.Z);
            }
            placement.BoundsValid = true;
        }
        StaticInstances.push_back(StaticInstance{it->second.get(), placement.World, placement.Bounds});
    }
    std::sort(StaticInstances.begin(), StaticInstances.end(), [](const StaticInstance& a, const StaticInstance& b)
    {
        if (a.resource != b.resource)
        {
            return std::less<StaticModelResource*>{}(a.resource, b.resource);
        }
        if (a.world._41 != b.world._41)
        {
            return a.world._41 < b.world._41;
        }
        return a.world._43 < b.world._43;
    });
    return PendingStaticModelLoads.empty();
}




void FD3D9GameWorldScene::PrewarmGrassModelCpuCache(const FResourceManager& resources, const FGameWorldConfig& config, FLogger* logger)
{
    try
    {
        Impl resolver;
        resolver.AssetResources = &resources;
        resolver.Config = config;

        std::unordered_map<std::string, std::string> uniqueModels;
        auto addModel = [&uniqueModels](const std::wstring& name)
        {
            const auto model = NarrowAscii(name);
            if (!model.empty())
            {
                uniqueModels.emplace(LowercaseAscii(model), model);
            }
        };
        for (const auto& model : config.GrassDetailModels)
        {
            addModel(model);
        }
        for (const auto& pattern : config.GrassPatterns)
        {
            for (const auto& model : pattern)
            {
                addModel(model);
            }
        }
        for (const auto& pattern : config.GrassFlowerPatterns)
        {
            for (const auto& model : pattern)
            {
                addModel(model);
            }
        }

        std::vector<std::pair<std::string, std::filesystem::path>> targets;
        targets.reserve(uniqueModels.size());
        for (const auto& [_, model] : uniqueModels)
        {
            try
            {
                targets.emplace_back(model, resolver.ResolveModelPath(model));
            }
            catch (const std::exception& ex)
            {
                if (logger)
                {
                    logger->Warning("grass model CPU prewarm skipped " + model + ": " + ex.what());
                }
            }
        }
        if (targets.empty())
        {
            return;
        }

        const size_t hardware = static_cast<size_t>((std::max)(1u, std::thread::hardware_concurrency()));
        const size_t threadCount = (std::min)(std::clamp(hardware - 1, size_t{1}, size_t{8}), targets.size());
        std::atomic_size_t next{0};
        std::atomic_size_t loaded{0};
        std::atomic_size_t failed{0};
        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
        {
            workers.emplace_back([&targets, &next, &loaded, &failed]()
            {
                for (;;)
                {
                    const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= targets.size())
                    {
                        break;
                    }
                    try
                    {
                        LoadStaticModelCpuResourceCached(targets[index].first, targets[index].second);
                        loaded.fetch_add(1, std::memory_order_relaxed);
                    }
                    catch (...)
                    {
                        failed.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (auto& worker : workers)
        {
            worker.join();
        }
        if (logger)
        {
            logger->Info("grass model CPU cache prewarmed: requested=" + std::to_string(targets.size()) + ", loaded=" + std::to_string(loaded.load(std::memory_order_relaxed)) + ", failed=" + std::to_string(failed.load(std::memory_order_relaxed)) + ", threads=" + std::to_string(threadCount));
        }
    }
    catch (const std::exception& ex)
    {
        if (logger)
        {
            logger->Warning(std::string("grass model CPU prewarm failed: ") + ex.what());
        }
    }
}

void FD3D9GameWorldScene::PrewarmStaticModelCpuCache(const FResourceManager& resources, const FModelRepository& models, FLogger* logger)
{
    std::vector<std::pair<std::string, std::filesystem::path>> targets;
    targets.reserve(models.Assets().size());
    for (const auto& record : models.Assets())
    {
        if (record.Kind != EModelAssetKind::Mdl)
        {
            continue;
        }
        auto file = resources.Catalog().FindByLogicalName(record.LogicalName);
        if (!file)
        {
            continue;
        }
        targets.emplace_back(record.RelativePath.stem().generic_string(), file->AbsolutePath);
    }
    if (targets.empty())
    {
        return;
    }

    const size_t hardware = static_cast<size_t>(std::max(1u, std::thread::hardware_concurrency()));
    const size_t threadCount = std::min(std::clamp(hardware - 1, size_t{1}, size_t{8}), targets.size());
    std::atomic_size_t next{0};
    std::atomic_size_t loaded{0};
    std::atomic_size_t failed{0};
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        workers.emplace_back([&targets, &next, &loaded, &failed]()
        {
            for (;;)
            {
                const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= targets.size())
                {
                    break;
                }
                try
                {
                    LoadStaticModelCpuResourceCached(targets[index].first, targets[index].second);
                    loaded.fetch_add(1, std::memory_order_relaxed);
                }
                catch (...)
                {
                    failed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    if (logger)
    {
        logger->Info("static model render CPU cache: requested=" + std::to_string(targets.size()) + ", loaded=" + std::to_string(loaded.load(std::memory_order_relaxed)) + ", failed=" + std::to_string(failed.load(std::memory_order_relaxed)) + ", threads=" + std::to_string(threadCount));
    }
}

const std::vector<uint8>& FD3D9GameWorldScene::Impl::LoadGrassMap(int ChunkX, int ChunkZ)
{
    if (ChunkX < 0 || ChunkZ < 0 || ChunkX >= Config.GrassmapGridSize || ChunkZ >= Config.GrassmapGridSize)
    {
        throw std::runtime_error("grass map chunk is out of range: " + std::to_string(ChunkX) + "," + std::to_string(ChunkZ));
    }

    const int key = ChunkZ * Config.GrassmapGridSize + ChunkX;
    const auto cached = GrassMaps.find(key);

    if (cached != GrassMaps.end())
    {
        return cached->second;
    }

    auto grassMapLogicalName = [this](int x, int z)
    {
        std::ostringstream name;
        name << NarrowAscii(Config.GrassmapDir) << "/grassmap_" << std::setw(2) << std::setfill('0') << x << "_" << std::setw(2) << std::setfill('0') << z << ".bin";
        return name.str();
    };

    const FWorldGrassPatch* patch = WorldScene ? WorldScene->Grass().Find(ChunkX, ChunkZ) : nullptr;
    std::string sourceName = patch ? patch->RelativePath.generic_string() : grassMapLogicalName(ChunkX, ChunkZ);
    FByteArray data;

    auto readRaw = [this](const std::string& logicalName) -> FByteArray
    {
        const auto path = ResolveOptionalPath(logicalName);
        return path.empty() ? FByteArray{} : ReadGameWorldFileBytes(path);
    };

    data = readRaw(sourceName);

    if (data.empty() && AssetResources)
    {
        auto blob = AssetResources->Load(sourceName);
        if (blob.IsOk())
        {
            data = std::move(blob.Value().Bytes);
        }
    }

    if (data.empty() && patch)
    {
        sourceName = grassMapLogicalName(ChunkX, ChunkZ);
        data = readRaw(sourceName);
        if (data.empty() && AssetResources)
        {
            auto blob = AssetResources->Load(sourceName);
            if (blob.IsOk())
            {
                data = std::move(blob.Value().Bytes);
            }
        }
    }

    if (data.empty())
    {
        throw std::runtime_error("grass map is missing: " + sourceName);
    }

    const std::size_t expected = static_cast<std::size_t>(Config.GrassmapTileResolution) * static_cast<std::size_t>(Config.GrassmapTileResolution);

    if (data.size() != expected)
    {
        throw std::runtime_error("invalid grass map size: " + sourceName);
    }

    return GrassMaps.emplace(key, std::move(data)).first->second;
}

uint8 FD3D9GameWorldScene::Impl::GrassTypeAt(float WorldX, float WorldZ)
{
    const int MapX = static_cast<int>(std::floor(
    (Config.GrassmapWorldOffsetX + static_cast<float>(Config.GrassmapWorldSignX) * WorldX) *
    Config.GrassmapWorldScale));
    const int MapZ = static_cast<int>(std::floor(
    (Config.GrassmapWorldOffsetZ + static_cast<float>(Config.GrassmapWorldSignZ) * WorldZ) *
    Config.GrassmapWorldScale));
    const int WorldResolution = Config.GrassmapGridSize * Config.GrassmapTileResolution;
    if (MapX < 0 || MapZ < 0 || MapX >= WorldResolution || MapZ >= WorldResolution)
    {
        return 0;
    }
    const int ChunkX = MapX / Config.GrassmapTileResolution;
    const int ChunkZ = MapZ / Config.GrassmapTileResolution;
    const int LocalX = MapX % Config.GrassmapTileResolution;
    const int LocalZ = MapZ % Config.GrassmapTileResolution;
    const auto& map = LoadGrassMap(ChunkX, ChunkZ);
    uint8 type =
    map[static_cast<std::size_t>(LocalZ) * Config.GrassmapTileResolution + LocalX] & 0x0f;
    if (type != 0 &&
    SpawnY > Config.GrassHighlandMinY &&
    SpawnY < Config.GrassHighlandMaxY)
    {
        type = static_cast<uint8>(type + Config.GrassHighlandPatternOffset);
    }
    return type;
}

bool FD3D9GameWorldScene::Impl::LoadVisibleGrass(int MaxNewCells)
{
    if (Config.GrassQuality <= 0)
    {
        GrassInstances.clear();
        GrassCells.clear();
        return true;
    }
    if (Config.GrassDetailModels.empty())
    {
        throw std::runtime_error("grass_detail_models is empty");
    }
    if (Config.GrassSampleOffsets.empty())
    {
        throw std::runtime_error("grass_sample_offsets is empty");
    }

    struct GrassSamplePlan
    {
        float X = 0.0f;
        float Z = 0.0f;
        uint8 Type = 0;
    };

    struct GrassCellPlan
    {
        int CellX = 0;
        int CellZ = 0;
        float X = 0.0f;
        float Z = 0.0f;
        uint64 Key = 0;
        std::vector<GrassSamplePlan> Samples;
    };

    const float spacing = Config.GrassSpacing;
    GrassAnchorX = SpawnX;
    GrassAnchorZ = SpawnZ;
    GrassAnchorValid = true;
    GrassCenterX = static_cast<int>(std::floor(GrassAnchorX / spacing));
    GrassCenterZ = static_cast<int>(std::floor(GrassAnchorZ / spacing));
    const float GenerationRadius = Config.GrassRadius + Config.GrassGenerationMargin;
    const int CellRadius = static_cast<int>(std::ceil(GenerationRadius / spacing)) + 1;
    const float CellSelectionRadius = GenerationRadius + spacing;
    const float CellSelectionRadiusSquared = CellSelectionRadius * CellSelectionRadius;
    auto CellKey = [](int x, int z)
    {
        return (static_cast<uint64>(static_cast<uint32>(x)) << 32) | static_cast<uint32>(z);
    };

    std::unordered_set<uint64> TargetCells;
    TargetCells.reserve(static_cast<std::size_t>((CellRadius * 2 + 1) * (CellRadius * 2 + 1)));
    for (int CellX = GrassCenterX - CellRadius; CellX <= GrassCenterX + CellRadius; ++CellX)
    {
        for (int CellZ = GrassCenterZ - CellRadius; CellZ <= GrassCenterZ + CellRadius; ++CellZ)
        {
            const float x = static_cast<float>(CellX) * spacing;
            const float z = static_cast<float>(CellZ) * spacing;
            const float dx = x + spacing * 0.5f - GrassAnchorX;
            const float dz = z + spacing * 0.5f - GrassAnchorZ;
            if (dx * dx + dz * dz <= CellSelectionRadiusSquared)
            {
                TargetCells.insert(CellKey(CellX, CellZ));
            }
        }
    }

    std::erase_if(GrassInstances, [&TargetCells, &CellKey](const GrassInstance& instance)
    {
        return !TargetCells.contains(CellKey(instance.CellX, instance.CellZ));
    });
    std::erase_if(GrassCells, [&TargetCells](uint64 key)
    {
        return !TargetCells.contains(key);
    });

    std::array<std::vector<StaticModelResource*>, 31> GrassPatternResources{};
    std::array<std::vector<StaticModelResource*>, 31> FlowerPatternResources{};
    std::array<bool, 31> UsedGrassTypes{};
    std::vector<StaticModelResource*> DetailResources;
    bool AnyTypedGrass = false;

    std::vector<GrassCellPlan> plans;
    plans.reserve(TargetCells.size());
    int GeneratedThisCall = 0;
    bool Complete = true;

    for (int CellX = GrassCenterX - CellRadius; CellX <= GrassCenterX + CellRadius; ++CellX)
    {
        for (int CellZ = GrassCenterZ - CellRadius; CellZ <= GrassCenterZ + CellRadius; ++CellZ)
        {
            const auto key = CellKey(CellX, CellZ);
            if (!TargetCells.contains(key) || GrassCells.contains(key))
            {
                continue;
            }
            if (GeneratedThisCall >= MaxNewCells)
            {
                Complete = false;
                continue;
            }
            GrassCells.insert(key);
            ++GeneratedThisCall;
            GrassCellPlan plan;
            plan.CellX = CellX;
            plan.CellZ = CellZ;
            plan.X = static_cast<float>(CellX) * spacing;
            plan.Z = static_cast<float>(CellZ) * spacing;
            plan.Key = key;
            plan.Samples.reserve(Config.GrassSampleOffsets.size());
            for (const auto& SampleOffset : Config.GrassSampleOffsets)
            {
                const float SampleX = plan.X + SampleOffset.X;
                const float SampleZ = plan.Z + SampleOffset.Y;
                const uint8 type = GrassTypeAt(SampleX, SampleZ);
                if (type > 0 && type < UsedGrassTypes.size())
                {
                    UsedGrassTypes[type] = true;
                    AnyTypedGrass = true;
                }
                plan.Samples.push_back(GrassSamplePlan{SampleX, SampleZ, type});
            }
            plans.push_back(std::move(plan));
        }
    }

    if (plans.empty() || !AnyTypedGrass)
    {
        return Complete;
    }

    auto ensureWideModel = [this](const std::wstring& name)
    {
        return EnsureStaticModelResource(NarrowAscii(name));
    };

    for (std::size_t type = 1; type < UsedGrassTypes.size(); ++type)
    {
        if (!UsedGrassTypes[type])
        {
            continue;
        }
        auto& patternOut = GrassPatternResources[type];
        patternOut.reserve(Config.GrassPatterns[type].size());
        for (const auto& model : Config.GrassPatterns[type])
        {
            patternOut.push_back(ensureWideModel(model));
        }
        if (Config.GrassQuality >= 2)
        {
            auto& flowerOut = FlowerPatternResources[type];
            flowerOut.reserve(Config.GrassFlowerPatterns[type].size());
            for (const auto& model : Config.GrassFlowerPatterns[type])
            {
                flowerOut.push_back(ensureWideModel(model));
            }
        }
    }

    DetailResources.reserve(Config.GrassDetailModels.size());
    for (const auto& model : Config.GrassDetailModels)
    {
        DetailResources.push_back(ensureWideModel(model));
    }

    std::vector<std::vector<GrassInstance>> generated(plans.size());
    std::atomic_size_t next{0};
    std::mutex errorMutex;
    std::string firstError;

    auto generateCell = [&](std::size_t planIndex)
    {
        const auto& plan = plans[planIndex];
        auto& out = generated[planIndex];
        out.reserve(Config.GrassSampleOffsets.size() * static_cast<std::size_t>((std::max)(1, Config.GrassDetailCount)));
        int FlatSampleCount = 0;
        bool AnyDetail = false;
        int FlowerType = 0;

        for (std::size_t SampleIndex = 0; SampleIndex < plan.Samples.size(); ++SampleIndex)
        {
            const auto& sample = plan.Samples[SampleIndex];
            const auto type = sample.Type;
            if (type == 0 || type >= GrassPatternResources.size())
            {
                continue;
            }
            const auto& pattern = GrassPatternResources[type];
            if (pattern.empty())
            {
                throw std::runtime_error("grass pattern has no models for type " + std::to_string(type));
            }

            uint32 RandomState = (static_cast<uint32>(plan.CellX) * 0x9e3779b9U) ^ (static_cast<uint32>(plan.CellZ) * 0x85ebca6bU) ^ (static_cast<uint32>(SampleIndex) * 0xc2b2ae35U) ^ type;
            auto NextRandom = [&RandomState]()
            {
                RandomState ^= RandomState << 13;
                RandomState ^= RandomState >> 17;
                RandomState ^= RandomState << 5;
                return RandomState;
            };
            auto UnitRandom = [&NextRandom]()
            {
                return static_cast<float>(NextRandom() & 0xffffU) / 65535.0f;
            };

            float FlatHeight = 0.0f;
            FVector3 FlatNormal{};
            if (FlatGrassSurfaceAt(sample.X, sample.Z, FlatHeight, FlatNormal))
            {
                StaticModelResource* resource = pattern[NextRandom() % pattern.size()];
                auto world = AlignUpMatrix(FlatNormal);
                world._41 = sample.X;
                world._42 = FlatHeight - resource->Bounds.Max.Y;
                world._43 = sample.Z;
                out.push_back(GrassInstance{resource, world, UnitRandom() * 2.0f * kPi, 0.65f + UnitRandom() * 0.35f, plan.CellX, plan.CellZ});
                ++FlatSampleCount;
                FlowerType = static_cast<int>(type);
                continue;
            }

            if (DetailResources.empty())
            {
                continue;
            }
            const int detailCount = Config.GrassQuality >= 2 ? (std::max)(0, Config.GrassDetailCount) : (std::min)((std::max)(0, Config.GrassDetailCount), 2);
            for (int detail = 0; detail < detailCount; ++detail)
            {
                const float jitter = Config.GrassSpacing * Config.GrassJitterFraction;
                const float DetailX = sample.X + (UnitRandom() * 2.0f - 1.0f) * jitter;
                const float DetailZ = sample.Z + (UnitRandom() * 2.0f - 1.0f) * jitter;
                float height = 0.0f;
                FVector3 DetailNormal{};
                if (!TerrainSurfaceAt(DetailX, DetailZ, height, DetailNormal))
                {
                    continue;
                }
                StaticModelResource* resource = DetailResources[NextRandom() % DetailResources.size()];
                const float Scale = Config.GrassScaleMin + UnitRandom() * (Config.GrassScaleMax - Config.GrassScaleMin);
                auto world = ScaleMatrix(Scale);
                world._41 = DetailX;
                world._42 = height - resource->Bounds.Max.Y * Scale;
                world._43 = DetailZ;
                out.push_back(GrassInstance{resource, world, UnitRandom() * 2.0f * kPi, 0.65f + UnitRandom() * 0.35f, plan.CellX, plan.CellZ});
                AnyDetail = true;
            }
        }

        const int SampleTotal = static_cast<int>(Config.GrassSampleOffsets.size());
        if (FlatSampleCount == SampleTotal && !AnyDetail && FlowerType > 0 && FlowerType < static_cast<int>(FlowerPatternResources.size()) && !FlowerPatternResources[static_cast<std::size_t>(FlowerType)].empty())
        {
            const auto& flowers = FlowerPatternResources[static_cast<std::size_t>(FlowerType)];
            uint32 FlowerState = (static_cast<uint32>(plan.CellX) * 0x27d4eb2dU) ^ (static_cast<uint32>(plan.CellZ) * 0x165667b1U) ^ 0x9e3779b9U;
            auto FlowerNext = [&FlowerState]()
            {
                FlowerState ^= FlowerState << 13;
                FlowerState ^= FlowerState >> 17;
                FlowerState ^= FlowerState << 5;
                return FlowerState;
            };
            auto FlowerUnit = [&FlowerNext]()
            {
                return static_cast<float>(FlowerNext() & 0xffffU) / 65535.0f;
            };
            const int flowerLimit = Config.GrassQuality >= 2 ? Config.GrassFlowerCountMax : (std::min)(Config.GrassFlowerCountMax, 3);
            const int FlowerCount = static_cast<int>(FlowerUnit() * static_cast<float>((std::max)(0, flowerLimit)));
            for (int f = 0; f < FlowerCount; ++f)
            {
                const float FlowerX = plan.X + FlowerUnit() * spacing;
                const float FlowerZ = plan.Z + FlowerUnit() * spacing;
                float FlowerH = 0.0f;
                FVector3 FlowerNormal{};
                if (!TerrainSurfaceAt(FlowerX, FlowerZ, FlowerH, FlowerNormal))
                {
                    continue;
                }
                const int slot = static_cast<int>(FlowerUnit() * 5.0f);
                if (slot < 0 || slot >= static_cast<int>(flowers.size()))
                {
                    continue;
                }
                StaticModelResource* resource = flowers[static_cast<std::size_t>(slot)];
                auto world = AlignUpMatrix(FlowerNormal);
                world._41 = FlowerX;
                world._42 = FlowerH - resource->Bounds.Max.Y;
                world._43 = FlowerZ;
                out.push_back(GrassInstance{resource, world, FlowerUnit() * 2.0f * kPi, 0.65f + FlowerUnit() * 0.35f, plan.CellX, plan.CellZ});
            }
        }
    };

    const size_t hardware = static_cast<size_t>((std::max)(1u, std::thread::hardware_concurrency()));
    const size_t threadCount = plans.size() < 8 ? 1 : (std::min)(std::clamp(hardware - 1, size_t{1}, size_t{8}), plans.size());
    auto workerBody = [&]()
    {
        for (;;)
        {
            const size_t index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= plans.size())
            {
                break;
            }
            try
            {
                generateCell(index);
            }
            catch (const std::exception& ex)
            {
                std::lock_guard<std::mutex> lock(errorMutex);
                if (firstError.empty())
                {
                    firstError = ex.what();
                }
            }
        }
    };

    if (threadCount == 1)
    {
        workerBody();
    }
    else
    {
        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
        {
            workers.emplace_back(workerBody);
        }
        for (auto& worker : workers)
        {
            worker.join();
        }
    }

    if (!firstError.empty())
    {
        throw std::runtime_error(firstError);
    }

    std::size_t newInstances = 0;
    for (const auto& bucket : generated)
    {
        newInstances += bucket.size();
    }
    GrassInstances.reserve(GrassInstances.size() + newInstances);
    for (auto& bucket : generated)
    {
        GrassInstances.insert(GrassInstances.end(), std::make_move_iterator(bucket.begin()), std::make_move_iterator(bucket.end()));
    }
    std::sort(GrassInstances.begin(), GrassInstances.end(), [](const GrassInstance& a, const GrassInstance& b)
    {
        if (a.resource != b.resource)
        {
            return std::less<StaticModelResource*>{}(a.resource, b.resource);
        }
        if (a.CellX != b.CellX)
        {
            return a.CellX < b.CellX;
        }
        return a.CellZ < b.CellZ;
    });
    return Complete;
}

bool FD3D9GameWorldScene::Impl::CollidesWithStatic(float x, float y, float z) const
{
    const float radius = Config.PlayerCollisionRadius;
    const float RadiusSquared = radius * radius;
    const float BodyTop = y - Config.PlayerCollisionHeight;
    for (const auto& instance : StaticInstances)
    {
        if (x < instance.Bounds.Min.X - radius || x > instance.Bounds.Max.X + radius ||
        z < instance.Bounds.Min.Z - radius || z > instance.Bounds.Max.Z + radius ||
        y < instance.Bounds.Min.Y - radius || BodyTop > instance.Bounds.Max.Y + radius)
        {
            continue;
        }
        const auto& positions = instance.resource->CollisionPositions;
        const auto& Indices = instance.resource->CollisionIndices;
        for (std::size_t triangle = 0; triangle + 2 < Indices.size(); triangle += 3)
        {
            const FVector3 a = TransformPoint(positions[Indices[triangle]], instance.world);
            const FVector3 b = TransformPoint(positions[Indices[triangle + 1]], instance.world);
            const FVector3 c = TransformPoint(positions[Indices[triangle + 2]], instance.world);
            const FVector3 normal = Cross(Subtract(b, a), Subtract(c, a));
            const float NormalLength = std::sqrt(Dot(normal, normal));
            if (NormalLength <= 0.00001f)
            {
                continue;
            }
            // Walkable (floor-facing) triangles — floors, slopes, ramps — are
            // never collision walls: you stand/walk on them (SupportHeightAt
            // handles the height). Only steep faces (walls) block horizontal
            // movement. Skipping only floors *below* the feet (the old check)
            // made the slope above your feet block you, so ramps could only be
            // jumped onto, not walked up.
            const bool FloorFacing =
            std::abs(normal.Y) / NormalLength >= Config.CollisionFloorNormalThreshold;
            if (FloorFacing)
            {
                continue;
            }
            for (float offset = radius; offset < Config.PlayerCollisionHeight; offset += radius)
            {
                const FVector3 center{x, y - offset, z};
                if (PointTriangleDistanceSquared(center, a, b, c) <= RadiusSquared)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool FD3D9GameWorldScene::Impl::PointInTriangleXz(float px, float pz, const FVector3& a, const FVector3& b, const FVector3& c)
{
    const float d1 = (px - b.X) * (a.Z - b.Z) - (a.X - b.X) * (pz - b.Z);
    const float d2 = (px - c.X) * (b.Z - c.Z) - (b.X - c.X) * (pz - c.Z);
    const float d3 = (px - a.X) * (c.Z - a.Z) - (c.X - a.X) * (pz - a.Z);
    const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

bool FD3D9GameWorldScene::Impl::StaticFloorHeightAt(
    float x,
    float z,
    float MinY,
    float MaxY,
    float& OutY,
    FVector3* OutNormal) const
{
    bool found = false;
    float best = MaxY;
    FVector3 BestNormal{0.0f, -1.0f, 0.0f};
    for (const auto& instance : StaticInstances)
    {
        if (x < instance.Bounds.Min.X || x > instance.Bounds.Max.X ||
        z < instance.Bounds.Min.Z || z > instance.Bounds.Max.Z ||
        instance.Bounds.Min.Y > MaxY || instance.Bounds.Max.Y < MinY)
        {
            continue;
        }
        const auto& positions = instance.resource->CollisionPositions;
        const auto& Indices = instance.resource->CollisionIndices;
        for (std::size_t t = 0; t + 2 < Indices.size(); t += 3)
        {
            const FVector3 a = TransformPoint(positions[Indices[t]], instance.world);
            const FVector3 b = TransformPoint(positions[Indices[t + 1]], instance.world);
            const FVector3 c = TransformPoint(positions[Indices[t + 2]], instance.world);
            const FVector3 normal = Cross(Subtract(b, a), Subtract(c, a));
            const float len = std::sqrt(Dot(normal, normal));
            if (len <= 0.00001f || std::abs(normal.Y) / len < Config.CollisionFloorNormalThreshold)
            {
                continue;  // only walkable (floor-facing) triangles
            }
            if (!PointInTriangleXz(x, z, a, b, c))
            {
                continue;
            }
            // Plane height at (x,z): y = a.Y - (nx*(x-a.X)+nz*(z-a.Z))/ny.
            const float y = a.Y - (normal.X * (x - a.X) + normal.Z * (z - a.Z)) / normal.Y;
            if (y < MinY || y > MaxY)
            {
                continue;
            }
            if (!found || y < best)
            {
                best = y;
                found = true;
                const float inv = 1.0f / len;
                BestNormal = FVector3{normal.X * inv, normal.Y * inv, normal.Z * inv};
            }
        }
    }
    if (found)
    {
        OutY = best;
        if (OutNormal)
        {
            *OutNormal = BestNormal;
        }
    }
    return found;
}

void FD3D9GameWorldScene::Impl::DrawStaticObjects()
{
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    Device->SetRenderState(D3DRS_ALPHAREF, 0x20);
    Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    // them broke foliage (leaf cards are single-sided geometry). Walking into
    // a wall is prevented by collision, not by culling, so you never get
    // inside a model to see its interior.
    const bool UseShader = WorldShadersReady;
    if (UseShader)
    {
        BeginBaseShader();
        SetBaseLightConstants();
    } else
    {
        Device->SetFVF(kWorldVertexFvf);
    }
    const StaticModelResource* boundResource = nullptr;
    IDirect3DTexture9* boundTexture = nullptr;
    for (const auto& instance : StaticInstances)
    {
        if (!IsBoundsVisibleToCamera(instance.Bounds, 1.0f))
        {
            continue;
        }
        const auto* resource = instance.resource;
        if (UseShader)
        {
            SetBaseWorld(instance.world);
        } else
        {
            Device->SetTransform(D3DTS_WORLD, &instance.world);
        }
        if (resource != boundResource)
        {
            Device->SetStreamSource(0, resource->VertexBuffer, 0, sizeof(WorldVertex));
            Device->SetIndices(resource->IndexBuffer);
            boundResource = resource;
            boundTexture = nullptr;
        }
        for (const auto& batch : resource->Batches)
        {
            if (batch.Texture != boundTexture)
            {
                Device->SetTexture(0, batch.Texture);
                boundTexture = batch.Texture;
            }
            const UINT triangleCount = batch.IndexCount / 3;
            Device->DrawIndexedPrimitive(
            D3DPT_TRIANGLELIST,
            0,
            0,
            resource->VertexCount,
            batch.StartIndex,
            triangleCount);
            RecordWorldDraw(triangleCount, EGameWorldDrawBucket::StaticObjects);
        }
    }
    if (UseShader)
    {
        EndBaseShader();
    }
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
}

void FD3D9GameWorldScene::Impl::DrawGrass()
{
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    Device->SetRenderState(D3DRS_ALPHAREF, 0x20);
    Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    // Lit + textured via the base shader (consistent with terrain/objects).
    // The wind is still the CPU coherent-wave approximation applied to each
    // (VS 04_0x with gWindCircle/gMiniSinTable) needs grass geometry carrying
    // per-vertex wind attributes (pivot/bend/phase/length), a separate task.
    const bool UseShader = WorldShadersReady;
    if (UseShader)
    {
        BeginBaseShader();
        SetBaseLightConstants();
    } else
    {
        Device->SetFVF(kWorldVertexFvf);
    }
    const StaticModelResource* boundResource = nullptr;
    IDirect3DTexture9* boundTexture = nullptr;
    const float GrassWindTime = ElapsedSeconds * Config.GrassWindSpeed;
    const float GrassGustTime = GrassWindTime * 0.21f;
    for (const auto& instance : GrassInstances)
    {
        const auto* resource = instance.resource;
        auto world = instance.world;
        if (!IsPointVisibleToCamera(world._41, world._42, world._43, 4.0f))
        {
            continue;
        }
        const float x = world._41;
        const float y = world._42;
        const float z = world._43;
        world._41 = 0.0f;
        world._42 = 0.0f;
        world._43 = 0.0f;
        if (Config.GrassQuality == 2)
        {
            constexpr float kWindDirX = 0.70f;
            constexpr float kWindDirZ = 0.71f;
            constexpr float kSpatialFreq = 0.06f;
            const float spatial = (x * kWindDirX + z * kWindDirZ) * kSpatialFreq;
            const float gust = 0.55f + 0.45f * std::sin(GrassGustTime + spatial * 0.5f);
            const float sway = std::sin(GrassWindTime - spatial + instance.WindPhase * 0.12f) * Config.GrassWindAmplitude * instance.WindScale * gust;
            world = MultiplyMatrix(world, RotationZMatrix(sway));
        }
        world._41 = x;
        world._42 = y;
        world._43 = z;
        if (UseShader)
        {
            SetBaseWorld(world);
        } else
        {
            Device->SetTransform(D3DTS_WORLD, &world);
        }
        if (resource != boundResource)
        {
            Device->SetStreamSource(0, resource->VertexBuffer, 0, sizeof(WorldVertex));
            Device->SetIndices(resource->IndexBuffer);
            boundResource = resource;
            boundTexture = nullptr;
        }
        for (const auto& batch : resource->Batches)
        {
            if (batch.Texture != boundTexture)
            {
                Device->SetTexture(0, batch.Texture);
                boundTexture = batch.Texture;
            }
            const UINT triangleCount = batch.IndexCount / 3;
            Device->DrawIndexedPrimitive(
            D3DPT_TRIANGLELIST,
            0,
            0,
            resource->VertexCount,
            batch.StartIndex,
            triangleCount);
            RecordWorldDraw(triangleCount, EGameWorldDrawBucket::Grass);
        }
    }
    if (UseShader)
    {
        EndBaseShader();
    }
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
}
