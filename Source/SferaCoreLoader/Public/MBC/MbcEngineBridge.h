#pragma once
#include "GameObjects/GameObjectService.h"
#include "MBC/MbcNativeRegistry.h"
#include "ResourceLoader/ResourceManager.h"
#include "WorldScene/WorldScene.h"

class FMbcEngineBridge 
{
public:
    static void Register(FMbcNativeRegistry& registry, FGameObjectService* objects, FWorldScene* world, const FResourceManager* resources, FLogger* logger = nullptr);
private:
    static std::string ReadSliceString(const FMbcNativeContext& ctx, const FMbcSlice& slice);
    static std::string BestStringArg(const FMbcNativeContext& ctx, std::string fallback);
    static int32 BestIntArg(const FMbcNativeContext& ctx, size_t reverseIndex, int32 fallback = 0);
    static float BestFloatArg(const FMbcNativeContext& ctx, size_t reverseIndex, float fallback = 0.0f);
};