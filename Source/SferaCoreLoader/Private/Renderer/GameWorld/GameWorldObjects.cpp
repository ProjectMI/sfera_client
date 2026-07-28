#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include "Config/ConfigDocument.h"
#include "Common/SferaGameConstants.h"


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
    std::shared_ptr<const StaticCollisionMesh> CollisionMesh;
    std::vector<StaticModelCpuBatch> Batches;
    FBox3 Bounds;
    bool IsSkinned = false;
    FMdlMesh BindMesh;
};


uint64 StaticRenderCellKey(int CellX, int CellZ)
{
    return (static_cast<uint64>(static_cast<uint32>(CellX)) << 32) | static_cast<uint32>(CellZ);
}

constexpr int kGrassRenderGroupSize = 4;

int SignedCellFromHighKey(uint64 Key)
{
    return static_cast<int32>(static_cast<uint32>(Key >> 32));
}

int SignedCellFromLowKey(uint64 Key)
{
    return static_cast<int32>(static_cast<uint32>(Key));
}

int FloorDivideCell(int Value, int Divisor)
{
    const int Quotient = Value / Divisor;
    const int Remainder = Value % Divisor;
    return Remainder < 0 ? Quotient - 1 : Quotient;
}

uint64 GrassRenderGroupKey(uint64 CellKey)
{
    return StaticRenderCellKey(FloorDivideCell(SignedCellFromHighKey(CellKey), kGrassRenderGroupSize), FloorDivideCell(SignedCellFromLowKey(CellKey), kGrassRenderGroupSize));
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

constexpr float kStaticCollisionCellSize = 8.0f;

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

void ExpandBounds(FBox3& Bounds, const FBox3& Other)
{
    if (!Other.IsValid())
    {
        return;
    }
    ExpandBounds(Bounds, Other.Min);
    ExpandBounds(Bounds, Other.Max);
}

bool BoundsInitialized(const FBox3& Bounds)
{
    return Bounds.Min.X <= Bounds.Max.X && Bounds.Min.Y <= Bounds.Max.Y && Bounds.Min.Z <= Bounds.Max.Z;
}

bool BoundsIntersect(const FBox3& A, const FBox3& B)
{
    return A.IsValid() && B.IsValid() && A.Min.X <= B.Max.X && A.Max.X >= B.Min.X && A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y && A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
}

float AxisValue(const FVector3& Value, int Axis)
{
    return Axis == 0 ? Value.X : Axis == 1 ? Value.Y : Value.Z;
}

FBox3 TransformBounds(const FBox3& Bounds, const D3DMATRIX& Matrix)
{
    const float centerX = (Bounds.Min.X + Bounds.Max.X) * 0.5f;
    const float centerY = (Bounds.Min.Y + Bounds.Max.Y) * 0.5f;
    const float centerZ = (Bounds.Min.Z + Bounds.Max.Z) * 0.5f;
    const float extentX = (Bounds.Max.X - Bounds.Min.X) * 0.5f;
    const float extentY = (Bounds.Max.Y - Bounds.Min.Y) * 0.5f;
    const float extentZ = (Bounds.Max.Z - Bounds.Min.Z) * 0.5f;
    const FVector3 center = TransformPoint(FVector3{centerX, centerY, centerZ}, Matrix);
    const float worldExtentX = std::abs(Matrix._11) * extentX + std::abs(Matrix._21) * extentY + std::abs(Matrix._31) * extentZ;
    const float worldExtentY = std::abs(Matrix._12) * extentX + std::abs(Matrix._22) * extentY + std::abs(Matrix._32) * extentZ;
    const float worldExtentZ = std::abs(Matrix._13) * extentX + std::abs(Matrix._23) * extentY + std::abs(Matrix._33) * extentZ;
    return FBox3{FVector3{center.X - worldExtentX, center.Y - worldExtentY, center.Z - worldExtentZ}, FVector3{center.X + worldExtentX, center.Y + worldExtentY, center.Z + worldExtentZ}};
}

bool InvertAffineMatrix(const D3DMATRIX& Matrix, D3DMATRIX& Out)
{
    const float Determinant = Matrix._11 * (Matrix._22 * Matrix._33 - Matrix._23 * Matrix._32) - Matrix._12 * (Matrix._21 * Matrix._33 - Matrix._23 * Matrix._31) + Matrix._13 * (Matrix._21 * Matrix._32 - Matrix._22 * Matrix._31);
    if (std::abs(Determinant) <= 0.000001f)
    {
        return false;
    }
    const float InvDet = 1.0f / Determinant;
    Out = {};
    Out._11 = (Matrix._22 * Matrix._33 - Matrix._23 * Matrix._32) * InvDet;
    Out._12 = (Matrix._13 * Matrix._32 - Matrix._12 * Matrix._33) * InvDet;
    Out._13 = (Matrix._12 * Matrix._23 - Matrix._13 * Matrix._22) * InvDet;
    Out._21 = (Matrix._23 * Matrix._31 - Matrix._21 * Matrix._33) * InvDet;
    Out._22 = (Matrix._11 * Matrix._33 - Matrix._13 * Matrix._31) * InvDet;
    Out._23 = (Matrix._13 * Matrix._21 - Matrix._11 * Matrix._23) * InvDet;
    Out._31 = (Matrix._21 * Matrix._32 - Matrix._22 * Matrix._31) * InvDet;
    Out._32 = (Matrix._12 * Matrix._31 - Matrix._11 * Matrix._32) * InvDet;
    Out._33 = (Matrix._11 * Matrix._22 - Matrix._12 * Matrix._21) * InvDet;
    Out._41 = -(Matrix._41 * Out._11 + Matrix._42 * Out._21 + Matrix._43 * Out._31);
    Out._42 = -(Matrix._41 * Out._12 + Matrix._42 * Out._22 + Matrix._43 * Out._32);
    Out._43 = -(Matrix._41 * Out._13 + Matrix._42 * Out._23 + Matrix._43 * Out._33);
    Out._44 = 1.0f;
    return true;
}

uint32 BuildCollisionBvhNode(StaticCollisionMesh& Mesh, uint32 First, uint32 Count)
{
    StaticCollisionBvhNode Node;
    Node.Bounds = EmptyBounds();
    FBox3 CenterBounds = EmptyBounds();
    for (uint32 Offset = 0; Offset < Count; ++Offset)
    {
        const auto& Triangle = Mesh.Triangles[Mesh.TriangleOrder[First + Offset]];
        ExpandBounds(Node.Bounds, Triangle.Bounds.Min);
        ExpandBounds(Node.Bounds, Triangle.Bounds.Max);
        ExpandBounds(CenterBounds, Triangle.Center);
    }
    const uint32 NodeIndex = static_cast<uint32>(Mesh.Nodes.size());
    Mesh.Nodes.push_back(Node);
    if (Count <= 8)
    {
        Mesh.Nodes[NodeIndex].First = First;
        Mesh.Nodes[NodeIndex].Count = Count;
        return NodeIndex;
    }
    const FVector3 Extent{CenterBounds.Max.X - CenterBounds.Min.X, CenterBounds.Max.Y - CenterBounds.Min.Y, CenterBounds.Max.Z - CenterBounds.Min.Z};
    const int Axis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : Extent.Y >= Extent.Z ? 1 : 2;
    const uint32 LeftCount = Count / 2;
    auto Begin = Mesh.TriangleOrder.begin() + First;
    auto Middle = Begin + LeftCount;
    auto End = Begin + Count;
    std::nth_element(Begin, Middle, End, [&](uint32 Left, uint32 Right) { return AxisValue(Mesh.Triangles[Left].Center, Axis) < AxisValue(Mesh.Triangles[Right].Center, Axis); });
    Mesh.Nodes[NodeIndex].Left = BuildCollisionBvhNode(Mesh, First, LeftCount);
    Mesh.Nodes[NodeIndex].Right = BuildCollisionBvhNode(Mesh, First + LeftCount, Count - LeftCount);
    return NodeIndex;
}

std::shared_ptr<const StaticCollisionMesh> BuildStaticCollisionMesh(const FMdlMesh& Mesh, const std::vector<uint16>& Indices)
{
    if (Indices.empty())
    {
        return {};
    }
    auto Collision = std::make_shared<StaticCollisionMesh>();
    Collision->Triangles.reserve(Indices.size() / 3);
    for (std::size_t Offset = 0; Offset + 2 < Indices.size(); Offset += 3)
    {
        const auto IA = static_cast<std::size_t>(Indices[Offset]);
        const auto IB = static_cast<std::size_t>(Indices[Offset + 1]);
        const auto IC = static_cast<std::size_t>(Indices[Offset + 2]);
        if (IA >= Mesh.Vertices.size() || IB >= Mesh.Vertices.size() || IC >= Mesh.Vertices.size())
        {
            continue;
        }
        StaticCollisionTriangle Triangle;
        Triangle.A = FVector3{Mesh.Vertices[IA].X, Mesh.Vertices[IA].Y, Mesh.Vertices[IA].Z};
        Triangle.B = FVector3{Mesh.Vertices[IB].X, Mesh.Vertices[IB].Y, Mesh.Vertices[IB].Z};
        Triangle.C = FVector3{Mesh.Vertices[IC].X, Mesh.Vertices[IC].Y, Mesh.Vertices[IC].Z};
        const FVector3 Normal = Cross(Subtract(Triangle.B, Triangle.A), Subtract(Triangle.C, Triangle.A));
        if (Dot(Normal, Normal) <= 0.0000001f)
        {
            continue;
        }
        Triangle.Center = Scale(Add(Add(Triangle.A, Triangle.B), Triangle.C), 1.0f / 3.0f);
        Triangle.Bounds = EmptyBounds();
        ExpandBounds(Triangle.Bounds, Triangle.A);
        ExpandBounds(Triangle.Bounds, Triangle.B);
        ExpandBounds(Triangle.Bounds, Triangle.C);
        Collision->Triangles.push_back(Triangle);
    }
    if (Collision->Triangles.empty())
    {
        return {};
    }
    Collision->TriangleOrder.resize(Collision->Triangles.size());
    for (uint32 Index = 0; Index < Collision->TriangleOrder.size(); ++Index)
    {
        Collision->TriangleOrder[Index] = Index;
    }
    Collision->Nodes.reserve(Collision->Triangles.size() * 2);
    BuildCollisionBvhNode(*Collision, 0, static_cast<uint32>(Collision->Triangles.size()));
    return Collision;
}

FVector3 ClosestPointOnTriangle(FVector3 Point, FVector3 A, FVector3 B, FVector3 C)
{
    const FVector3 AB = Subtract(B, A);
    const FVector3 AC = Subtract(C, A);
    const FVector3 AP = Subtract(Point, A);
    const float D1 = Dot(AB, AP);
    const float D2 = Dot(AC, AP);
    if (D1 <= 0.0f && D2 <= 0.0f)
    {
        return A;
    }
    const FVector3 BP = Subtract(Point, B);
    const float D3 = Dot(AB, BP);
    const float D4 = Dot(AC, BP);
    if (D3 >= 0.0f && D4 <= D3)
    {
        return B;
    }
    const float VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f)
    {
        return Add(A, Scale(AB, D1 / (D1 - D3)));
    }
    const FVector3 CP = Subtract(Point, C);
    const float D5 = Dot(AB, CP);
    const float D6 = Dot(AC, CP);
    if (D6 >= 0.0f && D5 <= D6)
    {
        return C;
    }
    const float VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f)
    {
        return Add(A, Scale(AC, D2 / (D2 - D6)));
    }
    const float VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0f && D4 - D3 >= 0.0f && D5 - D6 >= 0.0f)
    {
        return Add(B, Scale(Subtract(C, B), (D4 - D3) / ((D4 - D3) + (D5 - D6))));
    }
    const float Denominator = 1.0f / (VA + VB + VC);
    return Add(A, Add(Scale(AB, VB * Denominator), Scale(AC, VC * Denominator)));
}

float SegmentSegmentDistanceSquared(FVector3 P1, FVector3 Q1, FVector3 P2, FVector3 Q2, FVector3& OutA, FVector3& OutB)
{
    const FVector3 D1 = Subtract(Q1, P1);
    const FVector3 D2 = Subtract(Q2, P2);
    const FVector3 R = Subtract(P1, P2);
    const float A = Dot(D1, D1);
    const float E = Dot(D2, D2);
    const float F = Dot(D2, R);
    float S = 0.0f;
    float T = 0.0f;
    if (A <= 0.0000001f && E <= 0.0000001f)
    {
        OutA = P1;
        OutB = P2;
        return Dot(R, R);
    }
    if (A <= 0.0000001f)
    {
        T = std::clamp(F / E, 0.0f, 1.0f);
    }
    else
    {
        const float C = Dot(D1, R);
        if (E <= 0.0000001f)
        {
            S = std::clamp(-C / A, 0.0f, 1.0f);
        }
        else
        {
            const float B = Dot(D1, D2);
            const float Denominator = A * E - B * B;
            if (Denominator > 0.0000001f)
            {
                S = std::clamp((B * F - C * E) / Denominator, 0.0f, 1.0f);
            }
            T = (B * S + F) / E;
            if (T < 0.0f)
            {
                T = 0.0f;
                S = std::clamp(-C / A, 0.0f, 1.0f);
            }
            else if (T > 1.0f)
            {
                T = 1.0f;
                S = std::clamp((B - C) / A, 0.0f, 1.0f);
            }
        }
    }
    OutA = Add(P1, Scale(D1, S));
    OutB = Add(P2, Scale(D2, T));
    const FVector3 Delta = Subtract(OutA, OutB);
    return Dot(Delta, Delta);
}

bool SegmentIntersectsTriangle(FVector3 Start, FVector3 End, FVector3 A, FVector3 B, FVector3 C, FVector3& OutPoint)
{
    const FVector3 Direction = Subtract(End, Start);
    const FVector3 Edge1 = Subtract(B, A);
    const FVector3 Edge2 = Subtract(C, A);
    const FVector3 P = Cross(Direction, Edge2);
    const float Determinant = Dot(Edge1, P);
    if (std::abs(Determinant) <= 0.0000001f)
    {
        return false;
    }
    const float InvDet = 1.0f / Determinant;
    const FVector3 T = Subtract(Start, A);
    const float U = Dot(T, P) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }
    const FVector3 Q = Cross(T, Edge1);
    const float V = Dot(Direction, Q) * InvDet;
    if (V < 0.0f || U + V > 1.0f)
    {
        return false;
    }
    const float SegmentT = Dot(Edge2, Q) * InvDet;
    if (SegmentT < 0.0f || SegmentT > 1.0f)
    {
        return false;
    }
    OutPoint = Add(Start, Scale(Direction, SegmentT));
    return true;
}

