#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
constexpr int kTerrainSurfaceGridResolution = 32;

std::mutex TerrainCpuCacheMutex;
std::unordered_map<std::wstring, std::shared_ptr<const TerrainCpuResource>> TerrainCpuCache;

bool TerrainTriangleAt(const std::vector<FVector3>& positions, const std::vector<uint16>& indices, std::size_t triangleOffset, float localX, float localZ, float& outHeight, FVector3* outNormal)
{
    if (triangleOffset + 2 >= indices.size())
    {
        return false;
    }
    const auto& a = positions[indices[triangleOffset]];
    const auto& b = positions[indices[triangleOffset + 1]];
    const auto& c = positions[indices[triangleOffset + 2]];
    const float denominator = (b.Z - c.Z) * (a.X - c.X) + (c.X - b.X) * (a.Z - c.Z);
    if (std::abs(denominator) <= 0.000001f)
    {
        return false;
    }
    const float wa = ((b.Z - c.Z) * (localX - c.X) + (c.X - b.X) * (localZ - c.Z)) / denominator;
    const float wb = ((c.Z - a.Z) * (localX - c.X) + (a.X - c.X) * (localZ - c.Z)) / denominator;
    const float wc = 1.0f - wa - wb;
    if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
    {
        return false;
    }
    outHeight = wa * a.Y + wb * b.Y + wc * c.Y;
    if (outNormal)
    {
        FVector3 normal = NormalizeVector(Cross(Subtract(b, a), Subtract(c, a)));
        if (normal.Y < 0.0f)
        {
            normal = Scale(normal, -1.0f);
        }
        *outNormal = normal;
    }
    return true;
}

int TerrainSurfaceCell(float value, float tileSize, int gridResolution)
{
    const float scaled = value / tileSize * static_cast<float>(gridResolution);
    return std::clamp(static_cast<int>(std::floor(scaled)), 0, gridResolution - 1);
}

std::wstring TerrainCpuCacheKey(const std::filesystem::path& path)
{
    return path.lexically_normal().wstring();
}

uint64 TerrainTileKey(int tileX, int tileZ)
{
    return (static_cast<uint64>(static_cast<uint32>(tileX)) << 32) | static_cast<uint32>(tileZ);
}

int TerrainTileCoord(float value, float tileSize)
{
    return static_cast<int>(std::floor(value / tileSize));
}


FBox3 TranslatedTerrainBounds(const TerrainInstance& instance)
{
    FBox3 bounds = instance.resource ? instance.resource->Bounds : FBox3{};
    bounds.Min.X += instance.OriginX;
    bounds.Max.X += instance.OriginX;
    bounds.Min.Z += instance.OriginZ;
    bounds.Max.Z += instance.OriginZ;
    return bounds;
}

bool TerrainHeightAtInstance(const TerrainInstance& instance, float tileSize, float WorldX, float WorldZ, float ReferenceY, float& OutHeight)
{
    if (!instance.resource)
    {
        return false;
    }
    const float LocalX = WorldX - instance.OriginX;
    const float LocalZ = WorldZ - instance.OriginZ;
    if (LocalX < -0.01f || LocalX > tileSize + 0.01f || LocalZ < -0.01f || LocalZ > tileSize + 0.01f)
    {
        return false;
    }
    const auto& positions = instance.resource->positions;
    const auto& indices = instance.resource->Indices;
    float bestHeight = ReferenceY;
    float bestDistance = std::numeric_limits<float>::max();
    auto sampleTriangle = [&](std::size_t i)
    {
        float height = 0.0f;
        if (!TerrainTriangleAt(positions, indices, i, LocalX, LocalZ, height, nullptr))
        {
            return;
        }
        const float distance = std::abs(height - ReferenceY);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestHeight = height;
        }
    };
    if (instance.resource->SurfaceGridResolution > 0 && !instance.resource->SurfaceCells.empty())
    {
        const int cellX = TerrainSurfaceCell(LocalX, tileSize, instance.resource->SurfaceGridResolution);
        const int cellZ = TerrainSurfaceCell(LocalZ, tileSize, instance.resource->SurfaceGridResolution);
        const auto& cell = instance.resource->SurfaceCells[static_cast<std::size_t>(cellZ) * instance.resource->SurfaceGridResolution + cellX];
        for (const uint32 i : cell)
        {
            sampleTriangle(static_cast<std::size_t>(i));
        }
    }
    else
    {
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            sampleTriangle(i);
        }
    }
    if (bestDistance == std::numeric_limits<float>::max())
    {
        return false;
    }
    OutHeight = bestHeight;
    return true;
}

bool TerrainSurfaceAtInstance(const TerrainInstance& instance, float tileSize, float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal)
{
    if (!instance.resource)
    {
        return false;
    }
    const float LocalX = WorldX - instance.OriginX;
    const float LocalZ = WorldZ - instance.OriginZ;
    if (LocalX < -0.01f || LocalX > tileSize + 0.01f || LocalZ < -0.01f || LocalZ > tileSize + 0.01f)
    {
        return false;
    }
    const auto& positions = instance.resource->positions;
    const auto& indices = instance.resource->Indices;
    float bestHeight = -std::numeric_limits<float>::max();
    FVector3 bestNormal{};
    bool found = false;
    auto sampleTriangle = [&](std::size_t i)
    {
        float height = 0.0f;
        FVector3 normal{};
        if (!TerrainTriangleAt(positions, indices, i, LocalX, LocalZ, height, &normal))
        {
            return;
        }
        if (!found || height > bestHeight)
        {
            bestHeight = height;
            bestNormal = normal;
            found = true;
        }
    };
    if (instance.resource->SurfaceGridResolution > 0 && !instance.resource->SurfaceCells.empty())
    {
        const int cellX = TerrainSurfaceCell(LocalX, tileSize, instance.resource->SurfaceGridResolution);
        const int cellZ = TerrainSurfaceCell(LocalZ, tileSize, instance.resource->SurfaceGridResolution);
        const auto& cell = instance.resource->SurfaceCells[static_cast<std::size_t>(cellZ) * instance.resource->SurfaceGridResolution + cellX];
        for (const uint32 i : cell)
        {
            sampleTriangle(static_cast<std::size_t>(i));
        }
    }
    else
    {
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            sampleTriangle(i);
        }
    }
    if (!found)
    {
        return false;
    }
    OutHeight = bestHeight;
    OutNormal = bestNormal;
    return true;
}

