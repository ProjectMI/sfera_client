#include "UI/UiRuntimeCharacter.h"
#include "UI/UiRuntime.h"
#include "UI/UiRuntimeInternals.h"
#include "Common/SferaGameConstants.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"
#include "Common/ValueUtils.h"


#define Actions Runtime.Actions
#define ActiveCharacterEditId Runtime.ActiveCharacterEditId
#define Appearance Runtime.Appearance
#define AppearanceRules Runtime.AppearanceRules
#define Bootstrap Runtime.Bootstrap
#define CharacterActionLocked Runtime.CharacterActionLocked
#define CharacterNameEdits Runtime.CharacterNameEdits
#define CharacterSlotState Runtime.CharacterSlotState
#define Modal Runtime.Modal
#define ModalEditText Runtime.ModalEditText
#define ModalText Runtime.ModalText
#define PickPerson Runtime.PickPerson
#define SelectedSlot Runtime.SelectedSlot
#define ResolveText Runtime.ResolveText

FUiRuntimeCharacter::FUiRuntimeCharacter(FUiRuntime& runtime) : Runtime(runtime) {}

void FUiRuntimeCharacter::ApplyCharacterDeleted(int32 slot)
{
    const int32 index = Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount);
    FCharacterSlotInfo& info = CharacterSlotState[static_cast<size_t>(index)];
    info = FCharacterSlotInfo{};
    info.Slot = index;
    info.Present = false;
    info.CanCreate = true;
    CharacterNameEdits[static_cast<size_t>(index)] = DefaultCharacterNameForSlot(index);
    SelectedSlot = index;
    ActiveCharacterEditId = SferaUi::NameEditIdForSlot(index);
    SyncCharacterSelectControls();
}

void FUiRuntimeCharacter::ApplyCharacterCreated(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance)
{
    const int32 index = Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount);
    FCharacterSlotInfo& info = CharacterSlotState[static_cast<size_t>(index)];
    info = FCharacterSlotInfo{};
    info.Slot = index;
    info.Present = true;
    info.CanCreate = false;
    info.Name = name;
    info.Female = appearance.Female;
    info.Face = appearance.Face;
    info.Hair = appearance.Hair;
    info.HairColor = appearance.HairColor;
    info.Tattoo = appearance.Tattoo;
    info.Strength = Appearance.Strength;
    info.Dexterity = Appearance.Dexterity;
    info.Accuracy = Appearance.Accuracy;
    info.Endurance = Appearance.Endurance;
    info.Fire = Appearance.Fire;
    info.Water = Appearance.Water;
    info.Earth = Appearance.Earth;
    info.Air = Appearance.Air;
    info.MaxHp = Sfera::DefaultCharacterVital;
    info.CurrentHp = Sfera::DefaultCharacterVital;
    info.MaxMp = Sfera::DefaultCharacterVital;
    info.CurrentMp = Sfera::DefaultCharacterVital;
    info.MaxSatiety = Sfera::DefaultCharacterVital;
    info.CurrentSatiety = Sfera::DefaultCharacterVital;
    info.TitleId = Sfera::DefaultTitleId;
    info.DegreeId = Sfera::DefaultDegreeId;
    info.TitleLevel = Sfera::DefaultTitleLevel;
    info.DegreeLevel = Sfera::DefaultDegreeLevel;
    info.TitleXp = 0;
    info.DegreeXp = 0;
    info.TitleNextXp = Sfera::DefaultProgressNextXp;
    info.DegreeNextXp = Sfera::DefaultProgressNextXp;
    info.Karma = Sfera::DefaultKarma;
    CharacterNameEdits[static_cast<size_t>(index)] = name;
    SelectedSlot = index;
    ActiveCharacterEditId = 0;
    SyncCharacterSelectControls();
}

