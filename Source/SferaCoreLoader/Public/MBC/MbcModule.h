#pragma once
#include "Core/Types.h"

namespace Sfera {
struct FMbcStringRef { uint32 Offset = 0; std::string Value; };
struct FMbcHeaderProbe { bool HasKnownMagic = false; uint32 Version = 0; uint32 CodeOffset = 0; uint32 CodeSize = 0; std::string Commentary; };

class FMbcModule {
public:
    FStatus Load(std::string name, FByteArray bytes);
    const std::string& Name() const { return ModuleName; }
    const FByteArray& Bytes() const { return RawBytes; }
    const std::vector<FMbcStringRef>& Strings() const { return StringsFound; }
    const FMbcHeaderProbe& Header() const { return HeaderProbe; }
private:
    void ProbeHeader();
    void ExtractStrings();
    std::string ModuleName;
    FByteArray RawBytes;
    FMbcHeaderProbe HeaderProbe;
    std::vector<FMbcStringRef> StringsFound;
};
}