float SegmentTriangleDistanceSquared(FVector3 Start, FVector3 End, FVector3 A, FVector3 B, FVector3 C, FVector3& OutSegmentPoint, FVector3& OutTrianglePoint)
{
    FVector3 Intersection{};
    if (SegmentIntersectsTriangle(Start, End, A, B, C, Intersection))
    {
        OutSegmentPoint = Intersection;
        OutTrianglePoint = Intersection;
        return 0.0f;
    }
    OutSegmentPoint = Start;
    OutTrianglePoint = ClosestPointOnTriangle(Start, A, B, C);
    float Best = Dot(Subtract(OutSegmentPoint, OutTrianglePoint), Subtract(OutSegmentPoint, OutTrianglePoint));
    const FVector3 EndTriangle = ClosestPointOnTriangle(End, A, B, C);
    const float EndDistance = Dot(Subtract(End, EndTriangle), Subtract(End, EndTriangle));
    if (EndDistance < Best)
    {
        Best = EndDistance;
        OutSegmentPoint = End;
        OutTrianglePoint = EndTriangle;
    }
    const std::array<std::pair<FVector3, FVector3>, 3> Edges{{{A, B}, {B, C}, {C, A}}};
    for (const auto& Edge : Edges)
    {
        FVector3 SegmentPoint{};
        FVector3 TrianglePoint{};
        const float Distance = SegmentSegmentDistanceSquared(Start, End, Edge.first, Edge.second, SegmentPoint, TrianglePoint);
        if (Distance < Best)
        {
            Best = Distance;
            OutSegmentPoint = SegmentPoint;
            OutTrianglePoint = TrianglePoint;
        }
    }
    return Best;
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

void BuildWorldBatchSourceTemplates(StaticModelResource& Resource)
{
    Resource.BatchSourceTemplates.clear();
    if (Resource.CpuVertices.empty() || Resource.CpuIndices.empty())
    {
        return;
    }
    const float modelHeight = Resource.Bounds.Max.Y - Resource.Bounds.Min.Y;
    const float inverseHeight = modelHeight > 0.0001f ? 1.0f / modelHeight : 0.0f;
    std::vector<int32> remap(Resource.CpuVertices.size(), -1);
    Resource.BatchSourceTemplates.reserve(Resource.Batches.size());
    for (const auto& sourceBatch : Resource.Batches)
    {
        FWorldBatchSourceTemplate sourceTemplate;
        sourceTemplate.Texture = sourceBatch.Texture;
        sourceTemplate.Bounds = EmptyBounds();
        const uint32 endIndex = (std::min)(sourceBatch.StartIndex + sourceBatch.IndexCount, static_cast<UINT>(Resource.CpuIndices.size()));
        const uint32 validIndexCount = endIndex > sourceBatch.StartIndex ? endIndex - sourceBatch.StartIndex : 0;
        sourceTemplate.Vertices.reserve((std::min)(static_cast<std::size_t>(validIndexCount), Resource.CpuVertices.size()));
        sourceTemplate.Indices.reserve(validIndexCount);
        std::fill(remap.begin(), remap.end(), -1);
        for (uint32 index = sourceBatch.StartIndex; index < endIndex; ++index)
        {
            const uint16 sourceIndex = Resource.CpuIndices[index];
            if (sourceIndex >= Resource.CpuVertices.size())
            {
                continue;
            }
            int32& mapped = remap[sourceIndex];
            if (mapped < 0)
            {
                mapped = static_cast<int32>(sourceTemplate.Vertices.size());
                const auto& vertex = Resource.CpuVertices[sourceIndex];
                sourceTemplate.Vertices.push_back(vertex);
                sourceTemplate.GrassWeights.push_back(std::clamp((Resource.Bounds.Max.Y - vertex.Y) * inverseHeight, 0.0f, 1.0f));
                ExpandBounds(sourceTemplate.Bounds, FVector3{vertex.X, vertex.Y, vertex.Z});
            }
            sourceTemplate.Indices.push_back(static_cast<uint32>(mapped));
        }
        if (!sourceTemplate.Vertices.empty() && !sourceTemplate.Indices.empty())
        {
            Resource.BatchSourceTemplates.push_back(std::move(sourceTemplate));
        }
    }
}

template <typename SourceRange>
void ReserveAccumulatedWorldBatches(const SourceRange& Sources, AccumulatedWorldBatchMap& Batches)
{
    struct Capacity
    {
        std::size_t Vertices = 0;
        std::size_t Indices = 0;
    };
    std::unordered_map<IDirect3DTexture9*, Capacity> capacities;
    capacities.reserve(16);
    for (const auto& source : Sources)
    {
        if (!source.Resource)
        {
            continue;
        }
        for (const auto& sourceTemplate : source.Resource->BatchSourceTemplates)
        {
            auto& capacity = capacities[sourceTemplate.Texture];
            capacity.Vertices += sourceTemplate.Vertices.size();
            capacity.Indices += sourceTemplate.Indices.size();
        }
    }
    Batches.reserve(capacities.size());
    for (const auto& [texture, capacity] : capacities)
    {
        auto& batch = Batches[texture];
        batch.Vertices.reserve(capacity.Vertices);
        batch.Indices.reserve(capacity.Indices);
    }
}

struct WorldBatchAccumulateScratch
{
    std::vector<uint32> Remap;
    std::vector<uint32> Generation;
    uint32 CurrentGeneration = 0;

    void Begin(std::size_t VertexCount)
    {
        if (Remap.size() < VertexCount)
        {
            Remap.resize(VertexCount);
            Generation.resize(VertexCount, 0);
        }
        ++CurrentGeneration;
        if (CurrentGeneration == 0)
        {
            std::fill(Generation.begin(), Generation.end(), 0);
            CurrentGeneration = 1;
        }
    }
};

void AccumulateWorldBatches(const StaticModelResource& Resource, const D3DMATRIX& World, AccumulatedWorldBatchMap& Batches, bool GrassWind = false, DWORD Tint = 0xfffffffful, WorldBatchAccumulateScratch* ExternalScratch = nullptr)
{
    if (Resource.CpuVertices.empty() || Resource.CpuIndices.empty())
    {
        return;
    }
    if (!Resource.BatchSourceTemplates.empty())
    {
        const float rootWorldY = Resource.Bounds.Max.Y * World._22 + World._42;
        for (const auto& sourceTemplate : Resource.BatchSourceTemplates)
        {
            auto& output = Batches[sourceTemplate.Texture];
            const uint32 vertexBase = static_cast<uint32>(output.Vertices.size());
            const std::size_t oldVertexCount = output.Vertices.size();
            const std::size_t oldIndexCount = output.Indices.size();
            output.Vertices.resize(oldVertexCount + sourceTemplate.Vertices.size());
            output.Indices.resize(oldIndexCount + sourceTemplate.Indices.size());
            WorldVertex* destinationVertices = output.Vertices.data() + oldVertexCount;
            for (std::size_t index = 0; index < sourceTemplate.Vertices.size(); ++index)
            {
                const auto& source = sourceTemplate.Vertices[index];
                auto& vertex = destinationVertices[index];
                vertex = source;
                vertex.X = source.X * World._11 + source.Y * World._21 + source.Z * World._31 + World._41;
                vertex.Y = source.X * World._12 + source.Y * World._22 + source.Z * World._32 + World._42;
                vertex.Z = source.X * World._13 + source.Y * World._23 + source.Z * World._33 + World._43;
                vertex.NX = source.NX * World._11 + source.NY * World._21 + source.NZ * World._31;
                vertex.NY = source.NX * World._12 + source.NY * World._22 + source.NZ * World._32;
                vertex.NZ = source.NX * World._13 + source.NY * World._23 + source.NZ * World._33;
                if (GrassWind)
                {
                    vertex.DetailU = sourceTemplate.GrassWeights[index];
                    vertex.DetailV = rootWorldY;
                    vertex.Diffuse = Tint;
                }
            }
            uint32* destinationIndices = output.Indices.data() + oldIndexCount;
            for (std::size_t index = 0; index < sourceTemplate.Indices.size(); ++index)
            {
                destinationIndices[index] = vertexBase + sourceTemplate.Indices[index];
            }
            ExpandBounds(output.Bounds, TransformBounds(sourceTemplate.Bounds, World));
        }
        return;
    }
    WorldBatchAccumulateScratch localScratch;
    auto& scratch = ExternalScratch ? *ExternalScratch : localScratch;
    const float ModelHeight = Resource.Bounds.Max.Y - Resource.Bounds.Min.Y;
    const float InvModelHeight = ModelHeight > 0.0001f ? 1.0f / ModelHeight : 0.0f;
    const float RootWorldY = Resource.Bounds.Max.Y * World._22 + World._42;
    for (const auto& sourceBatch : Resource.Batches)
    {
        auto& output = Batches[sourceBatch.Texture];
        const uint32 endIndex = (std::min)(sourceBatch.StartIndex + sourceBatch.IndexCount, static_cast<UINT>(Resource.CpuIndices.size()));
        const uint32 validIndexCount = endIndex > sourceBatch.StartIndex ? endIndex - sourceBatch.StartIndex : 0;
        output.Indices.reserve(output.Indices.size() + validIndexCount);
        output.Vertices.reserve(output.Vertices.size() + (std::min)(static_cast<std::size_t>(validIndexCount), Resource.CpuVertices.size()));
        scratch.Begin(Resource.CpuVertices.size());
        for (uint32 index = sourceBatch.StartIndex; index < endIndex; ++index)
        {
            const uint16 sourceIndex = Resource.CpuIndices[index];
            if (sourceIndex >= Resource.CpuVertices.size())
            {
                continue;
            }
            if (scratch.Generation[sourceIndex] != scratch.CurrentGeneration)
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
                scratch.Remap[sourceIndex] = static_cast<uint32>(output.Vertices.size());
                scratch.Generation[sourceIndex] = scratch.CurrentGeneration;
                output.Vertices.push_back(vertex);
            }
            output.Indices.push_back(scratch.Remap[sourceIndex]);
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

std::vector<WorldRenderCpuBatch> TakeWorldCpuBatches(AccumulatedWorldBatchMap& Source)
{
    std::vector<WorldRenderCpuBatch> output;
    output.reserve(Source.size());
    for (auto& [texture, source] : Source)
    {
        if (source.Vertices.empty() || source.Indices.empty() || !BoundsInitialized(source.Bounds))
        {
            continue;
        }
        WorldRenderCpuBatch batch;
        batch.Texture = texture;
        batch.Vertices = std::move(source.Vertices);
        batch.Indices = std::move(source.Indices);
        batch.Bounds = source.Bounds;
        output.push_back(std::move(batch));
    }
    return output;
}

bool UploadWorldCpuBatch(IDirect3DDevice9* Device, WorldRenderCpuBatch& Source, WorldRenderBatch& Output)
{
    if (!Device || Source.Vertices.empty() || Source.Indices.empty() || !BoundsInitialized(Source.Bounds))
    {
        return false;
    }
    Output.Texture = Source.Texture;
    Output.Bounds = Source.Bounds;
    Output.VertexCount = static_cast<UINT>(Source.Vertices.size());
    Output.IndexCount = static_cast<UINT>(Source.Indices.size());
    if (!TryCreateManagedVertexBuffer(Device, Source.Vertices, kWorldVertexFvf, Output.VertexBuffer) || !TryCreateManagedIndexBuffer(Device, Source.Indices, D3DFMT_INDEX32, Output.IndexBuffer))
    {
        SafeRelease(Output.IndexBuffer);
        SafeRelease(Output.VertexBuffer);
        return false;
    }
    return true;
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

bool BuildMdlBoneLocalMatrix(const FMdlMesh& Mesh, std::size_t Bone, int Frame, D3DMATRIX& Out)
{
    if (Bone >= Mesh.Objects.size())
    {
        return false;
    }
    const auto& Object = Mesh.Objects[Bone];
    FMdlTransformKey Key;
    if (Object.IsAnimated == 0)
    {
        if (Object.KeyIndex < 0 || static_cast<std::size_t>(Object.KeyIndex) >= Mesh.TransformKeys.size())
        {
            return false;
        }
        Key = Mesh.TransformKeys[static_cast<std::size_t>(Object.KeyIndex)];
    }
    else
    {
        if (Object.KeyIndex < 0 || Frame < 0)
        {
            return false;
        }
        const std::size_t Index = static_cast<std::size_t>(Object.KeyIndex) + static_cast<std::size_t>(Frame);
        if (Index >= Mesh.SkinIndices.size())
        {
            return false;
        }
        const auto& Entry = Mesh.SkinIndices[Index];
        const std::size_t Record = static_cast<std::size_t>(Entry.Record);
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
            const float InvLength = Length > 0.00000001f ? 1.0f / Length : 1.0f;
            Key.QW = QW * InvLength;
            Key.QX = QX * InvLength;
            Key.QY = QY * InvLength;
            Key.QZ = QZ * InvLength;
        }
    }
    Out = MdlQuatTranslationMatrix(Key.QW, Key.QX, Key.QY, Key.QZ, Key.X, Key.Y, Key.Z);
    return true;
}

bool ApplyMdlRestPose(FMdlMesh& Mesh, int Frame = 0)
{
    const std::size_t BoneCount = Mesh.Objects.size();
    if (Mesh.Info.SkinWeightCount == 0 || BoneCount == 0 || Mesh.TransformKeys.empty())
    {
        return false;
    }
    std::vector<D3DMATRIX> Local(BoneCount, IdentityMatrix());
    for (std::size_t Index = 0; Index < BoneCount; ++Index)
    {
        if (!BuildMdlBoneLocalMatrix(Mesh, Index, Frame, Local[Index]))
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

void BuildMdlAnimationCache(StaticModelResource& Resource)
{
    const auto& Mesh = Resource.BindMesh;
    const std::size_t BoneCount = Mesh.Objects.size();
    Resource.VertexBones.assign(Mesh.Vertices.size(), -1);
    for (const auto& Surface : Mesh.Surfaces)
    {
        if (Surface.FirstVertexIndex < 0 || Surface.VertexCount < 0 || static_cast<std::size_t>(Surface.ObjectIndex) >= BoneCount)
        {
            continue;
        }
        for (int VertexOffset = 0; VertexOffset < Surface.VertexCount; ++VertexOffset)
        {
            const std::size_t VertexIndex = static_cast<std::size_t>(Surface.FirstVertexIndex) + static_cast<std::size_t>(VertexOffset);
            if (VertexIndex < Resource.VertexBones.size())
            {
                Resource.VertexBones[VertexIndex] = static_cast<int>(Surface.ObjectIndex);
            }
        }
    }

    Resource.BoneParents.assign(BoneCount, -1);
    for (std::size_t Parent = 0; Parent < BoneCount; ++Parent)
    {
        const auto& Object = Mesh.Objects[Parent];
        for (int ChildOffset = 0; ChildOffset < Object.ConnectedBoneCount; ++ChildOffset)
        {
            const std::size_t ObjectIndex = static_cast<std::size_t>(Object.ObjectIndexOffset) + static_cast<std::size_t>(ChildOffset);
            if (ObjectIndex >= Mesh.ObjectIndices.size())
            {
                continue;
            }
            const std::size_t Child = static_cast<std::size_t>(Mesh.ObjectIndices[ObjectIndex]);
            if (Child < BoneCount && Child != Parent && Resource.BoneParents[Child] < 0)
            {
                Resource.BoneParents[Child] = static_cast<int>(Parent);
            }
        }
    }

    Resource.BoneEvaluationOrder.clear();
    Resource.BoneEvaluationOrder.reserve(BoneCount);
    std::vector<uint8> Added(BoneCount, 0);
    while (Resource.BoneEvaluationOrder.size() < BoneCount)
    {
        bool Progress = false;
        for (std::size_t Bone = 0; Bone < BoneCount; ++Bone)
        {
            if (Added[Bone])
            {
                continue;
            }
            const int Parent = Resource.BoneParents[Bone];
            if (Parent >= 0 && !Added[static_cast<std::size_t>(Parent)])
            {
                continue;
            }
            Added[Bone] = 1;
            Resource.BoneEvaluationOrder.push_back(Bone);
            Progress = true;
        }
        if (Progress)
        {
            continue;
        }
        for (std::size_t Bone = 0; Bone < BoneCount; ++Bone)
        {
            if (!Added[Bone])
            {
                Resource.BoneParents[Bone] = -1;
                Added[Bone] = 1;
                Resource.BoneEvaluationOrder.push_back(Bone);
                break;
            }
        }
    }
    Resource.BoneLocalScratch.assign(BoneCount, IdentityMatrix());
    Resource.BoneWorldScratch.assign(BoneCount, IdentityMatrix());
    Resource.PoseFrameCache.clear();
    Resource.PoseFrameCache.resize(static_cast<std::size_t>((std::max)(0, Resource.FrameCount)));
    Resource.PoseFrameReady.assign(Resource.PoseFrameCache.size(), 0);
}

bool BuildMdlPoseVertices(StaticModelResource& Resource, int Frame, std::vector<FCharacterPoseVertex>& OutVertices)
{
    const auto& Mesh = Resource.BindMesh;
    const std::size_t BoneCount = Mesh.Objects.size();
    if (Mesh.Info.SkinWeightCount == 0 || BoneCount == 0 || Mesh.TransformKeys.empty() || Resource.VertexBones.size() != Mesh.Vertices.size() || Resource.BoneParents.size() != BoneCount || Resource.BoneEvaluationOrder.size() != BoneCount)
    {
        return false;
    }
    if (Resource.BoneLocalScratch.size() != BoneCount || Resource.BoneWorldScratch.size() != BoneCount)
    {
        Resource.BoneLocalScratch.assign(BoneCount, IdentityMatrix());
        Resource.BoneWorldScratch.assign(BoneCount, IdentityMatrix());
    }

    for (std::size_t Bone = 0; Bone < BoneCount; ++Bone)
    {
        if (!BuildMdlBoneLocalMatrix(Mesh, Bone, Frame, Resource.BoneLocalScratch[Bone]))
        {
            return false;
        }
    }

    for (const std::size_t Bone : Resource.BoneEvaluationOrder)
    {
        const int Parent = Resource.BoneParents[Bone];
        Resource.BoneWorldScratch[Bone] = Parent >= 0 ? MultiplyMatrix(Resource.BoneWorldScratch[static_cast<std::size_t>(Parent)], Resource.BoneLocalScratch[Bone]) : Resource.BoneLocalScratch[Bone];
    }

    OutVertices.resize(Mesh.Vertices.size());
    for (std::size_t VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
    {
        const auto& Source = Mesh.Vertices[VertexIndex];
        auto& Vertex = OutVertices[VertexIndex];
        Vertex = FCharacterPoseVertex{Source.X, Source.Y, Source.Z, Source.NX, Source.NY, Source.NZ};
        const int Bone = Resource.VertexBones[VertexIndex];
        if (Bone < 0 || static_cast<std::size_t>(Bone) >= Resource.BoneWorldScratch.size())
        {
            continue;
        }
        const D3DMATRIX& Matrix = Resource.BoneWorldScratch[static_cast<std::size_t>(Bone)];
        Vertex.X = Matrix._11 * Source.X + Matrix._12 * Source.Y + Matrix._13 * Source.Z + Matrix._14;
        Vertex.Y = Matrix._21 * Source.X + Matrix._22 * Source.Y + Matrix._23 * Source.Z + Matrix._24;
        Vertex.Z = Matrix._31 * Source.X + Matrix._32 * Source.Y + Matrix._33 * Source.Z + Matrix._34;
        Vertex.NX = Matrix._11 * Source.NX + Matrix._12 * Source.NY + Matrix._13 * Source.NZ;
        Vertex.NY = Matrix._21 * Source.NX + Matrix._22 * Source.NY + Matrix._23 * Source.NZ;
        Vertex.NZ = Matrix._31 * Source.NX + Matrix._32 * Source.NY + Matrix._33 * Source.NZ;
    }
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
    std::vector<uint16> CollisionIndices;
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
                CollisionIndices.push_back(ia);
                CollisionIndices.push_back(ib);
                CollisionIndices.push_back(ic);
            }
        }
    }

    resource->Bounds.Min = FVector3{mesh.Bounds.MinX, mesh.Bounds.MinY, mesh.Bounds.MinZ};
    resource->Bounds.Max = FVector3{mesh.Bounds.MaxX, mesh.Bounds.MaxY, mesh.Bounds.MaxZ};
    resource->CollisionMesh = IsSkinned ? std::shared_ptr<const StaticCollisionMesh>{} : BuildStaticCollisionMesh(mesh, CollisionIndices);
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

bool StaticPathAllowed(std::string_view rel, const std::vector<std::string>& configuredDirs, bool recursive)
{
    if (configuredDirs.empty())
    {
        return true;
    }
    for (const auto& dir : configuredDirs)
    {
        if (rel == dir)
        {
            return true;
        }
        const std::string prefix = dir + "/";
        if (rel.starts_with(prefix) && (recursive || rel.find('/', prefix.size()) == std::string_view::npos))
        {
            return true;
        }
    }
    return false;
}

bool StaticPlacementInRange(const FVector3& position, float centerX, float centerY, float centerZ, float radiusSquared)
{
    const float dx = position.X - centerX;
    const float dy = position.Y - centerY;
    const float dz = position.Z - centerZ;
    return dx * dx + dy * dy + dz * dz <= radiusSquared;
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

bool StartsWithNumberedPlacementName(std::string_view key, std::string_view prefix)
{
    return key.size() > prefix.size() && key.starts_with(prefix) && std::isdigit(static_cast<unsigned char>(key[prefix.size()]));
}

enum class EStaticPlacementKind
{
    Renderable,
    RuntimeActorPlaceholder,
    TechnicalMarker
};

bool IsDoorMarkerPlacementName(std::string_view key)
{
    return key == "edoor" || key == "edoor2" || key == "edoor_i" || key == "edoor2_i";
}

EStaticPlacementKind ClassifyStaticPlacement(std::string_view key)
{
    if (key == "char19" || IsDoorMarkerPlacementName(key))
    {
        return EStaticPlacementKind::RuntimeActorPlaceholder;
    }
    if (StartsWithNumberedPlacementName(key, "m_way") || key == "m_enter" || StartsWithNumberedPlacementName(key, "pyram") || StartsWithNumberedPlacementName(key, "tn3_light") || StartsWithNumberedPlacementName(key, "start") || key == "finish")
    {
        return EStaticPlacementKind::TechnicalMarker;
    }
    return EStaticPlacementKind::Renderable;
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
        if ((!rel.ends_with(".mbd") && !rel.ends_with(".mb")) || !StaticPathAllowed(rel, configuredDirs, config.StaticObjectRecursive))
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
            const EStaticPlacementKind kind = ClassifyStaticPlacement(source.Key);
            if (kind == EStaticPlacementKind::RuntimeActorPlaceholder)
            {
                ++result.SkippedRuntimeActorPlaceholders;
                continue;
            }
            if (kind == EStaticPlacementKind::TechnicalMarker)
            {
                ++result.SkippedTechnicalMarkers;
                continue;
            }
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
    StaticCollisionInstances.clear();
    StaticCollisionCells.clear();
    LargeStaticCollisionInstances.clear();
    StaticCollisionVisitMarks.clear();
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
    if (Logger)
    {
        Logger->Info("Static placements loaded=" + std::to_string(StaticPlacements.size()) + ", models=" + std::to_string(StaticPlacementModels.size()) + ", skipped_runtime_actor_placeholders=" + std::to_string(result.SkippedRuntimeActorPlaceholders) + ", skipped_technical_markers=" + std::to_string(result.SkippedTechnicalMarkers));
    }
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

void FD3D9GameWorldScene::Impl::BuildMonsterModelIndex()
{
    if (MonsterModelIndexReady) { return; }
    MonsterModelIndexReady = true;
    MonsterModelNames.clear();
    if (!AssetResources) { return; }
    for (const auto& record : AssetResources->Catalog().All())
    {
        const std::string relative = Common::NormalizePathKey(record.RelativePath.generic_string());
        if (!relative.ends_with("group_monst.cfg")) { continue; }
        auto blob = AssetResources->Load(record.RelativePath.generic_string());
        if (!blob.IsOk()) { continue; }
        const FByteArray& bytes = blob.Value().Bytes;
        if (bytes.empty()) { continue; }
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        size_t lineStart = 0;
        while (lineStart < text.size())
        {
            const size_t lineEnd = text.find_first_of("\r\n", lineStart);
            const std::string_view line(text.data() + lineStart, (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);
            size_t cursor = 0;
            auto nextToken = [&]() -> std::string_view
            {
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0) { ++cursor; }
                const size_t start = cursor;
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) == 0) { ++cursor; }
                return line.substr(start, cursor - start);
            };
            const std::string_view idToken = nextToken();
            const std::string_view kindToken = nextToken();
            const std::string_view modelToken = nextToken();
            if (!idToken.empty() && kindToken.starts_with("monster") && !modelToken.empty())
            {
                std::string idText(idToken);
                char* end = nullptr;
                const unsigned long parsed = std::strtoul(idText.c_str(), &end, 10);
                if (end != idText.c_str() && *end == '\0' && parsed <= std::numeric_limits<uint32>::max()) { MonsterModelNames.emplace(static_cast<uint32>(parsed), std::string(modelToken)); }
            }
            if (lineEnd == std::string::npos) { break; }
            lineStart = lineEnd + 1;
            if (lineStart < text.size() && text[lineStart - 1] == '\r' && text[lineStart] == '\n') { ++lineStart; }
        }
        break;
    }
    if (Logger) { Logger->Info("Runtime monster model mappings loaded=" + std::to_string(MonsterModelNames.size())); }
}

std::optional<std::string> FD3D9GameWorldScene::Impl::ResolveRemoteActorModelName(const FRemoteGameActor& actor)
{
    if (!SferaProtocol::IsMonsterObjectType(actor.ObjectType)) { return std::nullopt; }
    BuildModelPathIndex();
    BuildMonsterModelIndex();
    for (const uint32 modelType : actor.ModelTypeCandidates)
    {
        const auto mapped = MonsterModelNames.find(modelType);
        if (mapped == MonsterModelNames.end()) { continue; }
        const std::string key = LowercaseAscii(mapped->second);
        if (ModelPathIndex.find(key) != ModelPathIndex.end()) { return mapped->second; }
    }
    return std::nullopt;
}

void FD3D9GameWorldScene::Impl::RefreshRemoteActorResource(FRemoteActorRenderState& actor)
{
    actor.Resource = nullptr;
    actor.ModelName.clear();
    actor.BoundsValid = false;
    const auto modelName = ResolveRemoteActorModelName(actor.Actor);
    if (!modelName)
    {
        if (Logger) { Logger->Warning("runtime actor model unresolved: entity=" + std::to_string(actor.Actor.EntityId) + ", object_type=" + std::to_string(actor.Actor.ObjectType)); }
        return;
    }
    actor.ModelName = *modelName;
    const std::string resourceKey = LowercaseAscii(*modelName);
    if (const auto loaded = StaticResources.find(resourceKey); loaded != StaticResources.end())
    {
        actor.Resource = loaded->second.get();
        return;
    }
    try
    {
        QueueStaticModelCpuPreload(*modelName, ResolveModelPath(*modelName), true);
    }
    catch (const std::exception& exception)
    {
        actor.ModelName.clear();
        if (Logger) { Logger->Warning("runtime actor model preload failed: entity=" + std::to_string(actor.Actor.EntityId) + ", model=" + *modelName + ", error=" + exception.what()); }
    }
}

void FD3D9GameWorldScene::Impl::UpsertRemoteActor(const FRemoteGameActor& actor)
{
    if (actor.EntityId == 0 || !std::isfinite(actor.Position.X) || !std::isfinite(actor.Position.Y) || !std::isfinite(actor.Position.Z) || !std::isfinite(actor.Position.Angle)) { return; }
    auto [iterator, inserted] = RemoteActors.try_emplace(actor.EntityId);
    FRemoteActorRenderState& target = iterator->second;
    const bool modelChanged = inserted || target.Actor.ObjectType != actor.ObjectType || target.Actor.ModelTypeCandidates != actor.ModelTypeCandidates;
    target.Actor = actor;
    target.BoundsValid = false;
    if (modelChanged || !target.Resource) { RefreshRemoteActorResource(target); }
}

void FD3D9GameWorldScene::Impl::UpdateRemoteActorPosition(uint64 entityId, const FGameWorldPosition& position)
{
    const auto iterator = RemoteActors.find(entityId);
    if (iterator == RemoteActors.end() || !std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(position.Z) || !std::isfinite(position.Angle)) { return; }
    iterator->second.Actor.Position = position;
    iterator->second.BoundsValid = false;
}

void FD3D9GameWorldScene::Impl::RemoveRemoteActor(uint64 entityId)
{
    RemoteActors.erase(entityId);
}

void FD3D9GameWorldScene::Impl::ClearRemoteActors()
{
    RemoteActors.clear();
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
        StaticModelCpuPreloadTarget Target;
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
            Target = std::move(PendingStaticModelCpuPreloads.front());
            PendingStaticModelCpuPreloads.pop_front();
        }

        std::unique_ptr<StaticModelResource> resource;
        try
        {
            resource = LoadStaticModelCpuBackedResource(Target.ModelName, Target.ModelPath);
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
            if (resource)
            {
                CompletedStaticModelCpuPreloads.push_back(FStaticModelCpuPreloadResult{Target, std::move(resource)});
            }
            else
            {
                QueuedStaticModelCpuPreloads.erase(Target.Key);
            }
        }
    }
}

void FD3D9GameWorldScene::Impl::QueueStaticModelCpuPreload(const std::string& ModelName, const std::filesystem::path& ModelPath, bool HighPriority)
{
    if (ModelName.empty() || ModelPath.empty())
    {
        return;
    }
    const std::string ResourceKey = LowercaseAscii(ModelName);
    if (StaticResources.find(ResourceKey) != StaticResources.end() || QueuedStaticGpuPromotions.contains(ResourceKey))
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
        StaticModelCpuPreloadTarget target{ModelName, ModelPath, Key, HighPriority};
        if (HighPriority)
        {
            PendingStaticModelCpuPreloads.push_front(std::move(target));
        }
        else
        {
            PendingStaticModelCpuPreloads.push_back(std::move(target));
        }
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

std::unique_ptr<StaticModelResource> FD3D9GameWorldScene::Impl::LoadStaticModelCpuBackedResource(
    const std::string& ModelName,
    const std::filesystem::path& ModelPath)
{
    const auto cpu = LoadStaticModelCpuResourceCached(ModelName, ModelPath);
    auto resource = std::make_unique<StaticModelResource>();
    resource->VertexCount = static_cast<UINT>(cpu->Vertices.size());
    resource->Bounds = cpu->Bounds;
    resource->IsSkinned = cpu->IsSkinned;
    resource->CollisionMesh = cpu->CollisionMesh;
    resource->CpuVertices = cpu->Vertices;
    resource->CpuIndices = cpu->Indices;
    if (resource->IsSkinned)
    {
        resource->BindMesh = cpu->BindMesh;
        resource->AnimationVertices.resize(cpu->Vertices.size());
        resource->CpuStaticAttributes.resize(cpu->Vertices.size());
        for (std::size_t index = 0; index < cpu->Vertices.size(); ++index)
        {
            const auto& source = cpu->Vertices[index];
            resource->AnimationVertices[index] = FAnimatedWorldVertex{source.X, source.Y, source.Z, source.NX, source.NY, source.NZ};
            resource->CpuStaticAttributes[index] = FWorldVertexStaticAttributes{source.Diffuse, source.U, source.V, source.DetailU, source.DetailV};
        }
        std::size_t MaxFrames = 0;
        bool FirstAnimatedBone = true;
        for (const auto& Object : resource->BindMesh.Objects)
        {
            if (Object.IsAnimated == 0 || Object.KeyIndex < 0)
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
        BuildMdlAnimationCache(*resource);
        for (int frame = 0; frame < resource->FrameCount; ++frame)
        {
            const std::size_t frameIndex = static_cast<std::size_t>(frame);
            if (!BuildMdlPoseVertices(*resource, frame, resource->PoseFrameCache[frameIndex]))
            {
                auto& fallbackPose = resource->PoseFrameCache[frameIndex];
                fallbackPose.resize(resource->BindMesh.Vertices.size());
                for (std::size_t vertexIndex = 0; vertexIndex < resource->BindMesh.Vertices.size(); ++vertexIndex)
                {
                    const auto& source = resource->BindMesh.Vertices[vertexIndex];
                    fallbackPose[vertexIndex] = FCharacterPoseVertex{source.X, source.Y, source.Z, source.NX, source.NY, source.NZ};
                }
            }
            resource->PoseFrameReady[frameIndex] = 1;
        }
    }
    resource->Batches.reserve(cpu->Batches.size());
    resource->CpuTexturePaths.reserve(cpu->Batches.size());
    resource->CpuTextureBytes.reserve(cpu->Batches.size());
    for (const auto& cpuBatch : cpu->Batches)
    {
        FSceneBatch batch;
        batch.StartIndex = cpuBatch.StartIndex;
        batch.IndexCount = cpuBatch.IndexCount;
        resource->Batches.push_back(std::move(batch));
        const auto texturePath = ResolveModelTexturePath(ModelPath, cpuBatch.MaterialName);
        resource->CpuTexturePaths.push_back(texturePath);
        resource->CpuTextureBytes.push_back(ReadGameWorldFileBytes(texturePath));
    }
    return resource;
}

bool FD3D9GameWorldScene::Impl::AdvanceStaticModelGpuPromotion(FStaticModelGpuPromotion& Promotion)
{
    if (!Promotion.Resource)
    {
        Promotion.Resource = LoadStaticModelCpuBackedResource(Promotion.Target.ModelName, Promotion.Target.ModelPath);
        return false;
    }
    auto& resource = *Promotion.Resource;
    const auto cpu = LoadStaticModelCpuResourceCached(Promotion.Target.ModelName, Promotion.Target.ModelPath);
    if (resource.IsSkinned && !resource.AnimatedVertexBuffer)
    {
        resource.AnimatedVertexBuffer = CreateDynamicVertexBufferOrThrow(Device, resource.AnimationVertices, "CreateVertexBuffer animated positions");
        return false;
    }
    if (resource.IsSkinned && !resource.StaticAttributeVertexBuffer)
    {
        resource.StaticAttributeVertexBuffer = CreateManagedVertexBufferOrThrow(Device, resource.CpuStaticAttributes, 0, "CreateVertexBuffer animated attributes");
        resource.CpuStaticAttributes.clear();
        return false;
    }
    if (Promotion.NextTexture < resource.Batches.size())
    {
        const std::size_t index = Promotion.NextTexture++;
        if (index >= resource.CpuTexturePaths.size() || index >= resource.CpuTextureBytes.size())
        {
            throw std::runtime_error("static model texture preload state is incomplete: " + Promotion.Target.ModelName);
        }
        resource.Batches[index].Texture = LoadCachedDdsTextureFromBytes(resource.CpuTexturePaths[index], resource.CpuTextureBytes[index]);
        FByteArray{}.swap(resource.CpuTextureBytes[index]);
        return false;
    }
    if ((!resource.IsSkinned || !WorldShadersReady || !AnimatedWorldDecl) && !resource.VertexBuffer)
    {
        resource.VertexBuffer = CreateManagedVertexBufferOrThrow(Device, cpu->Vertices, kWorldVertexFvf, "CreateVertexBuffer static model");
        return false;
    }
    if (!resource.IndexBuffer)
    {
        resource.IndexBuffer = CreateManagedIndexBufferOrThrow(Device, cpu->Indices, D3DFMT_INDEX16, "CreateIndexBuffer static model");
        return false;
    }
    if (!resource.IsSkinned && resource.BatchSourceTemplates.empty())
    {
        BuildWorldBatchSourceTemplates(resource);
    }
    resource.CpuTexturePaths.clear();
    resource.CpuTextureBytes.clear();
    return true;
}

std::unique_ptr<StaticModelResource> FD3D9GameWorldScene::Impl::LoadStaticModelResource(
    const std::string& ModelName,
    const std::filesystem::path& ModelPath)
{
    FStaticModelGpuPromotion promotion;
    promotion.Target = StaticModelCpuPreloadTarget{ModelName, ModelPath, StaticModelCpuCacheKey(ModelPath)};
    while (!AdvanceStaticModelGpuPromotion(promotion))
    {
    }
    return std::move(promotion.Resource);
}

void FD3D9GameWorldScene::Impl::LoadVisibleStaticObjects()
{
    const float RadiusSquared = Config.StaticObjectRadius * Config.StaticObjectRadius;
    const float VerticalRefreshDistance = (std::max)(1.0f, Config.StaticObjectRadius * 0.1f);
    const bool needsPlan = !StaticVisibilityPlanReady || std::abs(SpawnX - StaticVisibilityAnchorX) > 1.0f || std::abs(SpawnY - StaticVisibilityAnchorY) > VerticalRefreshDistance || std::abs(SpawnZ - StaticVisibilityAnchorZ) > 1.0f;
    if (needsPlan)
    {
        VisibleStaticPlacementIndices.clear();
        VisibleStaticRenderCells.clear();
        std::unordered_set<uint64> visibleCells;
        StaticVisibilityAnchorX = SpawnX;
        StaticVisibilityAnchorY = SpawnY;
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
                    if (!StaticPlacementInRange(placement.Position, SpawnX, SpawnY, SpawnZ, RadiusSquared) || placement.ModelId >= StaticPlacementModels.size())
                    {
                        continue;
                    }
                    VisibleStaticPlacementIndices.push_back(index);
                    visibleCells.insert(CellKey);
                }
            }
        }
        VisibleStaticRenderCells.assign(visibleCells.begin(), visibleCells.end());
        StaticDrawBatchesDirty = true;
        StaticVisibilityPlanReady = true;
    }
    bool allResourcesReady = true;
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
            if (StaticInitialBlockingLoad)
            {
                StaticResources.emplace(model.Key, LoadStaticModelResource(model.Name, ModelPath));
            }
            else
            {
                QueueStaticModelCpuPreload(model.Name, ModelPath);
                allResourcesReady = false;
            }
        }
    }
    if (!allResourcesReady && !StaticInitialBlockingLoad)
    {
        StaticRefreshPending = true;
        return;
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
            placement.Bounds = TransformBounds(it->second->Bounds, placement.World);
            placement.BoundsValid = placement.Bounds.IsValid();
        }
        StaticInstances.push_back(StaticInstance{it->second.get(), placement.World, placement.Bounds, StaticRenderCellKeyForPoint(placement.Position.X, placement.Position.Z, Config.TileSize)});
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
    DirectStaticInstancesDirty = true;
    RebuildStaticCollisionIndex();
    // Static geometry is transformed by the background bake worker. Missing cells stay on the direct path until the completed GPU batches are installed.
    BuildVisibleStaticRenderBatches();
    StaticRefreshPending = false;
}


