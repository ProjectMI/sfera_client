#pragma once
#include "Core/Types.h"
#include "Renderer/CharacterSceneTypes.h"
#include "Model/SklSkeleton.h"

struct FSkinnedCharacterBatch
{
    uint32 StartIndex = 0;
    uint32 IndexCount = 0;
    std::filesystem::path TexturePath;
    bool IsHead = false;
};

struct FSkinnedCharacterModel
{
    FSklSkeleton Skeleton;
    std::size_t RootBone = 0;
    float RootBindX = 0.0f;
    float RootBindY = 0.0f;
    float RootBindZ = 0.0f;
    float CenterX = 0.0f;
    float CenterZ = 0.0f;
    float MinY = 0.0f;
    float Scale = 1.0f;
    int AnimIdle = 20;
    int AnimWalk = 20;
    int AnimRun = 20;
    std::vector<FSkinnedVertexSource> Sources;
    std::vector<uint16> Indices;
    std::vector<FSkinnedCharacterBatch> Batches;
    bool IsValid() const;
    std::size_t ActionCount() const;
    std::size_t ActionFrameStart(std::size_t action) const;
    std::size_t ActionFrameCount(std::size_t action) const;
    int BoneIndex(const char* name) const;
};

void SkinFrame(const FSkinnedCharacterModel& model, std::size_t frame, std::vector<float>& out);
void SkinFrameInterpolated(const FSkinnedCharacterModel& model, std::size_t frameA, std::size_t frameB, float alpha, std::vector<float>& out);
