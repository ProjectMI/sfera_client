#include "ResourceLoader/ResourceManager.h"
#include "FileSystem/NativeFile.h"

FResourceManager::FResourceManager(const FFileSystem& fileSystem, const FCompressionService& compression) : FileSystem(fileSystem), Compression(compression) {}

void FResourceManager::BuildCatalog(FLogger* logger)
{
    AssetCatalog.BuildFromFileSystem(FileSystem);

    if (logger)
    {
        logger->Info("ResourceLoader cataloged " + std::to_string(AssetCatalog.Count()) + " resources");
    }
}

TResult<FResourceBlob> FResourceManager::Load(std::string_view logicalName) const
{
    auto path = FileSystem.Resolve(logicalName);

    if (!path) { return FStatus::Error(EStatusCode::NotFound, "resource not found: " + std::string(logicalName)); }

    auto raw = FNativeFile::ReadAllBytes(*path);

    if (!raw.IsOk()) { return raw.Status(); }

    auto probe = Compression.Probe(raw.Value());
    FByteArray bytes;
    if (probe.Method == ECompressionMethod::Raw)
    {
        bytes = std::move(raw.Value());
    }
    else
    {
        auto decoded = Compression.Decompress(probe.Method, raw.Value().data() + probe.HeaderSize, raw.Value().size() - probe.HeaderSize, probe.ExpectedSize);
        if (!decoded.IsOk()) { return decoded.Status(); }
        bytes = std::move(decoded.Value());
    }

    FResourceBlob blob;
    blob.Id.LogicalName = std::string(logicalName);
    blob.Id.Kind = GuessResourceKind(*path);
    blob.SourcePath = *path;
    blob.Bytes = std::move(bytes);
    blob.WasCompressed = probe.Method != ECompressionMethod::Raw;
    return blob;
}
