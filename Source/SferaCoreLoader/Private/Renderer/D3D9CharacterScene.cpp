#include "Renderer/D3D9CharacterScene.h"
#include "Renderer/D3D9Utils.h"
#include "Renderer/D3D9TextureLoader.h"
#include "Core/BinaryReader.h"
#include "Core/Logger.h"
#include "Model/ChrModel.h"
#include "Model/MdlModel.h"
#include "Model/SklSkeleton.h"
#include "Common/StringUtils.h"
#include "Config/ConfigDocument.h"
#include <Windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <bit>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

constexpr unsigned long FVF_SCENE = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr size_t CHARACTER_FREE_ACTION = 20;
constexpr float CHARACTER_ANIMATION_SECONDS_PER_FRAME = 0.08f;

struct FSceneQuat { float W = 1.0f; float X = 0.0f; float Y = 0.0f; float Z = 0.0f; };

static float Approach(float current, float target, float factor) { return current + (target - current) * factor; }


static float Dot(FVector3 a, FVector3 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
static FVector3 Cross(FVector3 a, FVector3 b) { return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X}; }
static FVector3 NormalizeVec3(FVector3 value)
{
    const float length = std::sqrt(Dot(value, value));
    return length <= 0.00001f ? FVector3
    {
        0.0f, 0.0f, 1.0f
    }
    : FVector3
    {
        value.X / length, value.Y / length, value.Z / length
    };
}

static D3DMATRIX IdentityMatrix()
{
    D3DMATRIX matrix{};
    matrix._11 = 1.0f;
    matrix._22 = 1.0f;
    matrix._33 = 1.0f;
    matrix._44 = 1.0f;
    return matrix;
}
static D3DMATRIX RotationYMatrix(float radians)
{
    D3DMATRIX matrix = IdentityMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix._11 = c;
    matrix._13 = -s;
    matrix._31 = s;
    matrix._33 = c;
    return matrix;
}
static FSceneMatrix4 Identity4()
{
    FSceneMatrix4 matrix{};
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
    return matrix;
}
static FSceneMatrix4 MatrixFromSkl(const FSklTransform& transform)
{
    float x = transform.QX;
    float y = transform.QY;
    float z = transform.QZ;
    float w = transform.QW;
    const float length = std::sqrt(x * x + y * y + z * z + w * w);

    if (length <= 0.00001f)
    {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        w = 1.0f;
    }
    else
    {
        x /= length;
        y /= length;
        z /= length;
        w /= length;
    }

    auto matrix = Identity4();
    matrix[0] = 1.0f - 2.0f * y * y - 2.0f * z * z;
    matrix[1] = 2.0f * x * y + 2.0f * z * w;
    matrix[2] = 2.0f * x * z - 2.0f * y * w;
    matrix[4] = 2.0f * x * y - 2.0f * z * w;
    matrix[5] = 1.0f - 2.0f * x * x - 2.0f * z * z;
    matrix[6] = 2.0f * y * z + 2.0f * x * w;
    matrix[8] = 2.0f * x * z + 2.0f * y * w;
    matrix[9] = 2.0f * y * z - 2.0f * x * w;
    matrix[10] = 1.0f - 2.0f * x * x - 2.0f * y * y;
    matrix[12] = transform.TX;
    matrix[13] = transform.TY;
    matrix[14] = transform.TZ;
    return matrix;
}

static FSklTransform BlendSklTransform(const FSklTransform& a, const FSklTransform& b, float alpha)
{
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    FSklTransform out;
    out.TX = a.TX + (b.TX - a.TX) * t;
    out.TY = a.TY + (b.TY - a.TY) * t;
    out.TZ = a.TZ + (b.TZ - a.TZ) * t;
    const float dotQuat = a.QW * b.QW + a.QX * b.QX + a.QY * b.QY + a.QZ * b.QZ;
    const float sign = dotQuat < 0.0f ? -1.0f : 1.0f;
    out.QW = a.QW + (b.QW * sign - a.QW) * t;
    out.QX = a.QX + (b.QX * sign - a.QX) * t;
    out.QY = a.QY + (b.QY * sign - a.QY) * t;
    out.QZ = a.QZ + (b.QZ * sign - a.QZ) * t;
    const float length = std::sqrt(out.QW * out.QW + out.QX * out.QX + out.QY * out.QY + out.QZ * out.QZ);
    if (length <= 0.000001f)
    {
        out.QW = 1.0f;
        out.QX = 0.0f;
        out.QY = 0.0f;
        out.QZ = 0.0f;
    }
    else
    {
        const float inv = 1.0f / length;
        out.QW *= inv;
        out.QX *= inv;
        out.QY *= inv;
        out.QZ *= inv;
    }
    return out;
}

static FSceneMatrix4 MatrixMultiply(const FSceneMatrix4& a, const FSceneMatrix4& b)
{
    FSceneMatrix4 out{};

    for (std::size_t row = 0; row < 4; ++row)
    {
        const std::size_t rowBase = row * 4;

        for (std::size_t col = 0; col < 4; ++col)
        {
            out[rowBase + col] = a[rowBase + 0] * b[col] + a[rowBase + 1] * b[4 + col] + a[rowBase + 2] * b[8 + col] + a[rowBase + 3] * b[12 + col];
        }
    }

    return out;
}

static FVector3 TransformPoint(FSceneMatrix4 matrix, FVector3 value) { return {value.X * matrix[0] + value.Y * matrix[4] + value.Z * matrix[8] + matrix[12], value.X * matrix[1] + value.Y * matrix[5] + value.Z * matrix[9] + matrix[13], value.X * matrix[2] + value.Y * matrix[6] + value.Z * matrix[10] + matrix[14]}; }
static FVector3 TransformVector(FSceneMatrix4 matrix, FVector3 value) { return {value.X * matrix[0] + value.Y * matrix[4] + value.Z * matrix[8], value.X * matrix[1] + value.Y * matrix[5] + value.Z * matrix[9], value.X * matrix[2] + value.Y * matrix[6] + value.Z * matrix[10]}; }
static D3DMATRIX LookAtRh(FVector3 eye, FVector3 at, FVector3 up)
{
    const FVector3 viewDirection
    {
        eye.X - at.X, eye.Y - at.Y, eye.Z - at.Z
    };
    const FVector3 zaxis = NormalizeVec3(viewDirection);
    const FVector3 xaxis = NormalizeVec3(Cross(up, zaxis));
    const FVector3 yaxis = Cross(zaxis, xaxis);
    D3DMATRIX matrix = IdentityMatrix();
    matrix._11 = xaxis.X;
    matrix._12 = yaxis.X;
    matrix._13 = zaxis.X;
    matrix._21 = xaxis.Y;
    matrix._22 = yaxis.Y;
    matrix._23 = zaxis.Y;
    matrix._31 = xaxis.Z;
    matrix._32 = yaxis.Z;
    matrix._33 = zaxis.Z;
    matrix._41 = -Dot(xaxis, eye);
    matrix._42 = -Dot(yaxis, eye);
    matrix._43 = -Dot(zaxis, eye);
    return matrix;
}
static D3DMATRIX PerspectiveFovRh(float fovY, float aspect, float zNear, float zFar)
{
    const float yScale = 1.0f / std::tan(fovY * 0.5f);
    const float xScale = yScale / std::max(aspect, 0.001f);
    D3DMATRIX matrix{};
    matrix._11 = xScale;
    matrix._22 = yScale;
    matrix._33 = zFar / (zNear - zFar);
    matrix._34 = -1.0f;
    matrix._43 = (zNear * zFar) / (zNear - zFar);
    return matrix;
}

