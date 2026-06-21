#include "WorldScene/WorldScene.h"
#include "Core/NumericParse.h"
#include <algorithm>
#include <cctype>
#include <bit>
#include <cmath>
#include <regex>
#include <sstream>
#include <set>
#include <unordered_set>

static std::string LowerPathString(FPath path)
{
    std::string s = path.generic_string();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}
static uint16 ReadU16LE(const FByteArray& bytes, size_t offset)
{
    if (offset + 2 > bytes.size())
    {
        return 0;
    }

    return static_cast<uint16>(bytes[offset] | (bytes[offset + 1] << 8));
}
static uint32 ReadU32LE(const FByteArray& bytes, size_t offset)
{
    if (offset + 4 > bytes.size())
    {
        return 0;
    }

    return static_cast<uint32>(bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24));
}
static int32 ReadI32LE(const FByteArray& bytes, size_t offset) { return static_cast<int32>(ReadU32LE(bytes, offset)); }
static float ReadFloatLE(const FByteArray& bytes, size_t offset)
{
    if (offset + sizeof(float) > bytes.size())
    {
        return 0.0f;
    }

    return std::bit_cast<float>(ReadU32LE(bytes, offset));
}
static int64 MakeCoordKey(int32 x, int32 z) { return (static_cast<int64>(x) << 32) ^ static_cast<uint32>(z); }
static bool ReasonableWorldFloat(float v) { return std::isfinite(v) && std::abs(v) < 10000000.0f; }
static void ExpandBounds(FBox2& box, FVector2 point, bool& initialized)
{
    if (!initialized)
    {
        box.Min = point;
        box.Max = point;
        initialized = true;
        return;
    }

    box.Min.X = std::min(box.Min.X, point.X);
    box.Min.Y = std::min(box.Min.Y, point.Y);
    box.Max.X = std::max(box.Max.X, point.X);
    box.Max.Y = std::max(box.Max.Y, point.Y);
}
static std::string ReadFixedCString(const FByteArray& bytes, size_t offset, size_t maxLen)
{
    std::string out;

    for (size_t i = 0; i < maxLen && offset + i < bytes.size(); ++i)
    {
        char c = static_cast<char>(bytes[offset + i]);

        if (c == '\0')
        {
            break;
        }

        out.push_back(c);
    }

    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
    {
        out.pop_back();
    }

    return out;
}
static bool HasExtension(std::string_view lowerPath, std::string_view ext) { return lowerPath.size() >= ext.size() && lowerPath.substr(lowerPath.size() - ext.size()) == ext; }
static bool IsLandscapeMtxPath(std::string_view lowerPath) { return lowerPath.find("landscape") != std::string::npos && HasExtension(lowerPath, ".mtx"); }
static bool IsLandscapeSizPath(std::string_view lowerPath) { return lowerPath.find("landscape") != std::string::npos && HasExtension(lowerPath, ".siz"); }
static std::string StripKnownExtension(std::string s)
{
    size_t slash = s.find_last_of("/\\");
    size_t dot = s.find_last_of('.');

    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    {
        s.erase(dot);
    }

    return s;
}

FWorldScene::FWorldScene(const FResourceManager& resources, FGameObjectRegistry& objectRegistry) : Resources(resources), Objects(objectRegistry), Spatial(256.0f) {}