void FD3D9GameWorldScene::Impl::ClearStaticRenderBatches()
{
    DrainStaticRenderCellBakeJobs(true);
    for (auto& upload : PendingStaticRenderGpuUploads)
    {
        ReleaseWorldRenderBatches(upload.GpuBatches);
    }
    PendingStaticRenderGpuUploads.clear();
    ReleaseWorldRenderBatchMap(StaticCellRenderBatches);
    QueuedStaticRenderBakeCells.clear();
    StaticDrawBatches.clear();
    DirectStaticInstanceIndices.clear();
    StaticDrawBatchesDirty = true;
    DirectStaticInstancesDirty = true;
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
            continue;
        }
        const auto& world = placement.World;
        if (!placement.BoundsValid)
        {
            placement.Bounds = TransformBounds(resource->Bounds, world);
            placement.BoundsValid = placement.Bounds.IsValid();
        }
        AccumulateWorldBatches(*resource, world, batchesByTexture);
    }
    std::vector<WorldRenderBatch> baked;
    if (UploadWorldBatches(Device, batchesByTexture, baked))
    {
        StaticCellRenderBatches.emplace(CellKey, std::move(baked));
        StaticDrawBatchesDirty = true;
        DirectStaticInstancesDirty = true;
    }
}

void FD3D9GameWorldScene::Impl::StartStaticRenderBakeWorker()
{
    std::lock_guard<std::mutex> lock(StaticRenderBakeMutex);
    if (StaticRenderBakeWorkerStarted)
    {
        return;
    }
    StaticRenderBakeStop = false;
    StaticRenderBakeBusy = false;
    StaticRenderBakeWorkerStarted = true;
    StaticRenderBakeThread = std::thread([this]()
    {
        StaticRenderBakeWorkerMain();
    });
}