static size_t SkeletonAnimationFrameOffset(const FSklSkeleton& Skeleton, size_t action)
{
    if (action >= Skeleton.AnimationFrameCounts.size())
    {
        throw std::runtime_error("SKL has no requested animation action");
    }

    size_t offset = 0;

    for (size_t i = 0; i < action; ++i)
    {
        offset += static_cast<size_t>(Skeleton.AnimationFrameCounts[i]);
    }

    return offset;
}
static size_t SkeletonBoneIndex(const FSklSkeleton& Skeleton, const std::string& name)
{
    const std::string wanted = Common::ToLower(name);

    for (size_t i = 0; i < Skeleton.BoneNames.size(); ++i)
    {
        if (Common::ToLower(Skeleton.BoneNames[i]) == wanted)
        {
            return i;
        }
    }

    throw std::runtime_error("SKL bone not found: " + name);
}

static std::vector<std::string> QuotedStrings(const std::string& line)
{
    std::vector<std::string> values;
    size_t cursor = 0;

    while (cursor < line.size())
    {
        const size_t open = line.find('"', cursor);

        if (open == std::string::npos)
        {
            break;
        }

        const size_t close = line.find('"', open + 1);

        if (close == std::string::npos)
        {
            throw std::runtime_error("unterminated quoted string in subobjs.dat");
        }

        values.push_back(line.substr(open + 1, close - open - 1));
        cursor = close + 1;
    }

    return values;
}
static std::unordered_map<std::string, FXaddSubobject> LoadXaddSubobjects(const FResourceManager& resources)
{
    auto blob = resources.Load("xadd/subobjs.dat");

    if (!blob.IsOk())
    {
        throw std::runtime_error(blob.Status().Message());
    }

    std::string text;
    text.reserve(blob.Value().Bytes.size());

    for (uint8 value : blob.Value().Bytes)
    {
        text.push_back(static_cast<char>(value));
    }
    std::unordered_map<std::string, FXaddSubobject> subobjects;
    bool inSubobjects = false;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line))
    {
        if (line.find("subobjs<as>") != std::string::npos)
        {
            inSubobjects = true;
            continue;
        }

        if (inSubobjects && line.find("lods<as>") != std::string::npos)
        {
            break;
        }

        if (!inSubobjects || line.find('{') == std::string::npos || line.find("s<t>=") == std::string::npos)
        {
            continue;
        }

        const auto values = QuotedStrings(line);

        if (values.size() < 3)
        {
            throw std::runtime_error("bad subobjs.dat entry: " + line);
        }

        FXaddSubobject entry;
        entry.Code = values[0];
        entry.MeshName = values[1];
        entry.TextureNames.assign(values.begin() + 2, values.end());
        subobjects[entry.Code] = std::move(entry);
    }

    if (subobjects.empty())
    {
        throw std::runtime_error("subobjs.dat has no subobjects");
    }

    return subobjects;
}
static const std::vector<std::string>& FaceCodes(bool female)
{
    static const std::vector<std::string> male =
    {
        "mf0", "mf1", "mf2", "mf3", "mf4", "mf5", "mf6", "mf7", "mf8", "mf9", "mfa", "mfb", "mfc"
    };
    static const std::vector<std::string> femaleCodes =
    {
        "wf0", "wf1", "wf2", "wf3", "wf4", "wf5", "wf6", "wf7", "wf8", "wf9", "wfa", "wfb"
    };
    return female ? femaleCodes : male;
}
static const std::vector<std::string>& HairCodes(bool female)
{
    static const std::vector<std::string> male =
    {
        "mr0", "mr1", "mr2"
    };
    static const std::vector<std::string> femaleCodes =
    {
        "wr0", "wr1", "wr2", "wr3", "wr4"
    };
    return female ? femaleCodes : male;
}
static size_t WrapIndex(int value, size_t count)
{
    if (count == 0)
    {
        return 0;
    }

    int v = value % static_cast<int>(count);

    if (v < 0)
    {
        v += static_cast<int>(count);
    }

    return static_cast<size_t>(v);
}
static std::vector<std::string> CharacterSubobjectCodes(bool female, int face, int hair)
{
    const auto& faces = FaceCodes(female);
    const auto& hairs = HairCodes(female);

    if (female)
    {
        return
        {
            "wb0", "wt0", "wg0", "wc0", "we0", faces[WrapIndex(face, faces.size())], hairs[WrapIndex(hair, hairs.size())]
        };
    }

    return
    {
        "mb0", "mt0", "mg0", "mc0", "me0", faces[WrapIndex(face, faces.size())], hairs[WrapIndex(hair, hairs.size())]
    };
}
static bool IsFaceCode(const std::string& code) { return code.size() == 3 && ((code[0] == 'm' && code[1] == 'f') || (code[0] == 'w' && code[1] == 'f')); }
static bool IsHairCode(const std::string& code) { return code.size() == 3 && ((code[0] == 'm' && code[1] == 'r') || (code[0] == 'w' && code[1] == 'r')); }
static size_t TextureIndexForSubobject(const FXaddSubobject& entry, int hairColor, int tattoo)
{
    size_t textureIndex = 0;

    if (IsFaceCode(entry.Code))
    {
        textureIndex = WrapIndex(tattoo, entry.TextureNames.size());
    }
    else if (IsHairCode(entry.Code))
    {
        textureIndex = WrapIndex(hairColor, entry.TextureNames.size());
    }

    if (textureIndex >= entry.TextureNames.size())
    {
        throw std::runtime_error("subobject texture index out of range: " + entry.Code);
    }

    return textureIndex;
}
static std::string ModelTextureLogicalName(const std::string& materialName) { return "models/textures/" + Common::ToLower(materialName) + ".dds"; }
static std::string FindLogicalOrStem(const FResourceManager& resources, const std::string& preferred)
{
    if (resources.Catalog().FindByLogicalName(preferred))
    {
        return preferred;
    }

    const std::string preferredLower = Common::ToLower(FPath(preferred).filename().string());
    const std::string preferredStem = Common::ToLower(FPath(preferred).stem().string());

    for (const auto& record : resources.Catalog().All())
    {
        const std::string filename = Common::ToLower(record.RelativePath.filename().string());
        const std::string stem = Common::ToLower(record.RelativePath.stem().string());

        if (filename == preferredLower || stem == preferredStem)
        {
            return record.RelativePath.generic_string();
        }
    }

    return preferred;
}
static std::vector<uint8> ReadBytesOrThrow(const FResourceManager& resources, const std::string& logicalName)
{
    auto blob = resources.Load(logicalName);

    if (!blob.IsOk())
    {
        throw std::runtime_error(blob.Status().Message());
    }

    return blob.Value().Bytes;
}
static std::string HresultText(std::string_view action, HRESULT hr)
{
    std::ostringstream out;
    out << action << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
    return out.str();
}
static bool SameAppearance(const FCharacterCreationAppearance& a, const FCharacterCreationAppearance& b) { return a.Female == b.Female && a.Face == b.Face && a.Hair == b.Hair && a.HairColor == b.HairColor && a.Tattoo == b.Tattoo; }
FD3D9CharacterScene::FD3D9CharacterScene() = default;
FD3D9CharacterScene::~FD3D9CharacterScene()
{
    Shutdown();
}