FStatus FWorldScene::LoadBootstrapScene(FLogger* logger)
{
    PatchRecords.clear();
    TerrainRecords.clear();
    MicrotextureRecords.clear();
    PatchByName.clear();
    TerrainByName.clear();
    MicrotextureByName.clear();
    PatchByCoord.clear();
    BinaryBlobRecords.clear();
    MapGrid = {};
    Contours = {};
    GrassDatabase = {};
    SnowPath = {};
    WeatherProfile = {};
    SkyProfile = {};
    CatalogLandscapeResources(logger);
    LoadRuntimeBinaries(logger);
    LoadTextProfiles(logger);
    ReloadZoning(logger);
    RunAsmSelfTest(logger);

    if (logger)
    {
        auto s = Stats();
        logger->Info("WorldScene bootstrap: landscape_resources=" + std::to_string(s.PatchCount) + ", zones=" + std::to_string(s.ZoneCount) + ", contours=" + std::to_string(s.ContourCount) + ", map_present=" + std::to_string(s.MapPresentCells) + ", grass=" + std::to_string(s.GrassPatchCount) + ", snow_points=" + std::to_string(s.SnowPointCount) + ", weather_lines=" + std::to_string(s.WeatherLineCount) + ", sky_lines=" + std::to_string(s.SkyLineCount));
    }

    return FStatus::Ok();
}

FStatus FWorldScene::ReloadZoning(FLogger* logger) { return ZoningManager.LoadDefault(Resources, logger); }
void FWorldScene::SyncObject(uint32 objectId, FVector3 position, float radius, std::string tag)
{
    FBox2 b;
    b.Min =
    {
        position.X - radius, position.Z - radius
    };
    b.Max =
    {
        position.X + radius, position.Z + radius
    };
    Spatial.Insert(objectId, b, std::move(tag));
}
void FWorldScene::RemoveObject(uint32 objectId)
{
    Spatial.Remove(objectId);
}
std::vector<uint32> FWorldScene::QueryObjects(FBox2 area) const { return Spatial.Query(area); }
FWorldSceneStats FWorldScene::Stats() const
{
    FWorldSceneStats stats;
    stats.PatchCount = PatchRecords.size();
    stats.TerrainSizeRecordCount = TerrainRecords.size();
    stats.MicrotextureRecordCount = MicrotextureRecords.size();
    stats.ZoneCount = ZoningManager.Zones().size();
    stats.BinaryBlobCount = BinaryBlobRecords.size();
    stats.WeatherLineCount = WeatherProfile.Lines.size();
    stats.SkyLineCount = SkyProfile.Lines.size();
    stats.SpatialObjectCount = Spatial.Count();
    stats.ContourCount = Contours.Records.size();
    stats.MapCellCount = MapGrid.Cells.size();
    stats.MapPresentCells = MapGrid.PresentCells;
    stats.GrassPatchCount = GrassDatabase.Patches.size();
    stats.SnowPointCount = SnowPath.CandidatePointCount;
    return stats;
}

std::string FWorldScene::NormalizeResourceKey(std::string_view text)
{
    std::string s(text);
    std::replace(s.begin(), s.end(), '\\', '/');
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });

    while (!s.empty() && (s.front() == '/' || std::isspace(static_cast<unsigned char>(s.front()))))
    {
        s.erase(s.begin());
    }

    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.pop_back();
    }

    return s;
}

void FWorldScene::BuildPatchLookup()
{
    PatchByName.clear();
    PatchByCoord.clear();

    for (size_t i = 0; i < PatchRecords.size(); ++i)
    {
        const auto& patch = PatchRecords[i];
        std::string nameKey = NormalizeResourceKey(patch.Name);
        std::string pathKey = NormalizeResourceKey(patch.RelativePath.generic_string());
        std::string stemKey = NormalizeResourceKey(patch.StemName.empty() ? StripKnownExtension(patch.Name) : patch.StemName);
        PatchByName[nameKey] = i;
        PatchByName[pathKey] = i;

        if (!stemKey.empty())
        {
            PatchByName[stemKey] = i;
            PatchByName["landscape/" + stemKey] = i;
        }

        std::string pathNoExt = StripKnownExtension(pathKey);

        if (!pathNoExt.empty())
        {
            PatchByName[pathNoExt] = i;
        }

        if (patch.HasPatchCoords)
        {
            PatchByCoord[MakeCoordKey(patch.PatchX, patch.PatchZ)] = i;
        }
    }
}

