#pragma once
#include "Core/Types.h"
#include "ResourceLoader/ResourceTypes.h"

struct FVector2 
{ 
	float X = 0.0f; 
	float Y = 0.0f; 
};

struct FVector3 
{ 
	float X = 0.0f; 
	float Y = 0.0f; 
	float Z = 0.0f; 
};

struct FColor3 
{ 
	float R = 0.0f; 
	float G = 0.0f; 
	float B = 0.0f; 
};

struct FBox2 
{ 
	FVector2 Min; 
	FVector2 Max; 
	bool IsValid() const { return Min.X <= Max.X && Min.Y <= Max.Y; } 
	bool Contains(const FVector2& p) const { return p.X >= Min.X && p.X <= Max.X && p.Y >= Min.Y && p.Y <= Max.Y; } 
	bool Intersects(const FBox2& other) const 
	{ 
		return IsValid() && other.IsValid() 
			&& Min.X <= other.Max.X && Max.X >= other.Min.X 
			&& Min.Y <= other.Max.Y && Max.Y >= other.Min.Y; 
	} 
};

struct FBox3 
{ 
	FVector3 Min; 
	FVector3 Max; 
	bool IsValid() const { return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z; } 
	bool Contains(const FVector3& p) const { return p.X >= Min.X && p.X <= Max.X && p.Y >= Min.Y && p.Y <= Max.Y && p.Z >= Min.Z && p.Z <= Max.Z; } 
};

struct FWorldZoneLighting 
{ 
	std::array<FColor3, 8> FogColor{}; 
	std::array<FColor3, 8> AmbientColor{}; 
	std::array<FColor3, 8> SunColor{}; 
	float FogNear = 0.0f; 
	float FogFar = 0.0f; 
	float SkyFogAlpha = 0.0f; 
};

struct FWorldZoneParams 
{ 
	uint32 Index = 0; 
	FBox3 Bounds; 
	int32 PatchMinX = 0; 
	int32 PatchMinZ = 0; 
	float BorderFadeDist = 0.0f; 
	FWorldZoneLighting Lighting; 
	std::string SourceScope; 
	std::string Name; 
	std::unordered_map<std::string, std::string> RawFields; 
};

struct FWorldPatchRecord 
{ 
	std::string Name; 
	std::string StemName; 
	FPath RelativePath; 
	uint64 Size = 0; 
	EResourceKind Kind = EResourceKind::Unknown; 
	int32 PatchX = 0; 
	int32 PatchZ = 0; 
	bool HasPatchCoords = false; 
	bool IsMapTile = false; 
};

struct FWorldTerrainSizeRecord 
{ 
	std::string Name; 
	std::string StemName; 
	FPath RelativePath; 
	uint64 SizeFileBytes = 0; 
	uint32 RawWidth = 0; 
	uint32 RawHeight = 0; 
	bool HasRawDimensions = false; 
};

struct FWorldMicrotextureRecord 
{ 
	std::string Name; 
	std::string StemName; 
	FPath RelativePath; 
	uint64 Size = 0; 
};

struct FWorldMapCell 
{ 
	int32 X = 0; 
	int32 Z = 0; 
	std::string TileName; 
	uint16 Reserved = 0; 
	bool Present = false; 
	bool TileResolved = false; 
	bool ResolvedByTerrainSize = false; 
	bool ResolvedByPatchCatalog = false; 
	size_t TileRecordIndex = static_cast<size_t>(-1); 
	size_t TerrainSizeRecordIndex = static_cast<size_t>(-1); 
	std::string TerrainStem() const 
	{ 
		if (TileName.empty()) { return {}; } 
		if (TileName == "FILL_EMPT" || TileName == "fill_empt") { return "fill_empt_00"; } 
		const int first = static_cast<int>(Reserved & 0xff); 
		const int second = static_cast<int>((Reserved >> 8) & 0xff); 
		return TileName + "_" + std::to_string(first) + std::to_string(second); 
	} 
};

struct FWorldMapGrid 
{ 
	bool Loaded = false; 
	uint32 Width = 0; 
	uint32 Height = 0; 
	uint32 CellStride = 0; 
	std::vector<FWorldMapCell> Cells; 
	size_t PresentCells = 0; 
	size_t ResolvedCells = 0; 
	size_t ResolvedTerrainCells = 0; 
	size_t PatchCatalogResolvedCells = 0; 
	size_t MissingTileRefs = 0; 
	size_t UniqueTileNames = 0; 
	const FWorldMapCell* Find(int32 x, int32 z) const 
	{ 
		if (!Loaded || x < 0 || z < 0 || static_cast<uint32>(x) >= Width || static_cast<uint32>(z) >= Height) { return nullptr; } 
		return &Cells[static_cast<size_t>(z) * Width + static_cast<size_t>(x)]; 
	} 
};

struct FWorldContourRecord 
{ 
	uint32 Index = 0; 
	int32 SortKey = 0; 
	int32 PointCount = 0; 
	FBox2 Bounds; 
	std::vector<FVector2> Points; 
	std::vector<int32> ForwardLinks; 
	std::vector<int32> BackLinks; 
};

