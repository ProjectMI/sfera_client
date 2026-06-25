#pragma once
#include "Common/BinaryData.h"
#include "Common/StringUtils.h"
#include "Core/BinaryReader.h"
#include "Core/Logger.h"
#include "FileSystem/NativeFile.h"
#include "Model/MdlModel.h"
#include "ResourceLoader/ResourceTypes.h"

#include "Renderer/GameWorld/GameWorldTypes.h"
#include "Renderer/D3D9Utils.h"
#include "Renderer/D3D9TextureLoader.h"

inline FByteArray ReadGameWorldFileBytes(const std::filesystem::path& path)
{
    auto bytes = FNativeFile::ReadAllBytes(path);

    if (!bytes.IsOk())
    {
        throw std::runtime_error(bytes.Status().Message());
    }

    return bytes.Value();
}

inline float Dot(FVector3 a, FVector3 b)
{
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

inline FVector3 Cross(FVector3 a, FVector3 b)
{
    return FVector3{
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X,
    };
}

inline FVector3 NormalizeVector(FVector3 value)
{
    const float length = std::sqrt(Dot(value, value));
    if (length <= 0.00001f)
    {
        return FVector3{0.0f, 1.0f, 0.0f};
    }
    return FVector3{value.X / length, value.Y / length, value.Z / length};
}

inline FVector3 Subtract(FVector3 left, FVector3 right)
{
    return FVector3{left.X - right.X, left.Y - right.Y, left.Z - right.Z};
}

inline FVector3 Add(FVector3 left, FVector3 right)
{
    return FVector3{left.X + right.X, left.Y + right.Y, left.Z + right.Z};
}

inline FVector3 Scale(FVector3 value, float factor)
{
    return FVector3{value.X * factor, value.Y * factor, value.Z * factor};
}

inline FVector3 TransformPoint(FVector3 point, const D3DMATRIX& matrix)
{
    return FVector3{
        point.X * matrix._11 + point.Y * matrix._21 + point.Z * matrix._31 + matrix._41,
        point.X * matrix._12 + point.Y * matrix._22 + point.Z * matrix._32 + matrix._42,
        point.X * matrix._13 + point.Y * matrix._23 + point.Z * matrix._33 + matrix._43,
    };
}

inline float PointTriangleDistanceSquared(FVector3 point, FVector3 a, FVector3 b, FVector3 c)
{
    const FVector3 ab = Subtract(b, a);
    const FVector3 ac = Subtract(c, a);
    const FVector3 ap = Subtract(point, a);
    const float d1 = Dot(ab, ap);
    const float d2 = Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        return Dot(ap, ap);
    }

    const FVector3 bp = Subtract(point, b);
    const float d3 = Dot(ab, bp);
    const float d4 = Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
    {
        return Dot(bp, bp);
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        const FVector3 delta = Subtract(point, Add(a, Scale(ab, v)));
        return Dot(delta, delta);
    }

    const FVector3 cp = Subtract(point, c);
    const float d5 = Dot(ab, cp);
    const float d6 = Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
    {
        return Dot(cp, cp);
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        const FVector3 delta = Subtract(point, Add(a, Scale(ac, w)));
        return Dot(delta, delta);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const FVector3 bc = Subtract(c, b);
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const FVector3 delta = Subtract(point, Add(b, Scale(bc, w)));
        return Dot(delta, delta);
    }

    const float denominator = 1.0f / (va + vb + vc);
    const float v = vb * denominator;
    const float w = vc * denominator;
    const FVector3 delta = Subtract(point, Add(a, Add(Scale(ab, v), Scale(ac, w))));
    return Dot(delta, delta);
}

inline D3DMATRIX IdentityMatrix()
{
    D3DMATRIX matrix{};
    matrix._11 = 1.0f;
    matrix._22 = 1.0f;
    matrix._33 = 1.0f;
    matrix._44 = 1.0f;
    return matrix;
}

inline D3DMATRIX TranslationMatrix(float x, float y, float z)
{
    D3DMATRIX matrix = IdentityMatrix();
    matrix._41 = x;
    matrix._42 = y;
    matrix._43 = z;
    return matrix;
}

