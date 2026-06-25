#pragma once
#include "Core/Logger.h"
#include "Model/ChrModel.h"
#include "Model/MdlModel.h"
#include "Model/SklSkeleton.h"
#include "ResourceLoader/ResourceManager.h"

enum class EModelAssetKind
{ 
    Unknown,
    Mdl, 
    Chr, 
    Skl 
};

struct FModelAssetRecord
{ 
    std::string LogicalName; 
    FPath RelativePath; 
    EModelAssetKind Kind = EModelAssetKind::Unknown; 
    uint64 Size = 0; 
};

struct FModelRepositoryStats
{ 
    size_t MdlCount = 0; 
    size_t ChrCount = 0; 
    size_t SklCount = 0; 
    size_t TotalCount = 0; 
};

struct FModelPreloadStats
{
    size_t Requested = 0;
    size_t Loaded = 0;
    size_t Failed = 0;
    size_t ThreadCount = 0;
};

class FModelRepository 
{
public:
    explicit FModelRepository(const FResourceManager& resources);
    void BuildCatalog(FLogger* logger = nullptr);
    const std::vector<FModelAssetRecord>& Assets() const { return Records; }
    FModelRepositoryStats Stats() const;
    const FModelAssetRecord* Find(std::string_view logicalName) const;
    TResult<FMdlMesh> LoadMdl(std::string_view logicalName) const;
    TResult<FChrMesh> LoadChr(std::string_view logicalName) const;
    TResult<FSklSkeleton> LoadSkl(std::string_view logicalName) const;
    FModelPreloadStats PreloadMdlMeshes(size_t MaxWorkers, FLogger* logger = nullptr) const;
private:
    static EModelAssetKind GuessKind(const FPath& path);
    static std::string NormalizeKey(std::string_view key);
    const FResourceManager& Resources;
    std::vector<FModelAssetRecord> Records;
    std::unordered_map<std::string, size_t> Lookup;
};

std::string_view ToString(EModelAssetKind kind);