void FD3D9CharacterScene::ReleaseBatches(std::vector<FSceneBatch>& Batches)
{
    for (auto& batch : Batches)
    {
        SafeRelease(batch.Texture);
    }
}
void FD3D9CharacterScene::ReleaseBuffers()
{
    SafeRelease(CharacterVertexBuffer);
    SafeRelease(CharacterIndexBuffer);
    SafeRelease(GroundVertexBuffer);
    SafeRelease(GroundIndexBuffer);
    CharacterUploaded = false;
    GroundUploaded = false;
}
void FD3D9CharacterScene::ReleaseCharacterResources()
{
    ReleaseBatches(CharacterBatches);
    SafeRelease(CharacterVertexBuffer);
    SafeRelease(CharacterIndexBuffer);
    CharacterUploaded = false;
}
void FD3D9CharacterScene::Shutdown()
{
    ReleaseBatches(CharacterBatches);
    ReleaseBatches(GroundBatches);
    ReleaseBuffers();
    CharacterVertices.clear();
    CharacterIndices.clear();
    CharacterBatches.clear();
    CharacterSources.clear();
    GroundVertices.clear();
    GroundIndices.clear();
    GroundBatches.clear();
    Skeleton = {};
    Initialized = false;
}

bool FD3D9CharacterScene::LoadGroundMesh(const FResourceManager& resources, std::string& error)
{
    auto meshResult = LoadMdlMeshFromResource(resources, "models/loadscene.mdl");

    if (!meshResult.IsOk()) { error = meshResult.Status().Message(); return false; }

    const FMdlMesh& mesh = meshResult.Value();

    if (mesh.Vertices.empty() || mesh.Triangles.empty() || mesh.Info.Materials.empty()) { error = "loadscene.mdl has no renderable geometry"; return false; }

    bool haveBounds = false;
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f, minZ = 0.0f, maxZ = 0.0f;

    for (const auto& surface : mesh.Surfaces)
    {
        if (surface.TextureIndex < mesh.Info.Materials.size() && mesh.Info.Materials[surface.TextureIndex] == "LOAD_SC02") { continue; }

        if (surface.FirstVertexIndex < 0 || surface.VertexCount <= 0) { continue; }

        const size_t firstVertex = static_cast<size_t>(surface.FirstVertexIndex);
        const size_t vertexCount = static_cast<size_t>(surface.VertexCount);

        if (firstVertex > mesh.Vertices.size() || vertexCount > mesh.Vertices.size() - firstVertex) { continue; }

        for (size_t i = 0; i < vertexCount; ++i)
        {
            const auto& source = mesh.Vertices[firstVertex + i];
            const float sceneX = source.Z;
            const float sceneY = -source.Y;
            const float sceneZ = source.X;

            if (!haveBounds)
            {
                minX = maxX = sceneX;
                minY = maxY = sceneY;
                minZ = maxZ = sceneZ;
                haveBounds = true;
            }
            else
            {
                minX = std::min(minX, sceneX);
                maxX = std::max(maxX, sceneX);
                minY = std::min(minY, sceneY);
                maxY = std::max(maxY, sceneY);
                minZ = std::min(minZ, sceneZ);
                maxZ = std::max(maxZ, sceneZ);
            }
        }
    }

    if (!haveBounds) { error = "loadscene.mdl has no non-sky bounds"; return false; }

    const float centerX = (minX + maxX) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;
    const float spanX = maxX - minX;
    const float spanY = maxY - minY;
    const float spanZ = maxZ - minZ;
    const float scale = 8.8f / std::max({spanX, spanY, spanZ, 0.001f});
    GroundVertices.clear();
    GroundVertices.reserve(mesh.Vertices.size());

    for (const auto& source : mesh.Vertices)
    {
        const FVector3 groundNormalSource
        {
            source.NZ, -source.NY, source.NX
        };
        const auto normal = NormalizeVec3(groundNormalSource);
        FSceneVertex vertex;
        vertex.X = (source.Z - centerX) * scale;
        vertex.Y = ((-source.Y) - minY) * scale - 0.03f;
        vertex.Z = (source.X - centerZ) * scale;
        vertex.NX = normal.X;
        vertex.NY = normal.Y;
        vertex.NZ = normal.Z;
        vertex.Diffuse = D3DCOLOR_ARGB(255, 220, 214, 196);
        vertex.U = source.U;
        vertex.V = source.V;
        GroundVertices.push_back(vertex);
    }

    std::vector<std::vector<uint16>> indicesByMaterial(mesh.Info.Materials.size());

    for (const auto& surface : mesh.Surfaces)
    {
        if (surface.TextureIndex >= mesh.Info.Materials.size() || surface.FirstTriangleIndex < 0 || surface.TriangleCount <= 0 || surface.FirstVertexIndex < 0 || surface.VertexCount <= 0) { continue; }

        const size_t firstTriangle = static_cast<size_t>(surface.FirstTriangleIndex);
        const size_t triangleCount = static_cast<size_t>(surface.TriangleCount);
        const size_t firstVertex = static_cast<size_t>(surface.FirstVertexIndex);
        const size_t vertexCount = static_cast<size_t>(surface.VertexCount);

        if (firstTriangle > mesh.Triangles.size() || triangleCount > mesh.Triangles.size() - firstTriangle || firstVertex > mesh.Vertices.size() || vertexCount > mesh.Vertices.size() - firstVertex) { continue; }

        auto& out = indicesByMaterial[surface.TextureIndex];

        for (size_t i = 0; i < triangleCount; ++i)
        {
            const auto& t = mesh.Triangles[firstTriangle + i];

            if (t.A >= vertexCount || t.B >= vertexCount || t.C >= vertexCount)
            {
                continue;
            }

            out.push_back(static_cast<uint16>(firstVertex + t.A));
            out.push_back(static_cast<uint16>(firstVertex + t.B));
            out.push_back(static_cast<uint16>(firstVertex + t.C));
        }
    }

    GroundIndices.clear();
    GroundBatches.clear();

    for (size_t material = 0; material < indicesByMaterial.size(); ++material)
    {
        auto& group = indicesByMaterial[material];

        if (group.empty()) { continue; }

        const uint32 start = static_cast<uint32>(GroundIndices.size());
        GroundIndices.insert(GroundIndices.end(), group.begin(), group.end());
        const std::string materialName = mesh.Info.Materials[material];
        GroundBatches.push_back({start, static_cast<uint32>(group.size()), FindLogicalOrStem(resources, ModelTextureLogicalName(materialName)), nullptr, materialName == "LOAD_SC02", false});
    }

    if (GroundBatches.empty()) { error = "loadscene.mdl has no material Batches"; return false; }

    return true;
}

