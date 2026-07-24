#include "Components/UI/MapRuntimeComponent.h"

namespace
{
int32 ReadI32(const std::vector<uint8>& data, size_t offset)
{
    const uint32 value = static_cast<uint32>(data[offset]) | (static_cast<uint32>(data[offset + 1]) << 8) | (static_cast<uint32>(data[offset + 2]) << 16) | (static_cast<uint32>(data[offset + 3]) << 24);
    return static_cast<int32>(value);
}

bool IsKnownMapSprite(std::string_view value)
{
    static constexpr std::array<std::string_view, 13> names{"mhp1", "hmp1", "feb1", "smp1", "smp2", "smp3", "smp4", "smp5", "smp6", "cmp1", "cmp2", "cmp3", "cmp4"};
    return std::find(names.begin(), names.end(), value) != names.end();
}

bool ProjectionIsValid(const std::array<int32, 8>& p)
{
    const int64 worldWidth = static_cast<int64>(p[2]) - p[0];
    const int64 worldHeight = static_cast<int64>(p[1]) - p[3];
    const int64 mapWidth = static_cast<int64>(p[6]) - p[4];
    const int64 mapHeight = static_cast<int64>(p[7]) - p[5];
    return worldWidth > 0 && worldHeight > 0 && worldWidth < 10000000 && worldHeight < 10000000 && mapWidth > 0 && mapHeight > 0 && mapWidth <= 4096 && mapHeight <= 4096;
}
}

void FMapRuntimeComponent::Reset()
{
    Current = {};
}

bool FMapRuntimeComponent::InspectFrames(const std::vector<std::vector<uint8>>& frames)
{
    bool changed = false;
    for (const std::vector<uint8>& frame : frames) { changed = InspectFrame(frame) || changed; }
    return changed;
}

bool FMapRuntimeComponent::InspectFrame(const std::vector<uint8>& frame)
{
    constexpr size_t descriptorSize = 280;
    constexpr size_t projectionOffset = 128;
    constexpr size_t spriteOffset = 244;
    if (frame.size() < descriptorSize) { return false; }
    for (size_t base = 0; base + descriptorSize <= frame.size(); ++base)
    {
        const std::string sprite(reinterpret_cast<const char*>(frame.data() + base + spriteOffset), 4);
        if (!IsKnownMapSprite(sprite)) { continue; }
        std::array<int32, 8> projection{};
        for (size_t index = 0; index < projection.size(); ++index) { projection[index] = ReadI32(frame, base + projectionOffset + (index + 1) * 4); }
        if (!ProjectionIsValid(projection)) { continue; }
        const bool changed = !Current.HasProjection || Current.SpriteName != sprite || Current.Projection != projection;
        Current.SpriteName = sprite;
        Current.Projection = projection;
        Current.HasProjection = true;
        return changed;
    }
    return false;
}