void BuildTerrainSurfaceGrid(TerrainCpuResource& resource, float tileSize)
{
    resource.SurfaceGridResolution = kTerrainSurfaceGridResolution;
    resource.SurfaceCells.resize(static_cast<std::size_t>(resource.SurfaceGridResolution) * static_cast<std::size_t>(resource.SurfaceGridResolution));
    for (std::size_t tri = 0; tri + 2 < resource.Indices.size(); tri += 3)
    {
        const auto& a = resource.Positions[resource.Indices[tri]];
        const auto& b = resource.Positions[resource.Indices[tri + 1]];
        const auto& c = resource.Positions[resource.Indices[tri + 2]];
        const float minX = (std::min)({a.X, b.X, c.X});
        const float maxX = (std::max)({a.X, b.X, c.X});
        const float minZ = (std::min)({a.Z, b.Z, c.Z});
        const float maxZ = (std::max)({a.Z, b.Z, c.Z});
        const int minCellX = TerrainSurfaceCell(minX, tileSize, resource.SurfaceGridResolution);
        const int maxCellX = TerrainSurfaceCell(maxX, tileSize, resource.SurfaceGridResolution);
        const int minCellZ = TerrainSurfaceCell(minZ, tileSize, resource.SurfaceGridResolution);
        const int maxCellZ = TerrainSurfaceCell(maxZ, tileSize, resource.SurfaceGridResolution);
        for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
        {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
            {
                resource.SurfaceCells[static_cast<std::size_t>(cellZ) * resource.SurfaceGridResolution + cellX].push_back(static_cast<uint32>(tri));
            }
        }
    }
}

void ReadWaterCpuMesh(TerrainCpuResource& resource, const std::filesystem::path& LNDPath, float tileSize)
{
    auto WTRPath = LNDPath;
    WTRPath.replace_extension(".wtr");
    if (!std::filesystem::exists(WTRPath))
    {
        return;
    }
    const auto data = ReadGameWorldFileBytes(WTRPath);
    constexpr int kGrid = 12;
    if (data.size() < static_cast<std::size_t>(kGrid) * kGrid * 2 * 4)
    {
        return;
    }
    auto height = [&](int r, int c) { return Binary::F32LE(data, (static_cast<std::size_t>(r) * kGrid + c) * 2 * 4); };
    auto IsWater = [&](int r, int c) { return Binary::I32LE(data, (static_cast<std::size_t>(r) * kGrid + c) * 2 * 4 + 4) != 0; };
    const float step = tileSize / static_cast<float>(kGrid - 1);
    for (int r = 0; r < kGrid - 1; ++r)
    {
        for (int c = 0; c < kGrid - 1; ++c)
        {
            if (!IsWater(r, c) || !IsWater(r, c + 1) || !IsWater(r + 1, c) || !IsWater(r + 1, c + 1))
            {
                continue;
            }
            const float h00 = height(r, c), h01 = height(r, c + 1);
            const float h10 = height(r + 1, c), h11 = height(r + 1, c + 1);
            const auto base = static_cast<uint16>(resource.WaterVertices.size());
            auto push = [&](int rr, int cc, float h)
            {
                const float x = cc * step;
                const float z = rr * step;
                resource.WaterVertices.push_back(WorldVertex{x, h, z, 0.0f, -1.0f, 0.0f, 0xffffffff, x / 12.5f, z / 12.5f, 0.0f, 0.0f});
            };
            push(r, c, h00);
            push(r, c + 1, h01);
            push(r + 1, c, h10);
            push(r + 1, c + 1, h11);
            resource.WaterIndices.push_back(base);
            resource.WaterIndices.push_back(static_cast<uint16>(base + 1));
            resource.WaterIndices.push_back(static_cast<uint16>(base + 2));
            resource.WaterIndices.push_back(static_cast<uint16>(base + 2));
            resource.WaterIndices.push_back(static_cast<uint16>(base + 1));
            resource.WaterIndices.push_back(static_cast<uint16>(base + 3));
        }
    }
    if (!resource.WaterVertices.empty())
    {
        resource.HasWater = true;
        resource.WaterHeight = resource.WaterVertices.front().Y;
    }
}

