#pragma once


#include "Core/Logger.h"
#include "Core/Types.h"
#include "Model/MdlModel.h"
#include "Renderer/CharacterSceneTypes.h"
#include "ResourceLoader/ResourceTypes.h"
#include "WorldScene/WorldTypes.h"


constexpr DWORD kWorldVertexFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;
constexpr DWORD kOverlayVertexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr std::size_t kLndHeaderBytes = 0x68c0;
constexpr std::size_t kLndVertexBytes = 40;
constexpr std::size_t kLndTriangleBytes = 28;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kPlayerIdleAction = 20;
constexpr float kPlayerAnimSecondsPerFrame = 0.08f;
constexpr float kEyeBelowCrownWorld = 0.10f;
constexpr float kEyeForwardModel = 0.36f;
constexpr float kIdleBodyBackShift = 0.0f;
constexpr float kWalkBodyBackShift = 0.22f;
constexpr float kRunBodyBackShift = 0.5f;
constexpr float kWalkBobScale = 1.0f;

struct WorldVertex
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float NX = 0.0f;
    float NY = 1.0f;
    float NZ = 0.0f;
    DWORD Diffuse = 0xfffffffful;
    float U = 0.0f;
    float V = 0.0f;
    float DetailU = 0.0f;
    float DetailV = 0.0f;
};

struct OverlayVertex
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float Rhw = 1.0f;
    DWORD Diffuse = 0xfffffffful;
    float U = 0.0f;
    float V = 0.0f;
};


struct TerrainCpuResource
{
    std::filesystem::path LNDPath;
    std::filesystem::path TexturePath;
    FByteArray TextureBytes;
    std::vector<WorldVertex> Vertices;
    std::vector<FVector3> Positions;
    std::vector<uint16> Indices;
    int SurfaceGridResolution = 0;
    std::vector<std::vector<uint32>> SurfaceCells;
    std::vector<WorldVertex> WaterVertices;
    std::vector<uint16> WaterIndices;
    int TextureWidth = 0;
    int TextureHeight = 0;
    std::vector<uint16> TexturePixels;
    FBox3 Bounds{};
    float WaterHeight = 0.0f;
    bool HasWater = false;
};

struct TerrainResource
{
    std::filesystem::path LNDPath;
    std::filesystem::path TexturePath;
    IDirect3DVertexBuffer9* VertexBuffer = nullptr;
    IDirect3DIndexBuffer9* IndexBuffer = nullptr;
    IDirect3DTexture9* texture = nullptr;
    UINT VertexCount = 0;
    UINT IndexCount = 0;
    std::vector<FVector3> positions;
    std::vector<uint16> Indices;
    int SurfaceGridResolution = 0;
    std::vector<std::vector<uint32>> SurfaceCells;
    IDirect3DVertexBuffer9* WaterVertexBuffer = nullptr;
    IDirect3DIndexBuffer9* WaterIndexBuffer = nullptr;
    UINT WaterVertexCount = 0;
    UINT WaterIndexCount = 0;
    float WaterHeight = 0.0f;
    bool HasWater = false;
    std::vector<WorldVertex> WaterCpuVerts;
    int TextureWidth = 0;
    int TextureHeight = 0;
    std::vector<uint16> TexturePixels;
    FBox3 Bounds{};
};

struct TerrainInstance
{
    TerrainResource* resource = nullptr;
    float OriginX = 0.0f;
    float OriginZ = 0.0f;
};

struct StaticModelResource
{
    IDirect3DVertexBuffer9* VertexBuffer = nullptr;
    IDirect3DIndexBuffer9* IndexBuffer = nullptr;
    UINT VertexCount = 0;
    std::vector<FSceneBatch> Batches;
    std::vector<FVector3> CollisionPositions;
    std::vector<uint16> CollisionIndices;
    std::vector<WorldVertex> CpuVertices;
    std::vector<uint16> CpuIndices;
    FBox3 Bounds;
    bool IsSkinned = false;
    FMdlMesh BindMesh;
    std::vector<WorldVertex> AnimationVertices;
    std::vector<int> ClipStart;
    std::vector<int> ClipLength;
    std::vector<int> GestureClips;
    int FrameCount = 1;
    int LastAnimationFrame = -1;
    int IdleClip = -1;
    int CurrentClip = -1;
    float ClipTime = 0.0f;
};

struct WorldRenderBatch
{
    IDirect3DTexture9* Texture = nullptr;
    IDirect3DVertexBuffer9* VertexBuffer = nullptr;
    IDirect3DIndexBuffer9* IndexBuffer = nullptr;
    UINT VertexCount = 0;
    UINT IndexCount = 0;
    FBox3 Bounds{};
};

struct StaticPlacementModel
{
    std::string Name;
    std::string Key;
};

struct StaticPlacement
{
    uint32 ModelId = 0;
    FVector3 Position;
    FVector3 Rotation;
    D3DMATRIX World{};
    FBox3 Bounds{};
    bool BoundsValid = false;
};

struct StaticInstance
{
    StaticModelResource* resource = nullptr;
    D3DMATRIX world{};
    FBox3 Bounds;
};

struct StaticPlacementLoadResult
{
    std::vector<StaticPlacementModel> Models;
    std::vector<StaticPlacement> Placements;
};

struct GrassInstance
{
    StaticModelResource* resource = nullptr;
    D3DMATRIX world{};
    float WindPhase = 0.0f;
    float WindScale = 1.0f;
    int CellX = 0;
    int CellZ = 0;
    DWORD Tint = 0xfffffffful;
};
