#include "GameObjects/GameObjectRegistry.h"

namespace Sfera {
uint32 FGameObjectRegistry::Create(FGameObjectState state) { uint32 handle = state.Handle ? state.Handle : NextHandle++; state.Handle = handle; state.Alive = true; Objects[handle] = std::move(state); return handle; }
bool FGameObjectRegistry::Destroy(uint32 handle) { auto it = Objects.find(handle); if (it == Objects.end()) { return false; } it->second.Alive = false; Objects.erase(it); return true; }
FGameObjectState* FGameObjectRegistry::Find(uint32 handle) { auto it = Objects.find(handle); return it == Objects.end() ? nullptr : &it->second; }
const FGameObjectState* FGameObjectRegistry::Find(uint32 handle) const { auto it = Objects.find(handle); return it == Objects.end() ? nullptr : &it->second; }
std::vector<FGameObjectState> FGameObjectRegistry::Snapshot() const { std::vector<FGameObjectState> out; for (const auto& pair : Objects) { out.push_back(pair.second); } return out; }
}