std::shared_ptr<const TerrainCpuResource> BuildTerrainCpuResource(const std::filesystem::path& LNDPath, float tileSize)
{
    const auto data = ReadGameWorldFileBytes(LNDPath);
    if (data.size() < kLndHeaderBytes)
    {
        throw std::runtime_error("truncated LND header: " + LNDPath.string());
    }
    const auto VertexCount = static_cast<std::size_t>(Binary::U16LE(data, 4));
    if (VertexCount == 0 || VertexCount > 65535)
    {
        throw std::runtime_error("invalid LND vertex count: " + LNDPath.string());
    }
    const std::size_t TrianglesOffset = kLndHeaderBytes + VertexCount * kLndVertexBytes;
    Binary::RequireRange(data, kLndHeaderBytes, VertexCount * kLndVertexBytes, "LND vertices");
    if (TrianglesOffset > data.size() || (data.size() - TrianglesOffset) % kLndTriangleBytes != 0)
    {
        throw std::runtime_error("invalid LND triangle table: " + LNDPath.string());
    }
    const std::size_t TriangleCount = (data.size() - TrianglesOffset) / kLndTriangleBytes;
    if (TriangleCount == 0)
    {
        throw std::runtime_error("LND contains no triangles: " + LNDPath.string());
    }
    auto resource = std::make_shared<TerrainCpuResource>();
    resource->LNDPath = LNDPath;
    resource->TexturePath = LNDPath;
    resource->TexturePath.replace_extension(".dds");
    if (!std::filesystem::exists(resource->TexturePath))
    {
        throw std::runtime_error("required landscape texture is missing: " + resource->TexturePath.string());
    }
    resource->Vertices.reserve(VertexCount);
    resource->Positions.reserve(VertexCount);
    resource->Bounds.Min = FVector3{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    resource->Bounds.Max = FVector3{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (std::size_t i = 0; i < VertexCount; ++i)
    {
        const std::size_t offset = kLndHeaderBytes + i * kLndVertexBytes;
        const auto normal = NormalizeVector(FVector3{Binary::F32LE(data, offset + 12), Binary::F32LE(data, offset + 16), Binary::F32LE(data, offset + 20)});
        WorldVertex vertex{Binary::F32LE(data, offset), Binary::F32LE(data, offset + 4), Binary::F32LE(data, offset + 8), normal.X, normal.Y, normal.Z, 0xffffffff, Binary::F32LE(data, offset + 24), Binary::F32LE(data, offset + 28), Binary::F32LE(data, offset + 32), Binary::F32LE(data, offset + 36)};
        resource->Bounds.Min.X = (std::min)(resource->Bounds.Min.X, vertex.X);
        resource->Bounds.Min.Y = (std::min)(resource->Bounds.Min.Y, vertex.Y);
        resource->Bounds.Min.Z = (std::min)(resource->Bounds.Min.Z, vertex.Z);
        resource->Bounds.Max.X = (std::max)(resource->Bounds.Max.X, vertex.X);
        resource->Bounds.Max.Y = (std::max)(resource->Bounds.Max.Y, vertex.Y);
        resource->Bounds.Max.Z = (std::max)(resource->Bounds.Max.Z, vertex.Z);
        resource->Positions.push_back(FVector3{vertex.X, vertex.Y, vertex.Z});
        resource->Vertices.push_back(vertex);
    }
    resource->Indices.reserve(TriangleCount * 3);
    for (std::size_t i = 0; i < TriangleCount; ++i)
    {
        const std::size_t offset = TrianglesOffset + i * kLndTriangleBytes;
        const auto a = Binary::U16LE(data, offset);
        const auto b = Binary::U16LE(data, offset + 2);
        const auto c = Binary::U16LE(data, offset + 4);
        if (a >= VertexCount || b >= VertexCount || c >= VertexCount)
        {
            throw std::runtime_error("LND triangle references missing vertex: " + LNDPath.string());
        }
        resource->Indices.push_back(a);
        resource->Indices.push_back(b);
        resource->Indices.push_back(c);
    }
    BuildTerrainSurfaceGrid(*resource, tileSize);
    ReadWaterCpuMesh(*resource, LNDPath, tileSize);
    return resource;
}

std::shared_ptr<const TerrainCpuResource> LoadTerrainCpuResourceCached(const std::filesystem::path& LNDPath, float tileSize)
{
    const auto key = TerrainCpuCacheKey(LNDPath);
    {
        std::lock_guard<std::mutex> lock(TerrainCpuCacheMutex);
        if (auto it = TerrainCpuCache.find(key); it != TerrainCpuCache.end())
        {
            return it->second;
        }
    }
    auto built = BuildTerrainCpuResource(LNDPath, tileSize);
    std::lock_guard<std::mutex> lock(TerrainCpuCacheMutex);
    auto [it, inserted] = TerrainCpuCache.emplace(key, built);
    return inserted ? built : it->second;
}
}

std::unique_ptr<TerrainResource> FD3D9GameWorldScene::Impl::LoadTerrainResource(const std::filesystem::path& LNDPath)
{
    const auto cpu = LoadTerrainCpuResourceCached(LNDPath, Config.TileSize);
    auto resource = std::make_unique<TerrainResource>();
    resource->LNDPath = cpu->LNDPath;
    resource->TexturePath = cpu->TexturePath;
    resource->VertexCount = static_cast<UINT>(cpu->Vertices.size());
    resource->IndexCount = static_cast<UINT>(cpu->Indices.size());
    resource->positions = cpu->Positions;
    resource->Indices = cpu->Indices;
    resource->SurfaceGridResolution = cpu->SurfaceGridResolution;
    resource->SurfaceCells = cpu->SurfaceCells;
    resource->WaterVertexCount = static_cast<UINT>(cpu->WaterVertices.size());
    resource->WaterIndexCount = static_cast<UINT>(cpu->WaterIndices.size());
    resource->WaterHeight = cpu->WaterHeight;
    resource->HasWater = cpu->HasWater;
    resource->WaterCpuVerts = cpu->WaterVertices;
    resource->Bounds = cpu->Bounds;
    resource->texture = LoadCachedDdsTexture(resource->TexturePath);

    const UINT VertexBytes = static_cast<UINT>(cpu->Vertices.size() * sizeof(WorldVertex));
    HRESULT hr = Device->CreateVertexBuffer(VertexBytes, D3DUSAGE_WRITEONLY, kWorldVertexFvf, D3DPOOL_MANAGED, &resource->VertexBuffer, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateVertexBuffer terrain", hr));
    }
    void* VertexData = nullptr;
    hr = resource->VertexBuffer->Lock(0, VertexBytes, &VertexData, 0);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("TerrainVertexBuffer::Lock", hr));
    }
    CopyVectorBytes(VertexData, cpu->Vertices, VertexBytes);
    resource->VertexBuffer->Unlock();

    const UINT IndexBytes = static_cast<UINT>(cpu->Indices.size() * sizeof(uint16));
    hr = Device->CreateIndexBuffer(IndexBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &resource->IndexBuffer, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateIndexBuffer terrain", hr));
    }
    void* IndexData = nullptr;
    hr = resource->IndexBuffer->Lock(0, IndexBytes, &IndexData, 0);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("TerrainIndexBuffer::Lock", hr));
    }
    CopyVectorBytes(IndexData, cpu->Indices, IndexBytes);
    resource->IndexBuffer->Unlock();

    UploadWaterMesh(*resource, cpu->WaterIndices);
    return resource;
}

