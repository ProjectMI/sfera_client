#pragma once
#include "UI/UiRuntimeState.h"

class FUiRuntime;

class FUiRuntimeCharacter
{
public:
    explicit FUiRuntimeCharacter(FUiRuntime& runtime);
    void ApplyCharacterDeleted(int32 slot);
    void ApplyCharacterCreated(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance);
    void SetCharacterAppearanceRules(const FCharacterAppearanceRules& rules);
    void SetCharacterSlots(const std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount>& slots);
    void SetCharacterActionLocked(bool locked);
    bool SelectedCharacterPresent() const;
    bool SelectedCharacterCanCreate() const;
    std::wstring SelectedCharacterName() const;
    FCharacterCreationAppearance SelectedCharacterAppearance(const FCharacterAppearanceRules& rules) const;
    FCharacterCreationAppearance SelectedCharacterSceneAppearance() const;
    std::string CharacterControlText(const FUiControlDef& control) const;
    std::string CharacterProgressText(const FUiControlDef& control) const;
    std::string ModalControlText(const FUiControlDef& control) const;
    bool IsModalActionAllowed(const FUiControlDef& control) const;
    float CharacterProgressRatio(int32 controlId) const;
    std::wstring DefaultCharacterNameForSlot(int32 slot) const;
    FUiControlDef* MutableCharacterControlById(int32 id);
    const FUiControlDef* CharacterControlById(int32 id) const;
    void SyncCharacterSelectControls();
    void ClampCharacterAppearance();
    int32 SelectedSlotIndex() const;
    const std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount>& Slots() const;
    int32 SceneCameraFocusId() const;
    float SceneAngle() const;
    const FCharacterSlotInfo& SelectedSlotInfo() const;
    FCharacterSlotInfo& MutableSelectedSlotInfo();
    int32 CharacterAppearanceOptionCount(int32 controlId) const;
    bool ModalEditMatchesSelectedCharacter() const;
private:
    std::string EmptyCharacterSlotText() const;
    std::string CharacterGenderText(bool female) const;
    int32 CharacterTextValue(int32 id) const;
    std::string CharacterTitleText() const;
    std::string CharacterDegreeText() const;
    std::string CharacterKarmaText() const;
    FUiRuntime& Runtime;
};
