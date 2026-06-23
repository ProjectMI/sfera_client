#include "UI/UiRuntime.h"
#include "UI/UiRuntimeInternals.h"
#include "Common/SferaGameConstants.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"
#include "Common/ValueUtils.h"
#include <algorithm>
#include <cmath>

namespace
{
const FUiControlDef* HitTestControls(const FUiWindowDef& window, const FUiRectF& windowRect, int32 x, int32 y, float scale, bool spinFallbackSize)
{
    for (auto it = window.Controls.rbegin(); it != window.Controls.rend(); ++it)
    {
        const FUiControlDef& control = *it;
        if (!FUiRuntimeInternals::ControlCanReceiveMouse(control)) { continue; }
        FUiRectF rect = FUiRuntimeInternals::ControlRectInWindow(windowRect, control, scale);
        if (spinFallbackSize && FUiRuntimeInternals::IsSpinButton(control) && (control.Rect.W <= 0 || control.Rect.H <= 0))
        {
            rect.W = 37.0f;
            rect.H = 26.0f;
        }
        if (FUiRuntimeInternals::Contains(rect, x, y)) { return &control; }
    }

    return nullptr;
}
}

FUiRectF FUiRuntime::BuildDesignRect(const RECT& clientRect) const
{
    const int width = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
    const int height = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
    const float clientW = static_cast<float>(width);
    const float clientH = static_cast<float>(height);
    const float designW = static_cast<float>(Bootstrap.DesignWidth > 0 ? Bootstrap.DesignWidth : 1024);
    const float designH = static_cast<float>(Bootstrap.DesignHeight > 0 ? Bootstrap.DesignHeight : 768);
    return FUiRectF
    {
        std::floor((clientW - designW) * 0.5f), std::floor((clientH - designH) * 0.5f), designW, designH
    };
}

FUiRectF FUiRuntime::BuildConnectionRect(const RECT& clientRect) const
{
    FUiRectF design = BuildDesignRect(clientRect);
    const float w = static_cast<float>(Connection.Rect.W);
    const float h = static_cast<float>(Connection.Rect.H);
    return FUiRectF
    {
        std::floor(design.X + (design.W - w) * 0.5f), std::floor(design.Y + (design.H - h) * 0.5f), w, h
    };
}

FUiRectF FUiRuntime::BuildWindowRect(const FUiWindowDef& window, const RECT& clientRect) const
{
    const int width = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
    const int height = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
    const float clientW = static_cast<float>(width);
    const float clientH = static_cast<float>(height);
    float x = static_cast<float>(window.Rect.X);
    float y = static_cast<float>(window.Rect.Y);
    const float w = static_cast<float>(window.Rect.W);
    const float h = static_cast<float>(window.Rect.H);

    if (window.AlignRightX)
    {
        x = clientW - w + x;
    }
    else if (window.AlignCenterX)
    {
        x = (clientW - w) * 0.5f + x;
    }

    if (window.AlignRightY)
    {
        y = clientH - h + y;
    }
    else if (window.AlignCenterY)
    {
        y = (clientH - h) * 0.5f + y;
    }

    return FUiRectF
    {
        std::floor(x), std::floor(y), w, h
    };
}

const FUiControlDef* FUiRuntime::HitTestConnection(int32 x, int32 y, const RECT& clientRect) const
{
    if (!Ready) { return nullptr; }
    FUiRectF wr = BuildConnectionRect(clientRect);
    const float scale = wr.W / static_cast<float>(std::max(1, Connection.Rect.W));
    return HitTestControls(Connection, wr, x, y, scale, false);
}

const FUiControlDef* FUiRuntime::HitTestCharacterSelect(int32 x, int32 y, const RECT& clientRect) const
{
    if (!Ready || PickPerson.Name.empty()) { return nullptr; }
    return HitTestControls(PickPerson, BuildWindowRect(PickPerson, clientRect), x, y, 1.0f, true);
}