void FD3D9GameWorldScene::Impl::UploadWaterMesh(TerrainResource& resource, const std::vector<uint16>& indices)
{
    if (resource.WaterCpuVerts.empty() || indices.empty())
    {
        return;
    }
    const UINT vbytes = static_cast<UINT>(resource.WaterCpuVerts.size() * sizeof(WorldVertex));
    if (SUCCEEDED(Device->CreateVertexBuffer(vbytes, D3DUSAGE_WRITEONLY, kWorldVertexFvf, D3DPOOL_MANAGED, &resource.WaterVertexBuffer, nullptr)))
    {
        void* p = nullptr;
        if (SUCCEEDED(resource.WaterVertexBuffer->Lock(0, vbytes, &p, 0)))
        {
            CopyVectorBytes(p, resource.WaterCpuVerts, vbytes);
            resource.WaterVertexBuffer->Unlock();
        }
    }
    const UINT ibytes = static_cast<UINT>(indices.size() * sizeof(uint16));
    if (SUCCEEDED(Device->CreateIndexBuffer(ibytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &resource.WaterIndexBuffer, nullptr)))
    {
        void* p = nullptr;
        if (SUCCEEDED(resource.WaterIndexBuffer->Lock(0, ibytes, &p, 0)))
        {
            CopyVectorBytes(p, indices, ibytes);
            resource.WaterIndexBuffer->Unlock();
        }
    }
}

void FD3D9GameWorldScene::Impl::LoadVisibleTerrain()
{
    if (!WorldScene || !WorldScene->Map().Loaded)
    {
        throw std::runtime_error("world map grid is not loaded");
    }

    const FWorldMapGrid& map = WorldScene->Map();
    const int CenterRow = static_cast<int>(std::floor(SpawnX / Config.TileSize)) + Config.OriginRow;
    const int CenterColumn = Config.OriginColumn - static_cast<int>(std::floor(SpawnZ / Config.TileSize));

    if (CenterRow < 0 || CenterColumn < 0 || static_cast<uint32>(CenterRow) >= map.Height || static_cast<uint32>(CenterColumn) >= map.Width)
    {
        throw std::runtime_error("spawn coordinates are outside landscape map");
    }

    struct TerrainCandidate
    {
        int Row = 0;
        int Column = 0;
        int Distance = 0;
    };
    std::vector<TerrainCandidate> candidates;
    candidates.reserve(static_cast<std::size_t>((Config.VisibleRadius * 2 + 1) * (Config.VisibleRadius * 2 + 1)));
    for (int row = CenterRow - Config.VisibleRadius; row <= CenterRow + Config.VisibleRadius; ++row)
    {
        for (int column = CenterColumn - Config.VisibleRadius; column <= CenterColumn + Config.VisibleRadius; ++column)
        {
            candidates.push_back(TerrainCandidate{row, column, std::abs(row - CenterRow) + std::abs(column - CenterColumn)});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const TerrainCandidate& a, const TerrainCandidate& b)
    {
        if (a.Distance != b.Distance)
        {
            return a.Distance < b.Distance;
        }
        if (a.Row != b.Row)
        {
            return a.Row < b.Row;
        }
        return a.Column < b.Column;
    });

    TerrainInstances.clear();
    TerrainInstanceLookup.clear();
    TerrainInstanceLookup.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        const FWorldMapCell* cell = map.Find(candidate.Column, candidate.Row);

        if (!cell || !cell->Present)
        {
            continue;
        }

        try
        {
            const auto LNDPath = ResolveTerrainPath(*cell);
            const auto key = LNDPath.wstring();
            auto it = TerrainResources.find(key);

            if (it == TerrainResources.end())
            {
                it = TerrainResources.emplace(key, LoadTerrainResource(LNDPath)).first;
            }

            TerrainInstance instance{it->second.get(), static_cast<float>(candidate.Row - Config.OriginRow) * Config.TileSize, static_cast<float>(Config.OriginColumn - candidate.Column) * Config.TileSize};
            TerrainInstances.push_back(instance);
            TerrainInstanceLookup.emplace(TerrainTileKey(candidate.Row - Config.OriginRow, Config.OriginColumn - candidate.Column), instance);
        }
        catch (const std::exception& ex)
        {
            if (Logger)
            {
                Logger->Warning("skipped landscape tile " + std::to_string(candidate.Column) + "," + std::to_string(candidate.Row) + ": " + ex.what());
            }
        }
    }

    if (TerrainInstances.empty())
    {
        throw std::runtime_error("no landscape instances were loaded for spawn");
    }

    TerrainCenterRow = CenterRow;
    TerrainCenterColumn = CenterColumn;
}