void FWorldScene::BuildTerrainLookup()
{
    TerrainByName.clear();
    MicrotextureByName.clear();

    for (size_t i = 0; i < TerrainRecords.size(); ++i)
    {
        const auto& record = TerrainRecords[i];
        std::string nameKey = NormalizeResourceKey(record.Name);
        std::string pathKey = NormalizeResourceKey(record.RelativePath.generic_string());
        std::string stemKey = NormalizeResourceKey(record.StemName.empty() ? StripKnownExtension(record.Name) : record.StemName);
        TerrainByName[nameKey] = i;
        TerrainByName[pathKey] = i;

        if (!stemKey.empty())
        {
            TerrainByName[stemKey] = i;
            TerrainByName["landscape/" + stemKey] = i;
        }

        std::string pathNoExt = StripKnownExtension(pathKey);

        if (!pathNoExt.empty())
        {
            TerrainByName[pathNoExt] = i;
        }
    }

    for (size_t i = 0; i < MicrotextureRecords.size(); ++i)
    {
        const auto& record = MicrotextureRecords[i];
        std::string nameKey = NormalizeResourceKey(record.Name);
        std::string pathKey = NormalizeResourceKey(record.RelativePath.generic_string());
        std::string stemKey = NormalizeResourceKey(record.StemName.empty() ? StripKnownExtension(record.Name) : record.StemName);
        MicrotextureByName[nameKey] = i;
        MicrotextureByName[pathKey] = i;

        if (!stemKey.empty())
        {
            MicrotextureByName[stemKey] = i;
            MicrotextureByName["landscape/" + stemKey] = i;
        }

        std::string pathNoExt = StripKnownExtension(pathKey);

        if (!pathNoExt.empty())
        {
            MicrotextureByName[pathNoExt] = i;
        }
    }
}

const FWorldPatchRecord* FWorldScene::FindPatchByName(std::string_view name) const
{
    auto key = NormalizeResourceKey(name);
    auto it = PatchByName.find(key);

    if (it != PatchByName.end()) { return &PatchRecords[it->second]; }

    std::string stem = StripKnownExtension(key);

    if (stem != key)
    {
        it = PatchByName.find(stem);

        if (it != PatchByName.end())
        {
            return &PatchRecords[it->second];
        }
    }

    std::string withLandscape = "landscape/" + key;
    it = PatchByName.find(withLandscape);

    if (it != PatchByName.end()) { return &PatchRecords[it->second]; }

    std::string mtxName = key + ".mtx";
    it = PatchByName.find(mtxName);

    if (it != PatchByName.end()) { return &PatchRecords[it->second]; }

    it = PatchByName.find("landscape/" + mtxName);

    if (it != PatchByName.end()) { return &PatchRecords[it->second]; }

    return nullptr;
}

const FWorldTerrainSizeRecord* FWorldScene::FindTerrainSizeByName(std::string_view name) const
{
    auto key = NormalizeResourceKey(name);
    auto it = TerrainByName.find(key);

    if (it != TerrainByName.end()) { return &TerrainRecords[it->second]; }

    std::string stem = StripKnownExtension(key);

    if (stem != key)
    {
        it = TerrainByName.find(stem);

        if (it != TerrainByName.end())
        {
            return &TerrainRecords[it->second];
        }
    }

    std::string withLandscape = "landscape/" + key;
    it = TerrainByName.find(withLandscape);

    if (it != TerrainByName.end()) { return &TerrainRecords[it->second]; }

    std::string sizName = key + ".siz";
    it = TerrainByName.find(sizName);

    if (it != TerrainByName.end()) { return &TerrainRecords[it->second]; }

    it = TerrainByName.find("landscape/" + sizName);

    if (it != TerrainByName.end()) { return &TerrainRecords[it->second]; }

    return nullptr;
}

