#pragma once
#include "MBC/MbcModule.h"
#include "MBC/MbcOpcode.h"

namespace Sfera {
struct FMbcInstruction { uint32 Offset = 0; uint32 FileOffset = 0; uint8 Opcode = 0; std::string Mnemonic; std::string Text; FMbcDecodedOpcode Decoded; };
class FMbcDisassembler {
public:
    std::vector<FMbcInstruction> Disassemble(const FMbcModule& module, size_t maxInstructions = 4096) const;
    std::vector<FMbcInstruction> DisassembleProgram(const FMbcModule& module, const FMbcProgram& program, size_t maxInstructions = 4096) const;
};
}
