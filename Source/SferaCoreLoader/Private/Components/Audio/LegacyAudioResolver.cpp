#include "Components/Audio/LegacyAudioResolver.h"

int32 FLegacyAudioResolver::CreatureEffectId(int32 soundBase, ELegacyCreatureCue cue)
{
    return soundBase + static_cast<int32>(cue);
}

int32 FLegacyAudioResolver::FootstepEffectId(int32 stepBase, int32 rawGroundCode, ELegacyFootstepCue cue)
{
    return stepBase + GroundMaterialIndex(rawGroundCode) * 3 + static_cast<int32>(cue);
}

int32 FLegacyAudioResolver::GroundMaterialIndex(int32 rawGroundCode)
{
    static constexpr std::array<int32, 8> materials{6, 0, 1, 4, 3, 3, 4, 5};
    return rawGroundCode >= 0 && rawGroundCode < static_cast<int32>(materials.size()) ? materials[static_cast<size_t>(rawGroundCode)] : materials[0];
}

bool FLegacyAudioResolver::IsDirectSoundEffect(int32 effectId)
{
    return effectId >= DirectSoundEffectBase;
}

std::string FLegacyAudioResolver::CueNameForEffect(int32 effectId)
{
    return "legacy.effect." + std::to_string(effectId);
}
