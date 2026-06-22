#include "Model/ModelRepository.h"
#include "Common/StringUtils.h"
std::string_view ToString(EModelAssetKind kind)
{
    switch (kind)
    {
    case EModelAssetKind::Mdl: return "mdl";
    case EModelAssetKind::Chr: return "chr";
    case EModelAssetKind::Skl: return "skl";
    default: return "unknown";
    }
}
FModelRepository::FModelRepository(const FResourceManager& resources) : Resources(resources) {}
std::string FModelRepository::NormalizeKey(std::string_view key)
{
    return Common::NormalizePathKey(std::string(key));
}
EModelAssetKind FModelRepository::GuessKind(const FPath& path)
{
    std::string lower = NormalizeKey(path.generic_string());

    if (Common::EndsWith(lower, ".mdl"))
    {
        return EModelAssetKind::Mdl;
    }

    if (Common::EndsWith(lower, ".chr"))
    {
        return EModelAssetKind::Chr;
    }

    if (Common::EndsWith(lower, ".skl"))
    {
        return EModelAssetKind::Skl;
    }

    return EModelAssetKind::Unknown;
}
void FModelRepository::BuildCatalog(FLogger* logger)
{
    Records.clear();
    Lookup.clear();

    for (const auto& asset : Resources.Catalog().All())
    {
        EModelAssetKind kind = GuessKind(asset.RelativePath);

        if (kind == EModelAssetKind::Unknown)
        {
            continue;
        }

        FModelAssetRecord record;
        record.LogicalName = asset.RelativePath.generic_string();
        record.RelativePath = asset.RelativePath;
        record.Kind = kind;
        record.Size = asset.Size;
        size_t index = Records.size();
        Lookup[NormalizeKey(record.LogicalName)] = index;
        Lookup[NormalizeKey(record.RelativePath.filename().generic_string())] = index;
        Lookup[NormalizeKey(record.RelativePath.stem().generic_string())] = index;
        Records.push_back(std::move(record));
    }

    if (logger)
    {
        auto stats = Stats();
        logger->Info("ModelRepository catalog: mdl=" + std::to_string(stats.MdlCount) + ", chr=" + std::to_string(stats.ChrCount) + ", skl=" + std::to_string(stats.SklCount));
    }
}
FModelRepositoryStats FModelRepository::Stats() const
{
    FModelRepositoryStats stats;

    for (const auto& record : Records)
    {
        ++stats.TotalCount;

        if (record.Kind == EModelAssetKind::Mdl)
        {
            ++stats.MdlCount;
        }
        else if (record.Kind == EModelAssetKind::Chr)
        {
            ++stats.ChrCount;
        }
        else if (record.Kind == EModelAssetKind::Skl)
        {
            ++stats.SklCount;
        }
    }

    return stats;
}
const FModelAssetRecord* FModelRepository::Find(std::string_view logicalName) const
{
    auto key = NormalizeKey(logicalName);
    auto it = Lookup.find(key);

    if (it != Lookup.end())
    {
        return &Records[it->second];
    }

    auto noExt = Common::StripExtension(key);

    if (noExt != key)
    {
        it = Lookup.find(noExt);

        if (it != Lookup.end())
        {
            return &Records[it->second];
        }
    }

    return nullptr;
}
TResult<FMdlMesh> FModelRepository::LoadMdl(std::string_view logicalName) const
{
    const auto* record = Find(logicalName);

    if (!record)
    {
        return FStatus::Error(EStatusCode::NotFound, "model not found: " + std::string(logicalName));
    }

    if (record->Kind != EModelAssetKind::Mdl)
    {
        return FStatus::Error(EStatusCode::InvalidData, "model asset is not MDL: " + record->LogicalName);
    }

    return LoadMdlMeshFromResource(Resources, record->LogicalName);
}
TResult<FChrMesh> FModelRepository::LoadChr(std::string_view logicalName) const
{
    const auto* record = Find(logicalName);

    if (!record)
    {
        return FStatus::Error(EStatusCode::NotFound, "model not found: " + std::string(logicalName));
    }

    if (record->Kind != EModelAssetKind::Chr)
    {
        return FStatus::Error(EStatusCode::InvalidData, "model asset is not CHR: " + record->LogicalName);
    }

    return LoadChrMeshFromResource(Resources, record->LogicalName);
}
TResult<FSklSkeleton> FModelRepository::LoadSkl(std::string_view logicalName) const
{
    const auto* record = Find(logicalName);

    if (!record)
    {
        return FStatus::Error(EStatusCode::NotFound, "model not found: " + std::string(logicalName));
    }

    if (record->Kind != EModelAssetKind::Skl)
    {
        return FStatus::Error(EStatusCode::InvalidData, "model asset is not SKL: " + record->LogicalName);
    }

    return LoadSklSkeletonFromResource(Resources, record->LogicalName);
}