void FD3D9GameWorldScene::Impl::StopStaticRenderBakeWorker()
{
    {
        std::lock_guard<std::mutex> lock(StaticRenderBakeMutex);
        if (!StaticRenderBakeWorkerStarted)
        {
            return;
        }
        StaticRenderBakeStop = true;
    }
    StaticRenderBakeCv.notify_all();
    if (StaticRenderBakeThread.joinable())
    {
        StaticRenderBakeThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(StaticRenderBakeMutex);
        StaticRenderBakeWorkerStarted = false;
        StaticRenderBakeStop = false;
        StaticRenderBakeBusy = false;
    }
}

void FD3D9GameWorldScene::Impl::StaticRenderBakeWorkerMain()
{
    LowerStaticWorkerPriority();
    WorldBatchAccumulateScratch scratch;
    for (;;)
    {
        FStaticRenderBakeRequest request;
        {
            std::unique_lock<std::mutex> lock(StaticRenderBakeMutex);
            StaticRenderBakeCv.wait(lock, [this]()
            {
                return StaticRenderBakeStop || !PendingStaticRenderBakes.empty();
            });
            if (StaticRenderBakeStop && PendingStaticRenderBakes.empty())
            {
                break;
            }
            request = std::move(PendingStaticRenderBakes.back());
            PendingStaticRenderBakes.pop_back();
            StaticRenderBakeBusy = true;
        }

        FStaticRenderBakeResult result;
        result.CellKey = request.CellKey;
        try
        {
            AccumulatedWorldBatchMap batchesByTexture;
            ReserveAccumulatedWorldBatches(request.Sources, batchesByTexture);
            for (const auto& source : request.Sources)
            {
                if (source.Resource)
                {
                    AccumulateWorldBatches(*source.Resource, source.World, batchesByTexture, false, 0xfffffffful, &scratch);
                }
            }
            result.Batches = TakeWorldCpuBatches(batchesByTexture);
        }
        catch (...)
        {
            result.Batches.clear();
        }
        {
            std::lock_guard<std::mutex> lock(StaticRenderBakeMutex);
            CompletedStaticRenderBakes.push_back(std::move(result));
            StaticRenderBakeBusy = false;
        }
        StaticRenderBakeCv.notify_all();
    }
}

void FD3D9GameWorldScene::Impl::QueueStaticRenderCellBake(uint64 CellKey)
{
    if (StaticCellRenderBatches.find(CellKey) != StaticCellRenderBatches.end() || !QueuedStaticRenderBakeCells.insert(CellKey).second)
    {
        return;
    }
    const auto cellIt = StaticPlacementIndicesByRenderCell.find(CellKey);
    if (cellIt == StaticPlacementIndicesByRenderCell.end() || cellIt->second.empty())
    {
        StaticCellRenderBatches.emplace(CellKey, std::vector<WorldRenderBatch>{});
        QueuedStaticRenderBakeCells.erase(CellKey);
        StaticDrawBatchesDirty = true;
        DirectStaticInstancesDirty = true;
        return;
    }
    FStaticRenderBakeRequest request;
    request.CellKey = CellKey;
    request.Sources.reserve(cellIt->second.size());
    bool allResourcesReady = true;
    for (const std::size_t placementIndex : cellIt->second)
    {
        if (placementIndex >= StaticPlacements.size())
        {
            continue;
        }
        const auto& placement = StaticPlacements[placementIndex];
        if (placement.ModelId >= StaticPlacementModels.size())
        {
            continue;
        }
        const auto& model = StaticPlacementModels[placement.ModelId];
        const auto resourceIt = StaticResources.find(model.Key);
        if (resourceIt == StaticResources.end() || !resourceIt->second)
        {
            allResourcesReady = false;
            continue;
        }
        if (resourceIt->second->IsSkinned || resourceIt->second->CpuVertices.empty() || resourceIt->second->CpuIndices.empty())
        {
            continue;
        }
        request.Sources.push_back(FStaticRenderBakeSource{resourceIt->second.get(), placement.World});
    }
    if (!allResourcesReady)
    {
        QueuedStaticRenderBakeCells.erase(CellKey);
        return;
    }
    if (request.Sources.empty())
    {
        StaticCellRenderBatches.emplace(CellKey, std::vector<WorldRenderBatch>{});
        QueuedStaticRenderBakeCells.erase(CellKey);
        StaticDrawBatchesDirty = true;
        DirectStaticInstancesDirty = true;
        return;
    }
    StartStaticRenderBakeWorker();
    {
        std::lock_guard<std::mutex> lock(StaticRenderBakeMutex);
        PendingStaticRenderBakes.push_back(std::move(request));
    }
    StaticRenderBakeCv.notify_one();
}

void FD3D9GameWorldScene::Impl::DrainStaticRenderCellBakeJobs(bool Wait)
{
    if (Wait)
    {
        std::unique_lock<std::mutex> lock(StaticRenderBakeMutex);
        StaticRenderBakeCv.wait(lock, [this]()
        {
            return PendingStaticRenderBakes.empty() && !StaticRenderBakeBusy;
        });
    }
    std::vector<FStaticRenderBakeResult> completed;
    {
        std::lock_guard<std::mutex> lock(StaticRenderBakeMutex);
        completed.swap(CompletedStaticRenderBakes);
    }
    for (auto& result : completed)
    {
        FStaticRenderGpuUpload upload;
        upload.Result = std::move(result);
        upload.GpuBatches.reserve(upload.Result.Batches.size());
        PendingStaticRenderGpuUploads.push_back(std::move(upload));
    }

    auto ProcessOneBatch = [this]()
    {
        if (PendingStaticRenderGpuUploads.empty())
        {
            return;
        }
        auto& upload = PendingStaticRenderGpuUploads.front();
        if (upload.NextBatch < upload.Result.Batches.size())
        {
            WorldRenderBatch batch;
            if (UploadWorldCpuBatch(Device, upload.Result.Batches[upload.NextBatch], batch))
            {
                upload.GpuBatches.push_back(batch);
            }
            ++upload.NextBatch;
        }
        if (upload.NextBatch < upload.Result.Batches.size())
        {
            return;
        }
        if (!upload.GpuBatches.empty())
        {
            auto existing = StaticCellRenderBatches.find(upload.Result.CellKey);
            if (existing != StaticCellRenderBatches.end())
            {
                ReleaseWorldRenderBatches(existing->second);
                existing->second = std::move(upload.GpuBatches);
            }
            else
            {
                StaticCellRenderBatches.emplace(upload.Result.CellKey, std::move(upload.GpuBatches));
            }
            StaticDrawBatchesDirty = true;
            DirectStaticInstancesDirty = true;
        }
        QueuedStaticRenderBakeCells.erase(upload.Result.CellKey);
        PendingStaticRenderGpuUploads.pop_front();
    };

    if (Wait)
    {
        while (!PendingStaticRenderGpuUploads.empty())
        {
            ProcessOneBatch();
        }
    }
    else
    {
        ProcessOneBatch();
    }
}

void FD3D9GameWorldScene::Impl::RebuildStaticDrawBatchCache()
{
    StaticDrawBatches.clear();
    for (const uint64 cell : VisibleStaticRenderCells)
    {
        const auto iterator = StaticCellRenderBatches.find(cell);
        if (iterator == StaticCellRenderBatches.end())
        {
            continue;
        }
        for (const auto& batch : iterator->second)
        {
            StaticDrawBatches.push_back(&batch);
        }
    }
    std::sort(StaticDrawBatches.begin(), StaticDrawBatches.end(), [](const WorldRenderBatch* left, const WorldRenderBatch* right)
    {
        return std::less<IDirect3DTexture9*>{}(left->Texture, right->Texture);
    });
    StaticDrawBatchesDirty = false;
}

void FD3D9GameWorldScene::Impl::RebuildDirectStaticInstanceCache()
{
    DirectStaticInstanceIndices.clear();
    DirectStaticInstanceIndices.reserve(StaticInstances.size());
    for (std::size_t index = 0; index < StaticInstances.size(); ++index)
    {
        const auto& instance = StaticInstances[index];
        if (!instance.resource)
        {
            continue;
        }
        const auto baked = StaticCellRenderBatches.find(instance.RenderCellKey);
        if (instance.resource->IsSkinned || baked == StaticCellRenderBatches.end() || baked->second.empty())
        {
            DirectStaticInstanceIndices.push_back(index);
        }
    }
    DirectStaticInstancesDirty = false;
}

void FD3D9GameWorldScene::Impl::BuildVisibleStaticRenderBatches()
{
    for (const uint64 cell : VisibleStaticRenderCells)
    {
        QueueStaticRenderCellBake(cell);
    }
}

