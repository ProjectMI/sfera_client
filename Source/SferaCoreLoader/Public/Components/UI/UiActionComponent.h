#pragma once
#include "Components/UI/UiWindowStateComponent.h"
#include "Core/Types.h"

enum class EClientUiAction
{
    None,
    Quit,
    SaveLoginOn,
    SaveLoginOff,
    Login,
    Registration,
    CharacterSlotSelected,
    CharacterEnter,
    CharacterCreateConfirmed,
    CharacterDialogChanged,
    CharacterDeleteNameRequired,
    CharacterBackRequested,
    CharacterBackConfirmed,
    CharacterDeleteRequested,
    CharacterDeleteConfirmed,
    WindowCommand,
    WindowReplace,
    GameControl,
    GameHelp,
    GameChatSubmit,
    GameLeaveRequested,
    GameLeaveConfirmed
};

struct FClientUiCommand
{
    EClientUiAction Action = EClientUiAction::None;
    int32 Value = -1;
    std::string WindowName;
    EUiWindowOperation WindowOperation = EUiWindowOperation::Toggle;
    std::string Payload;
    std::string SecondaryWindowName;
};

class FUiActionComponent
{
public:
    FClientUiCommand Decode(std::string_view action) const;
};
