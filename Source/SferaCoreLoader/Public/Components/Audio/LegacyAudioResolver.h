#pragma once
#include "Core/Types.h"

enum class ELegacyCreatureCue : int32
{
    Pain = 0,
    Death = 1,
    Idle = 2,
    Attack = 3
};

enum class ELegacyFootstepCue : int32
{
    Walk = 0,
    Run = 1,
    Stop = 2
};

enum class ELegacyWeaponCue : int32
{
    DrawCrossbow = 5100,
    FireCrossbow = 5101,
    HolsterCrossbow = 5102,
    DrawSword = 5103,
    SwingBlade = 5104,
    HolsterSword = 5105,
    DrawAxe = 5106,
    HolsterAxe = 5107
};

class FLegacyAudioResolver final
{
public:
    static constexpr int32 DirectSoundEffectBase = 5000;
    static int32 CreatureEffectId(int32 soundBase, ELegacyCreatureCue cue);
    static int32 FootstepEffectId(int32 stepBase, int32 rawGroundCode, ELegacyFootstepCue cue);
    static int32 GroundMaterialIndex(int32 rawGroundCode);
    static bool IsDirectSoundEffect(int32 effectId);
    static std::string CueNameForEffect(int32 effectId);
};