const FUiControlDef* FUiRuntime::HitTestModal(int32 x, int32 y, const RECT& clientRect) const
{
    if (!Ready || Modal == EUiModalDialog::None) { return nullptr; }
    const FUiWindowDef& window = ActiveModalWindow();
    if (window.Name.empty()) { return nullptr; }
    return HitTestControls(window, BuildWindowRect(window, clientRect), x, y, 1.0f, false);
}

bool FUiRuntime::IsEditControl(const FUiControlDef& control) const { return Common::EqualsNoCase(control.ClassId, "EDIT"); }
bool FUiRuntime::IsCheckControl(const FUiControlDef& control) const { return Common::EqualsNoCase(control.ClassId, "CHECKBOX") || control.Id == SferaUi::SavePasswordId; }
bool FUiRuntime::IsButtonControl(const FUiControlDef& control) const { return Common::EqualsNoCase(control.ClassId, "BUTTON"); }

int32 FUiRuntime::CharacterFocusForControl(int32 controlId) const
{
    if (SferaUi::IsCharacterAppearanceControl(controlId)) { return controlId; }

    if (controlId >= 7 && controlId <= 11) { return controlId + 5; }

    return 0;
}

bool FUiRuntime::PointInsidePickPersonWindow(int32 x, int32 y, const RECT& clientRect) const
{
    if (PickPerson.Name.empty()) { return false; }

    return FUiRuntimeInternals::Contains(BuildWindowRect(PickPerson, clientRect), x, y);
}

int32 FUiRuntime::CharacterSpinDeltaForPoint(const FUiControlDef& control, int32 x, int32 y, const RECT& clientRect) const
{
    FUiRectF wr = BuildWindowRect(PickPerson, clientRect);
    const float controlX = wr.X + static_cast<float>(control.Rect.X);
    const float controlY = wr.Y + static_cast<float>(control.Rect.Y);
    auto contains = [&](const FUiSubButtonDef& button)
    {
        if (button.W <= 0 || button.H <= 0) { return false; }

        const float bx = controlX + static_cast<float>(button.X);
        const float by = controlY + static_cast<float>(button.Y);
        return static_cast<float>(x) >= bx && static_cast<float>(y) >= by && static_cast<float>(x) < bx + static_cast<float>(button.W) && static_cast<float>(y) < by + static_cast<float>(button.H);
    };

    if (contains(control.RightButton)) { return -1; }

    if (contains(control.LeftButton)) { return 1; }

    const float width = static_cast<float>(std::max(37, control.Rect.W));
    return static_cast<float>(x) < controlX + width * 0.5f ? -1 : 1;
}

void FUiRuntime::ActivateControl(const FUiControlDef& control, FLogger* logger)
{
    Actions.LastControlId = control.Id;

    if (IsEditControl(control)) { Actions.FocusedControlId = control.Id; Actions.LastAction = control.Id == SferaUi::PasswordEditId || control.Password ? "focus_password" : "focus_login"; return; }

    if (IsCheckControl(control)) { Actions.SaveLogin = !Actions.SaveLogin; Actions.LastAction = Actions.SaveLogin ? "save_login_on" : "save_login_off"; return; }

    if (IsButtonControl(control))
    {
        if (control.Id == SferaUi::LoginButtonId)
        {
            Actions.LastAction = "login_requested";
        }
        else if (control.Id == SferaUi::CancelButtonId || control.Id == SferaUi::QuitButtonId || control.SendQuit)
        {
            Actions.LastAction = "quit_requested";
        }
        else if (control.Id == SferaUi::RegistrationButtonId)
        {
            Actions.LastAction = "registration_requested";
        }
        else
        {
            Actions.LastAction = "click_control_" + std::to_string(control.Id);
        }

        if (logger)
        {
            logger->Info("UI action: " + Actions.LastAction);
        }
    }
}