struct FWorldContourDatabase 
{ 
	bool Loaded = false; 
	std::string SourceName; 
	uint32 RecordSize = 0; 
	std::vector<FWorldContourRecord> Records; 
	FBox2 Bounds; 
	size_t InvalidRecords = 0; 
	float IndexCellSize = 64.0f; 
	mutable bool IndexBuilt = false; 
	mutable std::unordered_map<int64, std::vector<uint32>> IndexCells; 

	static int32 FloorCell(float value, float cellSize) 
	{ 
		return static_cast<int32>(std::floor(value / cellSize)); 
	} 

	static int64 CellKey(int32 x, int32 y) 
	{ 
		return (static_cast<int64>(x) << 32) ^ static_cast<uint32>(y); 
	} 

	void BuildIndex() const 
	{ 
		IndexCells.clear(); 
		if (!Loaded || Records.empty() || IndexCellSize <= 1.0f) 
		{ 
			IndexBuilt = true; 
			return; 
		} 
		for (const auto& r : Records) 
		{ 
			if (!r.Bounds.IsValid()) 
			{ 
				continue; 
			} 
			const int32 minX = FloorCell(r.Bounds.Min.X, IndexCellSize); 
			const int32 maxX = FloorCell(r.Bounds.Max.X, IndexCellSize); 
			const int32 minY = FloorCell(r.Bounds.Min.Y, IndexCellSize); 
			const int32 maxY = FloorCell(r.Bounds.Max.Y, IndexCellSize); 
			for (int32 y = minY; y <= maxY; ++y) 
			{ 
				for (int32 x = minX; x <= maxX; ++x) 
				{ 
					IndexCells[CellKey(x, y)].push_back(r.Index); 
				} 
			} 
		} 
		IndexBuilt = true; 
	} 

	std::vector<uint32> Query(FBox2 area) const 
	{ 
		std::vector<uint32> out; 
		if (!Loaded || Records.empty() || !area.IsValid()) 
		{ 
			return out; 
		} 
		if (!IndexBuilt) 
		{ 
			BuildIndex(); 
		} 
		std::unordered_set<uint32> seen; 
		const int32 minX = FloorCell(area.Min.X, IndexCellSize); 
		const int32 maxX = FloorCell(area.Max.X, IndexCellSize); 
		const int32 minY = FloorCell(area.Min.Y, IndexCellSize); 
		const int32 maxY = FloorCell(area.Max.Y, IndexCellSize); 
		for (int32 y = minY; y <= maxY; ++y) 
		{ 
			for (int32 x = minX; x <= maxX; ++x) 
			{ 
				auto bucket = IndexCells.find(CellKey(x, y)); 
				if (bucket == IndexCells.end()) 
				{ 
					continue; 
				} 
				for (uint32 id : bucket->second) 
				{ 
					if (id >= Records.size() || !seen.insert(id).second) 
					{ 
						continue; 
					} 
					if (Records[id].Bounds.Intersects(area)) 
					{ 
						out.push_back(id); 
					} 
				} 
			} 
		} 
		return out; 
	} 
};

struct FWorldGrassPatch 
{ 
	int32 PatchX = 0; 
	int32 PatchZ = 0; 
	FPath RelativePath; 
	uint64 Size = 0; 
	uint32 Width = 256; 
	uint32 Height = 256; 
	size_t NonZeroCells = 0; 
	uint8 MaxValue = 0; 
};

struct FWorldGrassDatabase 
{ 
	bool Loaded = false; 
	std::vector<FWorldGrassPatch> Patches; 
	const FWorldGrassPatch* Find(int32 patchX, int32 patchZ) const 
	{ 
		for (const auto& p : Patches) 
		{ 
			if (p.PatchX == patchX && p.PatchZ == patchZ) 
			{ 
				return &p; 
			} 
		} 
		return nullptr; 
	} 
};

struct FWorldSnowPath 
{ 
	bool Loaded = false; 
	std::string SourceName; 
	uint64 Size = 0; 
	uint32 CandidatePointCount = 0; 
	FBox2 Bounds; 
};

struct FWorldBinaryBlobInfo 
{
	std::string LogicalName;
	uint64 Size = 0;
	uint32 CandidateRecordCount = 0;
	std::string Interpretation;
	FBox2 Bounds; 
};

struct FWeatherProfile 
{ 
	std::string SourceName;
	std::vector<std::string> Lines;
	std::unordered_map<std::string, std::string> Values;
};

struct FSkyState 
{ 
	std::string Texture1; 
	std::string Texture2;
	float Time = 0.0f; 
	float Blend = 0.0f; 
};

struct FSkyProfile 
{
	std::string SourceName;
	std::vector<std::string> Lines;
	std::unordered_map<std::string, std::string> Values; 
	std::vector<FSkyState> States; 
};

struct FWorldSceneStats 
{ 
	size_t PatchCount = 0;
	size_t TerrainSizeRecordCount = 0; 
	size_t MicrotextureRecordCount = 0; 
	size_t ZoneCount = 0; 
	size_t BinaryBlobCount = 0; 
	size_t WeatherLineCount = 0; 
	size_t SkyLineCount = 0; 
	size_t SpatialObjectCount = 0;
	size_t ContourCount = 0; 
	size_t MapCellCount = 0; 
	size_t MapPresentCells = 0;
	size_t GrassPatchCount = 0; 
	size_t SnowPointCount = 0; 
};