inline D3DMATRIX MultiplyMatrix(const D3DMATRIX& Left, const D3DMATRIX& Right)
{
    D3DMATRIX Out{};
    const auto* left = reinterpret_cast<const float*>(&Left);
    const auto* right = reinterpret_cast<const float*>(&Right);
    auto* out = reinterpret_cast<float*>(&Out);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            out[row * 4 + column] = left[row * 4] * right[column] + left[row * 4 + 1] * right[4 + column] + left[row * 4 + 2] * right[8 + column] + left[row * 4 + 3] * right[12 + column];
        }
    }
    return Out;
}

inline D3DMATRIX TransposeMatrix(const D3DMATRIX& Source)
{
    D3DMATRIX out{};
    out._11 = Source._11; out._12 = Source._21; out._13 = Source._31; out._14 = Source._41;
    out._21 = Source._12; out._22 = Source._22; out._23 = Source._32; out._24 = Source._42;
    out._31 = Source._13; out._32 = Source._23; out._33 = Source._33; out._34 = Source._43;
    out._41 = Source._14; out._42 = Source._24; out._43 = Source._34; out._44 = Source._44;
    return out;
}

template<class T>
inline void CopyVectorBytes(void* Destination, const std::vector<T>& Source, std::size_t ByteCount)
{
    const auto* SourceBytes = static_cast<const std::byte*>(static_cast<const void*>(Source.data()));
    auto* DestinationBytes = static_cast<std::byte*>(Destination);
    std::copy_n(SourceBytes, ByteCount, DestinationBytes);
}


inline DWORD FloatRenderStateValue(float Value)
{
    return static_cast<DWORD>(std::bit_cast<uint32>(Value));
}

inline std::vector<DWORD> MakeShaderWords(const FByteArray& Code)
{
    if (Code.size() % sizeof(DWORD) != 0)
    {
        throw std::runtime_error("invalid D3D shader bytecode size");
    }

    std::vector<DWORD> Words;
    Words.reserve(Code.size() / sizeof(DWORD));

    for (std::size_t Offset = 0; Offset < Code.size(); Offset += sizeof(DWORD))
    {
        Words.push_back(Binary::U32LE(Code, Offset));
    }

    return Words;
}

inline std::unordered_map<std::string, int> ParseShaderConstants(const FByteArray& Code)
{
    std::unordered_map<std::string, int> out;
    if (Code.size() < 8)
    {
        return out;
    }
    auto rd = [&](std::size_t o) -> uint32 {
        return static_cast<uint32>(Code[o]) | (static_cast<uint32>(Code[o + 1]) << 8) |
        (static_cast<uint32>(Code[o + 2]) << 16) | (static_cast<uint32>(Code[o + 3]) << 24);
    };
    std::size_t off = 4;  // skip version token
    while (off + 4 <= Code.size())
    {
        const uint32 tok = rd(off);
        if (tok == 0x0000FFFF)
        {
            break;  // END
        }
        if ((tok & 0xFFFF) == 0xFFFE)
        {  // comment token
            const uint32 clen = (tok >> 16) & 0x7FFF;
            const std::size_t cdata = off + 4;
            if (cdata + 4 <= Code.size() && rd(cdata) == 0x42415443)
            {  // 'CTAB'
                const std::size_t ct = cdata + 4;  // CTAB blob base (offsets are relative to here)
                const uint32 nconst = rd(ct + 12);
                const uint32 cinfo = rd(ct + 16);
                for (uint32 i = 0; i < nconst; ++i)
                {
                    const std::size_t e = ct + cinfo + i * 20;
                    if (e + 20 > Code.size())
                    {
                        break;
                    }
                    const uint32 NameOff = rd(e);
                    const uint16 RegSet = static_cast<uint16>(Code[e + 4] | (Code[e + 5] << 8));
                    const uint16 RegIdx = static_cast<uint16>(Code[e + 6] | (Code[e + 7] << 8));
                    if (RegSet != 2)
                    {
                        continue;
                    }
                    std::string name;
                    for (std::size_t p = ct + NameOff; p < Code.size() && Code[p]; ++p)
                    {
                        name.push_back(static_cast<char>(Code[p]));
                    }
                    out[name] = RegIdx;
                }
            }
            off += 1 + clen;
            continue;
        }
        off += 1 + ((tok >> 24) & 0x0F);  // normal instruction length
    }
    return out;
}

inline FByteArray ReadBinaryFile(const std::filesystem::path& path)
{
    auto data = FNativeFile::ReadAllBytes(path);
    return data.IsOk() ? data.Value() : FByteArray{};
}

inline D3DMATRIX RotationXMatrix(float radians)
{
    D3DMATRIX matrix = IdentityMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix._22 = c;
    matrix._23 = s;
    matrix._32 = -s;
    matrix._33 = c;
    return matrix;
}

