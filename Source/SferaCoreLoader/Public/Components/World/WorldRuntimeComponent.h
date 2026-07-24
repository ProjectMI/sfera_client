#pragma once
#include "GameObjects/GameObjectService.h"
#include "ResourceLoader/ResourceManager.h"
#include "WorldScene/WorldScene.h"

class FWorldRuntimeComponent
{
public:
    FWorldRuntimeComponent(FGameObjectService& objects, FWorldScene& world, const FResourceManager& resources, FLogger* logger = nullptr);
    uint32 CreateObject(std::string archetype, EGameObjectKind kind = EGameObjectKind::ScriptProxy, float radius = 32.0f);
    bool DestroyObject(uint32 handle);
    bool SetObjectPosition(uint32 handle, FVector3 position, float radius = 32.0f);
    bool AddObjectPosition(uint32 handle, FVector3 delta, float radius = 32.0f);
    std::optional<FVector3> ObjectPosition(uint32 handle) const;
    bool ResourceExists(std::string_view logicalName) const;
    bool DirectoryExists(std::string_view logicalPath) const;
    size_t PresentMapCellCount() const;
    size_t MapCellCount() const;
    int32 FindZoneIndex(FVector3 position);
    size_t QueryContourCount(float x, float z, float halfExtent = 128.0f);
private:
    FGameObjectService& Objects;
    FWorldScene& World;
    const FResourceManager& Resources;
    FLogger* Log = nullptr;
};