void FD3D9GameWorldScene::Impl::PreloadTerrainForCenter(int CenterRow, int CenterColumn, int Radius)
{
    if (!WorldScene || !WorldScene->Map().Loaded || Radius < 0)
    {
        return;
    }
    const FWorldMapGrid& map = WorldScene->Map();
    for (int row = CenterRow - Radius; row <= CenterRow + Radius; ++row)
    {
        for (int column = CenterColumn - Radius; column <= CenterColumn + Radius; ++column)
        {
            if (row < 0 || column < 0 || static_cast<uint32>(row) >= map.Height || static_cast<uint32>(column) >= map.Width)
            {
                continue;
            }
            const FWorldMapCell* cell = map.Find(column, row);
            if (!cell || !cell->Present)
            {
                continue;
            }
            try
            {
                const auto LNDPath = ResolveTerrainPath(*cell);
                const auto key = LNDPath.wstring();
                if (TerrainResources.find(key) == TerrainResources.end())
                {
                    TerrainResources.emplace(key, LoadTerrainResource(LNDPath));
                }
            }
            catch (const std::exception& ex)
            {
                if (Logger)
                {
                    Logger->Warning("terrain guard preload skipped " + std::to_string(column) + "," + std::to_string(row) + ": " + ex.what());
                }
            }
        }
    }
}

void FD3D9GameWorldScene::Impl::PreloadStreamingGuard()
{
    if (!WorldScene || !WorldScene->Map().Loaded || Config.TileSize <= 0.0f)
    {
        return;
    }
    const int CenterRow = static_cast<int>(std::floor(SpawnX / Config.TileSize)) + Config.OriginRow;
    const int CenterColumn = Config.OriginColumn - static_cast<int>(std::floor(SpawnZ / Config.TileSize));
    const int TileX = static_cast<int>(std::floor(SpawnX / Config.TileSize));
    const int TileZ = static_cast<int>(std::floor(SpawnZ / Config.TileSize));
    const float LocalX = SpawnX - static_cast<float>(TileX) * Config.TileSize;
    const float LocalZ = SpawnZ - static_cast<float>(TileZ) * Config.TileSize;
    const float Low = Config.TileSize * 0.35f;
    const float High = Config.TileSize * 0.65f;
    int RowStep = 0;
    int ColumnStep = 0;
    if (LocalX >= High || VelocityX > Config.WalkSpeed * 0.75f)
    {
        RowStep = 1;
    }
    else if (LocalX <= Low || VelocityX < -Config.WalkSpeed * 0.75f)
    {
        RowStep = -1;
    }
    if (LocalZ >= High || VelocityZ > Config.WalkSpeed * 0.75f)
    {
        ColumnStep = -1;
    }
    else if (LocalZ <= Low || VelocityZ < -Config.WalkSpeed * 0.75f)
    {
        ColumnStep = 1;
    }
    if (CenterRow == StreamingGuardRow && CenterColumn == StreamingGuardColumn && RowStep == StreamingGuardRowStep && ColumnStep == StreamingGuardColumnStep)
    {
        return;
    }
    StreamingGuardRow = CenterRow;
    StreamingGuardColumn = CenterColumn;
    StreamingGuardRowStep = RowStep;
    StreamingGuardColumnStep = ColumnStep;
    const int GuardRadius = Config.VisibleRadius + 1;
    PreloadTerrainForCenter(CenterRow, CenterColumn, GuardRadius);
    if (RowStep != 0)
    {
        PreloadTerrainForCenter(CenterRow + RowStep, CenterColumn, GuardRadius);
    }
    if (ColumnStep != 0)
    {
        PreloadTerrainForCenter(CenterRow, CenterColumn + ColumnStep, GuardRadius);
    }
    if (RowStep != 0 && ColumnStep != 0)
    {
        PreloadTerrainForCenter(CenterRow + RowStep, CenterColumn + ColumnStep, GuardRadius);
    }
    const float StaticGuardRadius = Config.StaticObjectRadius + Config.TileSize;
    PreloadStaticResourcesAround(SpawnX, SpawnZ, StaticGuardRadius);
}