std::wstring FUiRuntimeCharacter::DefaultCharacterNameForSlot(int32 slot) const
{
    std::wstring name;

    for (wchar_t ch : Common::Utf8ToWide(Actions.LoginText))
    {
        if (FUiRuntimeInternals::IsCharacterNameChar(ch))
        {
            name.push_back(ch);
        }

        if (name.size() >= SferaUi::DefaultCharacterNameChars)
        {
            break;
        }
    }

    if (name.empty())
    {
        name = L"Hero";
    }

    if (!name.empty() && name.front() >= L'0' && name.front() <= L'9')
    {
        name.insert(name.begin(), L'C');
    }

    if (slot > 0 && name.size() < SferaUi::DefaultCharacterNameChars)
    {
        name.push_back(static_cast<wchar_t>(L'1' + slot));
    }

    if (name.size() > SferaUi::DefaultCharacterNameChars)
    {
        name.resize(SferaUi::DefaultCharacterNameChars);
    }

    return name;
}

void FUiRuntimeCharacter::SetCharacterAppearanceRules(const FCharacterAppearanceRules& rules)
{
    AppearanceRules = rules;
    ClampCharacterAppearance();
    SyncCharacterSelectControls();
}

int32 FUiRuntimeCharacter::CharacterAppearanceOptionCount(int32 controlId) const
{
    const bool female = Appearance.Gender != 0;

    switch (controlId)
    {
    case SferaUi::CharacterGenderControlId: return 2;
    case SferaUi::CharacterFaceControlId: return female ? AppearanceRules.FemaleFaceCount : AppearanceRules.MaleFaceCount;
    case SferaUi::CharacterHairControlId: return female ? AppearanceRules.FemaleHairCount : AppearanceRules.MaleHairCount;
    case SferaUi::CharacterHairColorControlId: return AppearanceRules.HairColorCount;
    case SferaUi::CharacterTattooControlId: return AppearanceRules.TattooCount;
    default: return 1;
    }
}

void FUiRuntimeCharacter::ClampCharacterAppearance()
{
    Appearance.Gender = Common::ClampIndexToCount(Appearance.Gender, CharacterAppearanceOptionCount(SferaUi::CharacterGenderControlId));
    Appearance.Face = Common::ClampIndexToCount(Appearance.Face, CharacterAppearanceOptionCount(SferaUi::CharacterFaceControlId));
    Appearance.Hair = Common::ClampIndexToCount(Appearance.Hair, CharacterAppearanceOptionCount(SferaUi::CharacterHairControlId));
    Appearance.HairColor = Common::ClampIndexToCount(Appearance.HairColor, CharacterAppearanceOptionCount(SferaUi::CharacterHairColorControlId));
    Appearance.Tattoo = Common::ClampIndexToCount(Appearance.Tattoo, CharacterAppearanceOptionCount(SferaUi::CharacterTattooControlId));
}

int32 FUiRuntimeCharacter::SelectedSlotIndex() const
{
    return Common::ClampIndexToCount(SelectedSlot, Sfera::CharacterSlotCount);
}

const std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount>& FUiRuntimeCharacter::Slots() const
{
    return CharacterSlotState;
}

int32 FUiRuntimeCharacter::SceneCameraFocusId() const
{
    return Runtime.SceneCameraFocusId;
}

float FUiRuntimeCharacter::SceneAngle() const
{
    return Runtime.SceneAngle;
}

const FCharacterSlotInfo& FUiRuntimeCharacter::SelectedSlotInfo() const
{
    return CharacterSlotState[static_cast<size_t>(SelectedSlotIndex())];
}

FCharacterSlotInfo& FUiRuntimeCharacter::MutableSelectedSlotInfo()
{
    return CharacterSlotState[static_cast<size_t>(SelectedSlotIndex())];
}

void FUiRuntimeCharacter::SetCharacterSlots(const std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount>& slots)
{
    CharacterSlotState = slots;
    int32 firstSelectable = 0;
    bool found = false;

    for (int32 i = 0; i < Sfera::CharacterSlotCount; ++i)
    {
        if (CharacterSlotState[static_cast<size_t>(i)].Present)
        {
            firstSelectable = i;
            found = true;
            break;
        }
    }

    if (!found)
    {
        for (int32 i = 0; i < Sfera::CharacterSlotCount; ++i)
        {
            if (CharacterSlotState[static_cast<size_t>(i)].CanCreate)
            {
                firstSelectable = i;
                found = true;
                break;
            }
        }
    }

    SelectedSlot = Common::ClampIndexToCount(found ? firstSelectable : SelectedSlot, Sfera::CharacterSlotCount);

    for (int32 i = 0; i < Sfera::CharacterSlotCount; ++i)
    {
        const auto& slot = CharacterSlotState[static_cast<size_t>(i)];
        CharacterNameEdits[static_cast<size_t>(i)] = slot.Present ? slot.Name : DefaultCharacterNameForSlot(i);
    }

    ActiveCharacterEditId = SferaUi::NameEditIdForSlot(SelectedSlot);
    ClampCharacterAppearance();
    SyncCharacterSelectControls();
}

