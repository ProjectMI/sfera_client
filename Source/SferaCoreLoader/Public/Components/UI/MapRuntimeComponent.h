#pragma once
#include "Core/Types.h"

struct FMapRuntimeState
{
    std::string SpriteName = "mhp1";
    std::array<int32, 8> Projection{};
    bool HasProjection = false;
};

class FMapRuntimeComponent
{
public:
    bool InspectFrames(const std::vector<std::vector<uint8>>& frames);
    void Reset();
    const FMapRuntimeState& State() const { return Current; }
private:
    bool InspectFrame(const std::vector<uint8>& frame);
    FMapRuntimeState Current;
};