const FWorldMicrotextureRecord* FWorldScene::FindMicrotextureByName(std::string_view name) const
{
    auto key = NormalizeResourceKey(name);
    auto it = MicrotextureByName.find(key);

    if (it != MicrotextureByName.end()) { return &MicrotextureRecords[it->second]; }

    std::string stem = StripKnownExtension(key);

    if (stem != key)
    {
        it = MicrotextureByName.find(stem);

        if (it != MicrotextureByName.end())
        {
            return &MicrotextureRecords[it->second];
        }
    }

    std::string withLandscape = "landscape/" + key;
    it = MicrotextureByName.find(withLandscape);

    if (it != MicrotextureByName.end()) { return &MicrotextureRecords[it->second]; }

    std::string mtxName = key + ".mtx";
    it = MicrotextureByName.find(mtxName);

    if (it != MicrotextureByName.end()) { return &MicrotextureRecords[it->second]; }

    it = MicrotextureByName.find("landscape/" + mtxName);

    if (it != MicrotextureByName.end()) { return &MicrotextureRecords[it->second]; }

    return nullptr;
}

const FWorldPatchRecord* FWorldScene::FindPatchByCoord(int32 patchX, int32 patchZ) const
{
    auto it = PatchByCoord.find(MakeCoordKey(patchX, patchZ));

    if (it == PatchByCoord.end())
    {
        return nullptr;
    }

    return &PatchRecords[it->second];
}

void FWorldScene::CatalogLandscapeResources(FLogger* logger)
{
    for (const auto& item : Resources.Catalog().All())
    {
        std::string lower = LowerPathString(item.RelativePath);
        bool landscape = lower.find("landscape") != std::string::npos || lower.find("xadd/snowpath") != std::string::npos;

        if (!landscape) { continue; }

        FWorldPatchRecord record;
        record.Name = item.RelativePath.filename().string();
        record.StemName = item.RelativePath.stem().string();
        record.RelativePath = item.RelativePath;
        record.Size = item.Size;
        record.Kind = GuessResourceKind(item.RelativePath);
        record.IsMapTile = IsLandscapeMtxPath(lower);
        std::smatch match;
        std::string pathText = item.RelativePath.generic_string();
        std::regex coordRegex(R"((?:patch|grassmap|map|terrain)?[_\-/\\]?(-?[0-9]{1,3})[_xX-](-?[0-9]{1,3}))");

        if (std::regex_search(pathText, match, coordRegex) && match.size() >= 3)
        {
            int32 px = 0;
            int32 pz = 0;

            if (NumericParse::TryParseInt32Strict(match[1].str(), px) && NumericParse::TryParseInt32Strict(match[2].str(), pz))
            {
                record.PatchX = px;
                record.PatchZ = pz;
                record.HasPatchCoords = true;
            }
        }

        PatchRecords.push_back(std::move(record));

        if (IsLandscapeSizPath(lower))
        {
            FWorldTerrainSizeRecord terrain;
            terrain.Name = item.RelativePath.filename().string();
            terrain.StemName = item.RelativePath.stem().string();
            terrain.RelativePath = item.RelativePath;
            terrain.SizeFileBytes = item.Size;
            auto blob = Resources.Load(item.RelativePath.generic_string());

            if (blob.IsOk() && blob.Value().Bytes.size() >= 8)
            {
                terrain.RawWidth = ReadU32LE(blob.Value().Bytes, 0);
                terrain.RawHeight = ReadU32LE(blob.Value().Bytes, 4);
                terrain.HasRawDimensions = true;
            }

            TerrainRecords.push_back(std::move(terrain));
        }

        if (IsLandscapeMtxPath(lower))
        {
            FWorldMicrotextureRecord texture;
            texture.Name = item.RelativePath.filename().string();
            texture.StemName = item.RelativePath.stem().string();
            texture.RelativePath = item.RelativePath;
            texture.Size = item.Size;
            MicrotextureRecords.push_back(std::move(texture));
        }
    }

    BuildPatchLookup();
    BuildTerrainLookup();

    if (logger)
    {
        logger->Info("WorldScene cataloged landscape resources: " + std::to_string(PatchRecords.size()) + ", terrain_size_records=" + std::to_string(TerrainRecords.size()) + ", microtextures=" + std::to_string(MicrotextureRecords.size()) + ", coord_index=" + std::to_string(PatchByCoord.size()));
    }
}

