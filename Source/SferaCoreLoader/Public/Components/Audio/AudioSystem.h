#pragma once
#include "Components/Audio/AudioTypes.h"

class FLogger;
class FResourceManager;

enum class ELegacyCreatureCue : int32;
enum class ELegacyFootstepCue : int32;
enum class ELegacyWeaponCue : int32;

class FAudioSystem
{
public:
    FAudioSystem();
    ~FAudioSystem();
    FAudioSystem(const FAudioSystem&) = delete;
    FAudioSystem& operator=(const FAudioSystem&) = delete;
    FAudioSystem(FAudioSystem&&) = delete;
    FAudioSystem& operator=(FAudioSystem&&) = delete;

    FStatus Initialize(const FResourceManager& resources, FLogger* logger = nullptr);
    void Shutdown();
    bool IsInitialized() const;

    void RegisterCue(std::string name, FAudioCueDefinition definition);
    void RegisterMusicTrack(std::string name, FMusicTrackDefinition definition);
    bool RegisterMusicTrackIfMissing(std::string name, FMusicTrackDefinition definition);
    void RegisterLegacyEffect(int32 effectId, std::string cueName);
    FAudioImportReport ImportLegacyDefinitions();

    FAudioHandle PlayEvent(EAudioEvent event);
    FAudioHandle PlayCue(std::string_view cueName, const FAudioPlayContext& context = {});
    FAudioHandle PlayLegacyEffect(int32 effectId, const FAudioPlayContext& context = {});
    FAudioHandle PlayCreatureSound(int32 soundBase, ELegacyCreatureCue cue, const FAudioPlayContext& context = {});
    FAudioHandle PlayFootstep(int32 stepBase, int32 rawGroundCode, ELegacyFootstepCue cue, const FAudioPlayContext& context = {});
    FAudioHandle PlayWeapon(ELegacyWeaponCue cue, const FAudioPlayContext& context = {});
    bool Stop(FAudioHandle handle, float fadeSeconds = 0.0f);
    void StopBus(EAudioBus bus, float fadeSeconds = 0.0f);
    bool SetSourceTransform(FAudioHandle handle, const FAudioVector3& position, const FAudioVector3& velocity = {});

    void PlayMusic(const FMusicRequest& request);
    void StopMusic(float fadeSeconds = 1.0f);
    void SetListener(const FAudioListenerState& listener);
    void SetGameTimeHours(float hour);
    void SetMasterVolume(float volume);
    void SetBusVolume(EAudioBus bus, float volume);
    void ClearCache();
    void Update(float deltaSeconds);

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