inline D3DMATRIX RotationYMatrix(float radians)
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

inline D3DMATRIX RotationZMatrix(float radians)
{
    D3DMATRIX matrix = IdentityMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix._11 = c;
    matrix._12 = s;
    matrix._21 = -s;
    matrix._22 = c;
    return matrix;
}

inline D3DMATRIX ScaleMatrix(float Scale)
{
    D3DMATRIX matrix = IdentityMatrix();
    matrix._11 = Scale;
    matrix._22 = Scale;
    matrix._33 = Scale;
    return matrix;
}

inline D3DMATRIX AlignUpMatrix(FVector3 normal)
{
    normal = NormalizeVector(normal);
    FVector3 tangent = Cross(normal, FVector3{0.0f, 0.0f, 1.0f});
    if (Dot(tangent, tangent) <= 0.00001f)
    {
        tangent = Cross(normal, FVector3{1.0f, 0.0f, 0.0f});
    }
    tangent = NormalizeVector(tangent);
    const FVector3 bitangent = NormalizeVector(Cross(tangent, normal));
    auto matrix = IdentityMatrix();
    matrix._11 = tangent.X;
    matrix._12 = tangent.Y;
    matrix._13 = tangent.Z;
    matrix._21 = normal.X;
    matrix._22 = normal.Y;
    matrix._23 = normal.Z;
    matrix._31 = bitangent.X;
    matrix._32 = bitangent.Y;
    matrix._33 = bitangent.Z;
    return matrix;
}

inline D3DMATRIX PlacementMatrix(const StaticPlacement& placement)
{
    D3DMATRIX matrix = MultiplyMatrix(MultiplyMatrix(RotationYMatrix(-placement.Rotation.Y), RotationXMatrix(placement.Rotation.X)), RotationZMatrix(placement.Rotation.Z));
    matrix._41 = placement.Position.X;
    matrix._42 = placement.Position.Y;
    matrix._43 = placement.Position.Z;
    return matrix;
}

inline D3DMATRIX LookAtRhMatrix(FVector3 eye, FVector3 at, FVector3 up)
{
    const FVector3 zaxis = NormalizeVector(FVector3{eye.X - at.X, eye.Y - at.Y, eye.Z - at.Z});
    const FVector3 xaxis = NormalizeVector(Cross(up, zaxis));
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

inline D3DMATRIX PerspectiveFovRhMatrix(float FovY, float aspect, float ZNear, float ZFar)
{
    const float YScale = 1.0f / std::tan(FovY * 0.5f);
    const float XScale = YScale / (std::max)(aspect, 0.001f);
    D3DMATRIX matrix{};
    matrix._11 = XScale;
    matrix._22 = YScale;
    matrix._33 = ZFar / (ZNear - ZFar);
    matrix._34 = -1.0f;
    matrix._43 = (ZNear * ZFar) / (ZNear - ZFar);
    return matrix;
}

inline std::wstring HResultText(const char* action, HRESULT hr)
{
    std::wostringstream out;
    out << action << L" failed: 0x" << std::hex << static_cast<unsigned long>(hr);
    return out.str();
}

inline std::string HResultTextNarrow(const char* action, HRESULT hr)
{
    std::ostringstream out;
    out << action << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
    return out.str();
}


template<class TBuffer, class T>
inline bool UploadVectorToBuffer(TBuffer* Buffer, const std::vector<T>& Source, UINT Bytes)
{
    void* data = nullptr;
    if (FAILED(Buffer->Lock(0, Bytes, &data, 0)))
    {
        return false;
    }
    CopyVectorBytes(data, Source, Bytes);
    Buffer->Unlock();
    return true;
}

template<class T>
inline IDirect3DVertexBuffer9* CreateManagedVertexBufferOrThrow(IDirect3DDevice9* Device, const std::vector<T>& Source, DWORD Fvf, const char* Label)
{
    IDirect3DVertexBuffer9* buffer = nullptr;
    const UINT bytes = static_cast<UINT>(Source.size() * sizeof(T));
    HRESULT hr = Device->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, Fvf, D3DPOOL_MANAGED, &buffer, nullptr);
    if (FAILED(hr) || !UploadVectorToBuffer(buffer, Source, bytes))
    {
        SafeRelease(buffer);
        throw std::runtime_error(HResultTextNarrow(Label, FAILED(hr) ? hr : E_FAIL));
    }
    return buffer;
}

