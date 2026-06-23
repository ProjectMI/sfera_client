#pragma once
#include "GameObjects/GameObjectRegistry.h"
#include "ResourceLoader/ResourceManager.h"
#include "WorldScene/SpatialIndex.h"
#include "WorldScene/ZoningManager.h"

class FWorldScene 
{
public:
    FWorldScene(const FResourceManager& resources, FGameObjectRegistry& objectRegistry);
    FStatus LoadBootstrapScene(FLogger* logger = nullptr);
    FStatus ReloadZoning(FLogger* logger = nullptr);
    FStatus EnsureZoningLoaded(FLogger* logger = nullptr);
    FStatus EnsureContoursLoaded(FLogger* logger = nullptr);
    void SyncObject(uint32 objectId, FVector3 position, float radius, std::string tag = {});
    void RemoveObject(uint32 objectId);
    std::vector<uint32> QueryObjects(FBox2 area) const;
    const FWorldZoneParams* FindZone(FVector3 position);
    const FWorldPatchRecord* FindPatchByName(std::string_view name) const;
    const FWorldTerrainSizeRecord* FindTerrainSizeByName(std::string_view name) const;
    const FWorldMicrotextureRecord* FindMicrotextureByName(std::string_view name) const;
    const FWorldPatchRecord* FindPatchByCoord(int32 patchX, int32 patchZ) const;
    const FWorldMapCell* FindMapCell(int32 x, int32 z) const { return MapGrid.Find(x, z); }
    std::vector<uint32> QueryContours(FBox2 area);
    const FZoningManager& Zoning() const { return ZoningManager; }
    const std::vector<FWorldPatchRecord>& Patches() const { return PatchRecords; }
    const std::vector<FWorldTerrainSizeRecord>& TerrainSizeRecords() const { return TerrainRecords; }
    const std::vector<FWorldMicrotextureRecord>& Microtextures() const { return MicrotextureRecords; }
    const FWorldMapGrid& Map() const { return MapGrid; }
    const FWorldContourDatabase& ContourDatabase() const { return Contours; }
    const FWorldGrassDatabase& Grass() const { return GrassDatabase; }
    const FWorldSnowPath& Snow() const { return SnowPath; }
    const std::vector<FWorldBinaryBlobInfo>& BinaryBlobs() const { return BinaryBlobRecords; }
    const FWeatherProfile& Weather() const { return WeatherProfile; }
    const FSkyProfile& Sky() const { return SkyProfile; }
    FWorldSceneStats Stats() const;
private:
    void CatalogLandscapeResources(FLogger* logger);
    void LoadRuntimeBinaries(FLogger* logger);
    void LoadContours(FLogger* logger);
    void LoadMapGrid(FLogger* logger);
    void LoadGrassMaps(FLogger* logger);
    void LoadSnowPath(FLogger* logger);
    void LoadTextProfiles(FLogger* logger);
    void BuildPatchLookup();
    void BuildTerrainLookup();
    void RunAsmSelfTest(FLogger* logger) const;
    static FWorldBinaryBlobInfo AnalyzeBinaryBlob(std::string logicalName, const FByteArray& bytes);
    static void ParseKeyValueText(std::string_view text, std::vector<std::string>& lines, std::unordered_map<std::string, std::string>& values);
    static std::string NormalizeResourceKey(std::string_view text);
    const FResourceManager& Resources;
    FGameObjectRegistry& Objects;
    FZoningManager ZoningManager;
    FSpatialIndex Spatial;
    std::vector<FWorldPatchRecord> PatchRecords;
    std::vector<FWorldTerrainSizeRecord> TerrainRecords;
    std::vector<FWorldMicrotextureRecord> MicrotextureRecords;
    std::unordered_map<std::string, size_t> PatchByName;
    std::unordered_map<std::string, size_t> TerrainByName;
    std::unordered_map<std::string, size_t> MicrotextureByName;
    std::unordered_map<int64, size_t> PatchByCoord;
    FWorldMapGrid MapGrid;
    FWorldContourDatabase Contours;
    bool ZoningLoaded = false;
    bool ContoursLoaded = false;
    FWorldGrassDatabase GrassDatabase;
    FWorldSnowPath SnowPath;
    std::vector<FWorldBinaryBlobInfo> BinaryBlobRecords;
    FWeatherProfile WeatherProfile;
    FSkyProfile SkyProfile;
};