void FWorldScene::LoadRuntimeBinaries(FLogger* logger)
{
    LoadContours(logger);
    LoadMapGrid(logger);
    LoadGrassMaps(logger);
    LoadSnowPath(logger);

    if (logger)
    {
        logger->Info("WorldScene runtime binaries: contours=" + std::to_string(Contours.Records.size()) + ", map_cells=" + std::to_string(MapGrid.PresentCells) + ", grass_maps=" + std::to_string(GrassDatabase.Patches.size()) + ", snow_points=" + std::to_string(SnowPath.CandidatePointCount));
    }
}

void FWorldScene::LoadContours(FLogger* logger)
{
    auto blob = Resources.Load("landscape/contours.bin");

    if (!blob.IsOk()) { return; }

    const auto& bytes = blob.Value().Bytes;
    constexpr uint32 recordSize = 0x418;

    if (bytes.size() < 4) { return; }

    uint32 count = ReadU32LE(bytes, 0);
    uint64 expected = 4ull + static_cast<uint64>(count) * recordSize;

    if (count > 200000 || expected > bytes.size())
    {
        count = static_cast<uint32>(bytes.size() / recordSize);
    }

    Contours.Loaded = true;
    Contours.SourceName = "landscape/contours.bin";
    Contours.RecordSize = recordSize;
    bool globalInit = false;

    for (uint32 i = 0; i < count; ++i)
    {
        size_t base = 4 + static_cast<size_t>(i) * recordSize;

        if (base + recordSize > bytes.size()) { break; }

        FWorldContourRecord rec;
        rec.Index = i;
        rec.SortKey = ReadI32LE(bytes, base + 0);
        rec.PointCount = std::clamp(ReadI32LE(bytes, base + 4), 0, 64);
        bool localInit = false;
        size_t pointBase = base + 0x18;

        for (int32 p = 0; p < rec.PointCount; ++p)
        {
            float x = ReadFloatLE(bytes, pointBase + static_cast<size_t>(p) * 8);
            float z = ReadFloatLE(bytes, pointBase + static_cast<size_t>(p) * 8 + 4);

            if (!ReasonableWorldFloat(x) || !ReasonableWorldFloat(z)) { continue; }

            FVector2 pt
            {
                x, z
            };
            rec.Points.push_back(pt);
            ExpandBounds(rec.Bounds, pt, localInit);
            ExpandBounds(Contours.Bounds, pt, globalInit);
        }

        for (int32 p = 0; p < rec.PointCount; ++p)
        {
            rec.ForwardLinks.push_back(ReadI32LE(bytes, base + 0x218 + static_cast<size_t>(p) * 4));
            rec.BackLinks.push_back(ReadI32LE(bytes, base + 0x318 + static_cast<size_t>(p) * 4));
        }

        if (!localInit)
        {
            rec.Bounds.Min =
            {
                static_cast<float>(rec.SortKey), 0.0f
            };
            rec.Bounds.Max = rec.Bounds.Min;
            ++Contours.InvalidRecords;
        }

        Contours.Records.push_back(std::move(rec));
    }

    if (logger)
    {
        logger->Info("WorldScene contours loaded: records=" + std::to_string(Contours.Records.size()) + ", invalid=" + std::to_string(Contours.InvalidRecords) + ", record_size=0x418");
    }
}

