#pragma once
#include "SferaBase.h"
#include "SferaResourceTypes.h"

class SferaResourceManager;

class SferaSoundSystem
{
public:
    bool Initialize(const SferaResourceManager& Resources);
    void Shutdown();

    bool IsNoOp() const { return true; }
    int LoadSound(const char* LogicalPath);
    void PlaySound(int SoundId);
    void StopSound(int SoundId);
    void SetVolume(float Volume);

    const std::vector<const SferaResourceRecord*>& GetSoundDefinitions() const { return SoundDefinitions; }
    const std::vector<const SferaResourceRecord*>& GetSoundWaves() const { return SoundWaves; }

private:
    std::vector<const SferaResourceRecord*> SoundDefinitions;
    std::vector<const SferaResourceRecord*> SoundWaves;
    float MasterVolume = 1.0f;
};
