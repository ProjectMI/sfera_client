#include "Components/UI/UiWindowStateComponent.h"
#include "Common/StringUtils.h"

std::string FUiWindowStateComponent::Key(std::string_view window)
{
    return Common::ToLower(std::string(window));
}

FUiWindowTransition FUiWindowStateComponent::Apply(std::string_view window, EUiWindowOperation operation, int32 context)
{
    if (operation == EUiWindowOperation::Open) { return Open(window, context); }
    if (operation == EUiWindowOperation::Close) { return Close(window); }
    return Toggle(window, context);
}

FUiWindowTransition FUiWindowStateComponent::Toggle(std::string_view window, int32 context)
{
    return IsOpen(window) ? Close(window) : Open(window, context);
}

FUiWindowTransition FUiWindowStateComponent::Open(std::string_view window, int32 context)
{
    FWindowState& state = Windows[Key(window)];
    if (state.Open) { return {false, true, true, state.Context}; }
    state.Open = true;
    state.Context = context;
    return {true, true, true, context};
}

FUiWindowTransition FUiWindowStateComponent::Close(std::string_view window)
{
    FWindowState& state = Windows[Key(window)];
    if (!state.Open) { return {false, false, false, state.Context}; }
    state.Open = false;
    return {true, false, false, state.Context};
}

bool FUiWindowStateComponent::IsOpen(std::string_view window) const
{
    auto it = Windows.find(Key(window));
    return it != Windows.end() && it->second.Open;
}

int32 FUiWindowStateComponent::Context(std::string_view window) const
{
    auto it = Windows.find(Key(window));
    return it == Windows.end() ? 0 : it->second.Context;
}

void FUiWindowStateComponent::Reset()
{
    Windows.clear();
}
