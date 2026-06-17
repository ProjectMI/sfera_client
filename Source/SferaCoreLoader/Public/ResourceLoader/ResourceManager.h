#pragma once
#include "Compression/CompressionService.h"
#include "FileSystem/FileSystem.h"
#include "ResourceLoader/AssetCatalog.h"

namespace Sfera {
class FResourceManager {
public:
    FResourceManager(const FFileSystem& fileSystem, const FCompressionService& compression);
    void BuildCatalog(FLogger* logger = nullptr);
    TResult<FResourceBlob> Load(std::string_view logicalName) const;
    const FAssetCatalog& Catalog() const { return AssetCatalog; }
private:
    const FFileSystem& FileSystem;
    const FCompressionService& Compression;
    FAssetCatalog AssetCatalog;
};
}
