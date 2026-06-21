#include "ResourceLoader/AssetCatalog.h"
#include "FileSystem/PathUtils.h"

void FAssetCatalog::BuildFromFileSystem(const FFileSystem& fileSystem)
{
    Records = fileSystem.GetFiles();
    Lookup.clear();

    for (size_t i = 0; i < Records.size(); ++i)
    {
        Lookup[PathUtils::NormalizeForLookup(Records[i].RelativePath)] = i;
    }
}

std::vector<FFileRecord> FAssetCatalog::FindByKind(EResourceKind kind) const
{
    std::vector<FFileRecord> result;

    for (const auto& record : Records)
    {
        if (GuessResourceKind(record.RelativePath) == kind)
        {
            result.push_back(record);
        }
    }

    return result;
}

std::optional<FFileRecord> FAssetCatalog::FindByLogicalName(std::string_view logicalName) const
{
    auto it = Lookup.find(PathUtils::NormalizeForLookup(FPath{std::string(logicalName)}));

    if (it == Lookup.end()) { return std::nullopt; }

    return Records[it->second];
}