std::vector<FSceneMatrix4> FD3D9CharacterScene::BuildSkeletonMatrices(size_t frame) const
{
    if (Skeleton.BoneCount <= 0 || Skeleton.FrameCount <= 0 || frame >= static_cast<size_t>(Skeleton.FrameCount)) { throw std::runtime_error("SKL frame out of range"); }

    const size_t boneCount = static_cast<size_t>(Skeleton.BoneCount);
    std::vector<FSceneMatrix4> matrices(boneCount);
    std::vector<uint8> states(boneCount, 0);
    std::function<FSceneMatrix4(size_t)> resolve = [&](size_t bone) -> FSceneMatrix4
    {
        if (states[bone] == 2) { return matrices[bone]; }

        if (states[bone] == 1) { throw std::runtime_error("SKL parent hierarchy cycle"); }

        states[bone] = 1;
        const size_t transformIndex = frame * boneCount + bone;
        if (transformIndex >= Skeleton.Transforms.size()) { throw std::runtime_error("SKL transform index out of range"); }
        FSceneMatrix4 matrix = MatrixFromSkl(Skeleton.Transforms[transformIndex]);
        const int32 parent = Skeleton.Parents[bone];

        if (parent >= 0)
        {
            matrix = MatrixMultiply(matrix, resolve(static_cast<size_t>(parent)));
        }

        matrices[bone] = matrix;
        states[bone] = 2;
        return matrix;
    };

    for (size_t i = 0; i < boneCount; ++i)
    {
        resolve(i);
    }

    return matrices;
}

std::vector<FSceneMatrix4> FD3D9CharacterScene::BuildSkeletonMatrices(size_t frameA, size_t frameB, float alpha) const
{
    if (Skeleton.BoneCount <= 0 || Skeleton.FrameCount <= 0 || frameA >= static_cast<size_t>(Skeleton.FrameCount) || frameB >= static_cast<size_t>(Skeleton.FrameCount)) { throw std::runtime_error("SKL frame out of range"); }

    const size_t boneCount = static_cast<size_t>(Skeleton.BoneCount);
    std::vector<FSceneMatrix4> matrices(boneCount);
    std::vector<uint8> states(boneCount, 0);
    std::function<FSceneMatrix4(size_t)> resolve = [&](size_t bone) -> FSceneMatrix4
    {
        if (states[bone] == 2) { return matrices[bone]; }

        if (states[bone] == 1) { throw std::runtime_error("SKL parent hierarchy cycle"); }

        states[bone] = 1;
        const size_t transformIndexA = frameA * boneCount + bone;
        const size_t transformIndexB = frameB * boneCount + bone;
        if (transformIndexA >= Skeleton.Transforms.size() || transformIndexB >= Skeleton.Transforms.size()) { throw std::runtime_error("SKL transform index out of range"); }
        FSceneMatrix4 matrix = MatrixFromSkl(BlendSklTransform(Skeleton.Transforms[transformIndexA], Skeleton.Transforms[transformIndexB], alpha));
        const int32 parent = Skeleton.Parents[bone];

        if (parent >= 0)
        {
            matrix = MatrixMultiply(matrix, resolve(static_cast<size_t>(parent)));
        }

        matrices[bone] = matrix;
        states[bone] = 2;
        return matrix;
    };

    for (size_t i = 0; i < boneCount; ++i)
    {
        resolve(i);
    }

    return matrices;
}

void FD3D9CharacterScene::UpdateCharacterVerticesForFrame(size_t frame)
{
    UpdateCharacterVerticesForFrame(frame, frame, 0.0f);
}

