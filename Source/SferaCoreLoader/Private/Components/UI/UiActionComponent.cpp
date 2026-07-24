#include "Components/UI/UiActionComponent.h"

namespace
{
bool ParseInt(std::string_view value, int32& result)
{
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size();
}

FClientUiCommand DecodeWindowCommand(std::string_view action, std::string_view prefix, EUiWindowOperation operation)
{
    if (!action.starts_with(prefix) || action.size() == prefix.size()) { return {}; }
    FClientUiCommand command;
    command.Action = EClientUiAction::WindowCommand;
    command.WindowName = std::string(action.substr(prefix.size()));
    command.WindowOperation = operation;
    return command;
}
}

FClientUiCommand FUiActionComponent::Decode(std::string_view action) const
{
    if (action == "quit_requested") { return {EClientUiAction::Quit}; }
    if (action == "save_login_on") { return {EClientUiAction::SaveLoginOn}; }
    if (action == "save_login_off") { return {EClientUiAction::SaveLoginOff}; }
    if (action == "login_requested") { return {EClientUiAction::Login}; }
    if (action == "registration_requested") { return {EClientUiAction::Registration}; }
    if (action == "character_enter_requested") { return {EClientUiAction::CharacterEnter}; }
    if (action == "character_create_confirmed") { return {EClientUiAction::CharacterCreateConfirmed}; }
    if (action == "character_delete_name_required") { return {EClientUiAction::CharacterDeleteNameRequired}; }
    if (action == "character_back_requested") { return {EClientUiAction::CharacterBackRequested}; }
    if (action == "character_back_confirmed") { return {EClientUiAction::CharacterBackConfirmed}; }
    if (action == "character_delete_requested") { return {EClientUiAction::CharacterDeleteRequested}; }
    if (action == "character_delete_confirmed") { return {EClientUiAction::CharacterDeleteConfirmed}; }
    if (action == "game_chat_submit") { return {EClientUiAction::GameChatSubmit}; }
    if (action == "game_leave_requested") { return {EClientUiAction::GameLeaveRequested}; }
    if (action == "game_leave_confirmed") { return {EClientUiAction::GameLeaveConfirmed}; }
    if (action == "character_create_dialog" || action == "character_delete_dialog" || action == "character_exit_dialog" || action == "character_create_cancelled" || action == "character_delete_cancelled" || action == "character_back_cancelled" || action == "game_leave_dialog" || action == "game_leave_cancelled" || action == "modal_closed") { return {EClientUiAction::CharacterDialogChanged}; }

    constexpr std::string_view slotPrefix = "character_slot_";
    if (action.starts_with(slotPrefix))
    {
        int32 slot = -1;
        return ParseInt(action.substr(slotPrefix.size()), slot) ? FClientUiCommand{EClientUiAction::CharacterSlotSelected, slot} : FClientUiCommand{};
    }

    if (FClientUiCommand command = DecodeWindowCommand(action, "game_window_toggle:", EUiWindowOperation::Toggle); command.Action != EClientUiAction::None) { return command; }
    if (FClientUiCommand command = DecodeWindowCommand(action, "game_window_open:", EUiWindowOperation::Open); command.Action != EClientUiAction::None) { return command; }
    if (FClientUiCommand command = DecodeWindowCommand(action, "game_window_close:", EUiWindowOperation::Close); command.Action != EClientUiAction::None) { return command; }

    constexpr std::string_view replacePrefix = "game_window_replace:";
    if (action.starts_with(replacePrefix))
    {
        const std::string_view value = action.substr(replacePrefix.size());
        const size_t separator = value.find(':');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) { return {}; }
        FClientUiCommand command;
        command.Action = EClientUiAction::WindowReplace;
        command.WindowName = std::string(value.substr(0, separator));
        command.SecondaryWindowName = std::string(value.substr(separator + 1));
        return command;
    }

    constexpr std::string_view controlPrefix = "game_control:";
    if (action.starts_with(controlPrefix))
    {
        const std::string_view value = action.substr(controlPrefix.size());
        const size_t separator = value.rfind(':');
        int32 controlId = -1;
        if (separator == std::string_view::npos || !ParseInt(value.substr(separator + 1), controlId)) { return {}; }
        FClientUiCommand command;
        command.Action = EClientUiAction::GameControl;
        command.Value = controlId;
        command.WindowName = std::string(value.substr(0, separator));
        return command;
    }

    constexpr std::string_view helpPrefix = "game_help:";
    if (action.starts_with(helpPrefix))
    {
        const std::string_view value = action.substr(helpPrefix.size());
        const size_t separator = value.find(':');
        FClientUiCommand command;
        command.Action = EClientUiAction::GameHelp;
        command.WindowName = std::string(value.substr(0, separator));
        if (separator != std::string_view::npos) { command.Payload = std::string(value.substr(separator + 1)); }
        return command;
    }

    constexpr std::array<std::string_view, 4> legacyWindows{"inventory", "trade", "bank", "chat"};
    for (std::string_view window : legacyWindows)
    {
        if (!action.starts_with(window) || action.size() <= window.size() || action[window.size()] != '_') { continue; }
        const std::string_view operation = action.substr(window.size() + 1);
        if (operation == "toggle") { return {EClientUiAction::WindowCommand, 0, std::string(window), EUiWindowOperation::Toggle}; }
        if (operation == "open") { return {EClientUiAction::WindowCommand, 0, std::string(window), EUiWindowOperation::Open}; }
        if (operation == "close") { return {EClientUiAction::WindowCommand, 0, std::string(window), EUiWindowOperation::Close}; }
    }
    return {};
}
