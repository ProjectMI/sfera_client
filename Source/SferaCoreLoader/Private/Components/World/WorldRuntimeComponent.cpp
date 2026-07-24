#include "Components/World/WorldRuntimeComponent.h"
#include "Common/StringUtils.h"

FWorldRuntimeComponent::FWorldRuntimeComponent(FGameObjectService& objects, FWorldScene& world, const FResourceManager& resources, FLogger* logger) : Objects(objects), World(world), Resources(resources), Log(logger) {}

uint32 FWorldRuntimeComponent::CreateObject(std::string archetype, EGameObjectKind kind, float radius)
{
    const uint32 handle = Objects.CreateObject(std::move(archetype), kind);
    if (const FGameObjectState* object = Objects.Registry().Find(handle)) { World.SyncObject(handle, object->Position, radius, object->Archetype); }
    if (Log) { Log->Info("runtime object created: handle=" + std::to_string(handle)); }
    return handle;
}

bool FWorldRuntimeComponent::DestroyObject(uint32 handle)
{
    World.RemoveObject(handle);
    return Objects.DestroyObject(handle);
}

bool FWorldRuntimeComponent::SetObjectPosition(uint32 handle, FVector3 position, float radius)
{
    FGameObjectState* object = Objects.Registry().Find(handle);
    if (!object) { return false; }
    Objects.SetPosition(handle, position);
    World.SyncObject(handle, position, radius, object->Archetype);
    return true;
}

bool FWorldRuntimeComponent::AddObjectPosition(uint32 handle, FVector3 delta, float radius)
{
    FGameObjectState* object = Objects.Registry().Find(handle);
    if (!object) { return false; }
    Objects.AddPosition(handle, delta);
    World.SyncObject(handle, object->Position, radius, object->Archetype);
    return true;
}

std::optional<FVector3> FWorldRuntimeComponent::ObjectPosition(uint32 handle) const
{
    const FGameObjectState* object = Objects.Registry().Find(handle);
    return object ? std::optional<FVector3>(object->Position) : std::nullopt;
}

bool FWorldRuntimeComponent::ResourceExists(std::string_view logicalName) const
{
    return !logicalName.empty() && Resources.Catalog().FindByLogicalName(logicalName).has_value();
}

bool FWorldRuntimeComponent::DirectoryExists(std::string_view logicalPath) const
{
    std::string prefix(logicalPath);
    std::replace(prefix.begin(), prefix.end(), '\\', '/');
    if (!prefix.empty() && prefix.back() != '/') { prefix.push_back('/'); }
    const std::string loweredPrefix = Common::ToLower(prefix);
    return !loweredPrefix.empty() && std::any_of(Resources.Catalog().All().begin(), Resources.Catalog().All().end(), [&loweredPrefix](const FFileRecord& record)
    {
        return Common::ToLowerPath(record.RelativePath).starts_with(loweredPrefix);
    });
}

size_t FWorldRuntimeComponent::PresentMapCellCount() const { return World.Stats().MapPresentCells; }
size_t FWorldRuntimeComponent::MapCellCount() const { return World.Stats().MapCellCount; }
int32 FWorldRuntimeComponent::FindZoneIndex(FVector3 position)
{
    const FWorldZoneParams* zone = World.FindZone(position);
    return zone ? static_cast<int32>(zone->Index) : -1;
}

size_t FWorldRuntimeComponent::QueryContourCount(float x, float z, float halfExtent)
{
    FBox2 area;
    area.Min = {x - halfExtent, z - halfExtent};
    area.Max = {x + halfExtent, z + halfExtent};
    return World.QueryContours(area).size();
}