void FD3D9GameWorldScene::Impl::PreloadStaticResourcesAround(float CenterX, float CenterY, float CenterZ, float Radius)
{
    struct Target
    {
        std::string ModelName;
        std::string ResourceKey;
        std::filesystem::path ModelPath;
        float DistanceSquared = 0.0f;
    };
    const float RadiusSquared = Radius * Radius;
    const auto [MinCellX, MaxCellX] = StaticRenderCellRange(CenterX, Radius, Config.TileSize);
    const auto [MinCellZ, MaxCellZ] = StaticRenderCellRange(CenterZ, Radius, Config.TileSize);
    std::unordered_map<std::string, Target> targetsByModel;
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
                if (!StaticPlacementInRange(placement.Position, CenterX, CenterY, CenterZ, RadiusSquared) || placement.ModelId >= StaticPlacementModels.size())
                {
                    continue;
                }
                const auto& model = StaticPlacementModels[placement.ModelId];
                if (model.Key.empty() || StaticResources.find(model.Key) != StaticResources.end())
                {
                    continue;
                }
                const float dx = placement.Position.X - CenterX;
                const float dy = placement.Position.Y - CenterY;
                const float dz = placement.Position.Z - CenterZ;
                const float distanceSquared = dx * dx + dy * dy + dz * dz;
                auto [targetIt, inserted] = targetsByModel.try_emplace(model.Key);
                if (inserted || distanceSquared < targetIt->second.DistanceSquared)
                {
                    try
                    {
                        targetIt->second = Target{model.Name, model.Key, ResolveModelPath(model.Name), distanceSquared};
                    }
                    catch (const std::exception& ex)
                    {
                        targetsByModel.erase(model.Key);
                        if (Logger)
                        {
                            Logger->Warning("static guard preload skipped " + model.Name + ": " + ex.what());
                        }
                    }
                }
            }
        }
    }
    std::vector<Target> targets;
    targets.reserve(targetsByModel.size());
    for (auto& [_, target] : targetsByModel)
    {
        targets.push_back(std::move(target));
    }
    std::sort(targets.begin(), targets.end(), [](const Target& left, const Target& right)
    {
        return left.DistanceSquared < right.DistanceSquared;
    });
    for (const auto& target : targets)
    {
        QueueStaticModelCpuPreload(target.ModelName, target.ModelPath);
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

void FD3D9GameWorldScene::Impl::StartGrassMapPreloadWorker()
{
    std::lock_guard<std::mutex> lock(GrassMapPreloadMutex);
    if (GrassMapPreloadWorkerStarted)
    {
        return;
    }
    GrassMapPreloadStop = false;
    GrassMapPreloadWorkerStarted = true;
    GrassMapPreloadThread = std::thread([this]()
    {
        GrassMapPreloadWorkerMain();
    });
}

void FD3D9GameWorldScene::Impl::StopGrassMapPreloadWorker()
{
    {
        std::lock_guard<std::mutex> lock(GrassMapPreloadMutex);
        if (!GrassMapPreloadWorkerStarted)
        {
            return;
        }
        GrassMapPreloadStop = true;
    }
    GrassMapPreloadCv.notify_all();
    if (GrassMapPreloadThread.joinable())
    {
        GrassMapPreloadThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(GrassMapPreloadMutex);
        GrassMapPreloadWorkerStarted = false;
        GrassMapPreloadStop = false;
        PendingGrassMapPreloads.clear();
        QueuedGrassMapPreloads.clear();
    }
}

void FD3D9GameWorldScene::Impl::GrassMapPreloadWorkerMain()
{
    LowerStaticWorkerPriority();
    for (;;)
    {
        FGrassMapPreloadTarget target;
        {
            std::unique_lock<std::mutex> lock(GrassMapPreloadMutex);
            GrassMapPreloadCv.wait(lock, [this]()
            {
                return GrassMapPreloadStop || !PendingGrassMapPreloads.empty();
            });
            if (GrassMapPreloadStop && PendingGrassMapPreloads.empty())
            {
                break;
            }
            target = PendingGrassMapPreloads.back();
            PendingGrassMapPreloads.pop_back();
        }
        try
        {
            LoadGrassMap(target.ChunkX, target.ChunkZ);
        }
        catch (const std::exception& ex)
        {
            if (Logger)
            {
                Logger->Warning(std::string("grass map preload failed: ") + ex.what());
            }
        }
        {
            std::lock_guard<std::mutex> lock(GrassMapPreloadMutex);
            QueuedGrassMapPreloads.erase(target.Key);
        }
    }
}

void FD3D9GameWorldScene::Impl::QueueGrassMapPreload(int ChunkX, int ChunkZ)
{
    if (ChunkX < 0 || ChunkZ < 0 || ChunkX >= Config.GrassmapGridSize || ChunkZ >= Config.GrassmapGridSize)
    {
        return;
    }
    const int key = ChunkZ * Config.GrassmapGridSize + ChunkX;
    {
        std::lock_guard<std::mutex> cacheLock(GrassMapMutex);
        if (GrassMaps.find(key) != GrassMaps.end())
        {
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lock(GrassMapPreloadMutex);
        if (!QueuedGrassMapPreloads.insert(key).second)
        {
            return;
        }
        PendingGrassMapPreloads.push_back(FGrassMapPreloadTarget{ChunkX, ChunkZ, key});
    }
    GrassMapPreloadCv.notify_one();
}

void FD3D9GameWorldScene::Impl::PreloadGrassMapsAround(float CenterX, float CenterZ, float Radius)
{
    if (Config.GrassQuality <= 0 || Config.GrassmapGridSize <= 0 || Config.GrassmapTileResolution <= 0 || Config.GrassmapWorldScale <= 0.0f)
    {
        return;
    }
    const int worldResolution = Config.GrassmapGridSize * Config.GrassmapTileResolution;
    auto mapX = [this](float worldX)
    {
        return static_cast<int>(std::floor((Config.GrassmapWorldOffsetX + static_cast<float>(Config.GrassmapWorldSignX) * worldX) * Config.GrassmapWorldScale));
    };
    auto mapZ = [this](float worldZ)
    {
        return static_cast<int>(std::floor((Config.GrassmapWorldOffsetZ + static_cast<float>(Config.GrassmapWorldSignZ) * worldZ) * Config.GrassmapWorldScale));
    };
    const int x0 = mapX(CenterX - Radius);
    const int x1 = mapX(CenterX + Radius);
    const int z0 = mapZ(CenterZ - Radius);
    const int z1 = mapZ(CenterZ + Radius);
    const int minMapX = std::clamp((std::min)(x0, x1), 0, worldResolution - 1);
    const int maxMapX = std::clamp((std::max)(x0, x1), 0, worldResolution - 1);
    const int minMapZ = std::clamp((std::min)(z0, z1), 0, worldResolution - 1);
    const int maxMapZ = std::clamp((std::max)(z0, z1), 0, worldResolution - 1);
    const int minChunkX = minMapX / Config.GrassmapTileResolution;
    const int maxChunkX = maxMapX / Config.GrassmapTileResolution;
    const int minChunkZ = minMapZ / Config.GrassmapTileResolution;
    const int maxChunkZ = maxMapZ / Config.GrassmapTileResolution;
    for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX)
    {
        for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ)
        {
            QueueGrassMapPreload(chunkX, chunkZ);
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
    {
        std::lock_guard<std::mutex> lock(GrassMapMutex);
        const auto cached = GrassMaps.find(key);
        if (cached != GrassMaps.end())
        {
            return cached->second;
        }
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

    {
        std::lock_guard<std::mutex> lock(GrassMapMutex);
        return GrassMaps.emplace(key, std::move(data)).first->second;
    }
}

bool FD3D9GameWorldScene::Impl::TryGrassTypeAt(float WorldX, float WorldZ, bool AllowBlockingLoad, uint8& OutType)
{
    OutType = 0;
    const int MapX = static_cast<int>(std::floor((Config.GrassmapWorldOffsetX + static_cast<float>(Config.GrassmapWorldSignX) * WorldX) * Config.GrassmapWorldScale));
    const int MapZ = static_cast<int>(std::floor((Config.GrassmapWorldOffsetZ + static_cast<float>(Config.GrassmapWorldSignZ) * WorldZ) * Config.GrassmapWorldScale));
    const int WorldResolution = Config.GrassmapGridSize * Config.GrassmapTileResolution;
    if (MapX < 0 || MapZ < 0 || MapX >= WorldResolution || MapZ >= WorldResolution)
    {
        return true;
    }
    const int ChunkX = MapX / Config.GrassmapTileResolution;
    const int ChunkZ = MapZ / Config.GrassmapTileResolution;
    const int LocalX = MapX % Config.GrassmapTileResolution;
    const int LocalZ = MapZ % Config.GrassmapTileResolution;
    const int key = ChunkZ * Config.GrassmapGridSize + ChunkX;
    uint8 type = 0;
    if (AllowBlockingLoad)
    {
        const auto& map = LoadGrassMap(ChunkX, ChunkZ);
        type = map[static_cast<std::size_t>(LocalZ) * Config.GrassmapTileResolution + LocalX] & 0x0f;
    }
    else
    {
        bool loaded = false;
        {
            std::lock_guard<std::mutex> lock(GrassMapMutex);
            const auto cached = GrassMaps.find(key);
            if (cached != GrassMaps.end())
            {
                type = cached->second[static_cast<std::size_t>(LocalZ) * Config.GrassmapTileResolution + LocalX] & 0x0f;
                loaded = true;
            }
        }
        if (!loaded)
        {
            QueueGrassMapPreload(ChunkX, ChunkZ);
            return false;
        }
    }
    if (type != 0 && SpawnY > Config.GrassHighlandMinY && SpawnY < Config.GrassHighlandMaxY)
    {
        type = static_cast<uint8>(type + Config.GrassHighlandPatternOffset);
    }
    OutType = type;
    return true;
}

void FD3D9GameWorldScene::Impl::LoadVisibleGrass()
{
    if (Config.GrassQuality <= 0)
    {
        ClearGrassRenderBatches();
        GrassInstancesByCell.clear();
        GrassInstanceCount = 0;
        GrassCells.clear();
        GrassTargetCells.clear();
        GrassPendingCells.clear();
        GrassRefreshIncomplete = false;
        GrassAnchorValid = false;
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
    const auto CellKey = [](int x, int z)
    {
        return (static_cast<uint64>(static_cast<uint32>(x)) << 32) | static_cast<uint32>(z);
    };
    const auto CellXFromKey = [](uint64 key) { return static_cast<int32>(static_cast<uint32>(key >> 32)); };
    const auto CellZFromKey = [](uint64 key) { return static_cast<int32>(static_cast<uint32>(key)); };
    const float anchorDx = SpawnX - GrassAnchorX;
    const float anchorDz = SpawnZ - GrassAnchorZ;
    const float anchorMoveLimit = Config.GrassGenerationMargin * Config.GrassGenerationMargin;
    const bool rebuildTarget = !GrassAnchorValid || GrassTargetCells.empty() || anchorDx * anchorDx + anchorDz * anchorDz >= anchorMoveLimit;

    if (rebuildTarget)
    {
        GrassAnchorX = SpawnX;
        GrassAnchorZ = SpawnZ;
        GrassAnchorValid = true;
        GrassCenterX = static_cast<int>(std::floor(GrassAnchorX / spacing));
        GrassCenterZ = static_cast<int>(std::floor(GrassAnchorZ / spacing));
        const float generationRadius = Config.GrassRadius + Config.GrassGenerationMargin;
        const int cellRadius = static_cast<int>(std::ceil(generationRadius / spacing)) + 1;
        const float selectionRadius = generationRadius + spacing;
        const float selectionRadiusSquared = selectionRadius * selectionRadius;
        GrassTargetCells.clear();
        GrassTargetCells.reserve(static_cast<std::size_t>((cellRadius * 2 + 1) * (cellRadius * 2 + 1)));
        for (int cellX = GrassCenterX - cellRadius; cellX <= GrassCenterX + cellRadius; ++cellX)
        {
            for (int cellZ = GrassCenterZ - cellRadius; cellZ <= GrassCenterZ + cellRadius; ++cellZ)
            {
                const float x = static_cast<float>(cellX) * spacing;
                const float z = static_cast<float>(cellZ) * spacing;
                const float dx = x + spacing * 0.5f - GrassAnchorX;
                const float dz = z + spacing * 0.5f - GrassAnchorZ;
                if (dx * dx + dz * dz <= selectionRadiusSquared)
                {
                    GrassTargetCells.insert(CellKey(cellX, cellZ));
                }
            }
        }

        std::vector<uint64> removedGrassCells;
        removedGrassCells.reserve(GrassInstancesByCell.size());
        for (const auto& [cellKey, _] : GrassInstancesByCell)
        {
            if (!GrassTargetCells.contains(cellKey)) { removedGrassCells.push_back(cellKey); }
        }
        for (const uint64 cellKey : removedGrassCells) { BakeGrassCell(cellKey, {}); }
        std::erase_if(GrassCells, [this](uint64 key)
        {
            return !GrassTargetCells.contains(key);
        });
        GrassPendingCells.clear();
        GrassPendingCells.reserve(GrassTargetCells.size());
        for (const uint64 key : GrassTargetCells)
        {
            if (!GrassCells.contains(key))
            {
                GrassPendingCells.push_back(key);
            }
        }
        std::sort(GrassPendingCells.begin(), GrassPendingCells.end(), [this, spacing, &CellXFromKey, &CellZFromKey](uint64 left, uint64 right)
        {
            const float leftX = (static_cast<float>(CellXFromKey(left)) + 0.5f) * spacing - GrassAnchorX;
            const float leftZ = (static_cast<float>(CellZFromKey(left)) + 0.5f) * spacing - GrassAnchorZ;
            const float rightX = (static_cast<float>(CellXFromKey(right)) + 0.5f) * spacing - GrassAnchorX;
            const float rightZ = (static_cast<float>(CellZFromKey(right)) + 0.5f) * spacing - GrassAnchorZ;
            return leftX * leftX + leftZ * leftZ < rightX * rightX + rightZ * rightZ;
        });
        GrassPendingCellsPerRenderGroup.clear();
        GrassPendingCellsPerRenderGroup.reserve(GrassPendingCells.size() / 4 + 1);
        for (const uint64 key : GrassPendingCells)
        {
            ++GrassPendingCellsPerRenderGroup[GrassRenderGroupKey(key)];
        }
        FlushReadyGrassRenderGroupBakes();
    }


    if (GrassPendingCells.empty())
    {
        GrassRefreshIncomplete = false;
        FlushReadyGrassRenderGroupBakes();
        return;
    }

    constexpr std::size_t MaxGrassCellsPerUpdate = 2;
    const std::size_t planCount = GrassInitialBlockingLoad ? GrassPendingCells.size() : (std::min)(GrassPendingCells.size(), MaxGrassCellsPerUpdate);
    std::vector<GrassCellPlan> plans;
    plans.reserve(planCount);
    std::array<bool, 31> usedGrassTypes{};
    bool anyTypedGrass = false;
    for (std::size_t pendingIndex = 0; pendingIndex < planCount; ++pendingIndex)
    {
        const uint64 key = GrassPendingCells[pendingIndex];
        GrassCellPlan plan;
        plan.CellX = CellXFromKey(key);
        plan.CellZ = CellZFromKey(key);
        plan.X = static_cast<float>(plan.CellX) * spacing;
        plan.Z = static_cast<float>(plan.CellZ) * spacing;
        plan.Key = key;
        plan.Samples.reserve(Config.GrassSampleOffsets.size());
        for (const auto& sampleOffset : Config.GrassSampleOffsets)
        {
            const float sampleX = plan.X + sampleOffset.X;
            const float sampleZ = plan.Z + sampleOffset.Y;
            uint8 type = 0;
            if (!TryGrassTypeAt(sampleX, sampleZ, GrassInitialBlockingLoad, type))
            {
                GrassRefreshIncomplete = true;
                return;
            }
            plan.Samples.push_back(GrassSamplePlan{sampleX, sampleZ, type});
            if (type > 0 && type < GrassPatternResources.size())
            {
                usedGrassTypes[type] = true;
                anyTypedGrass = true;
            }
        }
        plans.push_back(std::move(plan));
    }

    if (!anyTypedGrass)
    {
        for (const auto& plan : plans)
        {
            GrassCells.insert(plan.Key);
            CompleteGrassPendingCell(plan.Key);
        }
        GrassPendingCells.erase(GrassPendingCells.begin(), GrassPendingCells.begin() + static_cast<std::ptrdiff_t>(planCount));
        GrassRefreshIncomplete = !GrassPendingCells.empty();
        return;
    }

    for (std::size_t type = 1; type < usedGrassTypes.size(); ++type)
    {
        if (!usedGrassTypes[type]) { continue; }
        auto& patternResources = GrassPatternResources[type];
        if (patternResources.size() != Config.GrassPatterns[type].size())
        {
            patternResources.clear();
            patternResources.reserve(Config.GrassPatterns[type].size());
            for (const auto& model : Config.GrassPatterns[type]) { patternResources.push_back(EnsureStaticModelResource(NarrowAscii(model))); }
        }
        if (Config.GrassQuality >= 2)
        {
            auto& flowerResources = GrassFlowerPatternResources[type];
            if (flowerResources.size() != Config.GrassFlowerPatterns[type].size())
            {
                flowerResources.clear();
                flowerResources.reserve(Config.GrassFlowerPatterns[type].size());
                for (const auto& model : Config.GrassFlowerPatterns[type]) { flowerResources.push_back(EnsureStaticModelResource(NarrowAscii(model))); }
            }
        }
    }
    if (GrassDetailResources.size() != Config.GrassDetailModels.size())
    {
        GrassDetailResources.clear();
        GrassDetailResources.reserve(Config.GrassDetailModels.size());
        for (const auto& model : Config.GrassDetailModels) { GrassDetailResources.push_back(EnsureStaticModelResource(NarrowAscii(model))); }
    }

    std::vector<std::vector<GrassInstance>> generated(plans.size());
    std::mutex errorMutex;
    std::string firstError;
    auto generateCell = [&](std::size_t planIndex)
    {
        const auto& plan = plans[planIndex];
        auto& out = generated[planIndex];
        out.reserve(Config.GrassSampleOffsets.size() * static_cast<std::size_t>((std::max)(1, Config.GrassDetailCount)));
        int flatSampleCount = 0;
        bool anyDetail = false;
        int flowerType = 0;
        for (std::size_t sampleIndex = 0; sampleIndex < plan.Samples.size(); ++sampleIndex)
        {
            const auto& sample = plan.Samples[sampleIndex];
            const auto type = sample.Type;
            if (type == 0 || type >= GrassPatternResources.size()) { continue; }
            const auto& pattern = GrassPatternResources[type];
            if (pattern.empty()) { throw std::runtime_error("grass pattern has no models for type " + std::to_string(type)); }
            FXorShift32 random{(static_cast<uint32>(plan.CellX) * 0x9e3779b9U) ^ (static_cast<uint32>(plan.CellZ) * 0x85ebca6bU) ^ (static_cast<uint32>(sampleIndex) * 0xc2b2ae35U) ^ type};
            float flatHeight = 0.0f;
            FVector3 flatNormal{};
            if (FlatGrassSurfaceAt(sample.X, sample.Z, flatHeight, flatNormal))
            {
                StaticModelResource* resource = pattern[random.Next() % pattern.size()];
                auto world = AlignUpMatrix(flatNormal);
                world._41 = sample.X;
                world._42 = flatHeight - resource->Bounds.Max.Y;
                world._43 = sample.Z;
                out.push_back(GrassInstance{resource, world, random.Unit() * 2.0f * kPi, 0.65f + random.Unit() * 0.35f, plan.CellX, plan.CellZ, TerrainColorAt(sample.X, sample.Z)});
                ++flatSampleCount;
                flowerType = static_cast<int>(type);
                continue;
            }
            if (GrassDetailResources.empty()) { continue; }
            const int detailCount = Config.GrassQuality >= 2 ? (std::max)(0, Config.GrassDetailCount) : (std::min)((std::max)(0, Config.GrassDetailCount), 2);
            for (int detail = 0; detail < detailCount; ++detail)
            {
                const float jitter = Config.GrassSpacing * Config.GrassJitterFraction;
                const float detailX = sample.X + (random.Unit() * 2.0f - 1.0f) * jitter;
                const float detailZ = sample.Z + (random.Unit() * 2.0f - 1.0f) * jitter;
                float height = 0.0f;
                FVector3 detailNormal{};
                if (!TerrainSurfaceAt(detailX, detailZ, height, detailNormal)) { continue; }
                StaticModelResource* resource = GrassDetailResources[random.Next() % GrassDetailResources.size()];
                const float scale = Config.GrassScaleMin + random.Unit() * (Config.GrassScaleMax - Config.GrassScaleMin);
                auto world = ScaleMatrix(scale);
                world._41 = detailX;
                world._42 = height - resource->Bounds.Max.Y * scale;
                world._43 = detailZ;
                out.push_back(GrassInstance{resource, world, random.Unit() * 2.0f * kPi, 0.65f + random.Unit() * 0.35f, plan.CellX, plan.CellZ, TerrainColorAt(detailX, detailZ)});
                anyDetail = true;
            }
        }
        const int sampleTotal = static_cast<int>(Config.GrassSampleOffsets.size());
        if (flatSampleCount == sampleTotal && !anyDetail && flowerType > 0 && flowerType < static_cast<int>(GrassFlowerPatternResources.size()) && !GrassFlowerPatternResources[static_cast<std::size_t>(flowerType)].empty())
        {
            const auto& flowers = GrassFlowerPatternResources[static_cast<std::size_t>(flowerType)];
            FXorShift32 flowerRandom{(static_cast<uint32>(plan.CellX) * 0x27d4eb2dU) ^ (static_cast<uint32>(plan.CellZ) * 0x165667b1U) ^ 0x9e3779b9U};
            const int flowerLimit = Config.GrassQuality >= 2 ? Config.GrassFlowerCountMax : (std::min)(Config.GrassFlowerCountMax, 3);
            const int flowerCount = static_cast<int>(flowerRandom.Unit() * static_cast<float>((std::max)(0, flowerLimit)));
            for (int flowerIndex = 0; flowerIndex < flowerCount; ++flowerIndex)
            {
                const float flowerX = plan.X + flowerRandom.Unit() * spacing;
                const float flowerZ = plan.Z + flowerRandom.Unit() * spacing;
                float flowerHeight = 0.0f;
                FVector3 flowerNormal{};
                if (!TerrainSurfaceAt(flowerX, flowerZ, flowerHeight, flowerNormal)) { continue; }
                const int slot = static_cast<int>(flowerRandom.Unit() * 5.0f);
                if (slot < 0 || slot >= static_cast<int>(flowers.size())) { continue; }
                StaticModelResource* resource = flowers[static_cast<std::size_t>(slot)];
                auto world = AlignUpMatrix(flowerNormal);
                world._41 = flowerX;
                world._42 = flowerHeight - resource->Bounds.Max.Y;
                world._43 = flowerZ;
                out.push_back(GrassInstance{resource, world, flowerRandom.Unit() * 2.0f * kPi, 0.65f + flowerRandom.Unit() * 0.35f, plan.CellX, plan.CellZ, TerrainColorAt(flowerX, flowerZ)});
            }
        }
    };

    const std::size_t hardware = static_cast<std::size_t>((std::max)(1u, std::thread::hardware_concurrency()));
    const std::size_t threadCount = plans.size() < 8 ? 1 : (std::min)(std::clamp(hardware - 1, std::size_t{1}, std::size_t{8}), plans.size());
    ParallelFor(plans.size(), threadCount, [&](std::size_t index)
    {
        try { generateCell(index); }
        catch (const std::exception& exception)
        {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (firstError.empty()) { firstError = exception.what(); }
        }
    });
    if (!firstError.empty()) { throw std::runtime_error(firstError); }

    for (std::size_t index = 0; index < generated.size(); ++index)
    {
        GrassCells.insert(plans[index].Key);
        BakeGrassCell(plans[index].Key, std::move(generated[index]));
        CompleteGrassPendingCell(plans[index].Key);
    }
    GrassPendingCells.erase(GrassPendingCells.begin(), GrassPendingCells.begin() + static_cast<std::ptrdiff_t>(planCount));
    GrassRefreshIncomplete = !GrassPendingCells.empty();
    FlushReadyGrassRenderGroupBakes();
}

void FD3D9GameWorldScene::Impl::RebuildStaticCollisionIndex()
{
    StaticCollisionInstances.clear();
    StaticCollisionCells.clear();
    LargeStaticCollisionInstances.clear();
    StaticCollisionInstances.reserve(StaticInstances.size());
    StaticCollisionCells.reserve(StaticInstances.size() * 2 + 1);
    LargeStaticCollisionInstances.reserve(16);
    if (StaticCollisionInstanceScratch.capacity() < 64)
    {
        StaticCollisionInstanceScratch.reserve(64);
    }
    if (StaticCollisionTriangleScratch.capacity() < 128)
    {
        StaticCollisionTriangleScratch.reserve(128);
    }
    for (const auto& Instance : StaticInstances)
    {
        if (!Instance.resource || !Instance.Bounds.IsValid())
        {
            continue;
        }
        const float Width = Instance.Bounds.Max.X - Instance.Bounds.Min.X;
        const float Depth = Instance.Bounds.Max.Z - Instance.Bounds.Min.Z;
        const float Height = Instance.Bounds.Max.Y - Instance.Bounds.Min.Y;
        if ((std::max)({Width, Depth, Height}) < 0.10f)
        {
            continue;
        }
        StaticCollisionInstance CollisionInstance;
        CollisionInstance.Resource = Instance.resource;
        CollisionInstance.World = Instance.world;
        CollisionInstance.Bounds = Instance.Bounds;
        CollisionInstance.Capsule = Instance.resource->IsSkinned;
        if (CollisionInstance.Capsule)
        {
            CollisionInstance.CenterX = Instance.world._41;
            CollisionInstance.CenterZ = Instance.world._43;
            CollisionInstance.Radius = std::clamp((std::max)(Width, Depth) * 0.18f, 0.24f, Config.PlayerCollisionRadius * 1.15f);
            CollisionInstance.Bounds.Min.X = CollisionInstance.CenterX - CollisionInstance.Radius;
            CollisionInstance.Bounds.Max.X = CollisionInstance.CenterX + CollisionInstance.Radius;
            CollisionInstance.Bounds.Min.Z = CollisionInstance.CenterZ - CollisionInstance.Radius;
            CollisionInstance.Bounds.Max.Z = CollisionInstance.CenterZ + CollisionInstance.Radius;
        }
        else if (!Instance.resource->CollisionMesh || !InvertAffineMatrix(Instance.world, CollisionInstance.InverseWorld))
        {
            continue;
        }
        const std::size_t InstanceIndex = StaticCollisionInstances.size();
        StaticCollisionInstances.push_back(CollisionInstance);
        const float Inflate = Config.PlayerCollisionRadius + Config.CollisionSkin;
        const int MinCellX = StaticRenderCellCoord(CollisionInstance.Bounds.Min.X - Inflate, kStaticCollisionCellSize);
        const int MaxCellX = StaticRenderCellCoord(CollisionInstance.Bounds.Max.X + Inflate, kStaticCollisionCellSize);
        const int MinCellZ = StaticRenderCellCoord(CollisionInstance.Bounds.Min.Z - Inflate, kStaticCollisionCellSize);
        const int MaxCellZ = StaticRenderCellCoord(CollisionInstance.Bounds.Max.Z + Inflate, kStaticCollisionCellSize);
        const uint64 CellCount = static_cast<uint64>(MaxCellX - MinCellX + 1) * static_cast<uint64>(MaxCellZ - MinCellZ + 1);
        if (CellCount > 256)
        {
            LargeStaticCollisionInstances.push_back(InstanceIndex);
            continue;
        }
        for (int CellZ = MinCellZ; CellZ <= MaxCellZ; ++CellZ)
        {
            for (int CellX = MinCellX; CellX <= MaxCellX; ++CellX)
            {
                StaticCollisionCells[StaticRenderCellKey(CellX, CellZ)].push_back(InstanceIndex);
            }
        }
    }
    StaticCollisionVisitMarks.assign(StaticCollisionInstances.size(), 0);
    StaticCollisionVisitGeneration = 0;
    PlayerCollisionNeedsRecovery = true;
}

void FD3D9GameWorldScene::Impl::QueryStaticCollisionInstances(const FBox3& Area, std::vector<std::size_t>& OutInstances) const
{
    OutInstances.clear();
    if (StaticCollisionInstances.empty() || !Area.IsValid())
    {
        return;
    }
    ++StaticCollisionVisitGeneration;
    if (StaticCollisionVisitGeneration == 0)
    {
        std::fill(StaticCollisionVisitMarks.begin(), StaticCollisionVisitMarks.end(), 0);
        StaticCollisionVisitGeneration = 1;
    }
    const int MinCellX = StaticRenderCellCoord(Area.Min.X, kStaticCollisionCellSize);
    const int MaxCellX = StaticRenderCellCoord(Area.Max.X, kStaticCollisionCellSize);
    const int MinCellZ = StaticRenderCellCoord(Area.Min.Z, kStaticCollisionCellSize);
    const int MaxCellZ = StaticRenderCellCoord(Area.Max.Z, kStaticCollisionCellSize);
    auto VisitInstance = [&](std::size_t InstanceIndex)
    {
        if (InstanceIndex >= StaticCollisionInstances.size() || StaticCollisionVisitMarks[InstanceIndex] == StaticCollisionVisitGeneration)
        {
            return;
        }
        StaticCollisionVisitMarks[InstanceIndex] = StaticCollisionVisitGeneration;
        if (BoundsIntersect(StaticCollisionInstances[InstanceIndex].Bounds, Area))
        {
            OutInstances.push_back(InstanceIndex);
        }
    };
    for (int CellZ = MinCellZ; CellZ <= MaxCellZ; ++CellZ)
    {
        for (int CellX = MinCellX; CellX <= MaxCellX; ++CellX)
        {
            const auto Cell = StaticCollisionCells.find(StaticRenderCellKey(CellX, CellZ));
            if (Cell == StaticCollisionCells.end())
            {
                continue;
            }
            for (const std::size_t InstanceIndex : Cell->second)
            {
                VisitInstance(InstanceIndex);
            }
        }
    }
    for (const std::size_t InstanceIndex : LargeStaticCollisionInstances)
    {
        VisitInstance(InstanceIndex);
    }
}

void FD3D9GameWorldScene::Impl::QueryStaticCollisionTriangles(const StaticCollisionInstance& Instance, const FBox3& Area, std::vector<uint32>& OutTriangles) const
{
    OutTriangles.clear();
    const auto& Mesh = Instance.Resource->CollisionMesh;
    if (!Mesh || Mesh->Nodes.empty())
    {
        return;
    }
    const FBox3 LocalArea = TransformBounds(Area, Instance.InverseWorld);
    std::array<uint32, 64> Stack{};
    std::size_t StackSize = 1;
    Stack[0] = 0;
    while (StackSize != 0)
    {
        const uint32 NodeIndex = Stack[--StackSize];
        if (NodeIndex >= Mesh->Nodes.size())
        {
            continue;
        }
        const auto& Node = Mesh->Nodes[NodeIndex];
        if (!BoundsIntersect(Node.Bounds, LocalArea))
        {
            continue;
        }
        if (Node.IsLeaf())
        {
            for (uint32 Offset = 0; Offset < Node.Count; ++Offset)
            {
                const uint32 OrderIndex = Node.First + Offset;
                if (OrderIndex >= Mesh->TriangleOrder.size())
                {
                    continue;
                }
                const uint32 TriangleIndex = Mesh->TriangleOrder[OrderIndex];
                if (TriangleIndex < Mesh->Triangles.size() && BoundsIntersect(Mesh->Triangles[TriangleIndex].Bounds, LocalArea))
                {
                    OutTriangles.push_back(TriangleIndex);
                }
            }
            continue;
        }
        if (StackSize + 2 <= Stack.size())
        {
            Stack[StackSize++] = Node.Left;
            Stack[StackSize++] = Node.Right;
        }
    }
}

bool FD3D9GameWorldScene::Impl::CapsuleOverlapsStatic(float X, float FeetY, float Z, FGameWorldCollisionHit* OutHit, bool IgnoreSupportingFloor) const
{
    const float Skin = std::clamp(Config.CollisionSkin, 0.0f, Config.PlayerCollisionRadius * 0.5f);
    const float Radius = (std::max)(0.05f, Config.PlayerCollisionRadius - Skin);
    const float TopY = FeetY - Config.PlayerCollisionHeight;
    float SegmentTopY = TopY + Radius;
    float SegmentBottomY = FeetY - Radius;
    if (SegmentTopY > SegmentBottomY)
    {
        const float Middle = (TopY + FeetY) * 0.5f;
        SegmentTopY = Middle;
        SegmentBottomY = Middle;
    }
    const FVector3 SegmentStart{X, SegmentTopY, Z};
    const FVector3 SegmentEnd{X, SegmentBottomY, Z};
    FBox3 Area;
    Area.Min = FVector3{X - Radius, TopY, Z - Radius};
    Area.Max = FVector3{X + Radius, FeetY, Z + Radius};
    QueryStaticCollisionInstances(Area, StaticCollisionInstanceScratch);
    bool Overlap = false;
    FGameWorldCollisionHit BestHit{};
    for (const std::size_t InstanceIndex : StaticCollisionInstanceScratch)
    {
        const auto& Instance = StaticCollisionInstances[InstanceIndex];
        if (Instance.Capsule)
        {
            if (FeetY < Instance.Bounds.Min.Y || TopY > Instance.Bounds.Max.Y)
            {
                continue;
            }
            const float DeltaX = X - Instance.CenterX;
            const float DeltaZ = Z - Instance.CenterZ;
            const float Limit = Radius + Instance.Radius;
            const float DistanceSquared = DeltaX * DeltaX + DeltaZ * DeltaZ;
            if (DistanceSquared >= Limit * Limit)
            {
                continue;
            }
            const float Distance = std::sqrt((std::max)(DistanceSquared, 0.0000001f));
            const float Penetration = Limit - Distance;
            if (!Overlap || Penetration > BestHit.Penetration)
            {
                BestHit.Normal = FVector3{DeltaX / Distance, 0.0f, DeltaZ / Distance};
                BestHit.Penetration = Penetration;
            }
            Overlap = true;
            continue;
        }
        QueryStaticCollisionTriangles(Instance, Area, StaticCollisionTriangleScratch);
        const auto& Mesh = *Instance.Resource->CollisionMesh;
        for (const uint32 TriangleIndex : StaticCollisionTriangleScratch)
        {
            const auto& LocalTriangle = Mesh.Triangles[TriangleIndex];
            const FVector3 A = TransformPoint(LocalTriangle.A, Instance.World);
            const FVector3 B = TransformPoint(LocalTriangle.B, Instance.World);
            const FVector3 C = TransformPoint(LocalTriangle.C, Instance.World);
            FVector3 TriangleNormal = Cross(Subtract(B, A), Subtract(C, A));
            const float NormalLengthSquared = Dot(TriangleNormal, TriangleNormal);
            if (NormalLengthSquared <= 0.0000001f)
            {
                continue;
            }
            TriangleNormal = Scale(TriangleNormal, 1.0f / std::sqrt(NormalLengthSquared));
            const bool Walkable = std::abs(TriangleNormal.Y) >= Config.CollisionFloorNormalThreshold;
            const float SupportingRegionTopY = FeetY - Radius - Skin;
            bool SupportsCenter = false;
            if (IgnoreSupportingFloor && Walkable && std::abs(TriangleNormal.Y) > 0.0001f && PointInTriangleXz(X, Z, A, B, C))
            {
                const float SurfaceY = A.Y - (TriangleNormal.X * (X - A.X) + TriangleNormal.Z * (Z - A.Z)) / TriangleNormal.Y;
                SupportsCenter = SurfaceY >= FeetY - Skin * 2.0f && SurfaceY <= FeetY + Skin * 2.0f;
            }
            FVector3 SegmentPoint{};
            FVector3 TrianglePoint{};
            const float DistanceSquared = SegmentTriangleDistanceSquared(SegmentStart, SegmentEnd, A, B, C, SegmentPoint, TrianglePoint);
            if (DistanceSquared >= Radius * Radius)
            {
                continue;
            }
            const float Distance = std::sqrt((std::max)(DistanceSquared, 0.0f));
            FVector3 Normal = Subtract(SegmentPoint, TrianglePoint);
            if (Distance > 0.00001f)
            {
                Normal = Scale(Normal, 1.0f / Distance);
            }
            else
            {
                const FVector3 Center{X, (TopY + FeetY) * 0.5f, Z};
                if (Dot(TriangleNormal, Subtract(Center, A)) < 0.0f)
                {
                    TriangleNormal = Scale(TriangleNormal, -1.0f);
                }
                Normal = TriangleNormal;
            }
            const bool LowerSupportContact = TrianglePoint.Y >= SupportingRegionTopY && TrianglePoint.Y <= FeetY + Skin * 2.0f && Normal.Y <= -0.05f;
            if (IgnoreSupportingFloor && Walkable && (SupportsCenter || LowerSupportContact))
            {
                continue;
            }
            const float Penetration = Radius - Distance;
            if (!Overlap || Penetration > BestHit.Penetration)
            {
                BestHit.Normal = Normal;
                BestHit.Penetration = Penetration;
            }
            Overlap = true;
        }
    }
    if (Overlap && OutHit)
    {
        *OutHit = BestHit;
    }
    return Overlap;
}

bool FD3D9GameWorldScene::Impl::RecoverFromPenetration()
{
    float TerrainY = 0.0f;
    FVector3 TerrainNormal{};
    if (TerrainSurfaceNearAt(SpawnX, SpawnZ, SpawnY, TerrainY, TerrainNormal) && SpawnY > TerrainY + Config.CollisionSkin)
    {
        SpawnY = TerrainY;
        VelocityY = 0.0f;
        Grounded = true;
    }
    FGameWorldCollisionHit Hit;
    for (int Iteration = 0; Iteration < 4; ++Iteration)
    {
        if (!CapsuleOverlapsStatic(SpawnX, SpawnY, SpawnZ, &Hit))
        {
            PlayerCollisionNeedsRecovery = false;
            return true;
        }
        const float NormalLength = std::sqrt(Dot(Hit.Normal, Hit.Normal));
        if (NormalLength <= 0.0001f)
        {
            break;
        }
        const float Push = Hit.Penetration + Config.CollisionSkin + 0.001f;
        SpawnX += Hit.Normal.X / NormalLength * Push;
        SpawnY += Hit.Normal.Y / NormalLength * Push;
        SpawnZ += Hit.Normal.Z / NormalLength * Push;
    }
    constexpr int DirectionCount = 12;
    for (int Ring = 1; Ring <= 5; ++Ring)
    {
        const float Distance = Config.PlayerCollisionRadius * 0.5f * static_cast<float>(Ring);
        for (int Direction = 0; Direction < DirectionCount; ++Direction)
        {
            const float Angle = static_cast<float>(Direction) * (2.0f * kPi / static_cast<float>(DirectionCount));
            const float CandidateX = SpawnX + std::cos(Angle) * Distance;
            const float CandidateZ = SpawnZ + std::sin(Angle) * Distance;
            float CandidateY = SpawnY;
            FVector3 Normal{};
            const bool HasSupport = SupportHeightAt(CandidateX, CandidateZ, SpawnY, CandidateY, &Normal);
            if (!CapsuleOverlapsStatic(CandidateX, CandidateY, CandidateZ))
            {
                SpawnX = CandidateX;
                SpawnY = CandidateY;
                SpawnZ = CandidateZ;
                Grounded = HasSupport;
                PlayerCollisionNeedsRecovery = false;
                return true;
            }
        }
    }
    return false;
}

bool FD3D9GameWorldScene::Impl::PointInTriangleXz(float PX, float PZ, const FVector3& A, const FVector3& B, const FVector3& C)
{
    const float D1 = (PX - B.X) * (A.Z - B.Z) - (A.X - B.X) * (PZ - B.Z);
    const float D2 = (PX - C.X) * (B.Z - C.Z) - (B.X - C.X) * (PZ - C.Z);
    const float D3 = (PX - A.X) * (C.Z - A.Z) - (C.X - A.X) * (PZ - A.Z);
    const bool Negative = D1 < -0.0001f || D2 < -0.0001f || D3 < -0.0001f;
    const bool Positive = D1 > 0.0001f || D2 > 0.0001f || D3 > 0.0001f;
    return !(Negative && Positive);
}

bool FD3D9GameWorldScene::Impl::StaticFloorHeightAt(float X, float Z, float MinY, float MaxY, float& OutY, FVector3* OutNormal) const
{
    FBox3 Area;
    const float ProbeRadius = 0.02f;
    Area.Min = FVector3{X - ProbeRadius, MinY - Config.CollisionSkin, Z - ProbeRadius};
    Area.Max = FVector3{X + ProbeRadius, MaxY + Config.CollisionSkin, Z + ProbeRadius};
    QueryStaticCollisionInstances(Area, StaticCollisionInstanceScratch);
    bool Found = false;
    float BestY = MaxY;
    FVector3 BestNormal{0.0f, 1.0f, 0.0f};
    for (const std::size_t InstanceIndex : StaticCollisionInstanceScratch)
    {
        const auto& Instance = StaticCollisionInstances[InstanceIndex];
        if (Instance.Capsule)
        {
            continue;
        }
        QueryStaticCollisionTriangles(Instance, Area, StaticCollisionTriangleScratch);
        const auto& Mesh = *Instance.Resource->CollisionMesh;
        for (const uint32 TriangleIndex : StaticCollisionTriangleScratch)
        {
            const auto& LocalTriangle = Mesh.Triangles[TriangleIndex];
            const FVector3 A = TransformPoint(LocalTriangle.A, Instance.World);
            const FVector3 B = TransformPoint(LocalTriangle.B, Instance.World);
            const FVector3 C = TransformPoint(LocalTriangle.C, Instance.World);
            FVector3 Normal = Cross(Subtract(B, A), Subtract(C, A));
            const float LengthSquared = Dot(Normal, Normal);
            if (LengthSquared <= 0.0000001f)
            {
                continue;
            }
            Normal = Scale(Normal, 1.0f / std::sqrt(LengthSquared));
            if (Normal.Y < 0.0f)
            {
                Normal = Scale(Normal, -1.0f);
            }
            if (Normal.Y < Config.CollisionFloorNormalThreshold || !PointInTriangleXz(X, Z, A, B, C))
            {
                continue;
            }
            const float Y = A.Y - (Normal.X * (X - A.X) + Normal.Z * (Z - A.Z)) / Normal.Y;
            if (Y < MinY || Y > MaxY || (Found && Y >= BestY))
            {
                continue;
            }
            Found = true;
            BestY = Y;
            BestNormal = Normal;
        }
    }
    if (Found)
    {
        OutY = BestY;
        if (OutNormal)
        {
            *OutNormal = BestNormal;
        }
    }
    return Found;
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

void FD3D9GameWorldScene::Impl::DrawWorldRenderBatches(std::vector<const WorldRenderBatch*>& DrawList, EGameWorldDrawBucket Bucket, float CullingMargin, bool AlreadySorted)
{
    if (DrawList.empty())
    {
        return;
    }
    if (!AlreadySorted)
    {
        std::sort(DrawList.begin(), DrawList.end(), [](const WorldRenderBatch* a, const WorldRenderBatch* b) { return std::less<IDirect3DTexture9*>{}(a->Texture, b->Texture); });
    }
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
    constexpr float FramesPerSecond = 1.0f / kPlayerAnimSecondsPerFrame;
    const float delta = std::clamp(DeltaSeconds, 0.0f, 0.1f);
    std::vector<StaticModelResource*> visibleResources;
    visibleResources.swap(VisibleAnimatedResources);

    for (auto* Resource : visibleResources)
    {
        if (!Resource || !Resource->IsSkinned || Resource->IdleClip < 0 || Resource->AnimationVertices.empty() || (!Resource->AnimatedVertexBuffer && !Resource->VertexBuffer))
        {
            continue;
        }
        if (Resource->CurrentClip < 0)
        {
            Resource->CurrentClip = Resource->IdleClip;
        }
        Resource->ClipTime += delta;
        bool isIdle = Resource->CurrentClip == Resource->IdleClip;
        int clipIndex = Resource->CurrentClip;
        if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= Resource->ClipLength.size())
        {
            Resource->CurrentClip = Resource->IdleClip;
            Resource->ClipTime = 0.0f;
            clipIndex = Resource->CurrentClip;
            isIdle = true;
        }
        if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= Resource->ClipLength.size())
        {
            continue;
        }
        const int clipLength = Resource->ClipLength[static_cast<std::size_t>(clipIndex)];
        if (clipLength <= 0)
        {
            continue;
        }
        const float framePosition = Resource->ClipTime * FramesPerSecond;
        const float frameFloor = std::floor(framePosition);
        int frameInClip = static_cast<int>(frameFloor);
        float frameAlpha = framePosition - frameFloor;
        if (isIdle)
        {
            frameInClip %= clipLength;
            if (!Resource->GestureClips.empty() && static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) < 0.3f * delta)
            {
                Resource->CurrentClip = Resource->GestureClips[static_cast<std::size_t>(std::rand() % static_cast<int>(Resource->GestureClips.size()))];
                Resource->ClipTime = 0.0f;
                clipIndex = Resource->CurrentClip;
                isIdle = false;
                frameInClip = 0;
                frameAlpha = 0.0f;
            }
        }
        else if (frameInClip >= clipLength)
        {
            Resource->CurrentClip = Resource->IdleClip;
            Resource->ClipTime = 0.0f;
            clipIndex = Resource->CurrentClip;
            isIdle = true;
            frameInClip = 0;
            frameAlpha = 0.0f;
        }
        if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= Resource->ClipLength.size() || static_cast<std::size_t>(clipIndex) >= Resource->ClipStart.size())
        {
            continue;
        }
        const int activeClipLength = Resource->ClipLength[static_cast<std::size_t>(clipIndex)];
        if (activeClipLength <= 0)
        {
            continue;
        }
        frameInClip = std::clamp(frameInClip, 0, activeClipLength - 1);
        const int nextFrameInClip = isIdle ? (frameInClip + 1) % activeClipLength : (std::min)(frameInClip + 1, activeClipLength - 1);
        const int frameIndex = Resource->ClipStart[static_cast<std::size_t>(clipIndex)] + frameInClip;
        const int nextFrameIndex = Resource->ClipStart[static_cast<std::size_t>(clipIndex)] + nextFrameInClip;
        if (frameIndex < 0 || nextFrameIndex < 0 || frameIndex >= Resource->FrameCount || nextFrameIndex >= Resource->FrameCount)
        {
            continue;
        }
        const std::size_t frameAIndex = static_cast<std::size_t>(frameIndex);
        const std::size_t frameBIndex = static_cast<std::size_t>(nextFrameIndex);
        if (Resource->PoseFrameCache.size() != static_cast<std::size_t>(Resource->FrameCount))
        {
            Resource->PoseFrameCache.clear();
            Resource->PoseFrameCache.resize(static_cast<std::size_t>(Resource->FrameCount));
            Resource->PoseFrameReady.assign(Resource->PoseFrameCache.size(), 0);
        }
        if (!Resource->PoseFrameReady[frameAIndex])
        {
            if (!BuildMdlPoseVertices(*Resource, frameIndex, Resource->PoseFrameCache[frameAIndex]))
            {
                continue;
            }
            Resource->PoseFrameReady[frameAIndex] = 1;
        }
        if (!Resource->PoseFrameReady[frameBIndex])
        {
            if (!BuildMdlPoseVertices(*Resource, nextFrameIndex, Resource->PoseFrameCache[frameBIndex]))
            {
                frameAlpha = 0.0f;
            }
            else
            {
                Resource->PoseFrameReady[frameBIndex] = 1;
            }
        }
        const auto& poseA = Resource->PoseFrameCache[frameAIndex];
        const auto& poseB = Resource->PoseFrameReady[frameBIndex] ? Resource->PoseFrameCache[frameBIndex] : poseA;
        Resource->LastAnimationFrame = frameIndex;
        const std::size_t count = (std::min)({Resource->AnimationVertices.size(), poseA.size(), poseB.size()});
        const bool interpolate = frameAlpha > 0.0001f && frameAIndex != frameBIndex && &poseB != &poseA;
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto& a = poseA[index];
            auto& vertex = Resource->AnimationVertices[index];
            if (!interpolate)
            {
                vertex = a;
                continue;
            }
            const auto& b = poseB[index];
            vertex.X = a.X + (b.X - a.X) * frameAlpha;
            vertex.Y = a.Y + (b.Y - a.Y) * frameAlpha;
            vertex.Z = a.Z + (b.Z - a.Z) * frameAlpha;
            vertex.NX = a.NX + (b.NX - a.NX) * frameAlpha;
            vertex.NY = a.NY + (b.NY - a.NY) * frameAlpha;
            vertex.NZ = a.NZ + (b.NZ - a.NZ) * frameAlpha;
        }

        const bool splitStream = WorldShadersReady && AnimatedWorldDecl && Resource->AnimatedVertexBuffer && Resource->StaticAttributeVertexBuffer;
        if (splitStream)
        {
            UploadVectorToDynamicVertexBuffer(Resource->AnimatedVertexBuffer, Resource->AnimationVertices);
            continue;
        }
        if (!Resource->VertexBuffer)
        {
            continue;
        }
        if (Resource->FallbackAnimationVertices.size() != Resource->CpuVertices.size())
        {
            Resource->FallbackAnimationVertices = Resource->CpuVertices;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto& source = Resource->AnimationVertices[index];
            auto& target = Resource->FallbackAnimationVertices[index];
            target.X = source.X;
            target.Y = source.Y;
            target.Z = source.Z;
            target.NX = source.NX;
            target.NY = source.NY;
            target.NZ = source.NZ;
        }
        void* data = nullptr;
        const UINT bytes = static_cast<UINT>(Resource->FallbackAnimationVertices.size() * sizeof(WorldVertex));
        if (SUCCEEDED(Resource->VertexBuffer->Lock(0, bytes, &data, D3DLOCK_NOSYSLOCK)))
        {
            CopyVectorBytes(data, Resource->FallbackAnimationVertices, bytes);
            Resource->VertexBuffer->Unlock();
        }
    }
}

