#include "Model/MdlModel.h"
#include "Core/BinaryReader.h"
#include "Common/StringUtils.h"
#include "Core/StatusUtils.h"
#include "Model/ModelParseUtils.h"
#include "ResourceLoader/ResourceLoadHelpers.h"

namespace
{
    constexpr size_t MdlHeaderSize = 0x102;
    std::mutex MdlMeshCacheMutex;
    std::unordered_map<std::string, std::shared_ptr<const FMdlMesh>> MdlMeshCache;

    std::string MdlCacheKey(std::string_view sourceName)
    {
        std::string key(sourceName);
        std::replace(key.begin(), key.end(), '\\', '/');
        return Common::ToLower(key);
    }

    std::shared_ptr<const FMdlMesh> FindCachedMdlMeshInternal(std::string_view sourceName)
    {
        const auto key = MdlCacheKey(sourceName);
        if (key.empty())
        {
            return {};
        }
        std::lock_guard<std::mutex> lock(MdlMeshCacheMutex);
        auto it = MdlMeshCache.find(key);
        return it == MdlMeshCache.end() ? std::shared_ptr<const FMdlMesh>{} : it->second;
    }

    std::shared_ptr<const FMdlMesh> StoreCachedMdlMesh(std::string_view sourceName, FMdlMesh mesh)
    {
        const auto key = MdlCacheKey(sourceName);
        auto parsed = std::make_shared<FMdlMesh>(std::move(mesh));
        if (key.empty())
        {
            return parsed;
        }
        std::lock_guard<std::mutex> lock(MdlMeshCacheMutex);
        auto [it, inserted] = MdlMeshCache.emplace(key, parsed);
        return inserted ? parsed : it->second;
    }