void FUiRuntime::ActivateCharacterControl(const FUiControlDef& control, FLogger* logger)
{
    Actions.LastControlId = control.Id;

    if (SferaUi::IsCharacterSlotRadio(control.Id))
    {
        SelectedSlot = Common::ClampIndexToCount(SferaUi::SlotFromRadioId(control.Id), Sfera::CharacterSlotCount);
        ActiveCharacterEditId = SferaUi::NameEditIdForSlot(SelectedSlot);
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
        Actions.LastAction = "character_slot_" + std::to_string(SelectedSlot);
    }
    else if (SferaUi::IsCharacterNameEdit(control.Id))
    {
        SelectedSlot = Common::ClampIndexToCount(SferaUi::SlotFromNameEditId(control.Id), Sfera::CharacterSlotCount);
        ActiveCharacterEditId = control.Id;
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
        Actions.LastAction = "character_edit_" + std::to_string(SelectedSlot);
    }
    else if (SferaUi::IsCharacterAppearanceControl(control.Id))
    {
        const int32 delta = CharacterSpinDelta < 0 ? -1 : 1;

        if (control.Id == SferaUi::CharacterGenderControlId)
        {
            Appearance.Gender = FUiRuntimeInternals::CycleIndex(Appearance.Gender, 2, delta);
            Appearance.Face = 0;
            Appearance.Hair = 0;
            Appearance.HairColor = 0;
            Appearance.Tattoo = 0;
        }
        else if (control.Id == SferaUi::CharacterFaceControlId)
        {
            Appearance.Face = FUiRuntimeInternals::CycleIndex(Appearance.Face, CharacterAppearanceOptionCount(SferaUi::CharacterFaceControlId), delta);
        }
        else if (control.Id == SferaUi::CharacterHairControlId)
        {
            Appearance.Hair = FUiRuntimeInternals::CycleIndex(Appearance.Hair, CharacterAppearanceOptionCount(SferaUi::CharacterHairControlId), delta);
        }
        else if (control.Id == SferaUi::CharacterHairColorControlId)
        {
            Appearance.HairColor = FUiRuntimeInternals::CycleIndex(Appearance.HairColor, CharacterAppearanceOptionCount(SferaUi::CharacterHairColorControlId), delta);
        }
        else if (control.Id == SferaUi::CharacterTattooControlId)
        {
            Appearance.Tattoo = FUiRuntimeInternals::CycleIndex(Appearance.Tattoo, CharacterAppearanceOptionCount(SferaUi::CharacterTattooControlId), delta);
        }

        ClampCharacterAppearance();
        SyncCharacterSelectControls();
        Actions.LastAction = "character_appearance_changed";
    }
    else if (control.Id == SferaUi::CharacterContinueButtonId)
    {
        if (SelectedCharacterPresent())
        {
            Actions.LastAction = "character_enter_requested";
        }
        else
        {
            ShowCreateConfirmation();
        }
    }
    else if (control.Id == SferaUi::CharacterExitButtonId || control.SendQuit)
    {
        ShowExitConfirmation();
    }
    else if (control.Id == SferaUi::CharacterDeleteButtonId)
    {
        ShowDeleteConfirmation();
    }
    else
    {
        Actions.LastAction = "character_click_" + std::to_string(control.Id);
    }

    if (logger)
    {
        logger->Info("UI action: " + Actions.LastAction);
    }
}