void FD3D9GameWorldScene::Impl::DrawStaticObjects()
{
    if (StaticDrawBatchesDirty)
    {
        RebuildStaticDrawBatchCache();
    }
    if (DirectStaticInstancesDirty)
    {
        RebuildDirectStaticInstanceCache();
    }
    const bool UseShader = BeginAlphaWorldPass(IdentityMatrix());
    if (!StaticDrawBatches.empty())
    {
        DrawWorldRenderBatches(StaticDrawBatches, EGameWorldDrawBucket::StaticObjects, 1.0f, true);
    }

    IDirect3DTexture9* boundTexture = nullptr;
    const StaticModelResource* boundResource = nullptr;
    const auto queueAnimation = [this](StaticModelResource* resource)
    {
        if (resource && resource->IsSkinned && resource->LastAnimationQueueFrame != RenderStatsFrameCounter)
        {
            resource->LastAnimationQueueFrame = RenderStatsFrameCounter;
            VisibleAnimatedResources.push_back(resource);
        }
    };
    const auto bindResource = [&](StaticModelResource* resource) -> bool
    {
        if (!resource || !resource->IndexBuffer)
        {
            return false;
        }
        const bool splitStream = UseShader && AnimatedWorldDecl && resource->IsSkinned && resource->AnimatedVertexBuffer && resource->StaticAttributeVertexBuffer;
        if (splitStream)
        {
            Device->SetVertexDeclaration(AnimatedWorldDecl);
            Device->SetStreamSource(0, resource->AnimatedVertexBuffer, 0, sizeof(FAnimatedWorldVertex));
            Device->SetStreamSource(1, resource->StaticAttributeVertexBuffer, 0, sizeof(FWorldVertexStaticAttributes));
        }
        else
        {
            if (!resource->VertexBuffer)
            {
                return false;
            }
            if (UseShader)
            {
                Device->SetVertexDeclaration(WorldDecl);
            }
            else
            {
                Device->SetFVF(kWorldVertexFvf);
            }
            Device->SetStreamSource(0, resource->VertexBuffer, 0, sizeof(WorldVertex));
            Device->SetStreamSource(1, nullptr, 0, 0);
        }
        Device->SetIndices(resource->IndexBuffer);
        return true;
    };

    for (const std::size_t instanceIndex : DirectStaticInstanceIndices)
    {
        if (instanceIndex >= StaticInstances.size())
        {
            continue;
        }
        const auto& instance = StaticInstances[instanceIndex];
        auto* resource = instance.resource;
        if (!resource || !IsBoundsVisibleToCamera(instance.Bounds, 1.0f))
        {
            continue;
        }
        queueAnimation(resource);
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
            if (!bindResource(resource))
            {
                boundResource = nullptr;
                continue;
            }
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

    for (auto& [_, actor] : RemoteActors)
    {
        auto* resource = actor.Resource;
        if (!resource)
        {
            continue;
        }
        auto world = RotationYMatrix(static_cast<float>(-actor.Actor.Position.Angle));
        world._41 = static_cast<float>(actor.Actor.Position.X);
        world._42 = static_cast<float>(actor.Actor.Position.Y);
        world._43 = static_cast<float>(actor.Actor.Position.Z);
        if (!actor.BoundsValid)
        {
            actor.Bounds = TransformBounds(resource->Bounds, world);
            actor.BoundsValid = true;
        }
        if (!IsBoundsVisibleToCamera(actor.Bounds, 1.0f))
        {
            continue;
        }
        queueAnimation(resource);
        if (UseShader)
        {
            SetBaseWorld(world);
        }
        else
        {
            Device->SetTransform(D3DTS_WORLD, &world);
        }
        if (resource != boundResource)
        {
            if (!bindResource(resource))
            {
                boundResource = nullptr;
                continue;
            }
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
    Device->SetStreamSource(1, nullptr, 0, 0);
    if (UseShader)
    {
        Device->SetVertexDeclaration(WorldDecl);
    }
    EndAlphaWorldPass(UseShader);
}


void FD3D9GameWorldScene::Impl::ClearGrassRenderBatches()
{
    ReleaseWorldRenderBatchMap(GrassCellRenderBatches);
    for (auto& upload : PendingGrassRenderGpuUploads)
    {
        ReleaseWorldRenderBatches(upload.GpuBatches);
    }
    PendingGrassRenderGpuUploads.clear();
    GrassDrawBatches.clear();
    GrassDrawBatchesDirty = true;
    {
        std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
        ++GrassRenderBakeEpoch;
        PendingGrassRenderBakes.clear();
        CompletedGrassRenderBakes.clear();
        GrassCellBakeRevisions.clear();
        GrassPendingCellsPerRenderGroup.clear();
        GrassDirtyRenderGroups.clear();
    }
    GrassRenderBakeCv.notify_all();
}

void FD3D9GameWorldScene::Impl::StartGrassRenderBakeWorker()
{
    std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
    if (GrassRenderBakeWorkerStarted)
    {
        return;
    }
    GrassRenderBakeStop = false;
    GrassRenderBakeBusy = false;
    GrassRenderBakeWorkerStarted = true;
    GrassRenderBakeThread = std::thread([this]()
    {
        GrassRenderBakeWorkerMain();
    });
}

void FD3D9GameWorldScene::Impl::StopGrassRenderBakeWorker()
{
    {
        std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
        if (!GrassRenderBakeWorkerStarted)
        {
            return;
        }
        GrassRenderBakeStop = true;
        PendingGrassRenderBakes.clear();
    }
    GrassRenderBakeCv.notify_all();
    if (GrassRenderBakeThread.joinable())
    {
        GrassRenderBakeThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
        GrassRenderBakeWorkerStarted = false;
        GrassRenderBakeStop = false;
        GrassRenderBakeBusy = false;
        CompletedGrassRenderBakes.clear();
    }
}

void FD3D9GameWorldScene::Impl::GrassRenderBakeWorkerMain()
{
    LowerStaticWorkerPriority();
    WorldBatchAccumulateScratch scratch;
    for (;;)
    {
        FGrassRenderBakeRequest request;
        {
            std::unique_lock<std::mutex> lock(GrassRenderBakeMutex);
            GrassRenderBakeCv.wait(lock, [this]()
            {
                return GrassRenderBakeStop || !PendingGrassRenderBakes.empty();
            });
            if (GrassRenderBakeStop && PendingGrassRenderBakes.empty())
            {
                break;
            }
            request = std::move(PendingGrassRenderBakes.back());
            PendingGrassRenderBakes.pop_back();
            GrassRenderBakeBusy = true;
        }

        FGrassRenderBakeResult result;
        result.CellKey = request.CellKey;
        result.Epoch = request.Epoch;
        result.Revision = request.Revision;
        try
        {
            AccumulatedWorldBatchMap batchesByTexture;
            ReserveAccumulatedWorldBatches(request.Sources, batchesByTexture);
            for (const auto& source : request.Sources)
            {
                if (source.Resource)
                {
                    AccumulateWorldBatches(*source.Resource, source.World, batchesByTexture, true, source.Tint, &scratch);
                }
            }
            result.Batches = TakeWorldCpuBatches(batchesByTexture);
        }
        catch (...)
        {
            result.Batches.clear();
        }
        {
            std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
            CompletedGrassRenderBakes.push_back(std::move(result));
            GrassRenderBakeBusy = false;
        }
        GrassRenderBakeCv.notify_all();
    }
}

void FD3D9GameWorldScene::Impl::DrainGrassRenderBakeJobs(bool Wait)
{
    if (Wait)
    {
        std::unique_lock<std::mutex> lock(GrassRenderBakeMutex);
        GrassRenderBakeCv.wait(lock, [this]()
        {
            return PendingGrassRenderBakes.empty() && !GrassRenderBakeBusy;
        });
    }
    std::vector<FGrassRenderBakeResult> completed;
    {
        std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
        completed.swap(CompletedGrassRenderBakes);
    }
    for (auto& result : completed)
    {
        if (result.Epoch != GrassRenderBakeEpoch)
        {
            continue;
        }
        const auto revision = GrassCellBakeRevisions.find(result.CellKey);
        if (revision == GrassCellBakeRevisions.end() || revision->second != result.Revision)
        {
            continue;
        }
        FGrassRenderGpuUpload upload;
        upload.Result = std::move(result);
        upload.GpuBatches.reserve(upload.Result.Batches.size());
        PendingGrassRenderGpuUploads.push_back(std::move(upload));
    }

    auto ProcessOneBatch = [this]()
    {
        if (PendingGrassRenderGpuUploads.empty())
        {
            return;
        }
        auto& upload = PendingGrassRenderGpuUploads.front();
        const auto revision = GrassCellBakeRevisions.find(upload.Result.CellKey);
        if (upload.Result.Epoch != GrassRenderBakeEpoch || revision == GrassCellBakeRevisions.end() || revision->second != upload.Result.Revision)
        {
            ReleaseWorldRenderBatches(upload.GpuBatches);
            PendingGrassRenderGpuUploads.pop_front();
            return;
        }
        if (upload.NextBatch < upload.Result.Batches.size())
        {
            WorldRenderBatch batch;
            if (UploadWorldCpuBatch(Device, upload.Result.Batches[upload.NextBatch], batch))
            {
                upload.GpuBatches.push_back(batch);
            }
            ++upload.NextBatch;
        }
        if (upload.NextBatch < upload.Result.Batches.size())
        {
            return;
        }
        auto existing = GrassCellRenderBatches.find(upload.Result.CellKey);
        if (!upload.GpuBatches.empty())
        {
            if (existing != GrassCellRenderBatches.end())
            {
                ReleaseWorldRenderBatches(existing->second);
                existing->second = std::move(upload.GpuBatches);
            }
            else
            {
                GrassCellRenderBatches.emplace(upload.Result.CellKey, std::move(upload.GpuBatches));
            }
            GrassDrawBatchesDirty = true;
        }
        GrassCellBakeRevisions.erase(revision);
        PendingGrassRenderGpuUploads.pop_front();
    };

    if (Wait)
    {
        while (!PendingGrassRenderGpuUploads.empty())
        {
            ProcessOneBatch();
        }
    }
    else
    {
        ProcessOneBatch();
    }
}

void FD3D9GameWorldScene::Impl::BakeGrassCell(uint64 CellKey, std::vector<GrassInstance> Instances)
{
    const auto previous = GrassInstancesByCell.find(CellKey);
    if (previous != GrassInstancesByCell.end())
    {
        GrassInstanceCount -= previous->second.size();
        GrassInstancesByCell.erase(previous);
    }
    if (!Instances.empty())
    {
        GrassInstanceCount += Instances.size();
        GrassInstancesByCell.emplace(CellKey, std::move(Instances));
    }
    GrassDirtyRenderGroups.insert(GrassRenderGroupKey(CellKey));
}

void FD3D9GameWorldScene::Impl::QueueGrassRenderGroupBake(uint64 GroupKey)
{
    if (!GrassDirtyRenderGroups.contains(GroupKey))
    {
        return;
    }
    const uint64 revision = ++GrassRenderBakeRevision;
    GrassCellBakeRevisions.insert_or_assign(GroupKey, revision);
    FGrassRenderBakeRequest request;
    request.CellKey = GroupKey;
    request.Epoch = GrassRenderBakeEpoch;
    request.Revision = revision;
    const int groupX = SignedCellFromHighKey(GroupKey);
    const int groupZ = SignedCellFromLowKey(GroupKey);
    std::size_t sourceCount = 0;
    for (int localX = 0; localX < kGrassRenderGroupSize; ++localX)
    {
        for (int localZ = 0; localZ < kGrassRenderGroupSize; ++localZ)
        {
            const uint64 memberKey = StaticRenderCellKey(groupX * kGrassRenderGroupSize + localX, groupZ * kGrassRenderGroupSize + localZ);
            const auto member = GrassInstancesByCell.find(memberKey);
            if (member != GrassInstancesByCell.end())
            {
                sourceCount += member->second.size();
            }
        }
    }
    request.Sources.reserve(sourceCount);
    for (int localX = 0; localX < kGrassRenderGroupSize; ++localX)
    {
        for (int localZ = 0; localZ < kGrassRenderGroupSize; ++localZ)
        {
            const uint64 memberKey = StaticRenderCellKey(groupX * kGrassRenderGroupSize + localX, groupZ * kGrassRenderGroupSize + localZ);
            const auto member = GrassInstancesByCell.find(memberKey);
            if (member == GrassInstancesByCell.end())
            {
                continue;
            }
            for (const auto& instance : member->second)
            {
                if (instance.resource)
                {
                    request.Sources.push_back(FGrassRenderBakeSource{instance.resource, instance.world, instance.Tint});
                }
            }
        }
    }
    GrassDirtyRenderGroups.erase(GroupKey);
    if (request.Sources.empty())
    {
        const auto existing = GrassCellRenderBatches.find(GroupKey);
        if (existing != GrassCellRenderBatches.end())
        {
            ReleaseWorldRenderBatches(existing->second);
            GrassCellRenderBatches.erase(existing);
            GrassDrawBatchesDirty = true;
        }
        GrassCellBakeRevisions.erase(GroupKey);
        return;
    }
    StartGrassRenderBakeWorker();
    {
        std::lock_guard<std::mutex> lock(GrassRenderBakeMutex);
        const auto pending = std::find_if(PendingGrassRenderBakes.rbegin(), PendingGrassRenderBakes.rend(), [GroupKey, this](const FGrassRenderBakeRequest& item)
        {
            return item.CellKey == GroupKey && item.Epoch == GrassRenderBakeEpoch;
        });
        if (pending != PendingGrassRenderBakes.rend())
        {
            *pending = std::move(request);
        }
        else
        {
            PendingGrassRenderBakes.push_back(std::move(request));
        }
    }
    GrassRenderBakeCv.notify_one();
}

void FD3D9GameWorldScene::Impl::FlushReadyGrassRenderGroupBakes()
{
    std::vector<uint64> ready;
    ready.reserve(GrassDirtyRenderGroups.size());
    for (const uint64 groupKey : GrassDirtyRenderGroups)
    {
        const auto pending = GrassPendingCellsPerRenderGroup.find(groupKey);
        if (pending == GrassPendingCellsPerRenderGroup.end() || pending->second == 0)
        {
            ready.push_back(groupKey);
        }
    }
    for (const uint64 groupKey : ready)
    {
        QueueGrassRenderGroupBake(groupKey);
    }
}

void FD3D9GameWorldScene::Impl::CompleteGrassPendingCell(uint64 CellKey)
{
    const uint64 groupKey = GrassRenderGroupKey(CellKey);
    const auto pending = GrassPendingCellsPerRenderGroup.find(groupKey);
    if (pending == GrassPendingCellsPerRenderGroup.end())
    {
        return;
    }
    if (pending->second > 1)
    {
        --pending->second;
        return;
    }
    GrassPendingCellsPerRenderGroup.erase(pending);
    if (GrassDirtyRenderGroups.contains(groupKey))
    {
        QueueGrassRenderGroupBake(groupKey);
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
        const float wind[4] = {0.0426f, 0.0420f, ElapsedSeconds * Config.GrassWindSpeed * WeatherWind, Config.GrassWindAmplitude * WeatherWind};
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
        const float control[4] = {std::cos(windAngle), std::sin(windAngle), Config.GrassGustRadiusScale * WeatherWind, std::clamp(Config.GrassBreeze * WeatherWind, 0.0f, 1.0f)};
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

    if (GrassDrawBatchesDirty)
    {
        GrassDrawBatches.clear();
        std::size_t batchCount = 0;
        for (const auto& [_, batches] : GrassCellRenderBatches) { batchCount += batches.size(); }
        GrassDrawBatches.reserve(batchCount);
        for (const auto& [_, batches] : GrassCellRenderBatches)
        {
            for (const auto& batch : batches) { GrassDrawBatches.push_back(&batch); }
        }
        std::sort(GrassDrawBatches.begin(), GrassDrawBatches.end(), [](const WorldRenderBatch* left, const WorldRenderBatch* right) { return std::less<IDirect3DTexture9*>{}(left->Texture, right->Texture); });
        GrassDrawBatchesDirty = false;
    }
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    DrawWorldRenderBatches(GrassDrawBatches, EGameWorldDrawBucket::Grass, Config.GrassSpacing * 2.0f, true);

    if (!UseGrassShader)
    {
        Device->LightEnable(0, FALSE);
    }
    EndBaseShader();
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    ConfigureRenderState();
}