void FD3D9GameWorldScene::PrewarmCpuCaches(const FResourceManager& resources, const FWorldScene& world, const FGameWorldConfig& config, float spawnX, float spawnZ, FLogger* logger)
{
    try
    {
        Impl resolver;
        resolver.AssetResources = &resources;
        resolver.WorldScene = &world;
        resolver.Config = config;
        resolver.SpawnX = spawnX;
        resolver.SpawnZ = spawnZ;
        const FWorldMapGrid& map = world.Map();
        if (!map.Loaded)
        {
            return;
        }
        const int CenterRow = static_cast<int>(std::floor(spawnX / config.TileSize)) + config.OriginRow;
        const int CenterColumn = config.OriginColumn - static_cast<int>(std::floor(spawnZ / config.TileSize));
        std::size_t loaded = 0;
        for (int row = CenterRow - config.VisibleRadius; row <= CenterRow + config.VisibleRadius; ++row)
        {
            for (int column = CenterColumn - config.VisibleRadius; column <= CenterColumn + config.VisibleRadius; ++column)
            {
                const FWorldMapCell* cell = map.Find(column, row);
                if (!cell || !cell->Present)
                {
                    continue;
                }
                try
                {
                    LoadTerrainCpuResourceCached(resolver.ResolveTerrainPath(*cell), config.TileSize);
                    ++loaded;
                }
                catch (const std::exception& ex)
                {
                    if (logger)
                    {
                        logger->Warning("terrain CPU prewarm skipped " + std::to_string(column) + "," + std::to_string(row) + ": " + ex.what());
                    }
                }
            }
        }
        if (logger)
        {
            logger->Info("terrain CPU cache prewarmed: tiles=" + std::to_string(loaded));
        }
    }
    catch (const std::exception& ex)
    {
        if (logger)
        {
            logger->Warning(std::string("terrain CPU prewarm failed: ") + ex.what());
        }
    }
}

bool FD3D9GameWorldScene::Impl::TerrainHeightAt(float WorldX, float WorldZ, float ReferenceY, float& OutHeight) const
{
    const int tileX = TerrainTileCoord(WorldX, Config.TileSize);
    const int tileZ = TerrainTileCoord(WorldZ, Config.TileSize);
    if (auto it = TerrainInstanceLookup.find(TerrainTileKey(tileX, tileZ)); it != TerrainInstanceLookup.end() && TerrainHeightAtInstance(it->second, Config.TileSize, WorldX, WorldZ, ReferenceY, OutHeight))
    {
        return true;
    }

    float BestHeight = ReferenceY;
    float BestDistance = std::numeric_limits<float>::max();
    for (const auto& instance : TerrainInstances)
    {
        float height = 0.0f;
        if (!TerrainHeightAtInstance(instance, Config.TileSize, WorldX, WorldZ, ReferenceY, height))
        {
            continue;
        }
        const float distance = std::abs(height - ReferenceY);
        if (distance < BestDistance)
        {
            BestDistance = distance;
            BestHeight = height;
        }
    }
    if (BestDistance < std::numeric_limits<float>::max())
    {
        OutHeight = BestHeight;
        return true;
    }
    return false;
}
bool FD3D9GameWorldScene::Impl::TerrainSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const
{
    const int tileX = TerrainTileCoord(WorldX, Config.TileSize);
    const int tileZ = TerrainTileCoord(WorldZ, Config.TileSize);
    if (auto it = TerrainInstanceLookup.find(TerrainTileKey(tileX, tileZ)); it != TerrainInstanceLookup.end() && TerrainSurfaceAtInstance(it->second, Config.TileSize, WorldX, WorldZ, OutHeight, OutNormal))
    {
        return true;
    }

    float BestHeight = -std::numeric_limits<float>::max();
    FVector3 BestNormal{};
    bool found = false;
    for (const auto& instance : TerrainInstances)
    {
        float height = 0.0f;
        FVector3 normal{};
        if (!TerrainSurfaceAtInstance(instance, Config.TileSize, WorldX, WorldZ, height, normal))
        {
            continue;
        }
        if (!found || height > BestHeight)
        {
            BestHeight = height;
            BestNormal = normal;
            found = true;
        }
    }
    if (!found)
    {
        return false;
    }
    OutHeight = BestHeight;
    OutNormal = BestNormal;
    return true;
}
bool FD3D9GameWorldScene::Impl::FlatGrassSurfaceAt(float WorldX, float WorldZ, float& OutHeight, FVector3& OutNormal) const
{
    float CenterHeight = 0.0f;
    FVector3 CenterNormal{};
    if (!TerrainSurfaceAt(WorldX, WorldZ, CenterHeight, CenterNormal) ||
    std::abs(CenterNormal.Y) < Config.GrassFlatnessNormalY)
    {
        return false;
    }

    const float Radius = Config.GrassFlatnessRadius;
    const float Diagonal = Radius * 0.70710678f;
    const std::array<FVector3, 8> Offsets
    {{
        {Radius, 0.0f, 0.0f},
        {-Radius, 0.0f, 0.0f},
        {0.0f, 0.0f, Radius},
        {0.0f, 0.0f, -Radius},
        {Diagonal, 0.0f, Diagonal},
        {-Diagonal, 0.0f, Diagonal},
        {Diagonal, 0.0f, -Diagonal},
        {-Diagonal, 0.0f, -Diagonal}
    }};

    for (const auto& Offset : Offsets)
    {
        float Height = 0.0f;
        FVector3 Normal{};
        if (!TerrainSurfaceAt(WorldX + Offset.X, WorldZ + Offset.Z, Height, Normal))
        {
            return false;
        }
        const float ExpectedHeight = CenterHeight - (CenterNormal.X * Offset.X + CenterNormal.Z * Offset.Z) / CenterNormal.Y;
        if (std::abs(Height - ExpectedHeight) > Config.GrassFlatnessThreshold)
        {
            return false;
        }
    }

    OutHeight = CenterHeight;
    OutNormal = CenterNormal;
    return true;
}

