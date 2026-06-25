#pragma once
#include "GameObjects/GameObjectTypes.h"

class FGameObjectRegistry 
{
public:
    uint32 Create(FGameObjectState state);
    bool Destroy(uint32 handle);
    FGameObjectState* Find(uint32 handle);
    const FGameObjectState* Find(uint32 handle) const;
    std::vector<FGameObjectState> Snapshot() const;
    size_t Count() const { return Objects.size(); }
private:
    uint32 NextHandle = 1;
    std::unordered_map<uint32, FGameObjectState> Objects;
};