void FUiRuntimeCharacter::SetCharacterActionLocked(bool locked)
{
    CharacterActionLocked = locked;
    SyncCharacterSelectControls();
}

FUiControlDef* FUiRuntimeCharacter::MutableCharacterControlById(int32 id)
{
    for (FUiControlDef& control : PickPerson.Controls)
    {
        if (control.Id == id)
        {
            return &control;
        }
    }

    return nullptr;
}

const FUiControlDef* FUiRuntimeCharacter::CharacterControlById(int32 id) const
{
    for (const FUiControlDef& control : PickPerson.Controls)
    {
        if (control.Id == id)
        {
            return &control;
        }
    }

    return nullptr;
}

void FUiRuntimeCharacter::SyncCharacterSelectControls()
{
    if (PickPerson.Name.empty()) { return; }

    const int32 selected = SelectedSlotIndex();

    for (int32 i = 0; i < Sfera::CharacterSlotCount; ++i)
    {
        const auto& slot = CharacterSlotState[static_cast<size_t>(i)];

        if (auto* edit = MutableCharacterControlById(SferaUi::NameEditIdForSlot(i)))
        {
            edit->Hidden = i != selected || slot.Present || !slot.CanCreate;
            edit->Disabled = edit->Hidden || CharacterActionLocked;
        }

        if (auto* radio = MutableCharacterControlById(SferaUi::RadioIdForSlot(i)))
        {
            radio->Hidden = false;
            radio->Disabled = CharacterActionLocked || (!slot.Present && !slot.CanCreate);
        }
    }

    const auto& selectedSlot = CharacterSlotState[static_cast<size_t>(selected)];
    const bool canEditAppearance = !selectedSlot.Present && selectedSlot.CanCreate && !CharacterActionLocked;

    for (int32 id = SferaUi::CharacterFirstAppearanceControlId; id <= SferaUi::CharacterLastAppearanceControlId; ++id)
    {
        if (auto* control = MutableCharacterControlById(id))
        {
            control->Hidden = false;
            control->Disabled = !canEditAppearance;
        }
    }

    if (auto* del = MutableCharacterControlById(SferaUi::CharacterDeleteButtonId))
    {
        del->Disabled = CharacterActionLocked || !selectedSlot.Present;
    }

    if (auto* exit = MutableCharacterControlById(SferaUi::CharacterExitButtonId))
    {
        exit->Disabled = CharacterActionLocked;
    }

    if (auto* cont = MutableCharacterControlById(SferaUi::CharacterContinueButtonId))
    {
        cont->Disabled = CharacterActionLocked || (!selectedSlot.Present && !selectedSlot.CanCreate);
    }

    if (!selectedSlot.Present && selectedSlot.CanCreate && !CharacterActionLocked)
    {
        ActiveCharacterEditId = SferaUi::NameEditIdForSlot(selected);
    }
}

bool FUiRuntimeCharacter::SelectedCharacterPresent() const
{
    return SelectedSlotInfo().Present;
}

bool FUiRuntimeCharacter::SelectedCharacterCanCreate() const
{
    return SelectedSlotInfo().CanCreate;
}

std::wstring FUiRuntimeCharacter::SelectedCharacterName() const
{
    const int32 slot = SelectedSlotIndex();
    const auto& info = SelectedSlotInfo();
    return info.Present ? info.Name : CharacterNameEdits[static_cast<size_t>(slot)];
}