void FWorldScene::LoadMapGrid(FLogger* logger)
{
    auto blob = Resources.Load("landscape/map.bin");

    if (!blob.IsOk()) { return; }

    const auto& bytes = blob.Value().Bytes;
    constexpr uint32 width = 80;
    constexpr uint32 height = 80;
    constexpr uint32 stride = 0x16;

    if (bytes.size() < static_cast<size_t>(width) * height * stride) { BinaryBlobRecords.push_back(AnalyzeBinaryBlob("landscape/map.bin", bytes)); return; }

    MapGrid.Loaded = true;
    MapGrid.Width = width;
    MapGrid.Height = height;
    MapGrid.CellStride = stride;
    MapGrid.Cells.reserve(static_cast<size_t>(width) * height);
    std::set<std::string> uniqueNames;
    std::vector<std::string> unresolvedSamples;
    std::vector<std::string> fallbackSamples;

    for (uint32 z = 0; z < height; ++z)
    {
        for (uint32 x = 0; x < width; ++x)
        {
            size_t off = (static_cast<size_t>(z) * width + x) * stride;
            FWorldMapCell cell;
            cell.X = static_cast<int32>(x);
            cell.Z = static_cast<int32>(z);
            cell.TileName = ReadFixedCString(bytes, off, 20);
            cell.Reserved = ReadU16LE(bytes, off + 20);
            cell.Present = !cell.TileName.empty();

            if (cell.Present)
            {
                ++MapGrid.PresentCells;
                uniqueNames.insert(NormalizeResourceKey(cell.TileName));

                if (const FWorldTerrainSizeRecord* terrain = FindTerrainSizeByName(cell.TileName))
                {
                    cell.TileResolved = true;
                    cell.ResolvedByTerrainSize = true;
                    cell.TerrainSizeRecordIndex = static_cast<size_t>(terrain - TerrainRecords.data());
                    ++MapGrid.ResolvedCells;
                    ++MapGrid.ResolvedTerrainCells;
                }
                else if (const FWorldPatchRecord* fallback = FindPatchByName(cell.TileName))
                {
                    cell.TileResolved = true;
                    cell.ResolvedByFallbackResource = true;
                    cell.TileRecordIndex = static_cast<size_t>(fallback - PatchRecords.data());
                    ++MapGrid.ResolvedCells;
                    ++MapGrid.FallbackResolvedCells;

                    if (fallbackSamples.size() < 8)
                    {
                        fallbackSamples.push_back(cell.TileName);
                    }
                }
                else
                {
                    ++MapGrid.MissingTileRefs;

                    if (unresolvedSamples.size() < 8)
                    {
                        unresolvedSamples.push_back(cell.TileName);
                    }
                }
            }

            MapGrid.Cells.push_back(std::move(cell));
        }
    }

    MapGrid.UniqueTileNames = uniqueNames.size();

    if (logger)
    {
        logger->Info("WorldScene map grid loaded: cells=" + std::to_string(MapGrid.Cells.size()) + ", present=" + std::to_string(MapGrid.PresentCells) + ", resolved=" + std::to_string(MapGrid.ResolvedCells) + ", terrain_resolved=" + std::to_string(MapGrid.ResolvedTerrainCells) + ", fallback_resolved=" + std::to_string(MapGrid.FallbackResolvedCells) + ", missing_tile_refs=" + std::to_string(MapGrid.MissingTileRefs) + ", unique_tiles=" + std::to_string(MapGrid.UniqueTileNames) + ", cell_stride=0x16");

        if (!fallbackSamples.empty())
        {
            std::string joined;

            for (size_t i = 0; i < fallbackSamples.size(); ++i)
            {
                if (i)
                {
                    joined += ", ";
                }

                joined += fallbackSamples[i];
            }

            logger->Warning("WorldScene map tile fallback samples: " + joined);
        }

        if (!unresolvedSamples.empty())
        {
            std::string joined;

            for (size_t i = 0; i < unresolvedSamples.size(); ++i)
            {
                if (i)
                {
                    joined += ", ";
                }

                joined += unresolvedSamples[i];
            }

            logger->Warning("WorldScene unresolved map tile samples: " + joined);
        }
    }
}