template<class T>
inline IDirect3DIndexBuffer9* CreateManagedIndexBufferOrThrow(IDirect3DDevice9* Device, const std::vector<T>& Source, D3DFORMAT Format, const char* Label)
{
    IDirect3DIndexBuffer9* buffer = nullptr;
    const UINT bytes = static_cast<UINT>(Source.size() * sizeof(T));
    HRESULT hr = Device->CreateIndexBuffer(bytes, D3DUSAGE_WRITEONLY, Format, D3DPOOL_MANAGED, &buffer, nullptr);
    if (FAILED(hr) || !UploadVectorToBuffer(buffer, Source, bytes))
    {
        SafeRelease(buffer);
        throw std::runtime_error(HResultTextNarrow(Label, FAILED(hr) ? hr : E_FAIL));
    }
    return buffer;
}

template<class T>
inline bool TryCreateManagedVertexBuffer(IDirect3DDevice9* Device, const std::vector<T>& Source, DWORD Fvf, IDirect3DVertexBuffer9*& Out)
{
    const UINT bytes = static_cast<UINT>(Source.size() * sizeof(T));
    if (FAILED(Device->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, Fvf, D3DPOOL_MANAGED, &Out, nullptr)) || !UploadVectorToBuffer(Out, Source, bytes))
    {
        SafeRelease(Out);
        return false;
    }
    return true;
}

template<class T>
inline bool TryCreateManagedIndexBuffer(IDirect3DDevice9* Device, const std::vector<T>& Source, D3DFORMAT Format, IDirect3DIndexBuffer9*& Out)
{
    const UINT bytes = static_cast<UINT>(Source.size() * sizeof(T));
    if (FAILED(Device->CreateIndexBuffer(bytes, D3DUSAGE_WRITEONLY, Format, D3DPOOL_MANAGED, &Out, nullptr)) || !UploadVectorToBuffer(Out, Source, bytes))
    {
        SafeRelease(Out);
        return false;
    }
    return true;
}

inline void AssignError(std::wstring& error, const std::string& text)
{
    error.assign(text.begin(), text.end());
}

inline IDirect3DTexture9* LoadDdsTexture(IDirect3DDevice9* Device, const std::filesystem::path& path)
{
    return CreateD3D9TextureFromDdsBytes(Device, ReadGameWorldFileBytes(path), path.string());
}

inline IDirect3DTexture9* LoadMtxTexture(IDirect3DDevice9* Device, const std::filesystem::path& path)
{
    const auto data = ReadGameWorldFileBytes(path);
    if (data.size() < 32 || Binary::U32LE(data, 0) != 0x6d786554)
    {
        throw std::runtime_error("bad MTX file: " + path.string());
    }
    const auto width = Binary::U32LE(data, 4);
    const auto height = Binary::U32LE(data, 8);
    const std::size_t PixelBytes = static_cast<std::size_t>(width) * height * sizeof(uint16);
    Binary::RequireRange(data, 32, PixelBytes, "MTX pixels");
    if (data.size() != 32 + PixelBytes)
    {
        throw std::runtime_error("unexpected MTX size: " + path.string());
    }

    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = Device->CreateTexture(width, height, 1, 0, D3DFMT_A4R4G4B4, D3DPOOL_MANAGED, &texture, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateTexture MTX", hr));
    }
    D3DLOCKED_RECT locked{};
    hr = texture->LockRect(0, &locked, nullptr, 0);
    if (FAILED(hr))
    {
        SafeRelease(texture);
        throw std::runtime_error(HResultTextNarrow("MTX Texture::LockRect", hr));
    }
    const std::size_t SourcePitch = static_cast<std::size_t>(width) * sizeof(uint16);
    for (uint32 row = 0; row < height; ++row)
    {
        std::copy_n(
        data.data() + 32 + static_cast<std::size_t>(row) * SourcePitch,
        SourcePitch,
        static_cast<uint8*>(locked.pBits) + static_cast<std::size_t>(row) * locked.Pitch);
    }
    texture->UnlockRect(0);
    return texture;
}


inline std::string LowercaseAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string NarrowAscii(const std::wstring& value)
{
    std::string out;
    out.reserve(value.size());
    for (const auto ch : value)
    {
        if (ch < 0 || ch > 0x7f)
        {
            throw std::runtime_error("Lua world asset name is not ASCII");
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}
