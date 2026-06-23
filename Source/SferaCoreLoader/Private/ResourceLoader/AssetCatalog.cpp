#include "ResourceLoader/AssetCatalog.h"
#include "FileSystem/PathUtils.h"

void FAssetCatalog::BuildFromFileSystem(const FFileSystem& fileSystem)
{
    Records = fileSystem.GetFiles();
    Lookup.clear();
    Lookup.reserve(Records.size());
    for (auto& bucket : KindLookup)
    {
        bucket.clear();
    }

    for (size_t i = 0; i < Records.size(); ++i)
    {
        Lookup[PathUtils::NormalizeForLookup(Records[i].RelativePath)] = i;
        const size_t kindSlot = static_cast<size_t>(GuessResourceKind(Records[i].RelativePath));
        if (kindSlot < KindLookup.size())
        {
            KindLookup[kindSlot].push_back(i);
        }
    }
}

std::vector<FFileRecord> FAssetCatalog::FindByKind(EResourceKind kind) const
{
    std::vector<FFileRecord> result;
    const size_t kindSlot = static_cast<size_t>(kind);
    if (kindSlot >= KindLookup.size())
    {
        return result;
    }

    result.reserve(KindLookup[kindSlot].size());
    for (size_t index : KindLookup[kindSlot])
    {
        result.push_back(Records[index]);
    }
    return result;
}

std::optional<FFileRecord> FAssetCatalog::FindByLogicalName(std::string_view logicalName) const
{
    auto it = Lookup.find(PathUtils::NormalizeForLookup(FPath{std::string(logicalName)}));

    if (it == Lookup.end()) { return std::nullopt; }

    return Records[it->second];
}