void FD3D9GameWorldScene::Impl::DrawTerrain()
{
    // Terrain stays fixed-function: it needs the tile texture (uv0) modulated
    // by the microtexture detail layer on a separate finer UV (uv1). The base
    // shader samples only one texture/UV, so routing terrain through it lost
    // the detail and made the ground a smeared single-tile texture. A faithful
    // 2-UV terrain shader is a later step.
    Device->SetFVF(kWorldVertexFvf);
    Device->SetTexture(1, TerrainMicrotexture);
    Device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    Device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    Device->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    Device->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    Device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    for (const auto& instance : TerrainInstances)
    {
        const auto* resource = instance.resource;
        if (!resource || !resource->VertexBuffer || !resource->IndexBuffer)
        {
            continue;
        }
        if (!IsBoundsVisibleToCamera(TranslatedTerrainBounds(instance), Config.TileSize * 0.1f))
        {
            continue;
        }
        const auto world = TranslationMatrix(instance.OriginX, 0.0f, instance.OriginZ);
        Device->SetTransform(D3DTS_WORLD, &world);
        Device->SetStreamSource(0, resource->VertexBuffer, 0, sizeof(WorldVertex));
        Device->SetIndices(resource->IndexBuffer);
        Device->SetTexture(0, resource->texture);
        const UINT triangleCount = resource->IndexCount / 3;
        Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, resource->VertexCount, 0, triangleCount);
        RecordWorldDraw(triangleCount, EGameWorldDrawBucket::Terrain);
    }
    Device->SetTexture(1, nullptr);
    Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

bool FD3D9GameWorldScene::Impl::WaterPlane(float& OutY) const
{
    bool found = false;
    float BestD2 = 0.0f;
    const float half = Config.TileSize * 0.5f;
    for (const auto& instance : TerrainInstances)
    {
        if (!instance.resource->HasWater)
        {
            continue;
        }
        const float cx = instance.OriginX + half;
        const float cz = instance.OriginZ + half;
        const float dx = cx - SpawnX;
        const float dz = cz - SpawnZ;
        const float d2 = dx * dx + dz * dz;
        if (!found || d2 < BestD2)
        {
            BestD2 = d2;
            OutY = instance.resource->WaterHeight;
            found = true;
        }
    }
    return found;
}

float FD3D9GameWorldScene::Impl::WaterReflectCoeff() const
{
    const float t = GameTimeFraction;
    float grad = 0.0f;
    float mult = 0.0f;
    if (t >= Config.WaterDayStart && t <= Config.WaterDayEnd)
    {
        grad = 0.0f;  // full day → no reflection
        mult = 0.0f;
    } else if (t <= Config.WaterNightBefore || t >= Config.WaterNightAfter)
    {
        grad = 1.0f;  // deep night
        mult = Config.WaterReflectNight;
    } else if (t < Config.WaterDayStart)
    {
        grad = 1.0f - (t - Config.WaterNightBefore) / Config.WaterTransitionWidth;  // dawn ramp
        mult = Config.WaterReflectTransition;
    } else
    {
        grad = (t - Config.WaterDayEnd) / Config.WaterTransitionWidth;  // dusk ramp
        mult = Config.WaterReflectTransition;
    }
    return std::clamp(grad, 0.0f, 1.0f) * mult;
}

void FD3D9GameWorldScene::Impl::RenderReflection()
{
    if (Config.WaterReflectionEnabled == 0 || !ReflectionSurface || !ReflectionDepth || WaterReflectCoeff() <= 0.01f)
    {
        return;
    }
    float WaterY = 0.0f;
    if (!WaterPlane(WaterY))
    {
        return;
    }
    IDirect3DSurface9* PrevRt = nullptr;
    IDirect3DSurface9* PrevDepth = nullptr;
    Device->GetRenderTarget(0, &PrevRt);
    Device->GetDepthStencilSurface(&PrevDepth);
    Device->SetRenderTarget(0, ReflectionSurface);
    Device->SetDepthStencilSurface(ReflectionDepth);
    Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
    D3DCOLOR_XRGB(EnvironmentClearRed, EnvironmentClearGreen, EnvironmentClearBlue),
    1.0f, 0);

    // Mirror the camera across y = water_y (world +Y is down, so the up vector
    // flips from -Y to +Y).
    const FVector3 eye{CameraEye.X, 2.0f * WaterY - CameraEye.Y, CameraEye.Z};
    const FVector3 target{CameraTarget.X, 2.0f * WaterY - CameraTarget.Y, CameraTarget.Z};
    const D3DMATRIX SavedView = ViewMatrix;
    const D3DMATRIX SavedVp = ViewProjectionMatrix;
    const auto view = LookAtRhMatrix(eye, target, FVector3{0.0f, 1.0f, 0.0f});
    ViewMatrix = view;
    ViewProjectionMatrix = MultiplyMatrix(view, ProjectionMatrix);
    Device->SetTransform(D3DTS_VIEW, &view);
    // Reflection reverses triangle winding; force two-sided so nothing vanishes.
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    RenderingReflection = true;
    DrawSky();
    DrawTerrain();
    DrawStaticObjects();
    RenderingReflection = false;
    ReflectionTextureReady = true;

    // Restore the main camera + back buffer.
    ViewMatrix = SavedView;
    ViewProjectionMatrix = SavedVp;
    Device->SetTransform(D3DTS_VIEW, &SavedView);
    Device->SetRenderTarget(0, PrevRt);
    Device->SetDepthStencilSurface(PrevDepth);
    SafeRelease(PrevRt);
    SafeRelease(PrevDepth);
    ConfigureRenderState();
}

