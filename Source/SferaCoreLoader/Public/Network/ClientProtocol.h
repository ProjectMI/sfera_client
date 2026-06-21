#pragma once
#include "Core/Types.h"
#include <string>
#include <vector>

struct FProtocolProbeResult 
{
    size_t Bytes = 0;
    bool LooksLikeU16Frame = false;
    bool LooksLikeU32Frame = false;
    bool LooksLikeCannotConnect = false;
    bool LooksLikeCharacterSelectStart = false;
    bool LooksLikeEmptyCharacterSlot = false;
    bool LooksLikeCharacterSlot = false;
    std::string AsciiPreview;
};

class FClientProtocolProbe 
{
public:
    FProtocolProbeResult Inspect(const FByteArray& bytes) const;
    static std::string Describe(const FProtocolProbeResult& result);
};