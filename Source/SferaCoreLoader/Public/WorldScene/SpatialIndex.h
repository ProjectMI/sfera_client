#pragma once
#include "WorldScene/WorldTypes.h"
#include <unordered_map>
#include <unordered_set>

namespace Sfera {
struct FSpatialItem { uint32 Id = 0; FBox2 Bounds; std::string Tag; std::vector<int64> Cells; };
class FSpatialIndex {
public:
    explicit FSpatialIndex(float cellSize = 256.0f) : CellSize(cellSize) {}
    void Clear();
    void Insert(uint32 id, FBox2 bounds, std::string tag = {});
    void Remove(uint32 id);
    std::vector<uint32> Query(FBox2 area) const;
    std::optional<FSpatialItem> Find(uint32 id) const;
    size_t Count() const { return Items.size(); }
    float GetCellSize() const { return CellSize; }
private:
    static int32 FloorCell(float value, float cellSize);
    static int64 MakeKey(int32 x, int32 y);
    std::vector<int64> CoveredCells(FBox2 bounds) const;
    float CellSize = 256.0f;
    std::unordered_map<uint32, FSpatialItem> Items;
    std::unordered_map<int64, std::vector<uint32>> Buckets;
};
}
