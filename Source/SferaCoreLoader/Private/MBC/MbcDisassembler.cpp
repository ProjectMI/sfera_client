#include "MBC/MbcDisassembler.h"
#include <iomanip>
#include <sstream>

namespace Sfera {
static std::string HexByte(uint8 value) {
    std::ostringstream out;
    out << "OP_" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(value);
    return out.str();
}

std::vector<FMbcInstruction> FMbcDisassembler::Disassemble(const FMbcModule& module, size_t maxInstructions) const {
    std::vector<FMbcInstruction> result;
    const FByteArray& bytes = module.Bytes();
    uint32 offset = module.Header().CodeOffset;
    uint32 end = module.Header().CodeSize ? std::min<uint32>(static_cast<uint32>(bytes.size()), offset + module.Header().CodeSize) : static_cast<uint32>(bytes.size());
    while (offset < end && result.size() < maxInstructions) {
        uint8 opcode = bytes[offset];
        result.push_back({offset, opcode, HexByte(opcode)});
        ++offset;
    }
    return result;
}
}
