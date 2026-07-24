#include "Components/UI/GameUiRoutingComponent.h"
#include "Common/StringUtils.h"

namespace
{
std::string WindowAction(std::string_view operation, std::string_view window)
{
    return "game_window_" + std::string(operation) + ":" + std::string(window);
}

std::string ReplaceWindow(std::string_view source, std::string_view target)
{
    return "game_window_replace:" + std::string(source) + ":" + std::string(target);
}
}

std::string FGameUiRoutingComponent::Route(const FGameUiControlRequest& request) const
{
    const std::string name = Common::ToLower(std::string(request.WindowName));
    if (name == "system_left")
    {
        static constexpr std::array<std::string_view, 7> targets{"", "statinfo", "puppet", "inventory", "hotkeys", "npc_trade", "options"};
        if (request.ControlId >= 1 && request.ControlId <= 6) { return WindowAction("toggle", targets[static_cast<size_t>(request.ControlId)]); }
        if (request.ControlId == 7) { return WindowAction("toggle", "WebShopWindow"); }
        if (request.ControlId == 8) { return ReplaceWindow("system_left", "system_leftmin"); }
    }
    if (name == "system_leftmin" && request.ControlId == 1) { return ReplaceWindow("system_leftmin", "system_left"); }
    if (name == "system_right")
    {
        if (request.ControlId == 1) { return ReplaceWindow("system_right", "system_rightmin"); }
        static constexpr std::array<std::pair<int32, std::string_view>, 6> targets{{{4, "minimap"}, {5, "bigmap"}, {7, "chat_st2"}, {8, "alchemic_book"}, {9, "mantra_book"}, {10, "journal_mini"}}};
        for (const auto& [controlId, target] : targets) { if (request.ControlId == controlId) { return WindowAction("toggle", target); } }
    }
    if (name == "system_rightmin" && request.ControlId == 1) { return ReplaceWindow("system_rightmin", "system_right"); }
    if (name == "options")
    {
        static constexpr std::array<std::pair<int32, std::string_view>, 5> targets{{{3, "gfx_options"}, {4, "sound_options"}, {5, "control_options"}, {6, "interface_options"}, {7, "authors"}}};
        for (const auto& [controlId, target] : targets) { if (request.ControlId == controlId) { return WindowAction("open", target); } }
        if (request.ControlId == 8) { return WindowAction("open", "quit"); }
    }
    if ((name == "gfx_options" || name == "sound_options" || name == "control_options" || name == "interface_options" || name == "font_options") && (request.ControlId == 1 || request.ControlId == 2)) { return WindowAction("close", name); }
    if (name == "mapbook" && request.ControlId == 3) { return WindowAction("open", "bigmap"); }
    if (name == "quit" && request.ControlId == 1) { return "game_leave_confirmed"; }
    if (name == "chat_st2" && request.ControlId == 23) { return "game_leave_requested"; }
    if (request.SendHelp) { return "game_help:" + std::string(request.WindowName) + ":" + std::string(request.WindowHelp); }
    if (request.SendQuit) { return WindowAction("close", request.WindowName); }
    return "game_control:" + std::string(request.WindowName) + ":" + std::to_string(request.ControlId);
}