FCharacterCreationAppearance FUiRuntimeCharacter::SelectedCharacterAppearance(const FCharacterAppearanceRules& rules) const
{
    FCharacterCreationAppearance out;
    out.ModelBase = rules.ModelBase;
    out.Female = Appearance.Gender != 0;
    out.Face = Common::ClampIndexToCount(Appearance.Face, out.Female ? rules.FemaleFaceCount : rules.MaleFaceCount);
    out.Hair = Common::ClampIndexToCount(Appearance.Hair, out.Female ? rules.FemaleHairCount : rules.MaleHairCount);
    out.HairColor = Common::ClampIndexToCount(Appearance.HairColor, rules.HairColorCount);
    out.Tattoo = Common::ClampIndexToCount(Appearance.Tattoo, rules.TattooCount);
    return out;
}

FCharacterCreationAppearance FUiRuntimeCharacter::SelectedCharacterSceneAppearance() const
{
    const auto& info = SelectedSlotInfo();
    FCharacterCreationAppearance out;
    out.ModelBase = AppearanceRules.ModelBase;

    if (info.Present)
    {
        out.Female = info.Female;
        out.Face = Common::ClampIndexToCount(info.Face, info.Female ? AppearanceRules.FemaleFaceCount : AppearanceRules.MaleFaceCount);
        out.Hair = Common::ClampIndexToCount(info.Hair, info.Female ? AppearanceRules.FemaleHairCount : AppearanceRules.MaleHairCount);
        out.HairColor = Common::ClampIndexToCount(info.HairColor, AppearanceRules.HairColorCount);
        out.Tattoo = Common::ClampIndexToCount(info.Tattoo, AppearanceRules.TattooCount);
    }
    else
    {
        out.Female = Appearance.Gender != 0;
        out.Face = Common::ClampIndexToCount(Appearance.Face, out.Female ? AppearanceRules.FemaleFaceCount : AppearanceRules.MaleFaceCount);
        out.Hair = Common::ClampIndexToCount(Appearance.Hair, out.Female ? AppearanceRules.FemaleHairCount : AppearanceRules.MaleHairCount);
        out.HairColor = Common::ClampIndexToCount(Appearance.HairColor, AppearanceRules.HairColorCount);
        out.Tattoo = Common::ClampIndexToCount(Appearance.Tattoo, AppearanceRules.TattooCount);
    }

    return out;
}

std::string FUiRuntimeCharacter::EmptyCharacterSlotText() const
{
    if (Bootstrap.Lang == 1) { return "Empty"; }

    if (Bootstrap.Lang == 3) { return "vazio"; }

    return "Пусто";
}

std::string FUiRuntimeCharacter::CharacterGenderText(bool female) const
{
    if (female) { return Bootstrap.Lang == 1 ? "Female" : "Жен."; }

    std::string text = ResolveText("UISTR_WT_CPS06");
    return text.empty() ? (Bootstrap.Lang == 1 ? "Male" : "Муж.") : text;
}

int32 FUiRuntimeCharacter::CharacterTextValue(int32 id) const
{
    const auto& slot = SelectedSlotInfo();
    const bool occupied = slot.Present;

    switch (id)
    {
    case SferaUi::CharacterStrengthTextId: return occupied ? slot.Strength : Appearance.Strength;
    case SferaUi::CharacterDexterityTextId: return occupied ? slot.Dexterity : Appearance.Dexterity;
    case SferaUi::CharacterAccuracyTextId: return occupied ? slot.Accuracy : Appearance.Accuracy;
    case SferaUi::CharacterEnduranceTextId: return occupied ? slot.Endurance : Appearance.Endurance;
    case SferaUi::CharacterFireTextId: return occupied ? slot.Fire : Appearance.Fire;
    case SferaUi::CharacterWaterTextId: return occupied ? slot.Water : Appearance.Water;
    case SferaUi::CharacterEarthTextId: return occupied ? slot.Earth : Appearance.Earth;
    case SferaUi::CharacterAirTextId: return occupied ? slot.Air : Appearance.Air;
    case SferaUi::CharacterPhysicalAttackTextId: return occupied ? slot.PhysicalAttack : 0;
    case SferaUi::CharacterPhysicalDefenseTextId: return occupied ? slot.PhysicalDefense : 0;
    case SferaUi::CharacterTitleStatsTextId: return occupied ? slot.TitleStats : 0;
    case SferaUi::CharacterMagicalAttackTextId: return occupied ? slot.MagicalAttack : 0;
    case SferaUi::CharacterMagicalDefenseTextId: return occupied ? slot.MagicalDefense : 0;
    case SferaUi::CharacterDegreeStatsTextId: return occupied ? slot.DegreeStats : 0;
    default: return 0;
    }
}

