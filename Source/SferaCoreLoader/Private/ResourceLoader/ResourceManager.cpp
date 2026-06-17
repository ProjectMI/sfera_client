#include "ResourceLoader/ResourceManager.h"

namespace Sfera {
FResourceManager::FResourceManager(const FFileSystem& fileSystem, const FCompressionService& compression) : FileSystem(fileSystem), Compression(compression) {}

void FResourceManager::BuildCatalog(FLogger* logger) {
    AssetCatalog.BuildFromFileSystem(FileSystem);
    if (logger) { logger->Info("ResourceLoader cataloged " + std::to_string(AssetCatalog.Count()) + " resources"); }
}

TResult<FResourceBlob> FResourceManager::Load(std::string_view logicalName) const {
    auto path = FileSystem.Resolve(logicalName);
    if (!path) { return FStatus::Error(EStatusCode::NotFound, "resource not found: " + std::string(logicalName)); }
    auto raw = FileSystem.ReadBytes(logicalName);
    if (!raw.IsOk()) { return raw.Status(); }
    auto probe = Compression.Probe(raw.Value());
    auto decoded = Compression.DecompressAuto(raw.Value());
    if (!decoded.IsOk()) { return decoded.Status(); }
    FResourceBlob blob;
    blob.Id.LogicalName = std::string(logicalName);
    blob.Id.Kind = GuessResourceKind(*path);
    blob.SourcePath = *path;
    blob.Bytes = std::move(decoded.Value());
    blob.WasCompressed = probe.Method != ECompressionMethod::Raw;
    return blob;
}
}
