#include "WorldScene/SpatialIndex.h"

void FSpatialIndex::Clear()
{
    Items.clear();
    Buckets.clear();
}
int32 FSpatialIndex::FloorCell(float value, float cellSize) { return static_cast<int32>(std::floor(value / cellSize)); }
int64 FSpatialIndex::MakeKey(int32 x, int32 y) { return (static_cast<int64>(x) << 32) ^ static_cast<uint32>(y); }
std::vector<int64> FSpatialIndex::CoveredCells(FBox2 bounds) const
{
    std::vector<int64> out;

    if (!bounds.IsValid()) { return out; }

    int32 minX = FloorCell(bounds.Min.X, CellSize);
    int32 maxX = FloorCell(bounds.Max.X, CellSize);
    int32 minY = FloorCell(bounds.Min.Y, CellSize);
    int32 maxY = FloorCell(bounds.Max.Y, CellSize);

    for (int32 y = minY; y <= maxY; ++y)
    {
        for (int32 x = minX; x <= maxX; ++x)
        {
            out.push_back(MakeKey(x, y));
        }
    }

    return out;
}
void FSpatialIndex::Insert(uint32 id, FBox2 bounds, std::string tag)
{
    Remove(id);
    FSpatialItem item;
    item.Id = id;
    item.Bounds = bounds;
    item.Tag = std::move(tag);
    item.Cells = CoveredCells(bounds);

    for (int64 key : item.Cells)
    {
        Buckets[key].push_back(id);
    }

    Items[id] = std::move(item);
}
void FSpatialIndex::Remove(uint32 id)
{
    auto it = Items.find(id);

    if (it == Items.end()) { return; }

    for (int64 key : it->second.Cells)
    {
        auto bucket = Buckets.find(key);

        if (bucket == Buckets.end()) { continue; }

        auto& ids = bucket->second;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());

        if (ids.empty())
        {
            Buckets.erase(bucket);
        }
    }

    Items.erase(it);
}
std::vector<uint32> FSpatialIndex::Query(FBox2 area) const
{
    std::vector<uint32> result;
    std::unordered_set<uint32> seen;

    for (int64 key : CoveredCells(area))
    {
        auto bucket = Buckets.find(key);

        if (bucket == Buckets.end()) { continue; }

        for (uint32 id : bucket->second)
        {
            if (!seen.insert(id).second) { continue; }

            auto it = Items.find(id);

            if (it != Items.end() && it->second.Bounds.Intersects(area))
            {
                result.push_back(id);
            }
        }
    }

    return result;
}
std::optional<FSpatialItem> FSpatialIndex::Find(uint32 id) const
{
    auto it = Items.find(id);

    if (it == Items.end())
    {
        return std::nullopt;
    }

    return it->second;
}