void FWorldScene::LoadGrassMaps(FLogger* logger)
{
    std::regex grassRegex(R"(grassmap[_\-/\\]?([0-9]{2})[_-]([0-9]{2})\.bin$)", std::regex::icase);

    for (const auto& item : Resources.Catalog().All())
    {
        std::string lower = LowerPathString(item.RelativePath);

        if (lower.find("grassmap") == std::string::npos || lower.find(".bin") == std::string::npos) { continue; }

        std::smatch match;
        std::string pathText = item.RelativePath.generic_string();

        if (!std::regex_search(pathText, match, grassRegex) || match.size() < 3) { continue; }

        auto blob = Resources.Load(item.RelativePath.generic_string());

        if (!blob.IsOk()) { continue; }

        FWorldGrassPatch patch;
        patch.PatchX = NumericParse::Int32Or(match[1].str(), 0);
        patch.PatchZ = NumericParse::Int32Or(match[2].str(), 0);
        patch.RelativePath = item.RelativePath;
        patch.Size = blob.Value().Bytes.size();

        for (uint8 b : blob.Value().Bytes)
        {
            if (b != 0)
            {
                ++patch.NonZeroCells;
                patch.MaxValue = std::max<uint8>(patch.MaxValue, b);
            }
        }

        GrassDatabase.Patches.push_back(std::move(patch));
    }

    GrassDatabase.Loaded = !GrassDatabase.Patches.empty();

    if (logger)
    {
        logger->Info("WorldScene grass maps loaded: " + std::to_string(GrassDatabase.Patches.size()) + (GrassDatabase.Patches.empty() ? "" : ", tile_bytes=0x10000"));
    }
}

void FWorldScene::LoadSnowPath(FLogger* logger)
{
    auto blob = Resources.Load("xadd/snowpath.bin");

    if (!blob.IsOk()) { return; }

    const auto& bytes = blob.Value().Bytes;
    SnowPath.Loaded = true;
    SnowPath.SourceName = "xadd/snowpath.bin";
    SnowPath.Size = bytes.size();
    bool init = false;

    if (bytes.size() >= 8 && bytes.size() % 8 == 0)
    {
        SnowPath.CandidatePointCount = static_cast<uint32>(bytes.size() / 8);

        for (size_t i = 0; i + 8 <= bytes.size(); i += 8)
        {
            float x = ReadFloatLE(bytes, i);
            float z = ReadFloatLE(bytes, i + 4);

            if (ReasonableWorldFloat(x) && ReasonableWorldFloat(z))
            {
                ExpandBounds(SnowPath.Bounds, {x, z}, init);
            }
        }
    }
    else
    {
        SnowPath.CandidatePointCount = static_cast<uint32>(bytes.size() / 4);
    }

    if (logger)
    {
        logger->Info("WorldScene snow path loaded: bytes=" + std::to_string(SnowPath.Size) + ", candidate_points=" + std::to_string(SnowPath.CandidatePointCount));
    }
}

void FWorldScene::LoadTextProfiles(FLogger* logger)
{
    auto loadWeatherProfile = [&](std::string_view logicalName)
    {
        auto blob = Resources.Load(logicalName);

        if (!blob.IsOk())
        {
            return false;
        }

        WeatherProfile.SourceName = std::string(logicalName);
        std::string text(blob.Value().Bytes.begin(), blob.Value().Bytes.end());
        ParseKeyValueText(text, WeatherProfile.Lines, WeatherProfile.Values);
        return true;
    };

    auto loadSkyProfile = [&](std::string_view logicalName)
    {
        auto blob = Resources.Load(logicalName);

        if (!blob.IsOk())
        {
            return false;
        }

        SkyProfile.SourceName = std::string(logicalName);
        std::string text(blob.Value().Bytes.begin(), blob.Value().Bytes.end());
        ParseKeyValueText(text, SkyProfile.Lines, SkyProfile.Values);
        return true;
    };

    if (!loadWeatherProfile("landscape/weather.txt"))
    {
        loadWeatherProfile("landscape_hr/weather_hr.txt");
    }

    if (!loadSkyProfile("sky.txt"))
    {
        loadSkyProfile("landscape_hr/sky_hr.txt");
    }

    for (const auto& pair : SkyProfile.Values)
    {
        const std::string& key = pair.first;

        if (key.find("nsky") == std::string::npos && key.find("tsky") == std::string::npos) { continue; }
    }

    if (logger)
    {
        logger->Info("WorldScene text profiles: weather=" + std::to_string(WeatherProfile.Lines.size()) + ", sky=" + std::to_string(SkyProfile.Lines.size()));
    }
}

