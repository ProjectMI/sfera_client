#pragma once
#include "Config/ConfigDocument.h"
#include "ResourceLoader/ResourceManager.h"
#include "WorldScene/WorldTypes.h"

namespace Sfera {
struct FZoningDocument { std::string Name; std::vector<FWorldZoneParams> Zones; std::vector<std::string> Warnings; std::vector<std::string> Notes; size_t RawEntryCount = 0; size_t ScopeCount = 0; size_t ZoneFieldCount = 0; size_t ContainerDeclarationCount = 0; std::vector<std::string> ScopeSamples; };
class FZoningManager {
public:
    FStatus LoadDefault(const FResourceManager& resources, FLogger* logger = nullptr);
    FStatus LoadOne(const FResourceManager& resources, std::string_view logicalName, FLogger* logger = nullptr);
    const std::vector<FZoningDocument>& Documents() const { return LoadedDocuments; }
    const std::vector<FWorldZoneParams>& Zones() const { return FlattenedZones; }
    const FWorldZoneParams* FindZone(FVector3 position) const;
private:
    static FZoningDocument BuildDocument(const FConfigDocument& cfg);
    static std::optional<uint32> ExtractZoneIndex(const FConfigEntry& entry);
    static bool SetKnownField(FWorldZoneParams& zone, const FConfigEntry& entry);
    static bool IsContainerDeclaration(const FConfigEntry& entry);
    static std::string DescribeEntry(const FConfigEntry& entry, std::string_view reason);
    std::vector<FZoningDocument> LoadedDocuments;
    std::vector<FWorldZoneParams> FlattenedZones;
};
}