void FUiRuntime::ActivateModalControl(const FUiControlDef& control, FLogger* logger)
{
    Actions.LastControlId = control.Id;
    const EUiModalDialog current = Modal;

    if (IsEditControl(control)) { Actions.FocusedControlId = control.Id; Actions.LastAction.clear(); return; }

    if (current == EUiModalDialog::CharacterCreate)
    {
        if (control.Id == SferaUi::ModalButton1Id)
        {
            DismissModal();
            Actions.LastAction = "character_create_confirmed";
        }
        else if (control.Id == SferaUi::ModalButton2Id || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "character_create_cancelled";
        }
        else
        {
            Actions.LastAction.clear();
        }
    }
    else if (current == EUiModalDialog::CharacterDelete)
    {
        if (control.Id == SferaUi::DeleteConfirmButtonId && ModalEditMatchesSelectedCharacter())
        {
            DismissModal();
            Actions.LastAction = "character_delete_confirmed";
        }
        else if (control.Id == SferaUi::ModalButton1Id || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "character_delete_cancelled";
        }
        else
        {
            Actions.LastAction = "character_delete_name_required";
        }
    }
    else if (current == EUiModalDialog::CharacterExit)
    {
        if (control.Id == SferaUi::ModalButton1Id)
        {
            DismissModal();
            Actions.LastAction = "character_back_confirmed";
        }
        else if (control.Id == SferaUi::ModalButton2Id || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "character_back_cancelled";
        }
        else
        {
            Actions.LastAction.clear();
        }
    }
    else
    {
        DismissModal();
        Actions.LastAction = "modal_closed";
    }

    if (logger && !Actions.LastAction.empty())
    {
        logger->Info("UI action: " + Actions.LastAction);
    }
}