void FD3D9CharacterScene::UpdateCharacterVerticesForFrame(size_t frameA, size_t frameB, float alpha)
{
    if (CharacterSources.empty()) { return; }

    auto matrices = BuildSkeletonMatrices(frameA, frameB, alpha);

    if (CharacterRootBone >= matrices.size()) { throw std::runtime_error("character root bone out of range"); }

    const FVector3 rootDelta
    {
        matrices[CharacterRootBone][12] - CharacterRootBindX, matrices[CharacterRootBone][13] - CharacterRootBindY, matrices[CharacterRootBone][14] - CharacterRootBindZ
    };

    for (auto& matrix : matrices)
    {
        matrix[12] -= rootDelta.X;
        matrix[13] -= rootDelta.Y;
        matrix[14] -= rootDelta.Z;
    }

    CharacterVertices.resize(CharacterSources.size());

    for (size_t i = 0; i < CharacterSources.size(); ++i)
    {
        const auto& source = CharacterSources[i];

        if (source.Bone0 >= matrices.size() || source.Bone1 >= matrices.size()) { throw std::runtime_error("skinned source bone out of range"); }

        const auto& matrix0 = matrices[source.Bone0];
        const auto& matrix1 = matrices[source.Bone1];
        const float weight0 = std::clamp(source.Blend, 0.0f, 1.0f);
        const float weight1 = 1.0f - weight0;
        const auto p0 = TransformPoint(matrix0, {source.X, source.Y, source.Z});
        const auto p1 = TransformPoint(matrix1, {source.X, source.Y, source.Z});
        const auto n0 = TransformVector(matrix0, {source.NX, source.NY, source.NZ});
        const auto n1 = TransformVector(matrix1, {source.NX, source.NY, source.NZ});
        const FVector3 skinnedPosition
        {
            p0.X * weight0 + p1.X * weight1, p0.Y * weight0 + p1.Y * weight1, p0.Z * weight0 + p1.Z * weight1
        };
        const FVector3 skinnedNormalSource
        {
            n0.X * weight0 + n1.X * weight1, n0.Y * weight0 + n1.Y * weight1, n0.Z * weight0 + n1.Z * weight1
        };
        const auto skinnedNormal = NormalizeVec3(skinnedNormalSource);
        FSceneVertex vertex;
        vertex.X = (skinnedPosition.X - CharacterCenterX) * CharacterScale;
        vertex.Y = ((-skinnedPosition.Y) - CharacterMinY) * CharacterScale;
        vertex.Z = (skinnedPosition.Z - CharacterCenterZ) * CharacterScale;
        const FVector3 normalSource
        {
            skinnedNormal.X, -skinnedNormal.Y, skinnedNormal.Z
        };
        const auto normal = NormalizeVec3(normalSource);
        vertex.NX = normal.X;
        vertex.NY = normal.Y;
        vertex.NZ = normal.Z;
        vertex.Diffuse = 0xfffffffful;
        vertex.U = source.U;
        vertex.V = source.V;
        CharacterVertices[i] = vertex;
    }
}

void FD3D9CharacterScene::LoadPlayerAnimationTable(const FResourceManager& resources, bool female)
{
    CharacterAnimIdle = static_cast<int>(CHARACTER_FREE_ACTION);
    CharacterAnimWalk = static_cast<int>(CHARACTER_FREE_ACTION);
    CharacterAnimRun = static_cast<int>(CHARACTER_FREE_ACTION);

    const std::string logicalName = female ? "params/char17.cfg" : "params/char04.cfg";
    auto blob = resources.Load(logicalName);
    if (!blob.IsOk())
    {
        return;
    }

    FConfigDocument config;
    std::string text(blob.Value().Bytes.begin(), blob.Value().Bytes.end());
    if (!config.Parse(std::move(text), logicalName).IsOk())
    {
        return;
    }

    if (auto value = config.FindInt("FREE")) { CharacterAnimIdle = static_cast<int>(*value); }
    if (auto value = config.FindInt("WALK")) { CharacterAnimWalk = static_cast<int>(*value); }
    if (auto value = config.FindInt("RUN")) { CharacterAnimRun = static_cast<int>(*value); }
}

bool FD3D9CharacterScene::LoadCharacterMesh(const FResourceManager& resources, const FCharacterCreationAppearance& appearance, std::string& error)
{
    try
    {
        const std::string skeletonLogical = appearance.Female ? "xadd/woman.skl" : "xadd/man.skl";
        auto skeletonResult = LoadSklSkeletonFromResource(resources, skeletonLogical);

        if (!skeletonResult.IsOk()) { error = skeletonResult.Status().Message(); return false; }

        Skeleton = skeletonResult.Value();
        LoadPlayerAnimationTable(resources, appearance.Female);
        CharacterAnimationStart = SkeletonAnimationFrameOffset(Skeleton, CHARACTER_FREE_ACTION);
        CharacterAnimationFrames = static_cast<size_t>(Skeleton.AnimationFrameCounts[CHARACTER_FREE_ACTION]);
        CharacterAnimationTick = GetTickCount();
        CharacterAnimationTime = 0.0f;
        const auto originMatrices = BuildSkeletonMatrices(CharacterAnimationStart);
        CharacterRootBone = SkeletonBoneIndex(Skeleton, "hips");
        CharacterRootBindX = originMatrices[CharacterRootBone][12];
        CharacterRootBindY = originMatrices[CharacterRootBone][13];
        CharacterRootBindZ = originMatrices[CharacterRootBone][14];
        const auto subobjects = LoadXaddSubobjects(resources);
        const auto codes = CharacterSubobjectCodes(appearance.Female, appearance.Face, appearance.Hair);
        std::set<std::string> headCodes;

        if (codes.size() >= 2)
        {
            headCodes.insert(codes[codes.size() - 1]);
            headCodes.insert(codes[codes.size() - 2]);
        }

        CharacterVertices.clear();
        CharacterIndices.clear();
        CharacterBatches.clear();
        CharacterSources.clear();
        bool haveBounds = false;
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f, minZ = 0.0f, maxZ = 0.0f;
        std::unordered_map<std::string, uint8> skeletonBones;

        for (size_t i = 0; i < Skeleton.BoneNames.size(); ++i)
        {
            if (i <= 0xff)
            {
                skeletonBones[Common::ToLower(Skeleton.BoneNames[i])] = static_cast<uint8>(i);
            }
        }

        for (const auto& code : codes)
        {
            const auto entryIt = subobjects.find(code);

            if (entryIt == subobjects.end()) { throw std::runtime_error("missing subobject code in subobjs.dat: " + code); }

            const auto& entry = entryIt->second;

            if (entry.TextureNames.empty()) { throw std::runtime_error("subobject has no textures: " + code); }

            const size_t textureIndex = TextureIndexForSubobject(entry, appearance.HairColor, appearance.Tattoo);
            const std::string chrLogical = FindLogicalOrStem(resources, "xadd/" + entry.MeshName + ".chr");
            auto meshResult = LoadChrMeshFromResource(resources, chrLogical);

            if (!meshResult.IsOk()) { throw std::runtime_error(meshResult.Status().Message()); }

            const auto& mesh = meshResult.Value();

            if (mesh.Vertices.empty() || mesh.Indices.empty()) { throw std::runtime_error("CHR mesh has no renderable triangles: " + chrLogical); }

            std::vector<uint8> boneRemap;
            boneRemap.reserve(mesh.Info.BoneNames.size());

            for (const auto& name : mesh.Info.BoneNames)
            {
                const auto it = skeletonBones.find(Common::ToLower(name));

                if (it == skeletonBones.end())
                {
                    throw std::runtime_error("CHR bone not found in Skeleton: " + name);
                }

                boneRemap.push_back(it->second);
            }

            const std::string textureLogical = FindLogicalOrStem(resources, ModelTextureLogicalName(entry.TextureNames[textureIndex]));
            const size_t vertexBase = CharacterSources.size();

            if (vertexBase + mesh.Vertices.size() > 0xffff) { throw std::runtime_error("combined character mesh exceeds 16-bit index range"); }

            for (const auto& source : mesh.Vertices)
            {
                if (source.Bone0 >= boneRemap.size() || source.Bone1 >= boneRemap.size()) { throw std::runtime_error("CHR vertex bone index out of range: " + chrLogical); }

                CharacterSources.push_back({source.X, source.Y, source.Z, source.NX, source.NY, source.NZ, source.U, source.V, boneRemap[source.Bone0], boneRemap[source.Bone1], source.Blend});
                const float bx = source.X;
                const float by = -source.Y;
                const float bz = source.Z;

                if (!haveBounds)
                {
                    minX = maxX = bx;
                    minY = maxY = by;
                    minZ = maxZ = bz;
                    haveBounds = true;
                }
                else
                {
                    minX = std::min(minX, bx);
                    maxX = std::max(maxX, bx);
                    minY = std::min(minY, by);
                    maxY = std::max(maxY, by);
                    minZ = std::min(minZ, bz);
                    maxZ = std::max(maxZ, bz);
                }
            }

            const uint32 start = static_cast<uint32>(CharacterIndices.size());

            for (uint16 index : mesh.Indices)
            {
                CharacterIndices.push_back(static_cast<uint16>(vertexBase + index));
            }

            CharacterBatches.push_back({start, static_cast<uint32>(mesh.Indices.size()), textureLogical, nullptr, false, headCodes.count(code) != 0});
        }

        if (!haveBounds || CharacterIndices.empty()) { throw std::runtime_error("XADD character has no renderable geometry"); }

        CharacterCenterX = (minX + maxX) * 0.5f;
        CharacterCenterZ = (minZ + maxZ) * 0.5f;
        CharacterMinY = minY;
        CharacterScale = 2.05f / std::max(maxY - minY, 0.001f);
        UpdateCharacterVerticesForFrame(CharacterAnimationStart);
        CurrentAppearance = appearance;
        return true;
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        return false;
    }
}