void FWorldScene::RunAsmSelfTest(FLogger* logger) const
{
    if (!logger) { return; }

    bool mapShapeOk = MapGrid.Loaded && MapGrid.Width == 80 && MapGrid.Height == 80 && MapGrid.CellStride == 0x16 && MapGrid.Cells.size() == 6400;
    bool mapRefsOk = !MapGrid.Loaded || MapGrid.MissingTileRefs == 0;
    bool contoursOk = !Contours.Loaded || Contours.RecordSize == 0x418;
    bool grassOk = GrassDatabase.Patches.empty() || std::all_of(GrassDatabase.Patches.begin(), GrassDatabase.Patches.end(), [](const FWorldGrassPatch& patch)
    {
        return patch.Size == 0x10000;
    });
    std::string sample;

    if (const FWorldMapCell* cell = MapGrid.Find(0, 0))
    {
        sample = cell->TileName;
    }

    logger->Info(std::string("WorldScene asm self-test: map_shape=") + (mapShapeOk ? "ok" : "bad") + ", map_refs=" + (mapRefsOk ? "ok" : "bad") + ", contours_stride=" + (contoursOk ? "ok" : "bad") + ", grass_tile_size=" + (grassOk ? "ok" : "bad") + ", terrain_catalog=" + std::to_string(TerrainRecords.size()) + ", microtexture_catalog=" + std::to_string(MicrotextureRecords.size()) + (sample.empty() ? "" : ", map00=" + sample));
}

FWorldBinaryBlobInfo FWorldScene::AnalyzeBinaryBlob(std::string logicalName, const FByteArray& bytes)
{
    FWorldBinaryBlobInfo info;
    info.LogicalName = std::move(logicalName);
    info.Size = bytes.size();

    if (bytes.size() >= 16 && bytes.size() % 16 == 0)
    {
        info.CandidateRecordCount = static_cast<uint32>(bytes.size() / 16);
        info.Interpretation = "candidate vec4/rect table";
    }
    else if (bytes.size() >= 8 && bytes.size() % 8 == 0)
    {
        info.CandidateRecordCount = static_cast<uint32>(bytes.size() / 8);
        info.Interpretation = "candidate point/edge table";
    }
    else
    {
        info.CandidateRecordCount = 0;
        info.Interpretation = "opaque world binary";
    }

    if (bytes.size() >= 8 && bytes.size() % 8 == 0)
    {
        bool initialized = false;

        for (size_t i = 0; i + 8 <= bytes.size() && i < 8 * 1024; i += 8)
        {
            float x = ReadFloatLE(bytes, i);
            float y = ReadFloatLE(bytes, i + 4);

            if (!ReasonableWorldFloat(x) || !ReasonableWorldFloat(y))
            {
                continue;
            }

            ExpandBounds(info.Bounds, {x, y}, initialized);
        }
    }

    return info;
}

void FWorldScene::ParseKeyValueText(std::string_view text, std::vector<std::string>& lines, std::unordered_map<std::string, std::string>& values)
{
    std::istringstream input
    {
        std::string(text)
    };
    std::string line;

    while (std::getline(input, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        {
            line.pop_back();
        }

        if (line.empty()) { continue; }

        lines.push_back(line);
        size_t split = line.find('=');

        if (split == std::string::npos)
        {
            split = line.find(':');
        }

        if (split != std::string::npos)
        {
            std::string key = NormalizeResourceKey(line.substr(0, split));
            std::string value = line.substr(split + 1);
            values[key] = value;
        }
    }
}
