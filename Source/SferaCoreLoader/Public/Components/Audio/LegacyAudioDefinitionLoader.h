#pragma once
#include "Components/Audio/AudioTypes.h"

class FAudioSystem;
class FLogger;
class FResourceManager;

class FLegacyAudioDefinitionLoader final
{
public:
    static FAudioImportReport Import(const FResourceManager& resources, FAudioSystem& audio, FLogger* logger);
};
