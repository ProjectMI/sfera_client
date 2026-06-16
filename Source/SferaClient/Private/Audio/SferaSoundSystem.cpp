#include "SferaSoundSystem.h"
#include "SferaResourceManager.h"

bool SferaSoundSystem::Initialize(const SferaResourceManager& Resources)
{
    // S0003 x64 policy: old proprietary 32-bit Sound.dll is not loaded.
    // Audio calls are accepted and ignored until a new backend is implemented.
    SoundDefinitions = Resources.FindByKind(ESferaResourceKind::SoundDefinition);
    SoundWaves = Resources.FindByKind(ESferaResourceKind::SoundWave);
    MasterVolume = 1.0f;
    return true;
}

void SferaSoundSystem::Shutdown()
{
    SoundDefinitions.clear();
    SoundWaves.clear();
    MasterVolume = 1.0f;
}

int SferaSoundSystem::LoadSound(const char* LogicalPath)
{
    (void)LogicalPath;
    return 0;
}

void SferaSoundSystem::PlaySound(int SoundId)
{
    (void)SoundId;
}

void SferaSoundSystem::StopSound(int SoundId)
{
    (void)SoundId;
}

void SferaSoundSystem::SetVolume(float Volume)
{
    MasterVolume = Volume;
}
