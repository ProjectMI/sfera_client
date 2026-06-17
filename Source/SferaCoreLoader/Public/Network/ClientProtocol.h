#pragma once
#include "Core/Types.h"
#include <string>
#include <vector>

namespace Sfera {
struct FProtocolProbeResult {
    size_t Bytes = 0;
    bool LooksLikeU16Frame = false;
    bool LooksLikeU32Frame = false;
    std::string AsciiPreview;
};

class FClientProtocolProbe {
public:
    FProtocolProbeResult Inspect(const FByteArray& bytes) const;
    static std::string Describe(const FProtocolProbeResult& result);
};
}
