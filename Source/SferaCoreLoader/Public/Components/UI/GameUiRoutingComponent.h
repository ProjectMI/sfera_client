#pragma once
#include "Core/Types.h"

struct FGameUiControlRequest
{
    std::string_view WindowName;
    int32 ControlId = 0;
    bool SendQuit = false;
    bool SendHelp = false;
    std::string_view WindowHelp;
};

class FGameUiRoutingComponent
{
public:
    std::string Route(const FGameUiControlRequest& request) const;
};