IDirect3DTexture9* FD3D9CharacterScene::LoadDdsTexture(IDirect3DDevice9* device, const FResourceManager& resources, const std::string& logicalName, FLogger* logger, std::string& error)
{
    try
    {
        return CreateD3D9TextureFromDdsBytes(device, ReadBytesOrThrow(resources, logicalName), logicalName);
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        if (logger)
        {
            logger->Warning("3D DDS texture load failed: " + logicalName + " - " + error);
        }
        return nullptr;
    }
}

bool FD3D9CharacterScene::UploadGroundBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error)
{
    if (!device || GroundVertices.empty() || GroundIndices.empty()) { error = "ground buffers have no source data"; return false; }

    for (auto& batch : GroundBatches)
    {
        batch.Texture = LoadDdsTexture(device, resources, batch.TextureLogicalName, logger, error);

        if (!batch.Texture)
        {
            return false;
        }
    }

    HRESULT hr = device->CreateVertexBuffer(static_cast<UINT>(GroundVertices.size() * sizeof(FSceneVertex)), 0, FVF_SCENE, D3DPOOL_MANAGED, &GroundVertexBuffer, nullptr);

    if (FAILED(hr)) { error = HresultText("CreateVertexBuffer ground", hr); return false; }

    void* vertexData = nullptr;
    hr = GroundVertexBuffer->Lock(0, static_cast<UINT>(GroundVertices.size() * sizeof(FSceneVertex)), &vertexData, 0);

    if (FAILED(hr)) { error = HresultText("GroundVertexBuffer::Lock", hr); return false; }

    std::copy(GroundVertices.begin(), GroundVertices.end(), static_cast<FSceneVertex*>(vertexData));
    GroundVertexBuffer->Unlock();
    hr = device->CreateIndexBuffer(static_cast<UINT>(GroundIndices.size() * sizeof(uint16)), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &GroundIndexBuffer, nullptr);

    if (FAILED(hr)) { error = HresultText("CreateIndexBuffer ground", hr); return false; }

    void* indexData = nullptr;
    hr = GroundIndexBuffer->Lock(0, static_cast<UINT>(GroundIndices.size() * sizeof(uint16)), &indexData, 0);

    if (FAILED(hr)) { error = HresultText("GroundIndexBuffer::Lock", hr); return false; }

    std::copy(GroundIndices.begin(), GroundIndices.end(), static_cast<uint16*>(indexData));
    GroundIndexBuffer->Unlock();
    GroundUploaded = true;
    return true;
}

bool FD3D9CharacterScene::UploadCharacterBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error)
{
    if (!device || CharacterVertices.empty() || CharacterIndices.empty()) { error = "character buffers have no source data"; return false; }

    for (auto& batch : CharacterBatches)
    {
        batch.Texture = LoadDdsTexture(device, resources, batch.TextureLogicalName, logger, error);

        if (!batch.Texture)
        {
            return false;
        }
    }

    HRESULT hr = device->CreateVertexBuffer(static_cast<UINT>(CharacterVertices.size() * sizeof(FSceneVertex)), 0, FVF_SCENE, D3DPOOL_MANAGED, &CharacterVertexBuffer, nullptr);

    if (FAILED(hr)) { error = HresultText("CreateVertexBuffer character", hr); return false; }

    void* vertexData = nullptr;
    hr = CharacterVertexBuffer->Lock(0, static_cast<UINT>(CharacterVertices.size() * sizeof(FSceneVertex)), &vertexData, 0);

    if (FAILED(hr)) { error = HresultText("CharacterVertexBuffer::Lock", hr); return false; }

    std::copy(CharacterVertices.begin(), CharacterVertices.end(), static_cast<FSceneVertex*>(vertexData));
    CharacterVertexBuffer->Unlock();
    hr = device->CreateIndexBuffer(static_cast<UINT>(CharacterIndices.size() * sizeof(uint16)), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &CharacterIndexBuffer, nullptr);

    if (FAILED(hr)) { error = HresultText("CreateIndexBuffer character", hr); return false; }

    void* indexData = nullptr;
    hr = CharacterIndexBuffer->Lock(0, static_cast<UINT>(CharacterIndices.size() * sizeof(uint16)), &indexData, 0);

    if (FAILED(hr)) { error = HresultText("CharacterIndexBuffer::Lock", hr); return false; }

    std::copy(CharacterIndices.begin(), CharacterIndices.end(), static_cast<uint16*>(indexData));
    CharacterIndexBuffer->Unlock();
    CharacterUploaded = true;
    return true;
}