float FUiRuntimeCharacter::CharacterProgressRatio(int32 controlId) const
{
    const auto& slot = SelectedSlotInfo();
    const bool occupied = slot.Present;
    auto ratio = [](int32 current, int32 maximum)
    {
        return maximum <= 0 ? 0.0f : std::clamp(static_cast<float>(current) / static_cast<float>(maximum), 0.0f, 1.0f);
    };

    if (!occupied) { return controlId == SferaUi::CharacterTitleProgressId || controlId == SferaUi::CharacterDegreeProgressId ? 0.0f : 1.0f; }

    if (controlId == SferaUi::CharacterTitleProgressId) { return ratio(slot.TitleXp, slot.TitleNextXp); }

    if (controlId == SferaUi::CharacterDegreeProgressId) { return ratio(slot.DegreeXp, slot.DegreeNextXp); }

    if (controlId == SferaUi::CharacterMpProgressId) { return ratio(slot.CurrentMp, slot.MaxMp); }

    if (controlId == SferaUi::CharacterSatietyProgressId) { return ratio(slot.CurrentSatiety, slot.MaxSatiety); }

    if (controlId == SferaUi::CharacterHpProgressId) { return ratio(slot.CurrentHp, slot.MaxHp); }

    return 1.0f;
}

bool FUiRuntimeCharacter::ModalEditMatchesSelectedCharacter() const
{
    if (Modal != EUiModalDialog::CharacterDelete) { return true; }

    return FUiRuntimeInternals::Utf8EqualsWideNoCase(ModalEditText, SelectedCharacterName());
}

std::string FUiRuntimeCharacter::CharacterTitleText() const
{
    const auto& slot = SelectedSlotInfo();

    if (!slot.Present) { return {}; }

    std::string_view name;

    switch (slot.TitleId)
    {
    case Sfera::DefaultTitleId: name = "странник";
        break;
    default: break;
    }

    std::string title = !name.empty() ? std::string(name) : (Bootstrap.Lang == 1 ? "title " + std::to_string(slot.TitleId) : "звание " + std::to_string(slot.TitleId));
    return title + " (" + std::to_string(std::max(1, slot.TitleLevel)) + ")";
}

std::string FUiRuntimeCharacter::CharacterDegreeText() const
{
    const auto& slot = SelectedSlotInfo();

    if (!slot.Present) { return {}; }

    std::string_view name;

    switch (slot.DegreeId)
    {
    case Sfera::DefaultDegreeId: name = "неучёный";
        break;
    default: break;
    }

    std::string degree = !name.empty() ? std::string(name) : (Bootstrap.Lang == 1 ? "degree " + std::to_string(slot.DegreeId) : "ремесло " + std::to_string(slot.DegreeId));
    return degree + " (" + std::to_string(std::max(1, slot.DegreeLevel)) + ")";
}

std::string FUiRuntimeCharacter::CharacterKarmaText() const
{
    const auto& slot = SelectedSlotInfo();

    if (!slot.Present) { return {}; }

    const int32 karma = slot.Karma <= 0 ? Sfera::DefaultKarma : slot.Karma;

    if (Bootstrap.Lang == 1)
    {
        switch (karma)
        {
        case 1: return "Karma: very bad";
        case 2: return "Karma: bad";
        case 4: return "Karma: good";
        case 5: return "Karma: benign";
        default: return "Karma: neutral";
        }
    }

    switch (karma)
    {
    case 1: return "Карма: очень плохая";
    case 2: return "Карма: плохая";
    case 4: return "Карма: хорошая";
    case 5: return "Карма: добрая";
    default: return "Карма: нейтральная";
    }
}

