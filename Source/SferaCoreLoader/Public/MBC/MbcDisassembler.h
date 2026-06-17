#pragma once
#include "MBC/MbcModule.h"

namespace Sfera {
struct FMbcInstruction { uint32 Offset = 0; uint8 Opcode = 0; std::string Text; };
class FMbcDisassembler {
public:
    std::vector<FMbcInstruction> Disassemble(const FMbcModule& module, size_t maxInstructions = 4096) const;
};
}
