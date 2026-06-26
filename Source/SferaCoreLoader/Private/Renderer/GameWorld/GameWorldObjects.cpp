#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include "Config/ConfigDocument.h"


namespace
{
void LowerStaticWorkerPriority()
{
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

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
    bool IsSkinned = false;
    FMdlMesh BindMesh;
};


uint64 StaticRenderCellKey(int CellX, int CellZ)
{
    return (static_cast<uint64>(static_cast<uint32>(CellX)) << 32) | static_cast<uint32>(CellZ);
}

int StaticRenderCellCoord(float Value, float CellSize)
{
    return static_cast<int>(std::floor(Value / CellSize));
}

uint64 StaticRenderCellKeyForPoint(float X, float Z, float CellSize)
{
    return StaticRenderCellKey(StaticRenderCellCoord(X, CellSize), StaticRenderCellCoord(Z, CellSize));
}

std::pair<int, int> StaticRenderCellRange(float Center, float Radius, float CellSize)
{
    return {StaticRenderCellCoord(Center - Radius, CellSize), StaticRenderCellCoord(Center + Radius, CellSize)};
}

constexpr float kModelCollisionCellSize = 8.0f;

bool BoundsIntersectsArea2D(const FBox3& bounds, const FBox2& area)
{
    return area.IsValid() && bounds.Min.X <= area.Max.X && bounds.Max.X >= area.Min.X && bounds.Min.Z <= area.Max.Y && bounds.Max.Z >= area.Min.Y;
}

bool AreaContainsArea(const FBox2& outer, const FBox2& inner)
{
    return outer.IsValid() && inner.IsValid() && inner.Min.X >= outer.Min.X && inner.Max.X <= outer.Max.X && inner.Min.Y >= outer.Min.Y && inner.Max.Y <= outer.Max.Y;
}

bool TriangleIntersectsArea2D(const PreparedModelCollisionTriangle& tri, const FBox2& area)
{
    return area.IsValid() && tri.Bounds.Min.X <= area.Max.X && tri.Bounds.Max.X >= area.Min.X && tri.Bounds.Min.Z <= area.Max.Y && tri.Bounds.Max.Z >= area.Min.Y;
}

constexpr float kCollisionWorkerActiveRadius = 14.0f;
constexpr float kCollisionWorkerRefreshDistance = 3.5f;

FBox3 EmptyBounds()
{
    FBox3 Bounds;
    Bounds.Min = FVector3{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Bounds.Max = FVector3{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    return Bounds;
}

void ExpandBounds(FBox3& Bounds, const FVector3& Point)
{
    Bounds.Min.X = (std::min)(Bounds.Min.X, Point.X);
    Bounds.Min.Y = (std::min)(Bounds.Min.Y, Point.Y);
    Bounds.Min.Z = (std::min)(Bounds.Min.Z, Point.Z);
    Bounds.Max.X = (std::max)(Bounds.Max.X, Point.X);
    Bounds.Max.Y = (std::max)(Bounds.Max.Y, Point.Y);
    Bounds.Max.Z = (std::max)(Bounds.Max.Z, Point.Z);
}

bool BoundsInitialized(const FBox3& Bounds)
{
    return Bounds.Min.X <= Bounds.Max.X && Bounds.Min.Y <= Bounds.Max.Y && Bounds.Min.Z <= Bounds.Max.Z;
}

WorldVertex TransformWorldVertex(WorldVertex vertex, const D3DMATRIX& world)
{
    const FVector3 position{vertex.X, vertex.Y, vertex.Z};
    const FVector3 normal{vertex.NX, vertex.NY, vertex.NZ};
    vertex.X = position.X * world._11 + position.Y * world._21 + position.Z * world._31 + world._41;
    vertex.Y = position.X * world._12 + position.Y * world._22 + position.Z * world._32 + world._42;
    vertex.Z = position.X * world._13 + position.Y * world._23 + position.Z * world._33 + world._43;
    vertex.NX = normal.X * world._11 + normal.Y * world._21 + normal.Z * world._31;
    vertex.NY = normal.X * world._12 + normal.Y * world._22 + normal.Z * world._32;
    vertex.NZ = normal.X * world._13 + normal.Y * world._23 + normal.Z * world._33;
    return vertex;
}

void ReleaseWorldRenderBatches(std::vector<WorldRenderBatch>& Batches)
{
    for (auto& batch : Batches)
    {
        SafeRelease(batch.IndexBuffer);
        SafeRelease(batch.VertexBuffer);
    }
    Batches.clear();
}

void ReleaseWorldRenderBatchMap(std::unordered_map<uint64, std::vector<WorldRenderBatch>>& Batches)
{
    for (auto& [_, list] : Batches)
    {
        ReleaseWorldRenderBatches(list);
    }
    Batches.clear();
}

template<class F>
void ParallelFor(std::size_t Count, std::size_t ThreadCount, F&& Body)
{
    if (ThreadCount <= 1)
    {
        for (std::size_t index = 0; index < Count; ++index)
        {
            Body(index);
        }
        return;
    }
    std::atomic_size_t next{0};
    std::vector<std::thread> workers;
    workers.reserve(ThreadCount);
    for (std::size_t threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        workers.emplace_back([&]()
        {
            for (std::size_t index = next.fetch_add(1, std::memory_order_relaxed); index < Count; index = next.fetch_add(1, std::memory_order_relaxed))
            {
                Body(index);
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
}

struct AccumulatedWorldBatch
{
    std::vector<WorldVertex> Vertices;
    std::vector<uint32> Indices;
    FBox3 Bounds = EmptyBounds();
};

using AccumulatedWorldBatchMap = std::unordered_map<IDirect3DTexture9*, AccumulatedWorldBatch>;

void AccumulateWorldBatches(const StaticModelResource& Resource, const D3DMATRIX& World, AccumulatedWorldBatchMap& Batches, bool GrassWind = false, DWORD Tint = 0xfffffffful)
{
    if (Resource.CpuVertices.empty() || Resource.CpuIndices.empty())
    {
        return;
    }
    constexpr uint32 InvalidRemap = (std::numeric_limits<uint32>::max)();
    const float ModelHeight = Resource.Bounds.Max.Y - Resource.Bounds.Min.Y;
    const float InvModelHeight = ModelHeight > 0.0001f ? 1.0f / ModelHeight : 0.0f;
    const float RootWorldY = Resource.Bounds.Max.Y * World._22 + World._42;
    std::vector<uint32> remap(Resource.CpuVertices.size(), InvalidRemap);
    std::vector<uint16> touched;
    for (const auto& sourceBatch : Resource.Batches)
    {
        auto& output = Batches[sourceBatch.Texture];
        const uint32 endIndex = (std::min)(sourceBatch.StartIndex + sourceBatch.IndexCount, static_cast<UINT>(Resource.CpuIndices.size()));
        output.Indices.reserve(output.Indices.size() + sourceBatch.IndexCount);
        touched.clear();
        for (uint32 index = sourceBatch.StartIndex; index < endIndex; ++index)
        {
            const uint16 sourceIndex = Resource.CpuIndices[index];
            if (sourceIndex >= Resource.CpuVertices.size())
            {
                continue;
            }
            uint32& mappedIndex = remap[sourceIndex];
            if (mappedIndex == InvalidRemap)
            {
                const auto& source = Resource.CpuVertices[sourceIndex];
                WorldVertex vertex = TransformWorldVertex(source, World);
                if (GrassWind)
                {
                    vertex.DetailU = std::clamp((Resource.Bounds.Max.Y - source.Y) * InvModelHeight, 0.0f, 1.0f);
                    vertex.DetailV = RootWorldY;
                    vertex.Diffuse = Tint;
                }
                ExpandBounds(output.Bounds, FVector3{vertex.X, vertex.Y, vertex.Z});
                mappedIndex = static_cast<uint32>(output.Vertices.size());
                output.Vertices.push_back(vertex);
                touched.push_back(sourceIndex);
            }
            output.Indices.push_back(mappedIndex);
        }
        for (const uint16 sourceIndex : touched)
        {
            remap[sourceIndex] = InvalidRemap;
        }
    }
}

bool UploadWorldBatches(IDirect3DDevice9* Device, AccumulatedWorldBatchMap& Source, std::vector<WorldRenderBatch>& Output)
{
    for (auto& [texture, source] : Source)
    {
        if (source.Vertices.empty() || source.Indices.empty() || !BoundsInitialized(source.Bounds))
        {
            continue;
        }
        WorldRenderBatch batch;
        batch.Texture = texture;
        batch.Bounds = source.Bounds;
        batch.VertexCount = static_cast<UINT>(source.Vertices.size());
        batch.IndexCount = static_cast<UINT>(source.Indices.size());
        if (!TryCreateManagedVertexBuffer(Device, source.Vertices, kWorldVertexFvf, batch.VertexBuffer) || !TryCreateManagedIndexBuffer(Device, source.Indices, D3DFMT_INDEX32, batch.IndexBuffer))
        {
            SafeRelease(batch.IndexBuffer);
            SafeRelease(batch.VertexBuffer);
            continue;
        }
        Output.push_back(batch);
    }
    return !Output.empty();
}

struct FXorShift32
{
    uint32 State = 0;

    uint32 Next()
    {
        State ^= State << 13;
        State ^= State >> 17;
        State ^= State << 5;
        return State;
    }

    float Unit()
    {
        return static_cast<float>(Next() & 0xffffU) / 65535.0f;
    }
};

std::mutex StaticModelCpuCacheMutex;
std::unordered_map<std::string, std::shared_ptr<const StaticModelCpuResource>> StaticModelCpuCache;

std::string StaticModelCpuCacheKey(const std::filesystem::path& path)
{
    return Common::NormalizePathKey(path.lexically_normal().generic_string());
}

int FindNpcAnimationClip(const FResourceManager* Resources, const std::string& ModelName, std::string_view Key)
{
    if (!Resources || ModelName.empty())
    {
        return -1;
    }
    auto LoadConfig = [&](const std::string& Name)
    {
        return Resources->Load("params/" + Name + ".cfg");
    };
    auto Blob = LoadConfig(ModelName);
    std::string ConfigName = ModelName;
    if (!Blob.IsOk())
    {
        ConfigName = LowercaseAscii(ModelName);
        Blob = LoadConfig(ConfigName);
    }
    if (!Blob.IsOk())
    {
        return -1;
    }
    FConfigDocument Config;
    std::string Text(Blob.Value().Bytes.begin(), Blob.Value().Bytes.end());
    if (!Config.Parse(std::move(Text), "params/" + ConfigName + ".cfg").IsOk())
    {
        return -1;
    }
    const auto Value = Config.FindInt(Key);
    return Value ? static_cast<int>(*Value) : -1;
}

void ConfigureNpcAnimationClips(StaticModelResource& Resource, const FResourceManager* Resources, const std::string& ModelName)
{
    if (!Resource.IsSkinned)
    {
        return;
    }
    int Start = 0;
    for (const auto Count : Resource.BindMesh.Actions)
    {
        Resource.ClipStart.push_back(Start);
        Resource.ClipLength.push_back(static_cast<int>(Count));
        Start += static_cast<int>(Count);
    }
    if (Resource.ClipLength.empty() && Resource.FrameCount > 1)
    {
        Resource.ClipStart.push_back(0);
        Resource.ClipLength.push_back(Resource.FrameCount);
    }
    auto ValidClip = [&](int Clip)
    {
        return Clip >= 0 && static_cast<std::size_t>(Clip) < Resource.ClipLength.size() && Resource.ClipLength[static_cast<std::size_t>(Clip)] > 0 && Resource.ClipStart[static_cast<std::size_t>(Clip)] + Resource.ClipLength[static_cast<std::size_t>(Clip)] <= Resource.FrameCount;
    };
    const int FreeClip = FindNpcAnimationClip(Resources, ModelName, "FREE");
    if (ValidClip(FreeClip))
    {
        Resource.IdleClip = FreeClip;
    }
    else if (!Resource.ClipLength.empty() && ValidClip(0))
    {
        Resource.IdleClip = 0;
    }
    for (const auto Key : {"FREE1", "FREE2", "FREE3"})
    {
        const int Clip = FindNpcAnimationClip(Resources, ModelName, Key);
        if (ValidClip(Clip))
        {
            Resource.GestureClips.push_back(Clip);
        }
    }
    Resource.CurrentClip = Resource.IdleClip;
}


D3DMATRIX MdlQuatTranslationMatrix(float W, float X, float Y, float Z, float TX, float TY, float TZ)
{
    const float XX = X * X;
    const float YY = Y * Y;
    const float ZZ = Z * Z;
    const float XY = X * Y;
    const float XZ = X * Z;
    const float YZ = Y * Z;
    const float WX = W * X;
    const float WY = W * Y;
    const float WZ = W * Z;
    D3DMATRIX Matrix{};
    Matrix._11 = 1.0f - 2.0f * (YY + ZZ);
    Matrix._12 = 2.0f * (WZ + XY);
    Matrix._13 = 2.0f * (XZ - WY);
    Matrix._14 = TX;
    Matrix._21 = 2.0f * (XY - WZ);
    Matrix._22 = 1.0f - 2.0f * (XX + ZZ);
    Matrix._23 = 2.0f * (YZ + WX);
    Matrix._24 = TY;
    Matrix._31 = 2.0f * (WY + XZ);
    Matrix._32 = 2.0f * (YZ - WX);
    Matrix._33 = 1.0f - 2.0f * (XX + YY);
    Matrix._34 = TZ;
    Matrix._44 = 1.0f;
    return Matrix;
}

void RecomputeMdlBounds(FMdlMesh& Mesh)
{
    if (Mesh.Vertices.empty())
    {
        Mesh.Bounds = FMdlBounds{};
        return;
    }
    Mesh.Bounds.MinX = Mesh.Bounds.MaxX = Mesh.Vertices.front().X;
    Mesh.Bounds.MinY = Mesh.Bounds.MaxY = Mesh.Vertices.front().Y;
    Mesh.Bounds.MinZ = Mesh.Bounds.MaxZ = Mesh.Vertices.front().Z;
    for (const auto& Vertex : Mesh.Vertices)
    {
        Mesh.Bounds.MinX = (std::min)(Mesh.Bounds.MinX, Vertex.X);
        Mesh.Bounds.MaxX = (std::max)(Mesh.Bounds.MaxX, Vertex.X);
        Mesh.Bounds.MinY = (std::min)(Mesh.Bounds.MinY, Vertex.Y);
        Mesh.Bounds.MaxY = (std::max)(Mesh.Bounds.MaxY, Vertex.Y);
        Mesh.Bounds.MinZ = (std::min)(Mesh.Bounds.MinZ, Vertex.Z);
        Mesh.Bounds.MaxZ = (std::max)(Mesh.Bounds.MaxZ, Vertex.Z);
    }
}

bool ApplyMdlRestPose(FMdlMesh& Mesh, int Frame = 0)
{
    const std::size_t BoneCount = Mesh.Objects.size();
    if (Mesh.Info.SkinWeightCount == 0 || BoneCount == 0 || Mesh.TransformKeys.empty())
    {
        return false;
    }
    auto BoneLocal = [&](std::size_t Bone, D3DMATRIX& Out) -> bool
    {
        const auto& Object = Mesh.Objects[Bone];
        FMdlTransformKey Key;
        if (Object.IsAnimated == 0)
        {
            const std::size_t KeyIndex = static_cast<std::size_t>(Object.KeyIndex);
            if (KeyIndex >= Mesh.TransformKeys.size())
            {
                return false;
            }
            Key = Mesh.TransformKeys[KeyIndex];
        }
        else
        {
            const std::size_t Index = static_cast<std::size_t>(Object.KeyIndex) + static_cast<std::size_t>(Frame);
            if (Index >= Mesh.SkinIndices.size())
            {
                return false;
            }
            const auto& Entry = Mesh.SkinIndices[Index];
            const std::size_t Record = Entry.Record;
            if (Record >= Mesh.TransformKeys.size())
            {
                return false;
            }
            const auto& A = Mesh.TransformKeys[Record];
            if (Entry.Blend == 0 || Entry.Blend == 0xff || Record + 1 >= Mesh.TransformKeys.size())
            {
                Key = A;
            }
            else
            {
                const float T = static_cast<float>(Entry.Blend) / 255.0f;
                const auto& B = Mesh.TransformKeys[Record + 1];
                Key.X = A.X + (B.X - A.X) * T;
                Key.Y = A.Y + (B.Y - A.Y) * T;
                Key.Z = A.Z + (B.Z - A.Z) * T;
                const float DotQuat = A.QW * B.QW + A.QX * B.QX + A.QY * B.QY + A.QZ * B.QZ;
                const float Sign = DotQuat < 0.0f ? -1.0f : 1.0f;
                float QW = A.QW + (B.QW * Sign - A.QW) * T;
                float QX = A.QX + (B.QX * Sign - A.QX) * T;
                float QY = A.QY + (B.QY * Sign - A.QY) * T;
                float QZ = A.QZ + (B.QZ * Sign - A.QZ) * T;
                const float Length = std::sqrt(QW * QW + QX * QX + QY * QY + QZ * QZ);
                const float Inv = Length > 0.00000001f ? 1.0f / Length : 1.0f;
                Key.QW = QW * Inv;
                Key.QX = QX * Inv;
                Key.QY = QY * Inv;
                Key.QZ = QZ * Inv;
            }
        }
        Out = MdlQuatTranslationMatrix(Key.QW, Key.QX, Key.QY, Key.QZ, Key.X, Key.Y, Key.Z);
        return true;
    };
    std::vector<D3DMATRIX> Local(BoneCount, IdentityMatrix());
    for (std::size_t Index = 0; Index < BoneCount; ++Index)
    {
        if (!BoneLocal(Index, Local[Index]))
        {
            return false;
        }
    }
    std::vector<uint8> IsChild(BoneCount, 0);
    auto ChildrenOf = [&](std::size_t Bone, auto&& Fn)
    {
        const auto& Object = Mesh.Objects[Bone];
        for (int ChildIndex = 0; ChildIndex < Object.ConnectedBoneCount; ++ChildIndex)
        {
            const std::size_t Index = static_cast<std::size_t>(Object.ObjectIndexOffset) + ChildIndex;
            if (Index < Mesh.ObjectIndices.size())
            {
                const uint8 Child = Mesh.ObjectIndices[Index];
                if (Child < BoneCount)
                {
                    Fn(static_cast<std::size_t>(Child));
                }
            }
        }
    };
    for (std::size_t Index = 0; Index < BoneCount; ++Index)
    {
        ChildrenOf(Index, [&](std::size_t Child) { IsChild[Child] = 1; });
    }
    std::vector<D3DMATRIX> World(BoneCount, IdentityMatrix());
    std::vector<uint8> Done(BoneCount, 0);
    std::vector<std::pair<std::size_t, D3DMATRIX>> Stack;
    for (std::size_t Index = 0; Index < BoneCount; ++Index)
    {
        if (IsChild[Index])
        {
            continue;
        }
        Stack.push_back({Index, IdentityMatrix()});
        while (!Stack.empty())
        {
            const std::pair<std::size_t, D3DMATRIX> StackItem = Stack.back();
            Stack.pop_back();
            const std::size_t Bone = StackItem.first;
            const D3DMATRIX& Parent = StackItem.second;

            if (Done[Bone])
            {
                continue;
            }

            World[Bone] = MultiplyMatrix(Parent, Local[Bone]);
            Done[Bone] = 1;
            ChildrenOf(Bone, [&](std::size_t Child)
            {
                if (!Done[Child])
                {
                    Stack.push_back({Child, World[Bone]});
                }
            });
        }
    }
    std::vector<int> VertexBone(Mesh.Vertices.size(), -1);
    for (const auto& Surface : Mesh.Surfaces)
    {
        if (Surface.FirstVertexIndex < 0 || Surface.VertexCount < 0)
        {
            continue;
        }
        const std::size_t Bone = static_cast<std::size_t>(Surface.ObjectIndex);
        if (Bone >= BoneCount)
        {
            continue;
        }
        for (int VertexOffset = 0; VertexOffset < Surface.VertexCount; ++VertexOffset)
        {
            const std::size_t VertexIndex = static_cast<std::size_t>(Surface.FirstVertexIndex) + VertexOffset;
            if (VertexIndex < VertexBone.size())
            {
                VertexBone[VertexIndex] = static_cast<int>(Bone);
            }
        }
    }
    for (std::size_t VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
    {
        const int Bone = VertexBone[VertexIndex];
        if (Bone < 0)
        {
            continue;
        }
        const D3DMATRIX& Matrix = World[static_cast<std::size_t>(Bone)];
        auto& Vertex = Mesh.Vertices[VertexIndex];
        const float X = Vertex.X;
        const float Y = Vertex.Y;
        const float Z = Vertex.Z;
        const float NX = Vertex.NX;
        const float NY = Vertex.NY;
        const float NZ = Vertex.NZ;
        Vertex.X = Matrix._11 * X + Matrix._12 * Y + Matrix._13 * Z + Matrix._14;
        Vertex.Y = Matrix._21 * X + Matrix._22 * Y + Matrix._23 * Z + Matrix._24;
        Vertex.Z = Matrix._31 * X + Matrix._32 * Y + Matrix._33 * Z + Matrix._34;
        Vertex.NX = Matrix._11 * NX + Matrix._12 * NY + Matrix._13 * NZ;
        Vertex.NY = Matrix._21 * NX + Matrix._22 * NY + Matrix._23 * NZ;
        Vertex.NZ = Matrix._31 * NX + Matrix._32 * NY + Matrix._33 * NZ;
    }
    RecomputeMdlBounds(Mesh);
    return true;
}

std::shared_ptr<const StaticModelCpuResource> BuildStaticModelCpuResource(const std::string& ModelName, const std::filesystem::path& ModelPath)
{
    FMdlMesh MeshStorage;
    const FMdlMesh* MeshPtr = nullptr;
    const auto CacheKey = ModelPath.generic_string();

    if (auto CachedMeshByPath = FindCachedMdlMesh(CacheKey))
    {
        MeshPtr = CachedMeshByPath.get();
    }
    else if (auto CachedMeshByName = FindCachedMdlMesh(ModelName))
    {
        AliasCachedMdlMesh(CacheKey, ModelName);
        MeshPtr = CachedMeshByName.get();
    }
    else if (auto CachedMeshByMdlName = FindCachedMdlMesh(ModelName + ".mdl"))
    {
        AliasCachedMdlMesh(CacheKey, ModelName + ".mdl");
        MeshPtr = CachedMeshByMdlName.get();
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
    FMdlMesh PosedMesh;
    const bool WillSkin = MeshPtr->Info.SkinWeightCount != 0 && !MeshPtr->Objects.empty() && !MeshPtr->TransformKeys.empty();
    bool IsSkinned = false;
    if (WillSkin)
    {
        PosedMesh = *MeshPtr;
        IsSkinned = ApplyMdlRestPose(PosedMesh);
    }
    const FMdlMesh& mesh = IsSkinned ? PosedMesh : *MeshPtr;
    if (mesh.Vertices.empty() || mesh.Triangles.empty() || mesh.Surfaces.empty() || mesh.Info.Materials.empty())
    {
        throw std::runtime_error("static model has no renderable geometry: " + ModelPath.string());
    }

    auto resource = std::make_shared<StaticModelCpuResource>();
    resource->IsSkinned = IsSkinned;
    if (IsSkinned)
    {
        resource->BindMesh = *MeshPtr;
    }
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

bool IsStaticModelCpuResourceCached(const std::filesystem::path& ModelPath)
{
    const auto key = StaticModelCpuCacheKey(ModelPath);
    std::lock_guard<std::mutex> lock(StaticModelCpuCacheMutex);
    return StaticModelCpuCache.find(key) != StaticModelCpuCache.end();
}

struct StaticPlacementFile
{
    std::filesystem::path AbsolutePath;
    std::string RelativeKey;
};

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
    std::string Name;
    std::string Key;
    FVector3 Position;
    FVector3 Rotation;
    D3DMATRIX World{};
};

bool ReadFixedAsciiName(const std::vector<uint8>& data, std::size_t offset, std::size_t limit, ParsedStaticPlacement& placement)
{
    placement.Name.clear();
    placement.Key.clear();
    for (std::size_t i = 0; i < limit && data[offset + i] != 0; ++i)
    {
        const auto ch = data[offset + i];
        placement.Name.push_back(static_cast<char>(ch));
        placement.Key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return !placement.Key.empty() && placement.Key != "empty";
}

uint32 InternPlacementModel(const ParsedStaticPlacement& placement, std::unordered_map<std::string, uint32>& ids, std::vector<StaticPlacementModel>& models)
{
    if (auto it = ids.find(placement.Key); it != ids.end())
    {
        return it->second;
    }
    const auto id = static_cast<uint32>(models.size());
    models.push_back(StaticPlacementModel{placement.Name, placement.Key});
    ids.emplace(placement.Key, id);
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
        if ((!rel.ends_with(".mbd") && !rel.ends_with(".mb")) || !StaticPathAllowed(rel, configuredDirs))
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
    std::unordered_map<std::string, uint32> modelIds;
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


void FD3D9GameWorldScene::Impl::LoadStaticPlacements()
{
    auto result = BuildStaticPlacementLoadResult(CollectStaticPlacementFiles(AssetResources, Config));
    StaticInstances.clear();
    ModelCollisionProxies.clear();
    ModelCollisionProxyCells.clear();
    VisibleStaticPlacementIndices.clear();
    VisibleStaticRenderCells.clear();
    ClearStaticRenderBatches();
    StaticPlacementModels = std::move(result.Models);
    StaticPlacements = std::move(result.Placements);
    StaticPlacementIndicesByRenderCell.clear();
    StaticPlacementIndicesByRenderCell.reserve(StaticPlacements.size() / 4 + 1);
    for (std::size_t index = 0; index < StaticPlacements.size(); ++index)
    {
        const auto& placement = StaticPlacements[index];
        StaticPlacementIndicesByRenderCell[StaticRenderCellKeyForPoint(placement.Position.X, placement.Position.Z, Config.TileSize)].push_back(index);
    }
    StaticVisibilityPlanReady = false;
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

void FD3D9GameWorldScene::Impl::StartStaticModelCpuPreloadWorker()
{
    std::lock_guard<std::mutex> lock(StaticModelCpuPreloadMutex);
    if (StaticModelCpuPreloadWorkerStarted)
    {
        return;
    }
    StaticModelCpuPreloadStop = false;
    StaticModelCpuPreloadWorkerStarted = true;
    StaticModelCpuPreloadThread = std::thread([this]()
    {
        StaticModelCpuPreloadWorkerMain();
    });
}

void FD3D9GameWorldScene::Impl::StopStaticModelCpuPreloadWorker()
{
    {
        std::lock_guard<std::mutex> lock(StaticModelCpuPreloadMutex);
        if (!StaticModelCpuPreloadWorkerStarted)
        {
            return;
        }
        StaticModelCpuPreloadStop = true;
    }
    StaticModelCpuPreloadCv.notify_all();
    if (StaticModelCpuPreloadThread.joinable())
    {
        StaticModelCpuPreloadThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(StaticModelCpuPreloadMutex);
        StaticModelCpuPreloadWorkerStarted = false;
        StaticModelCpuPreloadStop = false;
    }
}

void FD3D9GameWorldScene::Impl::StaticModelCpuPreloadWorkerMain()
{
    LowerStaticWorkerPriority();
    for (;;)
    {
        std::vector<StaticModelCpuPreloadTarget> Targets;
        {
            std::unique_lock<std::mutex> lock(StaticModelCpuPreloadMutex);
            StaticModelCpuPreloadCv.wait(lock, [this]()
            {
                return StaticModelCpuPreloadStop || !PendingStaticModelCpuPreloads.empty();
            });
            if (StaticModelCpuPreloadStop && PendingStaticModelCpuPreloads.empty())
            {
                break;
            }
            Targets.swap(PendingStaticModelCpuPreloads);
        }

        if (Targets.empty())
        {
            continue;
        }

        for (const auto& Target : Targets)
        {
            try
            {
                LoadStaticModelCpuResourceCached(Target.ModelName, Target.ModelPath);
            }
            catch (const std::exception& ex)
            {
                if (Logger)
                {
                    Logger->Warning(std::string("static model CPU preload failed: ") + ex.what());
                }
            }
            {
                std::lock_guard<std::mutex> lock(StaticModelCpuPreloadMutex);
                QueuedStaticModelCpuPreloads.erase(Target.Key);
            }
        }
    }
}

void FD3D9GameWorldScene::Impl::QueueStaticModelCpuPreload(const std::string& ModelName, const std::filesystem::path& ModelPath)
{
    if (ModelName.empty() || ModelPath.empty() || IsStaticModelCpuResourceCached(ModelPath))
    {
        return;
    }

    const auto Key = StaticModelCpuCacheKey(ModelPath);
    {
        std::lock_guard<std::mutex> lock(StaticModelCpuPreloadMutex);
        if (!QueuedStaticModelCpuPreloads.insert(Key).second)
        {
            return;
        }
        PendingStaticModelCpuPreloads.push_back(StaticModelCpuPreloadTarget{ModelName, ModelPath, Key});
    }
    StaticModelCpuPreloadCv.notify_one();
}

void FD3D9GameWorldScene::Impl::DrainStaticModelCpuPreloadJobs(bool Wait)
{
    if (Wait)
    {
        StopStaticModelCpuPreloadWorker();
        StaticModelCpuPreloadJobs.clear();
        return;
    }

    StaticModelCpuPreloadCv.notify_one();
}

std::unique_ptr<StaticModelResource> FD3D9GameWorldScene::Impl::LoadStaticModelResource(
    const std::string& ModelName,
    const std::filesystem::path& ModelPath)
{
    const auto cpu = LoadStaticModelCpuResourceCached(ModelName, ModelPath);
    auto resource = std::make_unique<StaticModelResource>();
    resource->VertexCount = static_cast<UINT>(cpu->Vertices.size());
    resource->Bounds = cpu->Bounds;
    resource->IsSkinned = cpu->IsSkinned;
    resource->CollisionPositions = cpu->CollisionPositions;
    resource->CollisionIndices = cpu->CollisionIndices;
    resource->CpuVertices = cpu->Vertices;
    resource->CpuIndices = cpu->Indices;
    if (resource->IsSkinned)
    {
        resource->BindMesh = cpu->BindMesh;
        resource->AnimationVertices = cpu->Vertices;
        std::size_t MaxFrames = 0;
        bool FirstAnimatedBone = true;
        for (const auto& Object : resource->BindMesh.Objects)
        {
            if (Object.IsAnimated == 0)
            {
                continue;
            }
            if (Object.KeyIndex < 0)
            {
                continue;
            }
            const std::size_t KeyIndex = static_cast<std::size_t>(Object.KeyIndex);
            const std::size_t AvailableFrames = resource->BindMesh.SkinIndices.size() > KeyIndex ? resource->BindMesh.SkinIndices.size() - KeyIndex : 0;
            MaxFrames = FirstAnimatedBone ? AvailableFrames : (std::min)(MaxFrames, AvailableFrames);
            FirstAnimatedBone = false;
        }
        resource->FrameCount = MaxFrames > 1 ? static_cast<int>(MaxFrames) : 1;
        ConfigureNpcAnimationClips(*resource, AssetResources, ModelName);
    }

    for (const auto& cpuBatch : cpu->Batches)
    {
        FSceneBatch batch;
        batch.StartIndex = cpuBatch.StartIndex;
        batch.IndexCount = cpuBatch.IndexCount;
        batch.Texture = LoadCachedDdsTexture(ResolveModelTexturePath(ModelPath, cpuBatch.MaterialName));
        resource->Batches.push_back(batch);
    }

    {
        resource->VertexBuffer = CreateManagedVertexBufferOrThrow(Device, cpu->Vertices, kWorldVertexFvf, "CreateVertexBuffer static model");
    }
    {
        resource->IndexBuffer = CreateManagedIndexBufferOrThrow(Device, cpu->Indices, D3DFMT_INDEX16, "CreateIndexBuffer static model");
    }
    return resource;
}

void FD3D9GameWorldScene::Impl::LoadVisibleStaticObjects()
{
    const float RadiusSquared = Config.StaticObjectRadius * Config.StaticObjectRadius;
    const bool needsPlan = !StaticVisibilityPlanReady || std::abs(SpawnX - StaticVisibilityAnchorX) > 1.0f || std::abs(SpawnZ - StaticVisibilityAnchorZ) > 1.0f;
    if (needsPlan)
    {
        VisibleStaticPlacementIndices.clear();
        VisibleStaticRenderCells.clear();
        std::unordered_set<uint64> visibleCells;
        StaticVisibilityAnchorX = SpawnX;
        StaticVisibilityAnchorZ = SpawnZ;
        const auto [MinCellX, MaxCellX] = StaticRenderCellRange(SpawnX, Config.StaticObjectRadius, Config.TileSize);
        const auto [MinCellZ, MaxCellZ] = StaticRenderCellRange(SpawnZ, Config.StaticObjectRadius, Config.TileSize);
        for (int cellX = MinCellX; cellX <= MaxCellX; ++cellX)
        {
            for (int cellZ = MinCellZ; cellZ <= MaxCellZ; ++cellZ)
            {
                const uint64 CellKey = StaticRenderCellKey(cellX, cellZ);
                const auto cellIt = StaticPlacementIndicesByRenderCell.find(CellKey);
                if (cellIt == StaticPlacementIndicesByRenderCell.end())
                {
                    continue;
                }
                for (const std::size_t index : cellIt->second)
                {
                    auto& placement = StaticPlacements[index];
                    const float dx = placement.Position.X - SpawnX;
                    const float dz = placement.Position.Z - SpawnZ;
                    if (dx * dx + dz * dz > RadiusSquared || placement.ModelId >= StaticPlacementModels.size())
                    {
                        continue;
                    }
                    VisibleStaticPlacementIndices.push_back(index);
                    visibleCells.insert(CellKey);
                }
            }
        }
        VisibleStaticRenderCells.assign(visibleCells.begin(), visibleCells.end());
        StaticVisibilityPlanReady = true;
    }
    for (const auto placementIndex : VisibleStaticPlacementIndices)
    {
        const auto& placement = StaticPlacements[placementIndex];
        if (placement.ModelId >= StaticPlacementModels.size())
        {
            continue;
        }
        const auto& model = StaticPlacementModels[placement.ModelId];
        if (!model.Key.empty() && StaticResources.find(model.Key) == StaticResources.end())
        {
            const auto ModelPath = ResolveModelPath(model.Name);
            StaticResources.emplace(model.Key, LoadStaticModelResource(model.Name, ModelPath));
        }
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
        if (it->second && it->second->IsSkinned)
        {
            float Ground = 0.0f;
            FVector3 GroundNormal{};
            if (TerrainSurfaceAt(placement.Position.X, placement.Position.Z, Ground, GroundNormal) && std::abs(placement.World._42 - Ground) > 0.0001f)
            {
                placement.Position.Y = Ground;
                placement.World._42 = Ground;
                placement.BoundsValid = false;
                const uint64 CellKey = StaticRenderCellKeyForPoint(placement.Position.X, placement.Position.Z, Config.TileSize);
                if (auto BatchIt = StaticCellRenderBatches.find(CellKey); BatchIt != StaticCellRenderBatches.end())
                {
                    ReleaseWorldRenderBatches(BatchIt->second);
                    StaticCellRenderBatches.erase(BatchIt);
                }
            }
        }
        if (!placement.BoundsValid)
        {
            placement.Bounds = EmptyBounds();
            for (int corner = 0; corner < 8; ++corner)
            {
                const FVector3 local{(corner & 1) ? it->second->Bounds.Max.X : it->second->Bounds.Min.X, (corner & 2) ? it->second->Bounds.Max.Y : it->second->Bounds.Min.Y, (corner & 4) ? it->second->Bounds.Max.Z : it->second->Bounds.Min.Z};
                ExpandBounds(placement.Bounds, TransformPoint(local, placement.World));
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
    RebuildModelCollisionProxies();
    // Static cell baking is intentionally not executed from streaming/update.
    // It transforms and uploads a whole visible static-cell set and can turn a tile crossing into a 100+ ms hitch.
    // Missing baked cells are rendered through the direct per-instance path in DrawStaticObjects().
}


void FD3D9GameWorldScene::Impl::ClearStaticRenderBatches()
{
    ReleaseWorldRenderBatchMap(StaticCellRenderBatches);
    VisibleStaticRenderCells.clear();
}

void FD3D9GameWorldScene::Impl::BakeStaticRenderCell(uint64 CellKey)
{
    if (StaticCellRenderBatches.find(CellKey) != StaticCellRenderBatches.end())
    {
        return;
    }
    auto cellIt = StaticPlacementIndicesByRenderCell.find(CellKey);
    if (cellIt == StaticPlacementIndicesByRenderCell.end() || cellIt->second.empty())
    {
        StaticCellRenderBatches.emplace(CellKey, std::vector<WorldRenderBatch>{});
        return;
    }
    AccumulatedWorldBatchMap batchesByTexture;
    for (const auto placementIndex : cellIt->second)
    {
        if (placementIndex >= StaticPlacements.size())
        {
            continue;
        }
        auto& placement = StaticPlacements[placementIndex];
        if (placement.ModelId >= StaticPlacementModels.size())
        {
            continue;
        }
        const auto& model = StaticPlacementModels[placement.ModelId];
        if (model.Key.empty())
        {
            continue;
        }
        auto resourceIt = StaticResources.find(model.Key);
        if (resourceIt == StaticResources.end())
        {
            try
            {
                resourceIt = StaticResources.emplace(model.Key, LoadStaticModelResource(model.Name, ResolveModelPath(model.Name))).first;
            }
            catch (const std::exception& ex)
            {
                if (Logger)
                {
                    Logger->Warning("static render batch skipped " + model.Name + ": " + ex.what());
                }
                continue;
            }
        }
        if (!resourceIt->second)
        {
            continue;
        }
        const auto* resource = resourceIt->second.get();
        if (resource->CpuVertices.empty() || resource->CpuIndices.empty())
        {
            continue;
        }
        if (resource->IsSkinned)
        {
            float Ground = 0.0f;
            FVector3 GroundNormal{};
            if (TerrainSurfaceAt(placement.Position.X, placement.Position.Z, Ground, GroundNormal) && std::abs(placement.World._42 - Ground) > 0.0001f)
            {
                placement.Position.Y = Ground;
                placement.World._42 = Ground;
                placement.BoundsValid = false;
                const uint64 AdjustedCellKey = StaticRenderCellKeyForPoint(placement.Position.X, placement.Position.Z, Config.TileSize);
                if (auto BatchIt = StaticCellRenderBatches.find(AdjustedCellKey); BatchIt != StaticCellRenderBatches.end())
                {
                    ReleaseWorldRenderBatches(BatchIt->second);
                    StaticCellRenderBatches.erase(BatchIt);
                }
            }
            continue;
        }
        const auto& world = placement.World;
        if (!placement.BoundsValid)
        {
            placement.Bounds = EmptyBounds();
            for (int corner = 0; corner < 8; ++corner)
            {
                const FVector3 local{(corner & 1) ? resource->Bounds.Max.X : resource->Bounds.Min.X, (corner & 2) ? resource->Bounds.Max.Y : resource->Bounds.Min.Y, (corner & 4) ? resource->Bounds.Max.Z : resource->Bounds.Min.Z};
                ExpandBounds(placement.Bounds, TransformPoint(local, world));
            }
            placement.BoundsValid = true;
        }
        AccumulateWorldBatches(*resource, world, batchesByTexture);
    }
    std::vector<WorldRenderBatch> baked;
    if (UploadWorldBatches(Device, batchesByTexture, baked))
    {
        StaticCellRenderBatches.emplace(CellKey, std::move(baked));
    }
}

void FD3D9GameWorldScene::Impl::BuildVisibleStaticRenderBatches()
{
    for (const auto cell : VisibleStaticRenderCells)
    {
        BakeStaticRenderCell(cell);
    }
}

void FD3D9GameWorldScene::Impl::PreloadStaticResourcesAround(float CenterX, float CenterZ, float Radius)
{
    const float RadiusSquared = Radius * Radius;
    const auto [MinCellX, MaxCellX] = StaticRenderCellRange(CenterX, Radius, Config.TileSize);
    const auto [MinCellZ, MaxCellZ] = StaticRenderCellRange(CenterZ, Radius, Config.TileSize);
    std::unordered_set<std::string> queuedModels;
    for (int cellX = MinCellX; cellX <= MaxCellX; ++cellX)
    {
        for (int cellZ = MinCellZ; cellZ <= MaxCellZ; ++cellZ)
        {
            const uint64 CellKey = StaticRenderCellKey(cellX, cellZ);
            const auto cellIt = StaticPlacementIndicesByRenderCell.find(CellKey);
            if (cellIt == StaticPlacementIndicesByRenderCell.end())
            {
                continue;
            }
            for (const std::size_t placementIndex : cellIt->second)
            {
                const auto& placement = StaticPlacements[placementIndex];
                const float dx = placement.Position.X - CenterX;
                const float dz = placement.Position.Z - CenterZ;
                if (dx * dx + dz * dz > RadiusSquared || placement.ModelId >= StaticPlacementModels.size())
                {
                    continue;
                }
                const auto& model = StaticPlacementModels[placement.ModelId];
                if (model.Key.empty() || StaticResources.find(model.Key) != StaticResources.end() || !queuedModels.insert(model.Key).second)
                {
                    continue;
                }
                try
                {
                    QueueStaticModelCpuPreload(model.Name, ResolveModelPath(model.Name));
                }
                catch (const std::exception& ex)
                {
                    if (Logger)
                    {
                        Logger->Warning("static guard preload skipped " + model.Name + ": " + ex.what());
                    }
                }
            }
        }
    }
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
        std::atomic_size_t loaded{0};
        std::atomic_size_t failed{0};
        ParallelFor(targets.size(), threadCount, [&](std::size_t index)
        {
            try
            {
                LoadStaticModelCpuResourceCached(targets[index].first, targets[index].second);
                loaded.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...)
            {
                failed.fetch_add(1, std::memory_order_relaxed);
            }
        });
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

void FD3D9GameWorldScene::Impl::LoadVisibleGrass()
{
    if (Config.GrassQuality <= 0)
    {
        ClearGrassRenderBatches();
        GrassInstances.clear();
        GrassCells.clear();
        GrassRefreshIncomplete = false;
        return;
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
    for (auto it = GrassCellRenderBatches.begin(); it != GrassCellRenderBatches.end();)
    {
        if (TargetCells.contains(it->first))
        {
            ++it;
            continue;
        }
        ReleaseWorldRenderBatches(it->second);
        it = GrassCellRenderBatches.erase(it);
    }

    std::array<std::vector<StaticModelResource*>, 31> GrassPatternResources{};
    std::array<std::vector<StaticModelResource*>, 31> FlowerPatternResources{};
    std::array<bool, 31> UsedGrassTypes{};
    std::vector<StaticModelResource*> DetailResources;
    bool AnyTypedGrass = false;

    std::vector<GrassCellPlan> plans;
    plans.reserve(TargetCells.size());
    for (int CellX = GrassCenterX - CellRadius; CellX <= GrassCenterX + CellRadius; ++CellX)
    {
        for (int CellZ = GrassCenterZ - CellRadius; CellZ <= GrassCenterZ + CellRadius; ++CellZ)
        {
            const auto key = CellKey(CellX, CellZ);
            if (!TargetCells.contains(key) || GrassCells.contains(key))
            {
                continue;
            }
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
                plan.Samples.push_back(GrassSamplePlan{SampleX, SampleZ, type});
            }
            plans.push_back(std::move(plan));
        }
    }

    if (plans.empty())
    {
        GrassRefreshIncomplete = false;
        return;
    }

    constexpr std::size_t MaxGrassCellsPerUpdate = 6;
    if (!GrassInitialBlockingLoad && plans.size() > MaxGrassCellsPerUpdate)
    {
        GrassRefreshIncomplete = true;
        plans.resize(MaxGrassCellsPerUpdate);
    }
    else
    {
        GrassRefreshIncomplete = false;
    }

    for (const auto& plan : plans)
    {
        GrassCells.insert(plan.Key);
        for (const auto& sample : plan.Samples)
        {
            if (sample.Type > 0 && sample.Type < UsedGrassTypes.size())
            {
                UsedGrassTypes[sample.Type] = true;
                AnyTypedGrass = true;
            }
        }
    }

    if (!AnyTypedGrass)
    {
        return;
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

            FXorShift32 Random{(static_cast<uint32>(plan.CellX) * 0x9e3779b9U) ^ (static_cast<uint32>(plan.CellZ) * 0x85ebca6bU) ^ (static_cast<uint32>(SampleIndex) * 0xc2b2ae35U) ^ type};

            float FlatHeight = 0.0f;
            FVector3 FlatNormal{};
            if (FlatGrassSurfaceAt(sample.X, sample.Z, FlatHeight, FlatNormal))
            {
                StaticModelResource* resource = pattern[Random.Next() % pattern.size()];
                auto world = AlignUpMatrix(FlatNormal);
                world._41 = sample.X;
                world._42 = FlatHeight - resource->Bounds.Max.Y;
                world._43 = sample.Z;
                out.push_back(GrassInstance{resource, world, Random.Unit() * 2.0f * kPi, 0.65f + Random.Unit() * 0.35f, plan.CellX, plan.CellZ, TerrainColorAt(sample.X, sample.Z)});
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
                const float DetailX = sample.X + (Random.Unit() * 2.0f - 1.0f) * jitter;
                const float DetailZ = sample.Z + (Random.Unit() * 2.0f - 1.0f) * jitter;
                float height = 0.0f;
                FVector3 DetailNormal{};
                if (!TerrainSurfaceAt(DetailX, DetailZ, height, DetailNormal))
                {
                    continue;
                }
                StaticModelResource* resource = DetailResources[Random.Next() % DetailResources.size()];
                const float Scale = Config.GrassScaleMin + Random.Unit() * (Config.GrassScaleMax - Config.GrassScaleMin);
                auto world = ScaleMatrix(Scale);
                world._41 = DetailX;
                world._42 = height - resource->Bounds.Max.Y * Scale;
                world._43 = DetailZ;
                out.push_back(GrassInstance{resource, world, Random.Unit() * 2.0f * kPi, 0.65f + Random.Unit() * 0.35f, plan.CellX, plan.CellZ, TerrainColorAt(DetailX, DetailZ)});
                AnyDetail = true;
            }
        }

        const int SampleTotal = static_cast<int>(Config.GrassSampleOffsets.size());
        if (FlatSampleCount == SampleTotal && !AnyDetail && FlowerType > 0 && FlowerType < static_cast<int>(FlowerPatternResources.size()) && !FlowerPatternResources[static_cast<std::size_t>(FlowerType)].empty())
        {
            const auto& flowers = FlowerPatternResources[static_cast<std::size_t>(FlowerType)];
            FXorShift32 FlowerRandom{(static_cast<uint32>(plan.CellX) * 0x27d4eb2dU) ^ (static_cast<uint32>(plan.CellZ) * 0x165667b1U) ^ 0x9e3779b9U};
            const int flowerLimit = Config.GrassQuality >= 2 ? Config.GrassFlowerCountMax : (std::min)(Config.GrassFlowerCountMax, 3);
            const int FlowerCount = static_cast<int>(FlowerRandom.Unit() * static_cast<float>((std::max)(0, flowerLimit)));
            for (int f = 0; f < FlowerCount; ++f)
            {
                const float FlowerX = plan.X + FlowerRandom.Unit() * spacing;
                const float FlowerZ = plan.Z + FlowerRandom.Unit() * spacing;
                float FlowerH = 0.0f;
                FVector3 FlowerNormal{};
                if (!TerrainSurfaceAt(FlowerX, FlowerZ, FlowerH, FlowerNormal))
                {
                    continue;
                }
                const int slot = static_cast<int>(FlowerRandom.Unit() * 5.0f);
                if (slot < 0 || slot >= static_cast<int>(flowers.size()))
                {
                    continue;
                }
                StaticModelResource* resource = flowers[static_cast<std::size_t>(slot)];
                auto world = AlignUpMatrix(FlowerNormal);
                world._41 = FlowerX;
                world._42 = FlowerH - resource->Bounds.Max.Y;
                world._43 = FlowerZ;
                out.push_back(GrassInstance{resource, world, FlowerRandom.Unit() * 2.0f * kPi, 0.65f + FlowerRandom.Unit() * 0.35f, plan.CellX, plan.CellZ, TerrainColorAt(FlowerX, FlowerZ)});
            }
        }
    };

    const size_t hardware = static_cast<size_t>((std::max)(1u, std::thread::hardware_concurrency()));
    const size_t threadCount = plans.size() < 8 ? 1 : (std::min)(std::clamp(hardware - 1, size_t{1}, size_t{8}), plans.size());
    ParallelFor(plans.size(), threadCount, [&](std::size_t index)
    {
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
    });

    if (!firstError.empty())
    {
        throw std::runtime_error(firstError);
    }

    std::size_t newInstances = 0;
    for (const auto& bucket : generated)
    {
        newInstances += bucket.size();
    }
    for (std::size_t index = 0; index < generated.size(); ++index)
    {
        if (!generated[index].empty())
        {
            BakeGrassCell(plans[index].Key, generated[index]);
        }
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
}

void FD3D9GameWorldScene::Impl::StartCollisionWorker()
{
    std::lock_guard<std::mutex> lock(CollisionWorkerMutex);
    if (CollisionWorkerStarted)
    {
        return;
    }
    CollisionWorkerStop = false;
    CollisionWorkerRequestPending = false;
    CollisionWorkerRebuildSource = false;
    CollisionWorkerPendingInstances.clear();
    CollisionWorkerThread = std::thread([this]() { CollisionWorkerMain(); });
    CollisionWorkerStarted = true;
}

void FD3D9GameWorldScene::Impl::StopCollisionWorker()
{
    {
        std::lock_guard<std::mutex> lock(CollisionWorkerMutex);
        if (!CollisionWorkerStarted)
        {
            return;
        }
        CollisionWorkerStop = true;
        CollisionWorkerRequestPending = true;
    }
    CollisionWorkerCv.notify_all();
    if (CollisionWorkerThread.joinable())
    {
        CollisionWorkerThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(CollisionWorkerMutex);
        CollisionWorkerStarted = false;
        CollisionWorkerStop = false;
        CollisionWorkerRequestPending = false;
        CollisionWorkerRebuildSource = false;
        CollisionWorkerPendingInstances.clear();
    }
    {
        std::lock_guard<std::mutex> lock(ActiveCollisionSnapshotMutex);
        ActiveCollisionSnapshot.reset();
    }
}

void FD3D9GameWorldScene::Impl::CollisionWorkerMain()
{
    LowerStaticWorkerPriority();
    std::vector<PreparedModelCollisionTriangle> sourceTriangles;
    std::vector<PreparedModelCollisionCapsule> sourceCapsules;
    uint64 sourceGeneration = 0;
    for (;;)
    {
        bool rebuildSource = false;
        uint64 requestGeneration = 0;
        float focusX = 0.0f;
        float focusZ = 0.0f;
        std::vector<ModelCollisionWorkerInstance> instances;
        {
            std::unique_lock<std::mutex> lock(CollisionWorkerMutex);
            CollisionWorkerCv.wait(lock, [&]() { return CollisionWorkerStop || CollisionWorkerRequestPending; });
            if (CollisionWorkerStop)
            {
                break;
            }
            rebuildSource = CollisionWorkerRebuildSource;
            requestGeneration = CollisionWorkerPendingGeneration;
            focusX = CollisionWorkerPendingFocusX;
            focusZ = CollisionWorkerPendingFocusZ;
            if (rebuildSource)
            {
                instances = std::move(CollisionWorkerPendingInstances);
                CollisionWorkerPendingInstances.clear();
            }
            CollisionWorkerRequestPending = false;
            CollisionWorkerRebuildSource = false;
        }
        if (rebuildSource)
        {
            sourceTriangles.clear();
            sourceCapsules.clear();
            sourceGeneration = requestGeneration;
            std::size_t expectedTriangles = 0;
            for (const auto& instance : instances)
            {
                if (!instance.Resource || instance.Proxy.Capsule2D || instance.Resource->IsSkinned)
                {
                    continue;
                }
                expectedTriangles += instance.Resource->CollisionIndices.size() / 3;
            }
            sourceTriangles.reserve(expectedTriangles);
            sourceCapsules.reserve(instances.size() / 4 + 1);
            for (const auto& instance : instances)
            {
                if (!instance.Resource || !instance.Bounds.IsValid())
                {
                    continue;
                }
                if (instance.Proxy.Capsule2D || instance.Resource->IsSkinned)
                {
                    PreparedModelCollisionCapsule capsule;
                    capsule.Bounds = instance.Proxy.Bounds;
                    capsule.CenterX = instance.Proxy.CenterX;
                    capsule.CenterZ = instance.Proxy.CenterZ;
                    capsule.Radius = instance.Proxy.Radius;
                    sourceCapsules.push_back(capsule);
                    continue;
                }
                const auto& positions = instance.Resource->CollisionPositions;
                const auto& indices = instance.Resource->CollisionIndices;
                if (positions.empty() || indices.empty())
                {
                    continue;
                }
                for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
                {
                    const auto ia = static_cast<std::size_t>(indices[t]);
                    const auto ib = static_cast<std::size_t>(indices[t + 1]);
                    const auto ic = static_cast<std::size_t>(indices[t + 2]);
                    if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size())
                    {
                        continue;
                    }
                    PreparedModelCollisionTriangle tri;
                    tri.A = TransformPoint(positions[ia], instance.World);
                    tri.B = TransformPoint(positions[ib], instance.World);
                    tri.C = TransformPoint(positions[ic], instance.World);
                    const FVector3 normal = Cross(Subtract(tri.B, tri.A), Subtract(tri.C, tri.A));
                    const float normalLength = std::sqrt(Dot(normal, normal));
                    if (normalLength <= 0.00001f)
                    {
                        continue;
                    }
                    const float invNormalLength = 1.0f / normalLength;
                    tri.Normal = FVector3{normal.X * invNormalLength, normal.Y * invNormalLength, normal.Z * invNormalLength};
                    tri.Walkable = std::abs(tri.Normal.Y) >= Config.CollisionFloorNormalThreshold;
                    tri.Bounds = EmptyBounds();
                    ExpandBounds(tri.Bounds, tri.A);
                    ExpandBounds(tri.Bounds, tri.B);
                    ExpandBounds(tri.Bounds, tri.C);
                    sourceTriangles.push_back(tri);
                }
            }
        }
        FBox2 activeArea;
        activeArea.Min = FVector2{focusX - kCollisionWorkerActiveRadius, focusZ - kCollisionWorkerActiveRadius};
        activeArea.Max = FVector2{focusX + kCollisionWorkerActiveRadius, focusZ + kCollisionWorkerActiveRadius};
        auto snapshot = std::make_shared<PreparedModelCollisionSnapshot>();
        snapshot->SourceGeneration = sourceGeneration;
        snapshot->Area = activeArea;
        snapshot->Triangles.reserve(sourceTriangles.size() / 8 + 64);
        snapshot->Capsules.reserve(sourceCapsules.size());
        for (const auto& tri : sourceTriangles)
        {
            if (TriangleIntersectsArea2D(tri, activeArea))
            {
                snapshot->Triangles.push_back(tri);
            }
        }
        for (const auto& capsule : sourceCapsules)
        {
            if (BoundsIntersectsArea2D(capsule.Bounds, activeArea))
            {
                snapshot->Capsules.push_back(capsule);
            }
        }
        {
            std::lock_guard<std::mutex> lock(ActiveCollisionSnapshotMutex);
            ActiveCollisionSnapshot = std::move(snapshot);
        }
    }
}

void FD3D9GameWorldScene::Impl::QueueCollisionWorkerRebuild()
{
    std::vector<ModelCollisionWorkerInstance> instances;
    instances.reserve(ModelCollisionProxies.size());
    for (const auto& proxy : ModelCollisionProxies)
    {
        if (proxy.InstanceIndex >= StaticInstances.size())
        {
            continue;
        }
        const auto& instance = StaticInstances[proxy.InstanceIndex];
        if (!instance.resource || !instance.Bounds.IsValid())
        {
            continue;
        }
        ModelCollisionWorkerInstance workerInstance;
        workerInstance.Resource = instance.resource;
        workerInstance.World = instance.world;
        workerInstance.Bounds = instance.Bounds;
        workerInstance.Proxy = proxy;
        instances.push_back(workerInstance);
    }
    const uint64 generation = ++ModelCollisionSourceGeneration;
    {
        std::lock_guard<std::mutex> lock(CollisionWorkerMutex);
        if (!CollisionWorkerStarted)
        {
            return;
        }
        CollisionWorkerPendingInstances = std::move(instances);
        CollisionWorkerPendingGeneration = generation;
        CollisionWorkerPendingFocusX = SpawnX;
        CollisionWorkerPendingFocusZ = SpawnZ;
        CollisionWorkerRebuildSource = true;
        CollisionWorkerRequestPending = true;
    }
    CollisionWorkerCv.notify_one();
}

void FD3D9GameWorldScene::Impl::RequestCollisionSnapshotAround(float CenterX, float CenterZ)
{
    if (!CollisionWorkerStarted)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(ActiveCollisionSnapshotMutex);
        if (ActiveCollisionSnapshot && ActiveCollisionSnapshot->SourceGeneration == ModelCollisionSourceGeneration)
        {
            const float snapshotCenterX = (ActiveCollisionSnapshot->Area.Min.X + ActiveCollisionSnapshot->Area.Max.X) * 0.5f;
            const float snapshotCenterZ = (ActiveCollisionSnapshot->Area.Min.Y + ActiveCollisionSnapshot->Area.Max.Y) * 0.5f;
            const float dx = CenterX - snapshotCenterX;
            const float dz = CenterZ - snapshotCenterZ;
            if (dx * dx + dz * dz <= kCollisionWorkerRefreshDistance * kCollisionWorkerRefreshDistance)
            {
                return;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(CollisionWorkerMutex);
        if (!CollisionWorkerStarted || CollisionWorkerStop)
        {
            return;
        }
        CollisionWorkerPendingGeneration = ModelCollisionSourceGeneration;
        CollisionWorkerPendingFocusX = CenterX;
        CollisionWorkerPendingFocusZ = CenterZ;
        CollisionWorkerRequestPending = true;
    }
    CollisionWorkerCv.notify_one();
}

std::shared_ptr<const PreparedModelCollisionSnapshot> FD3D9GameWorldScene::Impl::GetActiveCollisionSnapshot(FBox2 area) const
{
    std::lock_guard<std::mutex> lock(ActiveCollisionSnapshotMutex);
    if (!ActiveCollisionSnapshot || ActiveCollisionSnapshot->SourceGeneration != ModelCollisionSourceGeneration || !AreaContainsArea(ActiveCollisionSnapshot->Area, area))
    {
        return nullptr;
    }
    return ActiveCollisionSnapshot;
}

void FD3D9GameWorldScene::Impl::RebuildModelCollisionProxies()
{
    ModelCollisionProxies.clear();
    ModelCollisionProxyCells.clear();
    ModelCollisionProxies.reserve(StaticInstances.size());
    for (std::size_t index = 0; index < StaticInstances.size(); ++index)
    {
        const auto& instance = StaticInstances[index];
        if (!instance.resource || !instance.Bounds.IsValid())
        {
            continue;
        }
        const float width = instance.Bounds.Max.X - instance.Bounds.Min.X;
        const float depth = instance.Bounds.Max.Z - instance.Bounds.Min.Z;
        const float height = instance.Bounds.Max.Y - instance.Bounds.Min.Y;
        if (width < 0.05f || depth < 0.05f || height < 0.10f)
        {
            continue;
        }
        ModelCollisionProxy proxy;
        proxy.Bounds = instance.Bounds;
        proxy.InstanceIndex = index;
        proxy.SkinnedActor = instance.resource->IsSkinned;
        proxy.Capsule2D = instance.resource->IsSkinned;
        proxy.CenterX = (instance.Bounds.Min.X + instance.Bounds.Max.X) * 0.5f;
        proxy.CenterZ = (instance.Bounds.Min.Z + instance.Bounds.Max.Z) * 0.5f;
        proxy.Radius = 0.0f;
        if (proxy.Capsule2D)
        {
            proxy.CenterX = instance.world._41;
            proxy.CenterZ = instance.world._43;
            proxy.Radius = std::clamp((std::max)(width, depth) * 0.18f, 0.24f, Config.PlayerCollisionRadius * 1.15f);
            proxy.Bounds.Min.X = proxy.CenterX - proxy.Radius;
            proxy.Bounds.Max.X = proxy.CenterX + proxy.Radius;
            proxy.Bounds.Min.Z = proxy.CenterZ - proxy.Radius;
            proxy.Bounds.Max.Z = proxy.CenterZ + proxy.Radius;
        }
        const std::size_t proxyIndex = ModelCollisionProxies.size();
        ModelCollisionProxies.push_back(proxy);
        const float inflate = Config.PlayerCollisionRadius + 0.05f;
        const int minCellX = StaticRenderCellCoord(proxy.Bounds.Min.X - inflate, kModelCollisionCellSize);
        const int maxCellX = StaticRenderCellCoord(proxy.Bounds.Max.X + inflate, kModelCollisionCellSize);
        const int minCellZ = StaticRenderCellCoord(proxy.Bounds.Min.Z - inflate, kModelCollisionCellSize);
        const int maxCellZ = StaticRenderCellCoord(proxy.Bounds.Max.Z + inflate, kModelCollisionCellSize);
        for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
        {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
            {
                ModelCollisionProxyCells[StaticRenderCellKey(cellX, cellZ)].push_back(proxyIndex);
            }
        }
    }
    QueueCollisionWorkerRebuild();
}

std::vector<std::size_t> FD3D9GameWorldScene::Impl::QueryModelCollisionProxies(FBox2 area) const
{
    std::vector<std::size_t> result;
    if (ModelCollisionProxies.empty() || !area.IsValid())
    {
        return result;
    }
    const int minCellX = StaticRenderCellCoord(area.Min.X, kModelCollisionCellSize);
    const int maxCellX = StaticRenderCellCoord(area.Max.X, kModelCollisionCellSize);
    const int minCellZ = StaticRenderCellCoord(area.Min.Y, kModelCollisionCellSize);
    const int maxCellZ = StaticRenderCellCoord(area.Max.Y, kModelCollisionCellSize);
    for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
    {
        for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
        {
            const auto cell = ModelCollisionProxyCells.find(StaticRenderCellKey(cellX, cellZ));
            if (cell == ModelCollisionProxyCells.end())
            {
                continue;
            }
            for (const std::size_t proxyIndex : cell->second)
            {
                if (proxyIndex >= ModelCollisionProxies.size() || std::find(result.begin(), result.end(), proxyIndex) != result.end())
                {
                    continue;
                }
                if (BoundsIntersectsArea2D(ModelCollisionProxies[proxyIndex].Bounds, area))
                {
                    result.push_back(proxyIndex);
                }
            }
        }
    }
    return result;
}

bool FD3D9GameWorldScene::Impl::CollidesWithModelContacts(float fromX, float fromZ, float x, float y, float z) const
{
    return CollidesWithModelContactsSwept(fromX, y, fromZ, x, y, z, false);
}

bool FD3D9GameWorldScene::Impl::CollidesWithModelContactsSwept(float fromX, float fromY, float fromZ, float toX, float toY, float toZ, bool includeWalkableSurfaces) const
{
    const float radius = Config.PlayerCollisionRadius;
    const float radiusSquared = radius * radius;
    const float fromTopY = fromY - Config.PlayerCollisionHeight;
    const float toTopY = toY - Config.PlayerCollisionHeight;
    const float bodyTopY = (std::min)(fromTopY, toTopY);
    const float bodyBottomY = (std::max)(fromY, toY);
    FBox2 area;
    area.Min = FVector2{(std::min)(fromX, toX) - radius - 0.05f, (std::min)(fromZ, toZ) - radius - 0.05f};
    area.Max = FVector2{(std::max)(fromX, toX) + radius + 0.05f, (std::max)(fromZ, toZ) + radius + 0.05f};
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float dz = toZ - fromZ;
    const float sweepLength = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float sampleStep = (std::max)(radius * 0.75f, 0.05f);
    const int sweepSamples = (std::max)(1, (std::min)(4, static_cast<int>(std::ceil(sweepLength / sampleStep))));
    auto enteringOrCrossing = [](float start, float sample, float limit) -> bool
    {
        return start > limit || sample < start - 0.0001f;
    };
    auto testCapsule = [&](float centerX, float centerZ, float proxyRadius) -> bool
    {
        const float limit = radius + proxyRadius;
        const float limitSquared = limit * limit;
        const float startDx = fromX - centerX;
        const float startDz = fromZ - centerZ;
        const float startDistance = startDx * startDx + startDz * startDz;
        const float pathDistance = PointSegmentDistanceSquared2D(centerX, centerZ, FVector2{fromX, fromZ}, FVector2{toX, toZ});
        if (pathDistance <= limitSquared)
        {
            const float endDx = toX - centerX;
            const float endDz = toZ - centerZ;
            const float endDistance = endDx * endDx + endDz * endDz;
            return startDistance > limitSquared || endDistance < startDistance - 0.0001f || pathDistance < startDistance - 0.0001f;
        }
        return false;
    };
    auto testTriangle = [&](const FVector3& a, const FVector3& b, const FVector3& c, const FVector3& unitNormal, bool walkableSurface) -> bool
    {
        if (walkableSurface && !includeWalkableSurfaces)
        {
            return false;
        }
        if (walkableSurface && toY > fromY && PointInTriangleXz(toX, toZ, a, b, c))
        {
            const float planeY = a.Y - (unitNormal.X * (toX - a.X) + unitNormal.Z * (toZ - a.Z)) / unitNormal.Y;
            if (planeY >= fromY - 0.05f && planeY <= toY + 0.05f)
            {
                return false;
            }
        }
        const float triMinY = (std::min)(a.Y, (std::min)(b.Y, c.Y)) - radius;
        const float triMaxY = (std::max)(a.Y, (std::max)(b.Y, c.Y)) + radius;
        if (bodyBottomY < triMinY || bodyTopY > triMaxY)
        {
            return false;
        }
        for (float offset = radius; offset < Config.PlayerCollisionHeight; offset += radius)
        {
            const FVector3 startCenter{fromX, fromY - offset, fromZ};
            const float startDistance = PointTriangleDistanceSquared(startCenter, a, b, c);
            for (int sample = 1; sample <= sweepSamples; ++sample)
            {
                const float t = static_cast<float>(sample) / static_cast<float>(sweepSamples);
                const FVector3 center{fromX + dx * t, fromY + dy * t - offset, fromZ + dz * t};
                const float distance = PointTriangleDistanceSquared(center, a, b, c);
                if (distance <= radiusSquared && enteringOrCrossing(startDistance, distance, radiusSquared))
                {
                    return true;
                }
            }
        }
        return false;
    };
    if (auto snapshot = GetActiveCollisionSnapshot(area))
    {
        for (const auto& capsule : snapshot->Capsules)
        {
            if (bodyBottomY < capsule.Bounds.Min.Y - 0.05f || bodyTopY > capsule.Bounds.Max.Y + 0.05f)
            {
                continue;
            }
            if (testCapsule(capsule.CenterX, capsule.CenterZ, capsule.Radius))
            {
                return true;
            }
        }
        for (const auto& tri : snapshot->Triangles)
        {
            if (!TriangleIntersectsArea2D(tri, area) || bodyBottomY < tri.Bounds.Min.Y - radius || bodyTopY > tri.Bounds.Max.Y + radius)
            {
                continue;
            }
            if (testTriangle(tri.A, tri.B, tri.C, tri.Normal, tri.Walkable))
            {
                return true;
            }
        }
        return false;
    }
    const auto candidates = QueryModelCollisionProxies(area);
    for (const std::size_t proxyIndex : candidates)
    {
        const auto& proxy = ModelCollisionProxies[proxyIndex];
        if (bodyBottomY < proxy.Bounds.Min.Y - 0.05f || bodyTopY > proxy.Bounds.Max.Y + 0.05f)
        {
            continue;
        }
        if (proxy.Capsule2D)
        {
            if (testCapsule(proxy.CenterX, proxy.CenterZ, proxy.Radius))
            {
                return true;
            }
            continue;
        }
        if (proxy.InstanceIndex >= StaticInstances.size())
        {
            continue;
        }
        const auto& instance = StaticInstances[proxy.InstanceIndex];
        if (!instance.resource || instance.resource->CollisionPositions.empty() || instance.resource->CollisionIndices.empty())
        {
            continue;
        }
        if (!BoundsIntersectsArea2D(instance.Bounds, area) || bodyBottomY < instance.Bounds.Min.Y - radius || bodyTopY > instance.Bounds.Max.Y + radius)
        {
            continue;
        }
        const auto& positions = instance.resource->CollisionPositions;
        const auto& indices = instance.resource->CollisionIndices;
        for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
        {
            const FVector3 a = TransformPoint(positions[indices[triangle]], instance.world);
            const FVector3 b = TransformPoint(positions[indices[triangle + 1]], instance.world);
            const FVector3 c = TransformPoint(positions[indices[triangle + 2]], instance.world);
            const float triMinX = (std::min)(a.X, (std::min)(b.X, c.X)) - radius;
            const float triMaxX = (std::max)(a.X, (std::max)(b.X, c.X)) + radius;
            const float triMinZ = (std::min)(a.Z, (std::min)(b.Z, c.Z)) - radius;
            const float triMaxZ = (std::max)(a.Z, (std::max)(b.Z, c.Z)) + radius;
            if (area.Max.X < triMinX || area.Min.X > triMaxX || area.Max.Y < triMinZ || area.Min.Y > triMaxZ)
            {
                continue;
            }
            const FVector3 normal = Cross(Subtract(b, a), Subtract(c, a));
            const float normalLength = std::sqrt(Dot(normal, normal));
            if (normalLength <= 0.00001f)
            {
                continue;
            }
            const float invNormalLength = 1.0f / normalLength;
            const FVector3 unitNormal{normal.X * invNormalLength, normal.Y * invNormalLength, normal.Z * invNormalLength};
            const bool walkableSurface = std::abs(unitNormal.Y) >= Config.CollisionFloorNormalThreshold;
            if (testTriangle(a, b, c, unitNormal, walkableSurface))
            {
                return true;
            }
        }
    }
    return false;
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

bool FD3D9GameWorldScene::Impl::HasContourCollision() const
{
    if (!WorldScene)
    {
        return false;
    }
    const auto& contours = WorldScene->ContourDatabase();
    return contours.Loaded && !contours.Records.empty();
}

float FD3D9GameWorldScene::Impl::PointSegmentDistanceSquared2D(float px, float pz, FVector2 a, FVector2 b)
{
    const float vx = b.X - a.X;
    const float vz = b.Y - a.Y;
    const float wx = px - a.X;
    const float wz = pz - a.Y;
    const float len = vx * vx + vz * vz;
    if (len <= 0.000001f)
    {
        return wx * wx + wz * wz;
    }
    const float t = std::clamp((wx * vx + wz * vz) / len, 0.0f, 1.0f);
    const float dx = px - (a.X + vx * t);
    const float dz = pz - (a.Y + vz * t);
    return dx * dx + dz * dz;
}

bool FD3D9GameWorldScene::Impl::CollidesWithContours(float fromX, float fromZ, float toX, float toZ, float radius) const
{
    if (!WorldScene)
    {
        return false;
    }
    const auto& contours = WorldScene->ContourDatabase();
    if (!contours.Loaded || contours.Records.empty())
    {
        return false;
    }
    const float skin = 0.025f;
    const float testRadius = radius + skin;
    const float limit = testRadius * testRadius;
    FBox2 area;
    area.Min = FVector2{(std::min)(fromX, toX) - testRadius, (std::min)(fromZ, toZ) - testRadius};
    area.Max = FVector2{(std::max)(fromX, toX) + testRadius, (std::max)(fromZ, toZ) + testRadius};
    const auto candidates = contours.Query(area);
    for (uint32 id : candidates)
    {
        if (id >= contours.Records.size())
        {
            continue;
        }
        const auto& record = contours.Records[id];
        const auto& points = record.Points;
        if (points.size() < 2)
        {
            continue;
        }
        auto testEdge = [&](int32 aIndex, int32 bIndex) -> bool
        {
            if (aIndex < 0 || bIndex < 0 || aIndex == bIndex || static_cast<std::size_t>(aIndex) >= points.size() || static_cast<std::size_t>(bIndex) >= points.size())
            {
                return false;
            }
            const FVector2 a = points[static_cast<std::size_t>(aIndex)];
            const FVector2 b = points[static_cast<std::size_t>(bIndex)];
            const float previous = PointSegmentDistanceSquared2D(fromX, fromZ, a, b);
            const float next = PointSegmentDistanceSquared2D(toX, toZ, a, b);
            return next <= limit && (previous > limit || next < previous - 0.0001f);
        };
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            if (testEdge(static_cast<int32>(i - 1), static_cast<int32>(i)))
            {
                return true;
            }
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const int32 forward = i < record.ForwardLinks.size() ? record.ForwardLinks[i] : -1;
            const int32 current = static_cast<int32>(i);
            const int32 last = static_cast<int32>(points.size() - 1);
            const bool closesContour = current == last && forward == 0;
            const bool followsContour = forward == current + 1;
            if ((closesContour || followsContour) && testEdge(current, forward))
            {
                return true;
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
    FBox2 area;
    area.Min = FVector2{x - Config.PlayerCollisionRadius, z - Config.PlayerCollisionRadius};
    area.Max = FVector2{x + Config.PlayerCollisionRadius, z + Config.PlayerCollisionRadius};
    if (auto snapshot = GetActiveCollisionSnapshot(area))
    {
        for (const auto& tri : snapshot->Triangles)
        {
            if (!tri.Walkable || !TriangleIntersectsArea2D(tri, area))
            {
                continue;
            }
            if (x < tri.Bounds.Min.X || x > tri.Bounds.Max.X || z < tri.Bounds.Min.Z || z > tri.Bounds.Max.Z || tri.Bounds.Min.Y > MaxY || tri.Bounds.Max.Y < MinY)
            {
                continue;
            }
            if (!PointInTriangleXz(x, z, tri.A, tri.B, tri.C))
            {
                continue;
            }
            const float y = tri.A.Y - (tri.Normal.X * (x - tri.A.X) + tri.Normal.Z * (z - tri.A.Z)) / tri.Normal.Y;
            if (y < MinY || y > MaxY)
            {
                continue;
            }
            if (!found || y < best)
            {
                best = y;
                found = true;
                BestNormal = tri.Normal;
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
    const auto proxyCandidates = QueryModelCollisionProxies(area);
    auto testInstance = [&](const StaticInstance& instance)
    {
        if (!instance.resource || instance.resource->IsSkinned)
        {
            return;
        }
        if (x < instance.Bounds.Min.X || x > instance.Bounds.Max.X || z < instance.Bounds.Min.Z || z > instance.Bounds.Max.Z || instance.Bounds.Min.Y > MaxY || instance.Bounds.Max.Y < MinY)
        {
            return;
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
                continue;
            }
            if (!PointInTriangleXz(x, z, a, b, c))
            {
                continue;
            }
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
    };
    if (!proxyCandidates.empty())
    {
        for (const std::size_t proxyIndex : proxyCandidates)
        {
            if (proxyIndex >= ModelCollisionProxies.size())
            {
                continue;
            }
            const auto& proxy = ModelCollisionProxies[proxyIndex];
            if (proxy.InstanceIndex < StaticInstances.size())
            {
                testInstance(StaticInstances[proxy.InstanceIndex]);
            }
        }
    }
    else if (ModelCollisionProxies.empty())
    {
        for (const auto& instance : StaticInstances)
        {
            testInstance(instance);
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

bool FD3D9GameWorldScene::Impl::BeginAlphaWorldPass(const D3DMATRIX& World)
{
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    Device->SetRenderState(D3DRS_ALPHAREF, 0x20);
    Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    if (WorldShadersReady)
    {
        BeginBaseShader();
        SetBaseLightConstants();
        SetBaseWorld(World);
        return true;
    }
    Device->SetFVF(kWorldVertexFvf);
    Device->SetTransform(D3DTS_WORLD, &World);
    return false;
}

void FD3D9GameWorldScene::Impl::EndAlphaWorldPass(bool UsedShader)
{
    if (UsedShader)
    {
        EndBaseShader();
    }
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
}

void FD3D9GameWorldScene::Impl::DrawWorldRenderBatches(std::vector<const WorldRenderBatch*>& DrawList, EGameWorldDrawBucket Bucket, float CullingMargin)
{
    if (DrawList.empty())
    {
        return;
    }
    std::sort(DrawList.begin(), DrawList.end(), [](const WorldRenderBatch* a, const WorldRenderBatch* b) { return std::less<IDirect3DTexture9*>{}(a->Texture, b->Texture); });
    IDirect3DTexture9* boundTexture = nullptr;
    for (const auto* batch : DrawList)
    {
        if (!batch || !batch->VertexBuffer || !batch->IndexBuffer || batch->VertexCount == 0 || batch->IndexCount < 3)
        {
            continue;
        }
        if (BoundsInitialized(batch->Bounds) && !IsBoundsVisibleToCamera(batch->Bounds, CullingMargin))
        {
            continue;
        }
        if (batch->Texture != boundTexture)
        {
            Device->SetTexture(0, batch->Texture);
            boundTexture = batch->Texture;
        }
        Device->SetStreamSource(0, batch->VertexBuffer, 0, sizeof(WorldVertex));
        Device->SetIndices(batch->IndexBuffer);
        const UINT triangleCount = batch->IndexCount / 3;
        Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, batch->VertexCount, 0, triangleCount);
        RecordWorldDraw(triangleCount, Bucket);
    }
}


void FD3D9GameWorldScene::Impl::UpdateNpcAnimation(float DeltaSeconds)
{
    const float Delta = std::clamp(DeltaSeconds, 0.0f, 0.1f);
    constexpr float FramesPerSecond = 1.0f / kPlayerAnimSecondsPerFrame;
    for (auto& Entry : StaticResources)
    {
        auto* Resource = Entry.second.get();
        if (!Resource || !Resource->IsSkinned || Resource->IdleClip < 0 || Resource->AnimationVertices.empty() || !Resource->VertexBuffer)
        {
            continue;
        }
        if (Resource->CurrentClip < 0)
        {
            Resource->CurrentClip = Resource->IdleClip;
        }
        Resource->ClipTime += Delta;
        bool IsIdle = Resource->CurrentClip == Resource->IdleClip;
        int ClipIndex = Resource->CurrentClip;
        if (ClipIndex < 0 || static_cast<std::size_t>(ClipIndex) >= Resource->ClipLength.size())
        {
            Resource->CurrentClip = Resource->IdleClip;
            Resource->ClipTime = 0.0f;
            ClipIndex = Resource->CurrentClip;
            IsIdle = true;
        }
        if (ClipIndex < 0 || static_cast<std::size_t>(ClipIndex) >= Resource->ClipLength.size())
        {
            continue;
        }
        const int ClipLength = Resource->ClipLength[static_cast<std::size_t>(ClipIndex)];
        if (ClipLength <= 0)
        {
            continue;
        }
        const float FramePosition = Resource->ClipTime * FramesPerSecond;
        int FrameInClip = static_cast<int>(std::floor(FramePosition));
        float FrameAlpha = FramePosition - std::floor(FramePosition);
        if (IsIdle)
        {
            FrameInClip %= ClipLength;
            if (!Resource->GestureClips.empty() && static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) < 0.3f * Delta)
            {
                Resource->CurrentClip = Resource->GestureClips[static_cast<std::size_t>(std::rand() % static_cast<int>(Resource->GestureClips.size()))];
                Resource->ClipTime = 0.0f;
                ClipIndex = Resource->CurrentClip;
                IsIdle = false;
                FrameInClip = 0;
                FrameAlpha = 0.0f;
            }
        }
        else if (FrameInClip >= ClipLength)
        {
            Resource->CurrentClip = Resource->IdleClip;
            Resource->ClipTime = 0.0f;
            ClipIndex = Resource->CurrentClip;
            IsIdle = true;
            FrameInClip = 0;
            FrameAlpha = 0.0f;
        }
        if (ClipIndex < 0 || static_cast<std::size_t>(ClipIndex) >= Resource->ClipLength.size())
        {
            continue;
        }
        const int ActiveClipLength = Resource->ClipLength[static_cast<std::size_t>(ClipIndex)];
        if (ActiveClipLength <= 0)
        {
            continue;
        }
        FrameInClip = std::clamp(FrameInClip, 0, ActiveClipLength - 1);
        const int NextFrameInClip = IsIdle ? (FrameInClip + 1) % ActiveClipLength : (std::min)(FrameInClip + 1, ActiveClipLength - 1);
        const int Frame = Resource->ClipStart[static_cast<std::size_t>(ClipIndex)] + FrameInClip;
        const int NextFrame = Resource->ClipStart[static_cast<std::size_t>(ClipIndex)] + NextFrameInClip;
        if (Frame < 0 || NextFrame < 0 || Frame >= Resource->FrameCount || NextFrame >= Resource->FrameCount)
        {
            continue;
        }
        FMdlMesh PosedA = Resource->BindMesh;
        if (!ApplyMdlRestPose(PosedA, Frame))
        {
            continue;
        }
        FMdlMesh PosedB = Resource->BindMesh;
        if (!ApplyMdlRestPose(PosedB, NextFrame))
        {
            PosedB = PosedA;
            FrameAlpha = 0.0f;
        }
        Resource->LastAnimationFrame = Frame;
        const std::size_t Count = (std::min)({Resource->AnimationVertices.size(), PosedA.Vertices.size(), PosedB.Vertices.size()});
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            const auto& A = PosedA.Vertices[Index];
            const auto& B = PosedB.Vertices[Index];
            Resource->AnimationVertices[Index].X = A.X + (B.X - A.X) * FrameAlpha;
            Resource->AnimationVertices[Index].Y = A.Y + (B.Y - A.Y) * FrameAlpha;
            Resource->AnimationVertices[Index].Z = A.Z + (B.Z - A.Z) * FrameAlpha;
            const FVector3 Normal = NormalizeVector({A.NX + (B.NX - A.NX) * FrameAlpha, A.NY + (B.NY - A.NY) * FrameAlpha, A.NZ + (B.NZ - A.NZ) * FrameAlpha});
            Resource->AnimationVertices[Index].NX = Normal.X;
            Resource->AnimationVertices[Index].NY = Normal.Y;
            Resource->AnimationVertices[Index].NZ = Normal.Z;
        }
        void* Data = nullptr;
        const UINT Bytes = static_cast<UINT>(Resource->AnimationVertices.size() * sizeof(WorldVertex));
        if (SUCCEEDED(Resource->VertexBuffer->Lock(0, Bytes, &Data, 0)))
        {
            std::memcpy(Data, Resource->AnimationVertices.data(), Bytes);
            Resource->VertexBuffer->Unlock();
        }
    }
}


void FD3D9GameWorldScene::Impl::DrawStaticObjects()
{
    const bool UseShader = BeginAlphaWorldPass(IdentityMatrix());

    IDirect3DTexture9* boundTexture = nullptr;
    bool hadBatchedCells = false;
    if (!StaticCellRenderBatches.empty() && !VisibleStaticRenderCells.empty())
    {
        std::vector<const WorldRenderBatch*> drawList;
        for (const auto cell : VisibleStaticRenderCells)
        {
            const auto it = StaticCellRenderBatches.find(cell);
            if (it == StaticCellRenderBatches.end())
            {
                continue;
            }
            hadBatchedCells = true;
            for (const auto& batch : it->second)
            {
                drawList.push_back(&batch);
            }
        }
        DrawWorldRenderBatches(drawList, EGameWorldDrawBucket::StaticObjects, 1.0f);
    }

    {
        const StaticModelResource* boundResource = nullptr;
        for (const auto& instance : StaticInstances)
        {
            const auto* resource = instance.resource;
            const bool BakedByCell = hadBatchedCells && StaticCellRenderBatches.find(StaticRenderCellKeyForPoint(instance.world._41, instance.world._43, Config.TileSize)) != StaticCellRenderBatches.end();
            if (!resource || (BakedByCell && !resource->IsSkinned))
            {
                continue;
            }
            if (!IsBoundsVisibleToCamera(instance.Bounds, 1.0f))
            {
                continue;
            }
            if (UseShader)
            {
                SetBaseWorld(instance.world);
            }
            else
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
                Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, resource->VertexCount, batch.StartIndex, triangleCount);
                RecordWorldDraw(triangleCount, EGameWorldDrawBucket::StaticObjects);
            }
        }
    }
    EndAlphaWorldPass(UseShader);
}


void FD3D9GameWorldScene::Impl::ClearGrassRenderBatches()
{
    ReleaseWorldRenderBatchMap(GrassCellRenderBatches);
}

void FD3D9GameWorldScene::Impl::BakeGrassCell(uint64 CellKey, const std::vector<GrassInstance>& Instances)
{
    auto& existing = GrassCellRenderBatches[CellKey];
    ReleaseWorldRenderBatches(existing);
    if (!Device || Instances.empty())
    {
        GrassCellRenderBatches.erase(CellKey);
        return;
    }

    AccumulatedWorldBatchMap batchesByTexture;
    for (const auto& instance : Instances)
    {
        if (instance.resource)
        {
            AccumulateWorldBatches(*instance.resource, instance.world, batchesByTexture, true, instance.Tint);
        }
    }
    UploadWorldBatches(Device, batchesByTexture, existing);
    if (existing.empty())
    {
        GrassCellRenderBatches.erase(CellKey);
    }
}

void FD3D9GameWorldScene::Impl::DrawGrass()
{
    if (GrassCellRenderBatches.empty())
    {
        return;
    }

    Device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    Device->SetRenderState(D3DRS_ALPHAREF, 0x20);
    Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    const bool UseGrassShader = GrassVS && GrassPS && WorldDecl;
    if (UseGrassShader)
    {
        Device->SetVertexDeclaration(WorldDecl);
        Device->SetVertexShader(GrassVS);
        Device->SetPixelShader(GrassPS);
        const D3DMATRIX wvp = TransposeMatrix(ViewProjectionMatrix);
        Device->SetVertexShaderConstantF(0, reinterpret_cast<const float*>(&wvp), 4);
        const float sunDir[4] = {0.40452f, 0.86683f, -0.52009f, 0.0f};
        const float sunColor[4] = {Environment.SunRed / 255.0f, Environment.SunGreen / 255.0f, Environment.SunBlue / 255.0f, Config.GrassColorGain};
        const float wind[4] = {0.0426f, 0.0420f, ElapsedSeconds * Config.GrassWindSpeed, Config.GrassWindAmplitude};
        const float camera[4] = {CameraEye.X, CameraEye.Y, CameraEye.Z, Config.GrassFadeStart};
        const float fade[4] = {Config.GrassFadeEnd, 0.0f, 0.0f, 0.0f};
        Device->SetVertexShaderConstantF(4, sunDir, 1);
        Device->SetVertexShaderConstantF(5, sunColor, 1);
        Device->SetVertexShaderConstantF(7, wind, 1);
        Device->SetVertexShaderConstantF(8, camera, 1);
        Device->SetVertexShaderConstantF(9, fade, 1);
        float circles[12];
        ComputeWindCircles(circles);
        Device->SetVertexShaderConstantF(10, circles, 3);
        const float windAngle = ElapsedSeconds * 0.05f;
        const float control[4] = {std::cos(windAngle), std::sin(windAngle), Config.GrassGustRadiusScale, Config.GrassBreeze};
        Device->SetVertexShaderConstantF(13, control, 1);
    }
    else
    {
        Device->SetVertexShader(nullptr);
        Device->SetPixelShader(nullptr);
        Device->SetVertexDeclaration(nullptr);
        Device->SetFVF(kWorldVertexFvf);
        const auto identity = IdentityMatrix();
        Device->SetTransform(D3DTS_WORLD, &identity);
        D3DMATERIAL9 material{};
        material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1.0f;
        material.Ambient = material.Diffuse;
        Device->SetMaterial(&material);
        Device->SetRenderState(D3DRS_LIGHTING, TRUE);
        Device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        Device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
        Device->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
        Device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(Environment.AmbientRed, Environment.AmbientGreen, Environment.AmbientBlue));
        D3DLIGHT9 light{};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse.r = Environment.SunRed / 255.0f;
        light.Diffuse.g = Environment.SunGreen / 255.0f;
        light.Diffuse.b = Environment.SunBlue / 255.0f;
        light.Diffuse.a = 1.0f;
        light.Direction.x = -0.40452f;
        light.Direction.y = -0.86683f;
        light.Direction.z = 0.52009f;
        Device->SetLight(0, &light);
        Device->LightEnable(0, TRUE);
        Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    }

    std::vector<const WorldRenderBatch*> drawList;
    drawList.reserve(GrassCellRenderBatches.size() * 2);
    for (const auto& [_, batches] : GrassCellRenderBatches)
    {
        for (const auto& batch : batches)
        {
            drawList.push_back(&batch);
        }
    }
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    DrawWorldRenderBatches(drawList, EGameWorldDrawBucket::Grass, Config.GrassSpacing * 2.0f);

    if (!UseGrassShader)
    {
        Device->LightEnable(0, FALSE);
    }
    EndBaseShader();
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    ConfigureRenderState();
}