    void AddSection(FMdlInfo& info, std::string name, size_t& offset, size_t count, size_t stride)
    {
        const size_t size = ModelParse::CheckedByteSize(count, stride, "MDL section");
        info.Sections.push_back(FMdlSection{std::move(name), offset, count, stride, size});
        offset = ModelParse::CheckedEnd(offset, size, "MDL section");
    }
    void ReadMaterials(FMdlInfo& info, const FByteArray& data)
    {
        constexpr size_t namesOffset = MdlHeaderSize;
        Binary::RequireRange(data, namesOffset, info.MaterialNamesSize, "MDL material names");
        size_t cursor = namesOffset;
        const size_t end = ModelParse::CheckedEnd(namesOffset, static_cast<size_t>(info.MaterialNamesSize), "MDL material names");

        for (uint8 materialIndex = 0; materialIndex < info.MaterialCount; ++materialIndex)
        {
            info.Materials.push_back(Binary::ReadLengthPrefixedString(data, cursor, end, "MDL material name"));
        }
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
        if (data.size() < MdlHeaderSize || data[0] != 'M' || data[1] != 'D' || data[2] != 'L' || data[3] != '!')
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
        info.Sections.push_back(FMdlSection{"material_names", MdlHeaderSize, static_cast<size_t>(info.MaterialCount), 0, static_cast<size_t>(info.MaterialNamesSize)});
        size_t offset = ModelParse::CheckedEnd(MdlHeaderSize, static_cast<size_t>(info.MaterialNamesSize), "MDL material names");
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
            AddSection(info, "strip_records_0x18", offset, info.StripGroupCount, 0x18);
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
        const auto& section = ModelParse::RequireNamedSection(mesh.Info.Sections, "vertices_0x20", "MDL");
        Binary::RequireRange(data, section.Offset, section.Size, "MDL vertices");
        mesh.Vertices.reserve(section.Count);

        for (size_t i = 0; i < section.Count; ++i)
        {
            size_t offset = ModelParse::RecordOffset(section, i, "MDL record");
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
        const auto& section = ModelParse::RequireNamedSection(mesh.Info.Sections, "indices_0x0a", "MDL");
        Binary::RequireRange(data, section.Offset, section.Size, "MDL triangle records");
        mesh.Triangles.reserve(section.Count);

        for (size_t i = 0; i < section.Count; ++i)
        {
            size_t offset = ModelParse::RecordOffset(section, i, "MDL record");
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
        const auto& section = ModelParse::RequireNamedSection(mesh.Info.Sections, "triangle_groups_0x0f", "MDL");
        Binary::RequireRange(data, section.Offset, section.Size, "MDL surfaces");
        mesh.Surfaces.reserve(section.Count);

        for (size_t i = 0; i < section.Count; ++i)
        {
            size_t offset = ModelParse::RecordOffset(section, i, "MDL record");
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
        const auto* section = ModelParse::FindNamedSection(mesh.Info.Sections, "strip_groups_0x27");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL objects");
        mesh.Objects.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            size_t offset = ModelParse::RecordOffset(*section, i, "MDL record");
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
        const auto* section = ModelParse::FindNamedSection(mesh.Info.Sections, "small_block");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL object indices");
        const size_t endOffset = ModelParse::CheckedEnd(section->Offset, section->Size, "MDL object indices");
        mesh.ObjectIndices.assign(data.begin() + static_cast<std::ptrdiff_t>(section->Offset), data.begin() + static_cast<std::ptrdiff_t>(endOffset));
    }
    void ReadTransformKeys(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = ModelParse::FindNamedSection(mesh.Info.Sections, "skin_records_0x1c");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL transform keys");
        mesh.TransformKeys.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            size_t offset = ModelParse::RecordOffset(*section, i, "MDL record");
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
    void ReadSkinIndices(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = ModelParse::FindNamedSection(mesh.Info.Sections, "skin_indices_0x03");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL skin indices");
        mesh.SkinIndices.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            const size_t offset = ModelParse::RecordOffset(*section, i, "MDL record");
            FMdlSkinIndex entry;
            entry.Record = Binary::U16LE(data, offset + 0x00);
            entry.Blend = Binary::U8(data, offset + 0x02);
            mesh.SkinIndices.push_back(entry);
        }
    }

    void ReadActions(FMdlMesh& mesh, const FByteArray& data)
    {
        const auto* section = ModelParse::FindNamedSection(mesh.Info.Sections, "skin_weights_0x02");

        if (!section)
        {
            return;
        }

        Binary::RequireRange(data, section->Offset, section->Size, "MDL actions");
        mesh.Actions.reserve(section->Count);

        for (size_t i = 0; i < section->Count; ++i)
        {
            const size_t offset = ModelParse::RecordOffset(*section, i, "MDL record");
            mesh.Actions.push_back(Binary::U16LE(data, offset));
        }
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
        ReadSkinIndices(mesh, data);
        ReadActions(mesh, data);
        mesh.Bounds = ModelParse::ComputeXYZBounds<FMdlBounds>(mesh.Vertices);
        return mesh;
    }
}
TResult<FMdlInfo> LoadMdlInfoFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    try
    {
        return ParseInfo(bytes, sourceName);
    }
    catch (const std::exception& e)
    {
        return StatusUtils::InvalidDataFromException("MDL parse failed: ", e);
    }
}
TResult<FMdlMesh> LoadMdlMeshFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    if (auto cached = FindCachedMdlMeshInternal(sourceName))
    {
        return *cached;
    }
    try
    {
        return *StoreCachedMdlMesh(sourceName, ParseMesh(bytes, sourceName));
    }
    catch (const std::exception& e)
    {
        return StatusUtils::InvalidDataFromException("MDL mesh parse failed: ", e);
    }
}

std::shared_ptr<const FMdlMesh> FindCachedMdlMesh(std::string_view sourceName)
{
    return FindCachedMdlMeshInternal(sourceName);
}

void AliasCachedMdlMesh(std::string_view aliasName, std::string_view sourceName)
{
    auto mesh = FindCachedMdlMeshInternal(sourceName);
    const auto aliasKey = MdlCacheKey(aliasName);
    if (!mesh || aliasKey.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(MdlMeshCacheMutex);
    MdlMeshCache[aliasKey] = std::move(mesh);
}

void ClearMdlMeshCache()
{
    std::lock_guard<std::mutex> lock(MdlMeshCacheMutex);
    MdlMeshCache.clear();
}

size_t CachedMdlMeshCount()
{
    std::lock_guard<std::mutex> lock(MdlMeshCacheMutex);
    return MdlMeshCache.size();
}
TResult<FMdlInfo> LoadMdlInfoFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    return ResourceLoader::DecodeResource<FMdlInfo>(resources, logicalName, LoadMdlInfoFromBytes);
}
TResult<FMdlMesh> LoadMdlMeshFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    if (auto record = resources.Catalog().FindByLogicalName(logicalName))
    {
        const auto absoluteName = record->AbsolutePath.generic_string();
        if (auto cached = FindCachedMdlMeshInternal(absoluteName))
        {
            return *cached;
        }
        if (auto cached = FindCachedMdlMeshInternal(logicalName))
        {
            AliasCachedMdlMesh(absoluteName, logicalName);
            AliasCachedMdlMesh(record->RelativePath.generic_string(), logicalName);
            return *cached;
        }
        auto result = ResourceLoader::DecodeResource<FMdlMesh>(resources, logicalName, LoadMdlMeshFromBytes);
        if (result.IsOk())
        {
            AliasCachedMdlMesh(absoluteName, logicalName);
            AliasCachedMdlMesh(record->RelativePath.generic_string(), logicalName);
        }
        return result;
    }
    return ResourceLoader::DecodeResource<FMdlMesh>(resources, logicalName, LoadMdlMeshFromBytes);
}