void FD3D9GameWorldScene::Impl::UpdateWaterWaves(TerrainResource* resource, float OriginX, float OriginZ)
{
    if (!resource->WaterVertexBuffer || resource->WaterCpuVerts.empty() ||
    Config.WaveScale <= 0.0f)
    {
        return;
    }
    const float kx = Config.WaveFreqX / Config.WaveCellStep;
    const float kz = Config.WaveFreqZ / Config.WaveCellStep;
    const float TimePhase = ElapsedSeconds * Config.WaveSpeed;
    void* p = nullptr;
    if (FAILED(resource->WaterVertexBuffer->Lock(0, 0, &p, 0)))
    {
        return;
    }
    auto* out = static_cast<WorldVertex*>(p);
    for (std::size_t i = 0; i < resource->WaterCpuVerts.size(); ++i)
    {
        WorldVertex v = resource->WaterCpuVerts[i];
        const float phase = (OriginX + v.X) * kx + (OriginZ + v.Z) * kz + TimePhase;
        v.Y = resource->WaterHeight + (std::sin(phase) + Config.WaveAmp) * Config.WaveScale;
        out[i] = v;
    }
    resource->WaterVertexBuffer->Unlock();
}

void FD3D9GameWorldScene::Impl::DrawWater()
{
    bool any = false;
    for (const auto& instance : TerrainInstances)
    {
        if (instance.resource && instance.resource->WaterIndexCount > 0 && instance.resource->WaterVertexBuffer && instance.resource->WaterIndexBuffer && IsBoundsVisibleToCamera(TranslatedTerrainBounds(instance), Config.TileSize * 0.1f))
        {
            any = true;
            break;
        }
    }
    if (!any || !WaterTexture)
    {
        return;
    }
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetFVF(kWorldVertexFvf);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);   // blend over the scene, don't occlude
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(0xB0, 0xFF, 0xFF, 0xFF));
    // Project the reflection only when the eye is ABOVE the water surface (world
    // +Y is down → above means a smaller Y than the plane). From below/inside the
    // water a sky reflection is meaningless and, on the plane's back face, appears
    // to swing as the camera turns — so fall back to the flat texture there.
    // Reflection is gated only by the REFLQUAL graphics option (native:
    // FUN_0046a070 does the reflection pass when 0 < DAT_04f49a9c) + a IsValid RT.
    const float ReflectionCoeff = WaterReflectCoeff();
    const bool UseReflection =
    Config.WaterReflectionEnabled != 0 && ReflectionTexture != nullptr && ReflectionTextureReady && ReflectionCoeff > 0.01f;
    if (UseReflection)
    {
        // Project the planar-reflection RT onto the surface: texcoord = the
        // surface point's own screen Position (the RT holds the mirrored scene
        // at those same screen pixels). texMtx maps view-space pos → clip → [0,1].
        const D3DMATRIX bias = {
            0.5f,  0.0f,  0.0f, 0.0f,
            0.0f, -0.5f,  0.0f, 0.0f,
            0.0f,  0.0f,  1.0f, 0.0f,
            0.5f,  0.5f,  0.0f, 1.0f,
        };
        const D3DMATRIX TexMTX = MultiplyMatrix(ProjectionMatrix, bias);
        Device->SetTransform(D3DTS_TEXTURE0, &TexMTX);
        Device->SetTexture(0, ReflectionTexture);
        Device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        Device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT4 | D3DTTFF_PROJECTED);
        Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        // Blend reflection ↔ the water's own colour by the time-of-day reflect
        // coefficient (decoded FUN_004db5e0): result = k*reflection + (1-k)*base.
        // Base colour = the environment sky/clear colour (data-driven, Sky.txt) —
        // a stand-in for the native Fresnel gradient until step 4 (the shader).
        const int k = std::clamp(static_cast<int>(ReflectionCoeff * 255.0f + 0.5f), 0, 255);
        Device->SetTextureStageState(0, D3DTSS_CONSTANT,
        D3DCOLOR_ARGB(k, EnvironmentClearRed, EnvironmentClearGreen, EnvironmentClearBlue));
        Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_LERP);
        Device->SetTextureStageState(0, D3DTSS_COLORARG0, D3DTA_CONSTANT | D3DTA_ALPHAREPLICATE);  // k
        Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);                          // reflection
        Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CONSTANT);                         // base water colour
        Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);  // constant translucency
        Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    } else
    {
        Device->SetTexture(0, WaterTexture);
        Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);  // constant translucency
        Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    }
    for (const auto& instance : TerrainInstances)
    {
        auto* resource = instance.resource;
        if (!resource || resource->WaterIndexCount == 0 || !resource->WaterVertexBuffer || !resource->WaterIndexBuffer)
        {
            continue;
        }
        if (!IsBoundsVisibleToCamera(TranslatedTerrainBounds(instance), Config.TileSize * 0.1f))
        {
            continue;
        }
        UpdateWaterWaves(resource, instance.OriginX, instance.OriginZ);
        const auto world = TranslationMatrix(instance.OriginX, 0.0f, instance.OriginZ);
        Device->SetTransform(D3DTS_WORLD, &world);
        Device->SetStreamSource(0, resource->WaterVertexBuffer, 0, sizeof(WorldVertex));
        Device->SetIndices(resource->WaterIndexBuffer);
        const UINT triangleCount = resource->WaterIndexCount / 3;
        Device->DrawIndexedPrimitive(
        D3DPT_TRIANGLELIST, 0, 0, resource->WaterVertexCount, 0, triangleCount);
        RecordWorldDraw(triangleCount, EGameWorldDrawBucket::Water);
    }
    if (UseReflection)
    {
        // Undo the projective texture transform so later passes are unaffected.
        Device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        Device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    }
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    Device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    ConfigureRenderState();  // restore the shared (shader-era) render state
}
