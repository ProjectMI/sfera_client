#pragma once
#include "Core/Types.h"

enum class EAudioBus : uint8
{
    Ui,
    Effects,
    Creatures,
    Ambience,
    Music,
    Count
};

enum class EAudioEvent : uint8
{
    UiClick,
    UiLink,
    UiLineStep,
    UiPageStep,
    UiWindowOpen,
    UiWindowClose,
    Count
};

enum class EAudioSelectionMode : uint8
{
    First,
    Random,
    RandomNoRepeat,
    Sequence
};

enum class EMusicPatternCommandType : uint8
{
    PlaySample,
    Wait,
    Jump,
    Stop
};

struct FAudioVector3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

inline FAudioVector3 operator+(const FAudioVector3& left, const FAudioVector3& right)
{
    return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
}

struct FAudioListenerState
{
    FAudioVector3 Position;
    FAudioVector3 Forward{0.0f, 0.0f, 1.0f};
    FAudioVector3 Up{0.0f, 1.0f, 0.0f};
    FAudioVector3 Velocity;
};

struct FAudioHandle
{
    uint64 Value = 0;
    bool IsValid() const { return Value != 0; }
    explicit operator bool() const { return IsValid(); }
    bool operator==(const FAudioHandle&) const = default;
};

struct FAudioCueVariant
{
    std::string Asset;
    float Weight = 1.0f;
    float StartHour = -1.0f;
    float EndHour = -1.0f;
    float SilenceSeconds = 0.0f;
};

struct FAudioCueDefinition
{
    EAudioBus Bus = EAudioBus::Effects;
    EAudioSelectionMode Selection = EAudioSelectionMode::First;
    std::vector<FAudioCueVariant> Variants;
    bool Spatialized = false;
    bool Loop = false;
    bool RepeatVariants = false;
    bool RandomizeInRegion = false;
    float RegionRadius = 0.0f;
    float MinDistance = 1.0f;
    float MaxDistance = 80.0f;
    float VolumeBarrier = 0.0f;
    float Rolloff = 1.0f;
    float DopplerFactor = 1.0f;
    float Volume = 1.0f;
    float Pitch = 1.0f;
    float MixDuration = 0.0f;
    float MinRetriggerSeconds = 0.0f;
    float CacheLifetimeSeconds = 60.0f;
    int32 MaxInstances = 0;
    FAudioVector3 PositionOffset;
};

struct FAudioPlayContext
{
    FAudioVector3 Position;
    FAudioVector3 Velocity;
    float Volume = 1.0f;
    float Pitch = 1.0f;
    float GameTimeHours = -1.0f;
    std::optional<EAudioBus> BusOverride;
};

struct FMusicSampleRange
{
    int32 Index = 0;
    double StartSeconds = 0.0;
    double EndSeconds = 0.0;
};

struct FMusicPatternCommand
{
    EMusicPatternCommandType Type = EMusicPatternCommandType::Stop;
    int32 Value = 0;
};

struct FMusicPatternDefinition
{
    int32 Index = 0;
    std::vector<FMusicPatternCommand> Commands;
};

struct FMusicTrackDefinition
{
    std::string Asset;
    float Volume = 1.0f;
    bool AutoFree = false;
    int32 StartPattern = 0;
    std::vector<FMusicSampleRange> Samples;
    std::vector<FMusicPatternDefinition> Patterns;
};

struct FMusicRequest
{
    std::string Cue;
    float Volume = 1.0f;
    float FadeSeconds = 1.0f;
};

struct FAudioImportReport
{
    size_t FilesScanned = 0;
    size_t SoundEffectsImported = 0;
    size_t ScriptedEffectsImported = 0;
    size_t MusicDefinitionFilesSeen = 0;
    size_t MusicTracksImported = 0;
    size_t RawMusicAssetsSeen = 0;
    size_t RawMusicAssetsImported = 0;
    size_t MusicAliasesImported = 0;
    size_t DefinitionsSkipped = 0;
};