std::string FUiRuntimeCharacter::CharacterProgressText(const FUiControlDef& control) const
{
    const auto& slot = SelectedSlotInfo();

    if (!slot.Present) { return {}; }

    auto pairText = [](int32 current, int32 maximum)
    {
        return std::to_string(std::max(0, current)) + " / " + std::to_string(std::max(0, maximum));
    };

    if (control.Id == SferaUi::CharacterTitleProgressId) { return pairText(slot.TitleXp, slot.TitleNextXp); }

    if (control.Id == SferaUi::CharacterDegreeProgressId) { return pairText(slot.DegreeXp, slot.DegreeNextXp); }

    if (control.Id == SferaUi::CharacterHpProgressId) { return pairText(slot.CurrentHp, slot.MaxHp); }

    if (control.Id == SferaUi::CharacterMpProgressId) { return pairText(slot.CurrentMp, slot.MaxMp); }

    if (control.Id == SferaUi::CharacterSatietyProgressId) { return pairText(slot.CurrentSatiety, slot.MaxSatiety); }

    return {};
}

std::string FUiRuntimeCharacter::ModalControlText(const FUiControlDef& control) const
{
    if (Modal == EUiModalDialog::CharacterDelete)
    {
        if (control.Id == SferaUi::LoginButtonId || Common::EqualsNoCase(control.ClassId, "HYPER_TEXT") || Common::EqualsNoCase(control.ClassId, "TEXTLIST") || Common::EqualsNoCase(control.ClassId, "TEXT")) { return ModalText; }

        if (control.Id == SferaUi::DeleteConfirmEditId || Common::EqualsNoCase(control.ClassId, "EDIT")) { return Actions.FocusedControlId == control.Id ? ModalEditText + "_" : ModalEditText; }
    }

    if ((Modal == EUiModalDialog::CharacterCreate || Modal == EUiModalDialog::CharacterExit) && (Common::EqualsNoCase(control.ClassId, "TEXTLIST") || Common::EqualsNoCase(control.ClassId, "TEXT") || Common::EqualsNoCase(control.ClassId, "HYPER_TEXT"))) { return ModalText; }

    return control.TextKey.empty() ? std::string{} : ResolveText(control.TextKey);
}

bool FUiRuntimeCharacter::IsModalActionAllowed(const FUiControlDef& control) const
{
    if (Modal == EUiModalDialog::CharacterDelete && control.Id == SferaUi::DeleteConfirmButtonId) { return ModalEditMatchesSelectedCharacter(); }

    return !control.Disabled;
}

std::string FUiRuntimeCharacter::CharacterControlText(const FUiControlDef& control) const
{
    if (SferaUi::IsCharacterSlotRadio(control.Id))
    {
        const int32 slot = SferaUi::SlotFromRadioId(control.Id);
        const auto& info = CharacterSlotState[static_cast<size_t>(slot)];

        if (slot == SelectedSlotIndex() && !info.Present && info.CanCreate) { return {}; }

        return info.Present ? Common::WideToUtf8(info.Name) : EmptyCharacterSlotText();
    }

    if (SferaUi::IsCharacterNameEdit(control.Id))
    {
        const int32 slot = SferaUi::SlotFromNameEditId(control.Id);
        std::wstring text = CharacterNameEdits[static_cast<size_t>(slot)];

        if (ActiveCharacterEditId == control.Id && !CharacterActionLocked)
        {
            text.push_back(L'_');
        }

        return Common::WideToUtf8(text);
    }

    if (control.Id == SferaUi::CharacterGenderTextId) { const auto& info = SelectedSlotInfo(); return CharacterGenderText(info.Present ? info.Female : Appearance.Gender != 0); }

    if (control.Id == SferaUi::CharacterTitleTextId) { return CharacterTitleText(); }

    if (control.Id == SferaUi::CharacterDegreeTextId) { return CharacterDegreeText(); }

    if ((control.Id >= SferaUi::CharacterStrengthTextId && control.Id <= SferaUi::CharacterEnduranceTextId) || (control.Id >= SferaUi::CharacterFireTextId && control.Id <= SferaUi::CharacterAirTextId) || (control.Id >= SferaUi::CharacterPhysicalAttackTextId && control.Id <= SferaUi::CharacterDegreeStatsTextId)) { return std::to_string(CharacterTextValue(control.Id)); }

    if (control.Id == SferaUi::CharacterReservedTextId) { return {}; }

    if (control.Id == SferaUi::CharacterKarmaTextId) { return CharacterKarmaText(); }

    return control.TextKey.empty() ? std::string{} : ResolveText(control.TextKey);
}