bool FD3D9CharacterScene::UploadBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error) { return UploadGroundBuffers(device, resources, logger, error) && UploadCharacterBuffers(device, resources, logger, error); }

bool FD3D9CharacterScene::EnsureInitialized(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger)
{
    if (Initialized) { return true; }

    std::string error;

    if (!LoadGroundMesh(resources, error))
    {
        if (logger)
        {
            logger->Warning("3D create scene ground load failed: " + error);
        }

        return false;
    }

    if (!LoadCharacterMesh(resources, CurrentAppearance, error))
    {
        if (logger)
        {
            logger->Warning("3D create scene character load failed: " + error);
        }

        return false;
    }

    if (!UploadBuffers(device, resources, logger, error))
    {
        if (logger)
        {
            logger->Warning("3D create scene upload failed: " + error);
        }

        Shutdown();
        return false;
    }

    SetCameraFocus(0, true);
    Initialized = true;

    if (logger)
    {
        logger->Info("3D character creation scene initialized: vertices=" + std::to_string(CharacterVertices.size()) + ", ground=" + std::to_string(GroundVertices.size()));
    }

    return true;
}

bool FD3D9CharacterScene::UpdateCharacterAppearance(IDirect3DDevice9* device, const FResourceManager& resources, const FCharacterCreationAppearance& appearance, FLogger* logger)
{
    if (SameAppearance(appearance, CurrentAppearance) && CharacterUploaded) { return true; }

    ReleaseCharacterResources();
    std::string error;

    if (!LoadCharacterMesh(resources, appearance, error))
    {
        if (logger)
        {
            logger->Warning("3D character appearance load failed: " + error);
        }

        return false;
    }

    if (!UploadCharacterBuffers(device, resources, logger, error))
    {
        if (logger)
        {
            logger->Warning("3D character appearance upload failed: " + error);
        }

        return false;
    }

    return true;
}

void FD3D9CharacterScene::ConfigureRenderState(IDirect3DDevice9* device)
{
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device->SetRenderState(D3DRS_LIGHTING, TRUE);
    device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(50, 44, 40));
    device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    device->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    device->SetRenderState(D3DRS_FOGENABLE, TRUE);
    device->SetRenderState(D3DRS_FOGCOLOR, D3DCOLOR_XRGB(16, 17, 16));
    device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    const float fogStart = 7.0f;
    const float fogEnd = 16.0f;
    DWORD fogStartBits = 0;
    DWORD fogEndBits = 0;
    fogStartBits = static_cast<DWORD>(std::bit_cast<uint32>(fogStart));
    fogEndBits = static_cast<DWORD>(std::bit_cast<uint32>(fogEnd));
    device->SetRenderState(D3DRS_FOGSTART, fogStartBits);
    device->SetRenderState(D3DRS_FOGEND, fogEndBits);
    D3DMATERIAL9 material{};
    material.Diffuse.r = 1.0f;
    material.Diffuse.g = 1.0f;
    material.Diffuse.b = 1.0f;
    material.Diffuse.a = 1.0f;
    material.Ambient = material.Diffuse;
    device->SetMaterial(&material);
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = 1.0f;
    light.Diffuse.g = 0.92f;
    light.Diffuse.b = 0.82f;
    light.Ambient.r = 0.28f;
    light.Ambient.g = 0.25f;
    light.Ambient.b = 0.22f;
    light.Direction.x = -0.35f;
    light.Direction.y = -0.55f;
    light.Direction.z = 0.75f;
    device->SetLight(0, &light);
    device->LightEnable(0, TRUE);
}

void FD3D9CharacterScene::SetCameraFocus(int32 focusId, bool snap)
{
    struct FProfile
    {
        bool Valid;
        float X;
        float Y;
        float Z;
        float Yaw;
        float Distance;
        float Pitch;
        float Fov;
    };
    const FProfile body
    {
        true, -1.95f, 1.42f, -0.18f, -0.24f, 4.25f, 0.045f, 50.0f
    };
    const FProfile face
    {
        true, 0.14f, 2.22f, -0.18f, 0.08f, 1.58f, 0.01f, 50.0f
    };
    const FProfile hair
    {
        true, 0.12f, 2.38f, -0.18f, 0.08f, 1.42f, 0.0f, 50.0f
    };
    const FProfile hairColor
    {
        true, 0.12f, 2.30f, -0.18f, 0.08f, 1.46f, 0.0f, 50.0f
    };
    FProfile profile = body;

    if (focusId == 13 || focusId == 16)
    {
        profile = face;
    }
    else if (focusId == 14)
    {
        profile = hair;
    }
    else if (focusId == 15)
    {
        profile = hairColor;
    }
    else if (focusId == 12 || focusId == 0)
    {
        profile = body;
    }
    else
    {
        return;
    }

    CameraFocusId = focusId;
    CameraFocusXTarget = profile.X;
    CameraFocusYTarget = profile.Y;
    CameraFocusZTarget = profile.Z;
    CameraYawTarget = profile.Yaw;
    CameraDistanceTarget = profile.Distance;
    CameraPitchTarget = profile.Pitch;
    CameraFovDegreesTarget = profile.Fov;

    if (snap)
    {
        CameraFocusX = CameraFocusXTarget;
        CameraFocusY = CameraFocusYTarget;
        CameraFocusZ = CameraFocusZTarget;
        CameraYaw = CameraYawTarget;
        CameraDistance = CameraDistanceTarget;
        CameraPitch = CameraPitchTarget;
        CameraFovDegrees = CameraFovDegreesTarget;
    }
}

void FD3D9CharacterScene::UpdateCamera()
{
    CameraFocusX = Approach(CameraFocusX, CameraFocusXTarget, 0.12f);
    CameraFocusY = Approach(CameraFocusY, CameraFocusYTarget, 0.12f);
    CameraFocusZ = Approach(CameraFocusZ, CameraFocusZTarget, 0.12f);
    CameraYaw = Approach(CameraYaw, CameraYawTarget, 0.12f);
    CameraDistance = Approach(CameraDistance, CameraDistanceTarget, 0.12f);
    CameraPitch = Approach(CameraPitch, CameraPitchTarget, 0.12f);
    CameraFovDegrees = Approach(CameraFovDegrees, CameraFovDegreesTarget, 0.12f);
}

