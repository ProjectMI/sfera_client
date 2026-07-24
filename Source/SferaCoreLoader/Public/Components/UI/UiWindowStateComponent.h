#pragma once
#include "Core/Types.h"

enum class EUiWindowOperation
{
    Toggle,
    Open,
    Close
};

struct FUiWindowTransition
{
    bool Changed = false;
    bool Open = false;
    bool CursorRequired = false;
    int32 Context = 0;
};

class FUiWindowStateComponent
{
public:
    FUiWindowTransition Apply(std::string_view window, EUiWindowOperation operation, int32 context = 0);
    FUiWindowTransition Toggle(std::string_view window, int32 context = 0);
    FUiWindowTransition Open(std::string_view window, int32 context = 0);
    FUiWindowTransition Close(std::string_view window);
    bool IsOpen(std::string_view window) const;
    int32 Context(std::string_view window) const;
    void Reset();
private:
    struct FWindowState { bool Open = false; int32 Context = 0; };
    static std::string Key(std::string_view window);
    std::unordered_map<std::string, FWindowState> Windows;
};
