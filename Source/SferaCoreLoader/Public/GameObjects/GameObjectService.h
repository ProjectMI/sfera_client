#pragma once
#include "GameObjects/GameObjectRegistry.h"
#include "GameObjects/ObjectParamManager.h"
#include "MBC/MbcNativeRegistry.h"
#include "ResourceLoader/ResourceManager.h"

namespace Sfera {
class FGameObjectService {
public:
    explicit FGameObjectService(const FResourceManager& resources);
    FStatus Initialize(FLogger* logger = nullptr);
    uint32 CreateObject(std::string archetype, EGameObjectKind kind = EGameObjectKind::ScriptProxy);
    bool DestroyObject(uint32 handle);
    void SetPosition(uint32 handle, FVector3 position);
    void AddPosition(uint32 handle, FVector3 delta);
    void SetDirection(uint32 handle, FVector3 direction);
    void SetTrigger(uint32 handle, int32 trigger);
    float GetSpeed(uint32 handle) const;
    int32 GetDirectionCode(uint32 handle) const;
    FGameObjectRegistry& Registry() { return ObjectRegistry; }
    const FGameObjectRegistry& Registry() const { return ObjectRegistry; }
    FObjectParamManager& Params() { return ParamManager; }
    void RegisterMbcNatives(FMbcNativeRegistry& registry, FLogger* logger = nullptr);
private:
    const FResourceManager& Resources;
    FObjectParamManager ParamManager;
    FGameObjectRegistry ObjectRegistry;
};
}
