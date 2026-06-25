#include "Model/ChrModel.h"
#include "Core/BinaryReader.h"
#include "Core/StatusUtils.h"
#include "Model/ModelParseUtils.h"
#include "ResourceLoader/ResourceLoadHelpers.h"

namespace
{
    void ReadBoneNames(FChrInfo& info, const FByteArray& data, const FChrChunk& chunk)
    {
        if (chunk.Size < 4)
        {
            throw std::runtime_error("truncated CHR bone-name chunk");
        }

        const size_t end = chunk.Offset + chunk.Size;
        size_t cursor = chunk.Offset;
        const int32 count = Binary::I32LE(data, cursor);
        cursor += 4;

        if (count < 0)
        {
            throw std::runtime_error("negative CHR bone-name count");
        }

        info.BoneNames.clear();
        info.BoneNames.reserve(static_cast<size_t>(count));

        for (int32 i = 0; i < count; ++i)
        {
            info.BoneNames.push_back(Binary::ReadLengthPrefixedString(data, cursor, end, "CHR bone name"));
        }
    }
    const FChrChunk* FindChunk(const FChrInfo& info, int32 type)
    {
        auto it = std::find_if(info.Chunks.begin(), info.Chunks.end(), [&](const FChrChunk& chunk)
        {
            return chunk.Type == type;
        });
        return it == info.Chunks.end() ? nullptr : &*it;
    }
    FChrInfo ParseInfo(const FByteArray& data, std::string_view sourceName)
    {
        if (data.size() < 12 || Binary::U32LE(data, 0) != 0x30686373)
        {
            throw std::runtime_error("bad CHR file: " + std::string(sourceName));
        }

        FChrInfo info;
        info.SourcePath = FPath(std::string(sourceName));
        info.FileSize = data.size();
        info.VersionOrFlags = Binary::I32LE(data, 4);
        info.ChunkCount = Binary::I32LE(data, 8);

        if (info.ChunkCount < 0)
        {
            throw std::runtime_error("negative CHR chunk count");
        }

        size_t cursor = 12;
        const size_t chunkCount = static_cast<size_t>(info.ChunkCount);
        Binary::RequireRange(data, cursor, chunkCount * 12, "CHR chunk table");
        info.Chunks.reserve(chunkCount);

        for (size_t i = 0; i < chunkCount; ++i)
        {
            FChrChunk chunk;
            chunk.Type = Binary::I32LE(data, cursor + 0);
            chunk.Offset = Binary::U32LE(data, cursor + 4);
            chunk.Size = Binary::U32LE(data, cursor + 8);
            Binary::RequireRange(data, chunk.Offset, chunk.Size, "CHR chunk payload");
            info.Chunks.push_back(chunk);
            cursor += 12;
        }

        for (const auto& chunk : info.Chunks)
        {
            if (chunk.Type == 7)
            {
                ReadBoneNames(info, data, chunk);
            }
        }

        return info;
    }
    FChrMesh ParseMesh(const FByteArray& data, std::string_view sourceName)
    {
        FChrMesh mesh;
        mesh.Info = ParseInfo(data, sourceName);
        const auto* vertexChunk = FindChunk(mesh.Info, 1);
        const auto* indexChunk = FindChunk(mesh.Info, 2);

        if (!vertexChunk || !indexChunk)
        {
            throw std::runtime_error("CHR mesh chunks missing: " + std::string(sourceName));
        }

        if (vertexChunk->Size % 0x28 != 0 || indexChunk->Size % 2 != 0)
        {
            throw std::runtime_error("bad CHR mesh chunk size: " + std::string(sourceName));
        }

        const size_t vertexCount = vertexChunk->Size / 0x28;
        mesh.Vertices.reserve(vertexCount);

        for (size_t i = 0; i < vertexCount; ++i)
        {
            const size_t offset = vertexChunk->Offset + i * 0x28;
            FChrVertex vertex;
            vertex.X = Binary::F32LE(data, offset + 0x00);
            vertex.Y = Binary::F32LE(data, offset + 0x04);
            vertex.Z = Binary::F32LE(data, offset + 0x08);
            vertex.NX = Binary::F32LE(data, offset + 0x0c);
            vertex.NY = Binary::F32LE(data, offset + 0x10);
            vertex.NZ = Binary::F32LE(data, offset + 0x14);
            vertex.U = Binary::F32LE(data, offset + 0x18);
            vertex.V = Binary::F32LE(data, offset + 0x1c);
            vertex.Bone0 = Binary::U8(data, offset + 0x20);
            vertex.Bone1 = Binary::U8(data, offset + 0x21);
            vertex.Blend = Binary::F32LE(data, offset + 0x24);
            mesh.Vertices.push_back(vertex);
        }

        const size_t indexCount = indexChunk->Size / 2;
        mesh.Indices.reserve(indexCount);

        for (size_t i = 0; i < indexCount; ++i)
        {
            const auto index = Binary::U16LE(data, indexChunk->Offset + i * 2);

            if (index >= mesh.Vertices.size())
            {
                throw std::runtime_error("CHR index out of vertex range: " + std::string(sourceName));
            }

            mesh.Indices.push_back(index);
        }

        mesh.Bounds = ModelParse::ComputeXYZBounds<FChrBounds>(mesh.Vertices);
        return mesh;
    }
}
TResult<FChrInfo> LoadChrInfoFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    try
    {
        return ParseInfo(bytes, sourceName);
    }
    catch (const std::exception& e)
    {
        return StatusUtils::InvalidDataFromException("CHR parse failed: ", e);
    }
}
TResult<FChrMesh> LoadChrMeshFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    try
    {
        return ParseMesh(bytes, sourceName);
    }
    catch (const std::exception& e)
    {
        return StatusUtils::InvalidDataFromException("CHR mesh parse failed: ", e);
    }
}
TResult<FChrInfo> LoadChrInfoFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    return ResourceLoader::DecodeResource<FChrInfo>(resources, logicalName, LoadChrInfoFromBytes);
}
TResult<FChrMesh> LoadChrMeshFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    return ResourceLoader::DecodeResource<FChrMesh>(resources, logicalName, LoadChrMeshFromBytes);
}
