#pragma once
#include "Core/BinaryReader.h"

namespace ModelParse
{
inline size_t CheckedByteSize(size_t count, size_t stride, std::string_view what)
{
    if (stride != 0 && count > std::numeric_limits<size_t>::max() / stride) { throw std::runtime_error(std::string(what) + " size overflow"); }
    return count * stride;
}

inline size_t CheckedEnd(size_t offset, size_t size, std::string_view what)
{
    if (offset > std::numeric_limits<size_t>::max() - size) { throw std::runtime_error(std::string(what) + " offset overflow"); }
    return offset + size;
}

template <class TSection>
inline size_t RecordOffset(const TSection& section, size_t index, std::string_view what)
{
    return CheckedEnd(section.Offset, CheckedByteSize(index, section.Stride, what), what);
}

template <class TSection>
inline const TSection* FindNamedSection(const std::vector<TSection>& sections, std::string_view name)
{
    auto it = std::find_if(sections.begin(), sections.end(), [&](const TSection& section) { return section.Name == name; });
    return it == sections.end() ? nullptr : &*it;
}

template <class TSection>
inline const TSection& RequireNamedSection(const std::vector<TSection>& sections, std::string_view name, std::string_view fileKind)
{
    const TSection* section = FindNamedSection(sections, name);
    if (!section) { throw std::runtime_error(std::string("missing ") + std::string(fileKind) + " section: " + std::string(name)); }
    return *section;
}

template <class TBounds, class TVertex>
inline TBounds ComputeXYZBounds(const std::vector<TVertex>& vertices)
{
    if (vertices.empty()) { return {}; }
    TBounds bounds;
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
}