void FD3D9CharacterScene::UpdateViewProjection(IDirect3DDevice9* device, const RECT& clientRect)
{
    const int width = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
    const int height = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    UpdateCamera();
    const FVector3 target
    {
        CameraFocusX, CameraFocusY, CameraFocusZ
    };
    const float cp = std::cos(CameraPitch);
    const float sp = std::sin(CameraPitch);
    const float sy = std::sin(CameraYaw);
    const float cy = std::cos(CameraYaw);
    const float horizontalDistance = cp * CameraDistance;
    const FVector3 eye
    {
        target.X + sy * horizontalDistance, target.Y + sp * CameraDistance, target.Z - cy * horizontalDistance
    };
    const FVector3 up
    {
        0.0f, 1.0f, 0.0f
    };
    const D3DMATRIX view = LookAtRh(eye, target, up);
    const D3DMATRIX projection = PerspectiveFovRh(CameraFovDegrees * Sfera::InitialCharacterSceneAngle / 180.0f, aspect, 0.05f, 100.0f);
    device->SetTransform(D3DTS_VIEW, &view);
    device->SetTransform(D3DTS_PROJECTION, &projection);
}

void FD3D9CharacterScene::UpdateCharacterAnimation(IDirect3DDevice9*, float deltaSeconds)
{
    if (!CharacterVertexBuffer || CharacterSources.empty() || CharacterAnimationFrames == 0) { return; }

    CharacterAnimationTime += std::clamp(deltaSeconds, 0.0f, 0.1f);
    const float framePosition = CharacterAnimationTime / CHARACTER_ANIMATION_SECONDS_PER_FRAME;
    const size_t frameBase = static_cast<size_t>(std::floor(framePosition)) % CharacterAnimationFrames;
    const size_t frameNext = (frameBase + 1) % CharacterAnimationFrames;
    const float frameAlpha = framePosition - std::floor(framePosition);

    try
    {
        UpdateCharacterVerticesForFrame(CharacterAnimationStart + frameBase, CharacterAnimationStart + frameNext, frameAlpha);
    }
    catch (...)
    {
        return;
    }

    const UINT bytes = static_cast<UINT>(CharacterVertices.size() * sizeof(FSceneVertex));

    if (bytes == 0) { return; }

    void* data = nullptr;

    if (SUCCEEDED(CharacterVertexBuffer->Lock(0, bytes, &data, 0)))
    {
        std::copy(CharacterVertices.begin(), CharacterVertices.end(), static_cast<FSceneVertex*>(data));
        CharacterVertexBuffer->Unlock();
    }
}

void FD3D9CharacterScene::DrawGround(IDirect3DDevice9* device)
{
    if (!GroundVertexBuffer || !GroundIndexBuffer || GroundBatches.empty()) { return; }

    const D3DMATRIX world = IdentityMatrix();
    device->SetTransform(D3DTS_WORLD, &world);
    device->SetFVF(FVF_SCENE);
    device->SetStreamSource(0, GroundVertexBuffer, 0, sizeof(FSceneVertex));
    device->SetIndices(GroundIndexBuffer);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

    for (const auto& batch : GroundBatches)
    {
        if (batch.Sky && batch.Texture && batch.IndexCount >= 3)
        {
            device->SetTexture(0, batch.Texture);
            device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, static_cast<UINT>(GroundVertices.size()), batch.StartIndex, batch.IndexCount / 3);
        }
    }

    ConfigureRenderState(device);
    device->SetStreamSource(0, GroundVertexBuffer, 0, sizeof(FSceneVertex));
    device->SetIndices(GroundIndexBuffer);

    for (const auto& batch : GroundBatches)
    {
        if (!batch.Sky && batch.Texture && batch.IndexCount >= 3)
        {
            device->SetTexture(0, batch.Texture);
            device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, static_cast<UINT>(GroundVertices.size()), batch.StartIndex, batch.IndexCount / 3);
        }
    }
}

void FD3D9CharacterScene::DrawCharacter(IDirect3DDevice9* device)
{
    if (!CharacterVertexBuffer || !CharacterIndexBuffer || CharacterBatches.empty()) { return; }

    D3DMATRIX world = RotationYMatrix(CharacterAngle);
    world._42 = 0.56f;
    world._43 = -0.18f;
    device->SetTransform(D3DTS_WORLD, &world);
    device->SetFVF(FVF_SCENE);
    device->SetStreamSource(0, CharacterVertexBuffer, 0, sizeof(FSceneVertex));
    device->SetIndices(CharacterIndexBuffer);

    for (const auto& batch : CharacterBatches)
    {
        if (batch.Texture && batch.IndexCount >= 3)
        {
            device->SetTexture(0, batch.Texture);
            device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, static_cast<UINT>(CharacterVertices.size()), batch.StartIndex, batch.IndexCount / 3);
        }
    }
}


FSkinnedCharacterModel FD3D9CharacterScene::ExportSkinnedModel() const
{
    FSkinnedCharacterModel out;
    if (CharacterSources.empty() || CharacterIndices.empty() || CharacterBatches.empty() || Skeleton.BoneCount <= 0 || Skeleton.FrameCount <= 0) { return out; }
    out.Skeleton = Skeleton;
    out.RootBone = CharacterRootBone;
    out.RootBindX = CharacterRootBindX;
    out.RootBindY = CharacterRootBindY;
    out.RootBindZ = CharacterRootBindZ;
    out.CenterX = CharacterCenterX;
    out.CenterZ = CharacterCenterZ;
    out.MinY = CharacterMinY;
    out.Scale = CharacterScale;
    out.AnimIdle = CharacterAnimIdle;
    out.AnimWalk = CharacterAnimWalk;
    out.AnimRun = CharacterAnimRun;
    out.Indices = CharacterIndices;
    out.Sources = CharacterSources;
    out.Batches.reserve(CharacterBatches.size());
    for (const auto& batch : CharacterBatches) { out.Batches.push_back({batch.StartIndex, batch.IndexCount, std::filesystem::path(batch.TextureLogicalName), batch.Head}); }
    return out;
}

bool FD3D9CharacterScene::Draw(IDirect3DDevice9* device, const FResourceManager& resources, const FCharacterCreationAppearance& appearance, float characterAngle, int32 cameraFocusId, const RECT& clientRect, float deltaSeconds, FLogger* logger)
{
    if (!EnsureInitialized(device, resources, logger)) { return false; }

    if (!UpdateCharacterAppearance(device, resources, appearance, logger)) { return false; }

    CharacterAngle = characterAngle;

    if (CameraFocusId != cameraFocusId)
    {
        SetCameraFocus(cameraFocusId, false);
    }

    ConfigureRenderState(device);
    UpdateViewProjection(device, clientRect);
    UpdateCharacterAnimation(device, deltaSeconds);
    DrawGround(device);
    DrawCharacter(device);
    return true;
}
