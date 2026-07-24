#pragma once
#include "Core/Types.h"

struct FClanRuntimeState
{
    bool Known = false;
    bool Available = false;
    int32 Rank = -1;
    std::string Name;
};

class FClanRuntimeComponent
{
public:
    bool InspectFrames(const std::vector<std::vector<uint8>>& frames);
    void Reset();
    const FClanRuntimeState& State() const { return Current; }
private:
    bool InspectFrame(const std::vector<uint8>& frame);
    FClanRuntimeState Current;
};
