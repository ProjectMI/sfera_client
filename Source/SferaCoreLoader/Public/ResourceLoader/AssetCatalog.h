#pragma once
#include "FileSystem/FileSystem.h"
#include "ResourceLoader/ResourceTypes.h"
#include <unordered_map>

class FAssetCatalog 
{
public:
    void BuildFromFileSystem(const FFileSystem& fileSystem);
    std::vector<FFileRecord> FindByKind(EResourceKind kind) const;
    std::optional<FFileRecord> FindByLogicalName(std::string_view logicalName) const;
    size_t Count() const { return Records.size(); }
    const std::vector<FFileRecord>& All() const { return Records; }
private:
    std::vector<FFileRecord> Records;
    std::unordered_map<std::string, size_t> Lookup;
};