#include "Model/MdlModel.h"
#include "Core/BinaryReader.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace
{
    void AddSection(FMdlInfo& info, std::string name, size_t& offset, size_t count, size_t stride)
    {
        const size_t size = count * stride;
        info.Sections.push_back(FMdlSection{std::move(name), offset, count, stride, size});
        offset += size;
    }
    void ReadMaterials(FMdlInfo& info, const FByteArray& data)
    {
        constexpr size_t namesOffset = 0x102;
        Binary::RequireRange(data, namesOffset, info.MaterialNamesSize, "MDL material names");
        size_t cursor = namesOffset;
        const size_t end = namesOffset + info.MaterialNamesSize;

        for (uint8 i = 0; i < info.MaterialCount; ++i)
        {
            if (cursor >= end)
            {
                throw std::runtime_error("truncated MDL material name table");
            }

            const size_t length = data[cursor++];

            if (cursor + length > end)
            {
                throw std::runtime_error("truncated MDL material name");
            }

            info.Materials.emplace_back(reinterpret_cast<const char*>(data.data() + cursor), length);
            cursor += length;
        }
    }
    const FMdlSection& SectionByName(const FMdlInfo& info, const std::string& name)
    {
        auto it = std::find_if(info.Sections.begin(), info.Sections.end(), [&](const FMdlSection& section)
        {
            return section.Name == name;
        });

        if (it == info.Sections.end())
        {
            throw std::runtime_error("missing MDL section: " + name);
        }

        return *it;
    }
    const FMdlSection* FindSectionByName(const FMdlInfo& info, const std::string& name)
    {
        auto it = std::find_if(info.Sections.begin(), info.Sections.end(), [&](const FMdlSection& section)
        {
            return section.Name == name;
        });
        return it == info.Sections.end() ? nullptr : &*it;
    }
    void ValidateCounts(const FMdlInfo& info)
    {
        if (info.ExtraMode == 2 && (info.ExtraRecordCount < 0 || info.ExtraBlockCount < 0))
        {
            throw std::runtime_error("negative MDL extra section count");
        }
    }
    FMdlInfo ParseInfo(const FByteArray& data, std::string_view sourceName)
    {
        if (data.size() < 0x102 || data[0] != 'M' || data[1] != 'D' || data[2] != 'L' || data[3] != '!')
        {
            throw std::runtime_error("bad MDL file: " + std::string(sourceName));
        }

        FMdlInfo info;
        info.SourcePath = FPath(std::string(sourceName));
        info.FileSize = data.size();
        info.VertexCount = Binary::U16LE(data, 0x04);
        info.IndexCount = Binary::U16LE(data, 0x06);
        info.TriangleGroupCount = Binary::U16LE(data, 0x08);
        info.MaterialCount = Binary::U8(data, 0x0a);
        info.MaterialNamesSize = Binary::U16LE(data, 0x0b);
        info.StripGroupCount = Binary::U8(data, 0x0d);
        info.SmallBlockSize = Binary::U8(data, 0x0e);
        info.UnknownFlag0F = Binary::U8(data, 0x0f);
        info.SkinRecordCount = Binary::U16LE(data, 0x10);
        info.SkinIndexCount = Binary::U16LE(data, 0x12);
        info.SkinWeightCount = Binary::U8(data, 0x14);
        info.AnimationFlag = Binary::U8(data, 0x17);
        info.AnimationTableCount = Binary::U16LE(data, 0x18);
        info.ExtraMode = Binary::I32LE(data, 0x1a);
        info.ExtraRecordCount = Binary::I32LE(data, 0xfa);
        info.ExtraBlockCount = Binary::I32LE(data, 0xfe);
        ValidateCounts(info);
        ReadMaterials(info, data);
        info.Sections.push_back(FMdlSection{"material_names", 0x102, info.MaterialCount, 0, info.MaterialNamesSize});
        size_t offset = 0x102 + info.MaterialNamesSize;
        AddSection(info, "vertices_0x20", offset, info.VertexCount, 0x20);
        AddSection(info, "indices_0x0a", offset, info.IndexCount, 0x0a);
        AddSection(info, "triangle_groups_0x0f", offset, info.TriangleGroupCount, 0x0f);
        AddSection(info, "strip_groups_0x27", offset, info.StripGroupCount, 0x27);
        AddSection(info, "small_block", offset, info.SmallBlockSize, 1);

        if (info.SkinWeightCount != 0)
        {
            AddSection(info, "skin_records_0x1c", offset, info.SkinRecordCount, 0x1c);
            AddSection(info, "skin_indices_0x03", offset, info.SkinIndexCount, 0x03);
            AddSection(info, "skin_weights_0x02", offset, info.SkinWeightCount, 0x02);
        }
        else
        {
            AddSection(info, "strip_fallback_0x18", offset, info.StripGroupCount, 0x18);
        }

        if (info.AnimationFlag == 1)
        {
            AddSection(info, "animation_table_0x04", offset, info.AnimationTableCount, 0x04);
            AddSection(info, "animation_indices_0x06", offset, info.IndexCount, 0x06);
        }

        if (info.ExtraMode == 2)
        {
            AddSection(info, "extra_records_0x0c", offset, static_cast<size_t>(info.ExtraRecordCount), 0x0c);
            AddSection(info, "extra_blocks_0x50", offset, static_cast<size_t>(info.ExtraBlockCount), 0x50);
        }

        info.ComputedSize = offset;

        if (info.ComputedSize != info.FileSize)
        {
            throw std::runtime_error("MDL section size mismatch: " + std::string(sourceName));
        }

        return info;
    }
    void ReadVertices(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto& section = SectionByName(mesh.Info, "vertices_0x20");
        Binary::RequireRange(data, section.Offset, section.Size, "MDL vertices");
        mesh.Vertices.reserve(section.Count);

        for (size_t i = 0; i < section.Count; ++i)
        {
            size_t offset = section.Offset + i * section.Stride;
            FMdlVertex vertex;
            vertex.X = Binary::F32LE(data, offset + 0x00);
            vertex.Y = Binary::F32LE(data, offset + 0x04);
            vertex.Z = Binary::F32LE(data, offset + 0x08);
            vertex.NX = Binary::F32LE(data, offset + 0x0c);
            vertex.NY = Binary::F32LE(data, offset + 0x10);
            vertex.NZ = Binary::F32LE(data, offset + 0x14);
            vertex.U = Binary::F32LE(data, offset + 0x18);
            vertex.V = Binary::F32LE(data, offset + 0x1c);
            mesh.Vertices.push_back(vertex);
        }
    }
    void ReadTriangles(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto& section = SectionByName(mesh.Info, "indices_0x0a");
        Binary::RequireRange(data, section.Offset, section.Size, "MDL triangle records");
        mesh.Triangles.reserve(section.Count);

        for (size_t i = 0; i < section.Count; ++i)
        {
            size_t offset = section.Offset + i * section.Stride;
            FMdlTriangle triangle;
            triangle.A = Binary::U16LE(data, offset + 0x00);
            triangle.B = Binary::U16LE(data, offset + 0x02);
            triangle.C = Binary::U16LE(data, offset + 0x04);
            triangle.Flags = Binary::U16LE(data, offset + 0x06);
            triangle.Reserved = Binary::U16LE(data, offset + 0x08);
            mesh.Triangles.push_back(triangle);
        }
    }
    void ReadSurfaces(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto& section = SectionByName(mesh.Info, "triangle_groups_0x0f");
        Binary::RequireRange(data, section.Offset, section.Size, "MDL surfaces");
        mesh.Surfaces.reserve(section.Count);

        for (size_t i = 0; i < section.Count; ++i)
        {
            size_t offset = section.Offset + i * section.Stride;
            FMdlSurface surface;
            surface.ObjectIndex = Binary::U8(data, offset + 0x00);
            surface.TextureIndex = Binary::U8(data, offset + 0x01);
            surface.FirstTriangleIndex = Binary::I16LE(data, offset + 0x02);
            surface.TriangleCount = Binary::I16LE(data, offset + 0x04);
            surface.FirstVertexIndex = Binary::I16LE(data, offset + 0x06);
            surface.VertexCount = Binary::I16LE(data, offset + 0x08);
            mesh.Surfaces.push_back(surface);
        }
    }
    void ReadObjects(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = FindSectionByName(mesh.Info, "strip_groups_0x27");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL objects");
        mesh.Objects.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            size_t offset = section->Offset + i * section->Stride;
            FMdlObject object;
            object.Name = Binary::ReadFixedString(data, offset, 32);
            object.BoneType = Binary::U8(data, offset + 0x20);
            object.ConnectedBoneCount = Binary::U8(data, offset + 0x21);
            object.ObjectIndexOffset = Binary::U8(data, offset + 0x22);
            object.IsAnimated = Binary::U8(data, offset + 0x23);
            object.KeyIndex = Binary::I16LE(data, offset + 0x24);
            object.ParentIndex = Binary::U8(data, offset + 0x26);
            mesh.Objects.push_back(std::move(object));
        }
    }
    void ReadObjectIndices(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = FindSectionByName(mesh.Info, "small_block");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL object indices");
        mesh.ObjectIndices.assign(data.begin() + static_cast<std::ptrdiff_t>(section->Offset), data.begin() + static_cast<std::ptrdiff_t>(section->Offset + section->Size));
    }
    void ReadTransformKeys(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = FindSectionByName(mesh.Info, "skin_records_0x1c");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL transform keys");
        mesh.TransformKeys.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            size_t offset = section->Offset + i * section->Stride;
            FMdlTransformKey key;
            key.X = Binary::F32LE(data, offset + 0x00);
            key.Y = Binary::F32LE(data, offset + 0x04);
            key.Z = Binary::F32LE(data, offset + 0x08);
            key.QW = Binary::F32LE(data, offset + 0x0c);
            key.QX = Binary::F32LE(data, offset + 0x10);
            key.QY = Binary::F32LE(data, offset + 0x14);
            key.QZ = Binary::F32LE(data, offset + 0x18);
            mesh.TransformKeys.push_back(key);
        }
    }
    void ReadActions(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = FindSectionByName(mesh.Info, "skin_weights_0x02");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL actions");
        mesh.Actions.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            mesh.Actions.push_back(Binary::U16LE(data, section->Offset + i * section->Stride));
        }
    }
    FMdlBounds ComputeBounds(const std::vector<FMdlVertex>& vertices)
    {
        if (vertices.empty())
        {
            return {};
        }

        FMdlBounds bounds;
        bounds.MinX = bounds.MaxX = vertices.front().X;
        bounds.MinY = bounds.MaxY = vertices.front().Y;
        bounds.MinZ = bounds.MaxZ = vertices.front().Z;

        for (const auto& vertex : vertices)
        {
            bounds.MinX = std::min(bounds.MinX, vertex.X);
            bounds.MinY = std::min(bounds.MinY, vertex.Y);
            bounds.MinZ = std::min(bounds.MinZ, vertex.Z);
            bounds.MaxX = std::max(bounds.MaxX, vertex.X);
            bounds.MaxY = std::max(bounds.MaxY, vertex.Y);
            bounds.MaxZ = std::max(bounds.MaxZ, vertex.Z);
        }

        return bounds;
    }
    FMdlMesh ParseMesh(const FByteArray& data, std::string_view sourceName)
    {
        FMdlMesh mesh;
        mesh.Info = ParseInfo(data, sourceName);
        ReadVertices(mesh, data);
        ReadTriangles(mesh, data);
        ReadSurfaces(mesh, data);
        ReadObjects(mesh, data);
        ReadObjectIndices(mesh, data);
        ReadTransformKeys(mesh, data);
        ReadActions(mesh, data);
        mesh.Bounds = ComputeBounds(mesh.Vertices);
        return mesh;
    }
    FStatus ExceptionStatus(std::string_view prefix, const std::exception& e) { return FStatus::Error(EStatusCode::InvalidData, std::string(prefix) + e.what()); }
}
TResult<FMdlInfo> LoadMdlInfoFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    try
    {
        return ParseInfo(bytes, sourceName);
    }
    catch (const std::exception& e)
    {
        return ExceptionStatus("MDL parse failed: ", e);
    }
}
TResult<FMdlMesh> LoadMdlMeshFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    try
    {
        return ParseMesh(bytes, sourceName);
    }
    catch (const std::exception& e)
    {
        return ExceptionStatus("MDL mesh parse failed: ", e);
    }
}
TResult<FMdlInfo> LoadMdlInfoFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    auto blob = resources.Load(logicalName);

    if (!blob.IsOk())
    {
        return blob.Status();
    }

    return LoadMdlInfoFromBytes(blob.Value().Bytes, blob.Value().SourcePath.generic_string());
}
TResult<FMdlMesh> LoadMdlMeshFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    auto blob = resources.Load(logicalName);

    if (!blob.IsOk())
    {
        return blob.Status();
    }

    return LoadMdlMeshFromBytes(blob.Value().Bytes, blob.Value().SourcePath.generic_string());
}