bool FUiRuntime::HandleInputFrame(const FInputSnapshot& input, const RECT& clientRect, FLogger* logger)
{
    if (!Ready) { return false; }

    bool changed = false;

    if (CurrentMode == EUiRuntimeMode::CharacterSelect)
    {
        SyncCharacterSelectControls();
    }

    const FUiControlDef* hovered = nullptr;

    if (Modal != EUiModalDialog::None)
    {
        hovered = HitTestModal(input.MouseX, input.MouseY, clientRect);
    }
    else
    {
        hovered = CurrentMode == EUiRuntimeMode::CharacterSelect ? HitTestCharacterSelect(input.MouseX, input.MouseY, clientRect) : HitTestConnection(input.MouseX, input.MouseY, clientRect);
    }

    const int32 newHover = hovered ? hovered->Id : 0;
    int32 spinHoverDirection = 0;

    if (hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && FUiRuntimeInternals::IsSpinButton(*hovered))
    {
        spinHoverDirection = CharacterSpinDeltaForPoint(*hovered, input.MouseX, input.MouseY, clientRect);
    }

    if (newHover != Actions.HoverControlId || spinHoverDirection != Actions.SpinHoverDirection)
    {
        Actions.HoverControlId = newHover;
        Actions.SpinHoverDirection = spinHoverDirection;
        changed = true;
    }

    if (CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && SceneRotateDragActive && input.LeftButton)
    {
        const int32 dx = input.MouseX - SceneRotateLastX;
        SceneRotateLastX = input.MouseX;

        if (dx != 0)
        {
            SceneAngle += static_cast<float>(dx) * 0.01f;
            changed = true;
        }
    }

    if (input.LeftPressed)
    {
        Actions.PressedControlId = newHover;
        Actions.SpinPressedDirection = 0;
        SceneRotateDragActive = false;

        if (hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && FUiRuntimeInternals::IsSpinButton(*hovered))
        {
            CharacterSpinDelta = CharacterSpinDeltaForPoint(*hovered, input.MouseX, input.MouseY, clientRect);
            Actions.SpinPressedDirection = CharacterSpinDelta;
            const int32 focus = CharacterFocusForControl(hovered->Id);

            if (focus != 0)
            {
                SceneCameraFocusId = focus;
            }
        }
        else if (!hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && !PointInsidePickPersonWindow(input.MouseX, input.MouseY, clientRect))
        {
            SceneRotateDragActive = true;
            SceneRotateLastX = input.MouseX;
        }

        changed = true;
    }

    if (input.LeftReleased)
    {
        const int32 pressed = Actions.PressedControlId;
        Actions.PressedControlId = 0;
        Actions.SpinPressedDirection = 0;
        SceneRotateDragActive = false;
        changed = true;

        if (hovered && hovered->Id != 0 && hovered->Id == pressed)
        {
            if (Modal != EUiModalDialog::None)
            {
                ActivateModalControl(*hovered, logger);
            }
            else if (CurrentMode == EUiRuntimeMode::CharacterSelect)
            {
                ActivateCharacterControl(*hovered, logger);
            }
            else
            {
                ActivateControl(*hovered, logger);
            }
        }
    }

    if (Modal != EUiModalDialog::None)
    {
        if (Modal == EUiModalDialog::CharacterDelete && Actions.FocusedControlId == SferaUi::DeleteConfirmEditId)
        {
            changed = FUiRuntimeInternals::ApplyUtf8TextEdit(ModalEditText, input, SferaUi::MaxDeleteConfirmChars) || changed;
        }

        if (input.EnterPressed)
        {
            const EUiModalDialog current = Modal;

            if (current == EUiModalDialog::CharacterDelete && !ModalEditMatchesSelectedCharacter())
            {
                Actions.LastAction = "character_delete_name_required";
            }
            else
            {
                DismissModal();
                Actions.LastAction = current == EUiModalDialog::CharacterCreate ? "character_create_confirmed" : current == EUiModalDialog::CharacterDelete ? "character_delete_confirmed" : "character_back_confirmed";
            }

            changed = true;
        }

        return changed;
    }

    if (CurrentMode == EUiRuntimeMode::Login && (Actions.FocusedControlId == SferaUi::LoginEditId || Actions.FocusedControlId == SferaUi::PasswordEditId))
    {
        std::string& target = Actions.FocusedControlId == SferaUi::PasswordEditId ? Actions.PasswordText : Actions.LoginText;

        const size_t limit = Actions.FocusedControlId == SferaUi::PasswordEditId ? SferaUi::MaxPasswordChars : SferaUi::MaxLoginChars;
        changed = FUiRuntimeInternals::ApplyUtf8TextEdit(target, input, limit) || changed;

        if (input.TabPressed)
        {
            Actions.FocusedControlId = Actions.FocusedControlId == SferaUi::LoginEditId ? SferaUi::PasswordEditId : SferaUi::LoginEditId;
            changed = true;
        }
    }

    if (CurrentMode == EUiRuntimeMode::CharacterSelect && SferaUi::IsCharacterNameEdit(ActiveCharacterEditId) && !CharacterActionLocked)
    {
        std::wstring& target = CharacterNameEdits[static_cast<size_t>(SferaUi::SlotFromNameEditId(ActiveCharacterEditId))];

        changed = FUiRuntimeInternals::ApplyWideTextEdit(target, input, SferaUi::MaxCharacterNameChars, FUiRuntimeInternals::IsCharacterNameChar) || changed;
    }

    if (CurrentMode == EUiRuntimeMode::Game)
    {
        changed = FUiRuntimeInternals::ApplyUtf8TextEdit(GameChat, input, SferaUi::MaxGameChatChars) || changed;
    }

    if (input.EnterPressed)
    {
        if (CurrentMode == EUiRuntimeMode::CharacterSelect && !SelectedCharacterPresent() && SelectedCharacterCanCreate())
        {
            ShowCreateConfirmation();
        }
        else if (CurrentMode == EUiRuntimeMode::Game)
        {
            Actions.LastAction = "game_chat_enter";
        }
        else
        {
            Actions.LastAction = CurrentMode == EUiRuntimeMode::CharacterSelect ? "character_enter_requested" : "login_requested";
        }

        changed = true;

        if (logger)
        {
            logger->Info("UI action: " + Actions.LastAction);
        }
    }

    return changed;
}

void FUiRuntime::SetMode(EUiRuntimeMode mode)
{
    CurrentMode = mode;
    Actions.HoverControlId = 0;
    Actions.PressedControlId = 0;
    Actions.LastAction.clear();
    Modal = EUiModalDialog::None;
    ModalText.clear();
    SceneRotateDragActive = false;
    Actions.SpinHoverDirection = 0;
    Actions.SpinPressedDirection = 0;

    if (CurrentMode == EUiRuntimeMode::CharacterSelect)
    {
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
    }
}

