#include "UI/UiRuntimeInput.h"
#include "UI/UiRuntime.h"
#include "UI/UiRuntimeInternals.h"
#include "Common/SferaGameConstants.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"
#include "Common/ValueUtils.h"

namespace
{
uint64 GameUiKey(std::string_view window, int32 controlId)
{
    uint32 hash = 2166136261u;
    for (unsigned char ch : window)
    {
        const unsigned char lower = static_cast<unsigned char>(std::tolower(ch));
        hash = (hash ^ static_cast<uint32>(lower)) * 16777619u;
    }
    return (static_cast<uint64>(hash) << 32) | static_cast<uint32>(controlId);
}

int32 SpinDirectionForPoint(const FUiControlDef& control, const FUiRectF& windowRect, int32 x, int32 y)
{
    const float controlX = windowRect.X + static_cast<float>(control.Rect.X);
    const float controlY = windowRect.Y + static_cast<float>(control.Rect.Y);
    const auto contains = [&](const FUiSubButtonDef& button)
    {
        if (button.W <= 0 || button.H <= 0) { return false; }
        const FUiRectF rect{controlX + static_cast<float>(button.X), controlY + static_cast<float>(button.Y), static_cast<float>(button.W), static_cast<float>(button.H)};
        return FUiRuntimeInternals::Contains(rect, x, y);
    };
    if (contains(control.RightButton)) { return -1; }
    if (contains(control.LeftButton)) { return 1; }
    const float width = static_cast<float>(std::max(37, control.Rect.W));
    return static_cast<float>(x) < controlX + width * 0.5f ? -1 : 1;
}

bool SpinContainsPoint(const FUiControlDef& control, const FUiRectF& windowRect, int32 x, int32 y)
{
    const float controlX = windowRect.X + static_cast<float>(control.Rect.X);
    const float controlY = windowRect.Y + static_cast<float>(control.Rect.Y);
    const auto contains = [&](const FUiSubButtonDef& button)
    {
        if (button.W <= 0 || button.H <= 0) { return false; }
        return FUiRuntimeInternals::Contains(FUiRectF{controlX + static_cast<float>(button.X), controlY + static_cast<float>(button.Y), static_cast<float>(button.W), static_cast<float>(button.H)}, x, y);
    };
    if (contains(control.RightButton) || contains(control.LeftButton)) { return true; }
    if (control.Rect.W <= 0 || control.Rect.H <= 0) { return false; }
    return FUiRuntimeInternals::Contains(FUiRectF{controlX, controlY, static_cast<float>(control.Rect.W), static_cast<float>(control.Rect.H)}, x, y);
}

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


#define Actions Runtime.Actions
#define ActiveCharacterEditId Runtime.ActiveCharacterEditId
#define Appearance Runtime.Appearance
#define Bootstrap Runtime.Bootstrap
#define CharacterActionLocked Runtime.CharacterActionLocked
#define CharacterNameEdits Runtime.CharacterNameEdits
#define CharacterSpinDelta Runtime.CharacterSpinDelta
#define Connection Runtime.Connection
#define CurrentMode Runtime.CurrentMode
#define GameChat Runtime.GameChat
#define GameChatFocused Runtime.GameChatFocused
#define GameControlChecks Runtime.GameControlChecks
#define GameDragOffsetX Runtime.GameDragOffsetX
#define GameDragOffsetY Runtime.GameDragOffsetY
#define GameEditValues Runtime.GameEditValues
#define GameDragWindowIndex Runtime.GameDragWindowIndex
#define GameWindowDefs Runtime.GameWindowDefs
#define GameWindowOrder Runtime.GameWindowOrder
#define GameWindowPositionOverrides Runtime.GameWindowPositionOverrides
#define GameWindowPositions Runtime.GameWindowPositions
#define GameWindowVisible Runtime.GameWindowVisible
#define Modal Runtime.Modal
#define ModalClosing Runtime.ModalClosing
#define ModalEditText Runtime.ModalEditText
#define PickPerson Runtime.PickPerson
#define Ready Runtime.Ready
#define SceneAngle Runtime.SceneAngle
#define SceneCameraFocusId Runtime.SceneCameraFocusId
#define SceneRotateDragActive Runtime.SceneRotateDragActive
#define SceneRotateLastX Runtime.SceneRotateLastX
#define SelectedSlot Runtime.SelectedSlot
#define ActiveModalWindow Runtime.ActiveModalWindow
#define BuildAnimatedModalRect Runtime.BuildAnimatedModalRect
#define ClearModalState Runtime.ClearModalState
#define CharacterAppearanceOptionCount Runtime.CharacterRuntime.CharacterAppearanceOptionCount
#define ClampCharacterAppearance Runtime.CharacterRuntime.ClampCharacterAppearance
#define DismissModal Runtime.DismissModal
#define ModalEditMatchesSelectedCharacter Runtime.CharacterRuntime.ModalEditMatchesSelectedCharacter
#define SelectedCharacterCanCreate Runtime.CharacterRuntime.SelectedCharacterCanCreate
#define SelectedCharacterPresent Runtime.CharacterRuntime.SelectedCharacterPresent
#define ShowCreateConfirmation Runtime.ShowCreateConfirmation
#define ShowDeleteConfirmation Runtime.ShowDeleteConfirmation
#define ShowExitConfirmation Runtime.ShowExitConfirmation
#define ShowGameExitConfirmation Runtime.ShowGameExitConfirmation
#define SyncCharacterSelectControls Runtime.CharacterRuntime.SyncCharacterSelectControls

FUiRuntimeInput::FUiRuntimeInput(FUiRuntime& runtime) : Runtime(runtime) {}

FUiRectF FUiRuntimeInput::BuildDesignRect(const RECT& clientRect) const
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

FUiRectF FUiRuntimeInput::BuildConnectionRect(const RECT& clientRect) const
{
    FUiRectF design = BuildDesignRect(clientRect);
    const float w = static_cast<float>(Connection.Rect.W);
    const float h = static_cast<float>(Connection.Rect.H);
    return FUiRectF
    {
        std::floor(design.X + (design.W - w) * 0.5f), std::floor(design.Y + (design.H - h) * 0.5f), w, h
    };
}

FUiRectF FUiRuntimeInput::BuildWindowRect(const FUiWindowDef& window, const RECT& clientRect) const
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

const FUiControlDef* FUiRuntimeInput::HitTestConnection(int32 x, int32 y, const RECT& clientRect) const
{
    if (!Ready) { return nullptr; }
    FUiRectF wr = BuildConnectionRect(clientRect);
    const float scale = wr.W / static_cast<float>(std::max(1, Connection.Rect.W));
    return HitTestControls(Connection, wr, x, y, scale, false);
}

const FUiControlDef* FUiRuntimeInput::HitTestCharacterSelect(int32 x, int32 y, const RECT& clientRect) const
{
    if (!Ready || PickPerson.Name.empty()) { return nullptr; }
    return HitTestControls(PickPerson, BuildWindowRect(PickPerson, clientRect), x, y, 1.0f, true);
}

const FUiControlDef* FUiRuntimeInput::HitTestModal(int32 x, int32 y, const RECT& clientRect) const
{
    if (!Ready || Modal == EUiModalDialog::None) { return nullptr; }
    const FUiWindowDef& window = ActiveModalWindow();
    if (window.Name.empty()) { return nullptr; }
    return ModalClosing ? nullptr : HitTestControls(window, BuildAnimatedModalRect(clientRect), x, y, 1.0f, false);
}

int32 FUiRuntimeInput::HitTestGameWindow(int32 x, int32 y, const RECT& clientRect) const
{
    for (auto it = GameWindowOrder.rbegin(); it != GameWindowOrder.rend(); ++it)
    {
        const size_t index = *it;
        if (index >= GameWindowDefs.size() || index >= GameWindowVisible.size() || !GameWindowVisible[index]) { continue; }
        if (FUiRuntimeInternals::Contains(Runtime.BuildGameWindowRect(index, clientRect), x, y)) { return static_cast<int32>(index); }
    }
    return -1;
}

const FUiControlDef* FUiRuntimeInput::HitTestGame(int32 x, int32 y, const RECT& clientRect, int32& windowIndex) const
{
    windowIndex = HitTestGameWindow(x, y, clientRect);
    if (windowIndex < 0) { return nullptr; }
    const size_t index = static_cast<size_t>(windowIndex);
    const FUiWindowDef& window = GameWindowDefs[index];
    const FUiRectF windowRect = Runtime.BuildGameWindowRect(index, clientRect);
    for (auto it = window.Controls.rbegin(); it != window.Controls.rend(); ++it)
    {
        const FUiControlDef& control = *it;
        const bool staticDisabled = control.Disabled && !Runtime.OverridesStaticDisabled(window.Name, control.Id);
        if (control.Hidden || staticDisabled || !FUiRuntimeInternals::IsMouseControlClass(control.ClassId) || Runtime.IsGameControlHidden(window.Name, control.Id) || Runtime.IsGameControlDisabled(window.Name, control.Id)) { continue; }
        if (FUiRuntimeInternals::IsSpinButton(control))
        {
            if (SpinContainsPoint(control, windowRect, x, y)) { return &control; }
            continue;
        }
        if (FUiRuntimeInternals::Contains(FUiRuntimeInternals::ControlRectInWindow(windowRect, control, 1.0f), x, y)) { return &control; }
    }
    return nullptr;
}

bool FUiRuntimeInput::IsEditControl(const FUiControlDef& control) const { return control.Class == EUiControlClass::Edit; }
bool FUiRuntimeInput::IsCheckControl(const FUiControlDef& control) const { return control.Class == EUiControlClass::CheckBox || control.Id == SferaUi::SavePasswordId; }
bool FUiRuntimeInput::IsButtonControl(const FUiControlDef& control) const { return control.Class == EUiControlClass::Button; }

int32 FUiRuntimeInput::CharacterFocusForControl(int32 controlId) const
{
    if (SferaUi::IsCharacterAppearanceControl(controlId)) { return controlId; }

    if (controlId >= 7 && controlId <= 11) { return controlId + 5; }

    return 0;
}

bool FUiRuntimeInput::PointInsidePickPersonWindow(int32 x, int32 y, const RECT& clientRect) const
{
    if (PickPerson.Name.empty()) { return false; }

    return FUiRuntimeInternals::Contains(BuildWindowRect(PickPerson, clientRect), x, y);
}

int32 FUiRuntimeInput::CharacterSpinDeltaForPoint(const FUiControlDef& control, int32 x, int32 y, const RECT& clientRect) const
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

void FUiRuntimeInput::ActivateControl(const FUiControlDef& control, FLogger* logger)
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

void FUiRuntimeInput::ActivateCharacterControl(const FUiControlDef& control, FLogger* logger)
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

std::string FUiRuntimeInput::GameControlAction(const FUiWindowDef& window, const FUiControlDef& control) const
{
    return GameUiRoutes.Route({window.Name, control.Id, control.SendQuit, control.SendHelp, control.WindowHelp});
}

void FUiRuntimeInput::ActivateGameControl(int32 windowIndex, const FUiControlDef& control, FLogger* logger)
{
    if (windowIndex < 0 || static_cast<size_t>(windowIndex) >= GameWindowDefs.size()) { return; }
    const FUiWindowDef& window = GameWindowDefs[static_cast<size_t>(windowIndex)];
    if (Runtime.IsGameControlDisabled(window.Name, control.Id) || Runtime.IsGameControlHidden(window.Name, control.Id)) { return; }
    Actions.LastControlId = control.Id;
    Actions.FocusedWindowIndex = windowIndex;
    if (control.Class == EUiControlClass::SpinButton) { Runtime.AdjustGameControlValue(window.Name, control, Actions.SpinPressedDirection == 0 ? 1 : Actions.SpinPressedDirection); }
    Runtime.HandleLocalGameControl(window.Name, control.Id);
    if (IsEditControl(control))
    {
        Actions.FocusedControlId = control.Id;
        GameChatFocused = Common::ToLower(window.Name).starts_with("chat");
        Actions.LastAction = "game_control:" + window.Name + ":" + std::to_string(control.Id);
    }
    else
    {
        if (control.Class == EUiControlClass::CheckBox || control.Class == EUiControlClass::RadioButton) { Runtime.ToggleGameControlChecked(window.Name, control); }
        Actions.LastAction = GameControlAction(window, control);
    }
    if (logger && !Actions.LastAction.empty()) { logger->Info("UI action: " + Actions.LastAction); }
}

void FUiRuntimeInput::HandleGameHotkeys(const FInputSnapshot& input, FLogger* logger, bool& changed)
{
    if (input.WasKeyPressed(VK_ESCAPE))
    {
        if (Runtime.IsGameTextInputFocused())
        {
            GameChatFocused = false;
            Actions.FocusedWindowIndex = -1;
            Actions.FocusedControlId = 0;
        }
        else
        {
            for (auto it = GameWindowOrder.rbegin(); it != GameWindowOrder.rend(); ++it)
            {
                const size_t index = *it;
                if (index >= GameWindowDefs.size() || index >= GameWindowVisible.size() || !GameWindowVisible[index] || !GameWindowDefs[index].EscapeHandle) { continue; }
                Actions.LastAction = "game_window_close:" + GameWindowDefs[index].Name;
                changed = true;
                return;
            }
            Actions.LastAction = "game_window_toggle:options";
        }
        changed = true;
        return;
    }
    if (Runtime.IsGameTextInputFocused()) { return; }
    const std::array<std::pair<int32, std::string_view>, 7> hotkeys{{{'I', "inventory"}, {'P', "puppet"}, {'C', "statinfo"}, {'K', "hotkeys"}, {'M', "bigmap"}, {'J', "journal_mini"}, {'B', "mantra_book"}}};
    for (const auto& [key, window] : hotkeys)
    {
        if (!input.WasKeyPressed(key)) { continue; }
        Actions.LastAction = "game_window_toggle:" + std::string(window);
        changed = true;
        if (logger) { logger->Info("UI action: " + Actions.LastAction); }
        return;
    }
}

void FUiRuntimeInput::ActivateModalControl(const FUiControlDef& control, FLogger* logger)
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
    else if (current == EUiModalDialog::GameExit)
    {
        if (control.Id == SferaUi::ModalButton1Id)
        {
            DismissModal();
            Actions.LastAction = "game_leave_confirmed";
        }
        else if (control.Id == SferaUi::ModalButton2Id || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "game_leave_cancelled";
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

bool FUiRuntimeInput::HandleInputFrame(const FInputSnapshot& input, const RECT& clientRect, FLogger* logger)
{
    if (!Ready) { return false; }

    bool changed = false;
    if (CurrentMode == EUiRuntimeMode::CharacterSelect) { SyncCharacterSelectControls(); }

    int32 hoveredWindow = -1;
    const FUiControlDef* hovered = nullptr;
    if (Modal != EUiModalDialog::None)
    {
        hovered = HitTestModal(input.MouseX, input.MouseY, clientRect);
    }
    else if (CurrentMode == EUiRuntimeMode::CharacterSelect)
    {
        hovered = HitTestCharacterSelect(input.MouseX, input.MouseY, clientRect);
    }
    else if (CurrentMode == EUiRuntimeMode::Game)
    {
        hovered = HitTestGame(input.MouseX, input.MouseY, clientRect, hoveredWindow);
    }
    else
    {
        hovered = HitTestConnection(input.MouseX, input.MouseY, clientRect);
    }

    const int32 newHover = hovered ? hovered->Id : 0;
    const int32 newHoverWindow = CurrentMode == EUiRuntimeMode::Game && Modal == EUiModalDialog::None ? hoveredWindow : -1;
    int32 spinHoverDirection = 0;
    if (hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && FUiRuntimeInternals::IsSpinButton(*hovered)) { spinHoverDirection = CharacterSpinDeltaForPoint(*hovered, input.MouseX, input.MouseY, clientRect); }
    else if (hovered && CurrentMode == EUiRuntimeMode::Game && FUiRuntimeInternals::IsSpinButton(*hovered) && hoveredWindow >= 0)
    {
        const FUiRectF wr = Runtime.BuildGameWindowRect(static_cast<size_t>(hoveredWindow), clientRect);
        spinHoverDirection = SpinDirectionForPoint(*hovered, wr, input.MouseX, input.MouseY);
    }
    if (newHover != Actions.HoverControlId || newHoverWindow != Actions.HoverWindowIndex || spinHoverDirection != Actions.SpinHoverDirection)
    {
        Actions.HoverControlId = newHover;
        Actions.HoverWindowIndex = newHoverWindow;
        Actions.SpinHoverDirection = spinHoverDirection;
        changed = true;
    }

    if (CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && SceneRotateDragActive && input.LeftButton)
    {
        const int32 dx = input.MouseX - SceneRotateLastX;
        SceneRotateLastX = input.MouseX;
        if (dx != 0) { SceneAngle += static_cast<float>(dx) * 0.01f; changed = true; }
    }

    if (CurrentMode == EUiRuntimeMode::Game && Modal == EUiModalDialog::None && GameDragWindowIndex >= 0 && input.LeftButton)
    {
        const size_t index = static_cast<size_t>(GameDragWindowIndex);
        if (index < GameWindowDefs.size())
        {
            const int32 clientW = std::max(1, static_cast<int32>(clientRect.right - clientRect.left));
            const int32 clientH = std::max(1, static_cast<int32>(clientRect.bottom - clientRect.top));
            const int32 width = std::max(1, GameWindowDefs[index].Rect.W);
            const int32 height = std::max(1, GameWindowDefs[index].Rect.H);
            GameWindowPositions[index].X = std::clamp(input.MouseX - GameDragOffsetX, -width + 24, clientW - 24);
            GameWindowPositions[index].Y = std::clamp(input.MouseY - GameDragOffsetY, 0, clientH - 24);
            GameWindowPositionOverrides[index] = true;
            changed = true;
        }
    }

    if (CurrentMode == EUiRuntimeMode::Game && SliderDragWindow >= 0 && SliderDragControl != 0 && input.LeftButton)
    {
        const size_t index = static_cast<size_t>(SliderDragWindow);
        if (index < GameWindowDefs.size())
        {
            const FUiWindowDef& window = GameWindowDefs[index];
            auto controlIt = std::find_if(window.Controls.begin(), window.Controls.end(), [&](const FUiControlDef& control) { return control.Id == SliderDragControl; });
            if (controlIt != window.Controls.end())
            {
                const FUiRectF wr = Runtime.BuildGameWindowRect(index, clientRect);
                const float local = static_cast<float>(input.MouseX) - wr.X - static_cast<float>(controlIt->Rect.X);
                const float ratio = std::clamp(local / static_cast<float>(std::max(1, controlIt->Rect.W)), 0.0f, 1.0f);
                Runtime.SetGameControlValue(window.Name, *controlIt, static_cast<float>(controlIt->RangeMin) + ratio * static_cast<float>(controlIt->RangeMax - controlIt->RangeMin));
                changed = true;
            }
        }
    }

    if (input.LeftPressed)
    {
        Actions.PressedControlId = newHover;
        Actions.PressedWindowIndex = newHoverWindow;
        Actions.SpinPressedDirection = 0;
        SceneRotateDragActive = false;
        GameDragWindowIndex = -1;
        SliderDragWindow = -1;
        SliderDragControl = 0;

        if (CurrentMode == EUiRuntimeMode::Game && Modal == EUiModalDialog::None && hoveredWindow >= 0)
        {
            const size_t index = static_cast<size_t>(hoveredWindow);
            Runtime.BringGameWindowToFront(index);
            if (hovered && Common::EqualsNoCase(hovered->ClassId, "SCROLL_BAR"))
            {
                SliderDragWindow = hoveredWindow;
                SliderDragControl = hovered->Id;
                const FUiRectF wr = Runtime.BuildGameWindowRect(index, clientRect);
                const float local = static_cast<float>(input.MouseX) - wr.X - static_cast<float>(hovered->Rect.X);
                const float ratio = std::clamp(local / static_cast<float>(std::max(1, hovered->Rect.W)), 0.0f, 1.0f);
                Runtime.SetGameControlValue(GameWindowDefs[index].Name, *hovered, static_cast<float>(hovered->RangeMin) + ratio * static_cast<float>(hovered->RangeMax - hovered->RangeMin));
            }
            if (hovered && FUiRuntimeInternals::IsSpinButton(*hovered)) { Actions.SpinPressedDirection = spinHoverDirection == 0 ? 1 : spinHoverDirection; }
            if (!hovered || !IsEditControl(*hovered))
            {
                GameChatFocused = false;
                Actions.FocusedWindowIndex = -1;
                Actions.FocusedControlId = 0;
            }
            if (!hovered && index < GameWindowDefs.size() && GameWindowDefs[index].CanDragDrop)
            {
                const FUiRectF windowRect = Runtime.BuildGameWindowRect(index, clientRect);
                FUiRectF titleRect{windowRect.X, windowRect.Y, windowRect.W, std::min(28.0f, windowRect.H)};
                const FUiRect& title = GameWindowDefs[index].TitleRect;
                if (title.W > title.X && title.H > title.Y) { titleRect = {windowRect.X + static_cast<float>(title.X), windowRect.Y + static_cast<float>(title.Y), static_cast<float>(title.W - title.X), static_cast<float>(title.H - title.Y)}; }
                if (FUiRuntimeInternals::Contains(titleRect, input.MouseX, input.MouseY))
                {
                    GameDragWindowIndex = hoveredWindow;
                    GameWindowPositions[index] = {static_cast<int32>(windowRect.X), static_cast<int32>(windowRect.Y)};
                    GameWindowPositionOverrides[index] = true;
                    GameDragOffsetX = input.MouseX - GameWindowPositions[index].X;
                    GameDragOffsetY = input.MouseY - GameWindowPositions[index].Y;
                }
            }
        }
        else if (hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && FUiRuntimeInternals::IsSpinButton(*hovered))
        {
            CharacterSpinDelta = CharacterSpinDeltaForPoint(*hovered, input.MouseX, input.MouseY, clientRect);
            Actions.SpinPressedDirection = CharacterSpinDelta;
            const int32 focus = CharacterFocusForControl(hovered->Id);
            if (focus != 0) { SceneCameraFocusId = focus; }
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
        const int32 pressedWindow = Actions.PressedWindowIndex;
        const int32 pressedDirection = Actions.SpinPressedDirection;
        Actions.PressedControlId = 0;
        Actions.PressedWindowIndex = -1;
        Actions.SpinPressedDirection = pressedDirection;
        SceneRotateDragActive = false;
        GameDragWindowIndex = -1;
        SliderDragWindow = -1;
        SliderDragControl = 0;
        changed = true;
        if (hovered && hovered->Id != 0 && hovered->Id == pressed && (CurrentMode != EUiRuntimeMode::Game || hoveredWindow == pressedWindow))
        {
            if (Modal != EUiModalDialog::None) { ActivateModalControl(*hovered, logger); }
            else if (CurrentMode == EUiRuntimeMode::CharacterSelect) { ActivateCharacterControl(*hovered, logger); }
            else if (CurrentMode == EUiRuntimeMode::Game)
            {
                if (hoveredWindow >= 0 && Common::EqualsNoCase(GameWindowDefs[static_cast<size_t>(hoveredWindow)].Name, "journal_mini") && hovered->Id == 9 && Runtime.JournalEntryCount() > 0)
                {
                    const FUiRectF wr = Runtime.BuildGameWindowRect(static_cast<size_t>(hoveredWindow), clientRect);
                    const float localY = static_cast<float>(input.MouseY) - wr.Y - static_cast<float>(hovered->Rect.Y);
                    const float ratio = std::clamp(localY / static_cast<float>(std::max(1, hovered->Rect.H)), 0.0f, 0.9999f);
                    Runtime.SelectJournalEntry(static_cast<int32>(ratio * static_cast<float>(Runtime.JournalEntryCount())));
                }
                ActivateGameControl(hoveredWindow, *hovered, logger);
            }
            else { ActivateControl(*hovered, logger); }
        }
        Actions.SpinPressedDirection = 0;
    }

    if (Modal != EUiModalDialog::None)
    {
        if (Modal == EUiModalDialog::CharacterDelete && Actions.FocusedControlId == SferaUi::DeleteConfirmEditId) { changed = FUiRuntimeInternals::ApplyUtf8TextEdit(ModalEditText, input, SferaUi::MaxDeleteConfirmChars) || changed; }
        if (input.WasKeyPressed(VK_ESCAPE))
        {
            const bool gameExit = Modal == EUiModalDialog::GameExit;
            DismissModal();
            Actions.LastAction = gameExit ? "game_leave_cancelled" : "modal_closed";
            changed = true;
        }
        else if (input.EnterPressed)
        {
            const EUiModalDialog current = Modal;
            if (current == EUiModalDialog::CharacterDelete && !ModalEditMatchesSelectedCharacter()) { Actions.LastAction = "character_delete_name_required"; }
            else
            {
                DismissModal();
                if (current == EUiModalDialog::CharacterCreate) { Actions.LastAction = "character_create_confirmed"; }
                else if (current == EUiModalDialog::CharacterDelete) { Actions.LastAction = "character_delete_confirmed"; }
                else if (current == EUiModalDialog::GameExit) { Actions.LastAction = "game_leave_confirmed"; }
                else { Actions.LastAction = "character_back_confirmed"; }
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
        if (input.TabPressed) { Actions.FocusedControlId = Actions.FocusedControlId == SferaUi::LoginEditId ? SferaUi::PasswordEditId : SferaUi::LoginEditId; changed = true; }
    }

    if (CurrentMode == EUiRuntimeMode::CharacterSelect && SferaUi::IsCharacterNameEdit(ActiveCharacterEditId) && !CharacterActionLocked)
    {
        std::wstring& target = CharacterNameEdits[static_cast<size_t>(SferaUi::SlotFromNameEditId(ActiveCharacterEditId))];
        changed = FUiRuntimeInternals::ApplyWideTextEdit(target, input, SferaUi::MaxCharacterNameChars, FUiRuntimeInternals::IsCharacterNameChar) || changed;
    }

    if (CurrentMode == EUiRuntimeMode::Game)
    {
        HandleGameHotkeys(input, logger, changed);
        const FUiControlDef* focusedEdit = nullptr;
        const FUiWindowDef* focusedWindow = nullptr;
        if (Actions.FocusedWindowIndex >= 0 && static_cast<size_t>(Actions.FocusedWindowIndex) < GameWindowDefs.size())
        {
            focusedWindow = &GameWindowDefs[static_cast<size_t>(Actions.FocusedWindowIndex)];
            auto it = std::find_if(focusedWindow->Controls.begin(), focusedWindow->Controls.end(), [&](const FUiControlDef& control) { return control.Id == Actions.FocusedControlId && IsEditControl(control); });
            if (it != focusedWindow->Controls.end()) { focusedEdit = &*it; }
        }
        if (focusedEdit && focusedWindow)
        {
            const size_t maxChars = focusedEdit->MaxSymbols > 0 ? static_cast<size_t>(focusedEdit->MaxSymbols) : SferaUi::MaxGameChatChars;
            const uint64 key = GameUiKey(focusedWindow->Name, focusedEdit->Id);
            std::string& text = GameChatFocused ? GameChat : GameEditValues[key];
            changed = FUiRuntimeInternals::ApplyUtf8TextEdit(text, input, maxChars) || changed;
        }
        if (input.EnterPressed)
        {
            if (!focusedEdit || !focusedWindow)
            {
                GameChatFocused = true;
                Runtime.SetGameWindowVisible("chat_st2", true);
                const std::optional<size_t> chatIndex = Runtime.FindGameWindowIndex("chat_st2");
                if (chatIndex)
                {
                    Actions.FocusedWindowIndex = static_cast<int32>(*chatIndex);
                    const auto edit = std::find_if(GameWindowDefs[*chatIndex].Controls.begin(), GameWindowDefs[*chatIndex].Controls.end(), [&](const FUiControlDef& control) { return IsEditControl(control); });
                    Actions.FocusedControlId = edit == GameWindowDefs[*chatIndex].Controls.end() ? 0 : edit->Id;
                }
                Actions.LastAction = "game_window_open:chat_st2";
            }
            else if (GameChatFocused && GameChat.empty())
            {
                GameChatFocused = false;
                Actions.FocusedControlId = 0;
                Actions.FocusedWindowIndex = -1;
            }
            else if (GameChatFocused)
            {
                Actions.LastAction = "game_chat_submit";
            }
            else
            {
                Actions.LastAction = "game_control:" + focusedWindow->Name + ":" + std::to_string(focusedEdit->Id);
            }
            changed = true;
        }
        return changed;
    }

    if (input.EnterPressed)
    {
        if (CurrentMode == EUiRuntimeMode::CharacterSelect && !SelectedCharacterPresent() && SelectedCharacterCanCreate()) { ShowCreateConfirmation(); }
        else { Actions.LastAction = CurrentMode == EUiRuntimeMode::CharacterSelect ? "character_enter_requested" : "login_requested"; }
        changed = true;
        if (logger) { logger->Info("UI action: " + Actions.LastAction); }
    }
    return changed;
}

void FUiRuntimeInput::SetMode(EUiRuntimeMode mode)
{
    CurrentMode = mode;
    Actions.HoverControlId = 0;
    Actions.PressedControlId = 0;
    Actions.HoverWindowIndex = -1;
    Actions.PressedWindowIndex = -1;
    Actions.FocusedWindowIndex = -1;
    Actions.LastAction.clear();
    ClearModalState();
    SceneRotateDragActive = false;
    Actions.SpinHoverDirection = 0;
    Actions.SpinPressedDirection = 0;
    GameChatFocused = false;
    GameDragWindowIndex = -1;
    SliderDragWindow = -1;
    SliderDragControl = 0;

    if (CurrentMode == EUiRuntimeMode::CharacterSelect)
    {
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
    }
    else if (CurrentMode == EUiRuntimeMode::Game)
    {
        Runtime.ResetGameWindows();
    }
}

