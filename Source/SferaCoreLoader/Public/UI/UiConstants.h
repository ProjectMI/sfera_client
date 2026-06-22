#pragma once
#include "Common/SferaGameConstants.h"
#include <cstddef>

namespace SferaUi
{
constexpr int32 LoginEditId = 7;
constexpr int32 PasswordEditId = 8;
constexpr int32 SavePasswordId = 9;
constexpr int32 LoginButtonId = 3;
constexpr int32 CancelButtonId = 1;
constexpr int32 QuitButtonId = 4;
constexpr int32 RegistrationButtonId = 11;
constexpr int32 CharacterGenderTextId = 2;
constexpr int32 ModalButton1Id = 1;
constexpr int32 ModalButton2Id = 2;
constexpr int32 DeleteConfirmEditId = 4;
constexpr int32 DeleteConfirmButtonId = 5;
constexpr int32 CharacterGenderControlId = 12;
constexpr int32 CharacterFaceControlId = 13;
constexpr int32 CharacterHairControlId = 14;
constexpr int32 CharacterHairColorControlId = 15;
constexpr int32 CharacterTattooControlId = 16;
constexpr int32 CharacterFirstAppearanceControlId = CharacterGenderControlId;
constexpr int32 CharacterLastAppearanceControlId = CharacterTattooControlId;
constexpr int32 CharacterNameEditBaseId = 60;
constexpr int32 CharacterSlotRadioBaseId = 63;
constexpr int32 CharacterDeleteButtonId = 57;
constexpr int32 CharacterExitButtonId = 58;
constexpr int32 CharacterContinueButtonId = 59;
constexpr int32 CharacterStrengthTextId = 17;
constexpr int32 CharacterDexterityTextId = 18;
constexpr int32 CharacterAccuracyTextId = 19;
constexpr int32 CharacterEnduranceTextId = 20;
constexpr int32 CharacterFireTextId = 29;
constexpr int32 CharacterWaterTextId = 30;
constexpr int32 CharacterEarthTextId = 31;
constexpr int32 CharacterAirTextId = 32;
constexpr int32 CharacterTitleProgressId = 41;
constexpr int32 CharacterDegreeProgressId = 42;
constexpr int32 CharacterTitleTextId = 43;
constexpr int32 CharacterDegreeTextId = 44;
constexpr int32 CharacterHpProgressId = 45;
constexpr int32 CharacterMpProgressId = 46;
constexpr int32 CharacterSatietyProgressId = 47;
constexpr int32 CharacterPhysicalAttackTextId = 48;
constexpr int32 CharacterPhysicalDefenseTextId = 49;
constexpr int32 CharacterTitleStatsTextId = 50;
constexpr int32 CharacterMagicalAttackTextId = 51;
constexpr int32 CharacterMagicalDefenseTextId = 52;
constexpr int32 CharacterDegreeStatsTextId = 53;
constexpr int32 CharacterReservedTextId = 55;
constexpr int32 CharacterKarmaTextId = 56;
constexpr size_t MaxLoginChars = 63;
constexpr size_t MaxPasswordChars = 29;
constexpr size_t MaxCharacterNameChars = 15;
constexpr size_t DefaultCharacterNameChars = 12;
constexpr size_t MaxDeleteConfirmChars = 20;
constexpr size_t MaxGameChatChars = 240;
constexpr size_t MaxStatusLines = 6;

inline bool IsCharacterAppearanceControl(int32 id) { return id >= CharacterFirstAppearanceControlId && id <= CharacterLastAppearanceControlId; }
inline bool IsCharacterNameEdit(int32 id) { return id >= CharacterNameEditBaseId && id < CharacterNameEditBaseId + Sfera::CharacterSlotCount; }
inline bool IsCharacterSlotRadio(int32 id) { return id >= CharacterSlotRadioBaseId && id < CharacterSlotRadioBaseId + Sfera::CharacterSlotCount; }
inline int32 SlotFromNameEditId(int32 id) { return id - CharacterNameEditBaseId; }
inline int32 SlotFromRadioId(int32 id) { return id - CharacterSlotRadioBaseId; }
inline int32 NameEditIdForSlot(int32 slot) { return CharacterNameEditBaseId + slot; }
inline int32 RadioIdForSlot(int32 slot) { return CharacterSlotRadioBaseId + slot; }
}
