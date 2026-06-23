#pragma once
#include "FileSystem/FileSystem.h"
#include "ResourceLoader/ResourceTypes.h"
#include <array>
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
    static constexpr size_t ResourceKindSlots = 11;
    std::vector<FFileRecord> Records;
    std::unordered_map<std::string, size_t> Lookup;
    std::array<std::vector<size_t>, ResourceKindSlots> KindLookup;
};