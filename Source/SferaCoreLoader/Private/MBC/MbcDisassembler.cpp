#include "MBC/MbcDisassembler.h"
#include <iomanip>
#include <sstream>

namespace Sfera {
static std::string Hex(uint32 value) { std::ostringstream out; out << "0x" << std::uppercase << std::hex << value; return out.str(); }
static std::string RenderOperands(const FMbcDecodedOpcode& decoded) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [key, value] : decoded.Operands) { if (key == "semantic" || key == "handler" || key == "handler_ea") { continue; } out << (first ? " " : ", ") << key << "=" << value; first = false; }
    return out.str();
}
std::vector<FMbcInstruction> FMbcDisassembler::Disassemble(const FMbcModule& module, size_t maxInstructions) const {
    std::vector<FMbcInstruction> result;
    uint32 offset = 0;
    const FByteArray& code = module.Code();
    while (offset < code.size() && result.size() < maxInstructions) {
        FMbcDecodedOpcode decoded = DecodeMbcOpcode(code, offset);
        uint32 length = std::max<uint32>(decoded.Length, 1);
        std::string text = Hex(offset) + " " + decoded.Mnemonic + RenderOperands(decoded);
        result.push_back({offset, offset + Mbc::CodeFileOffset, code[offset], decoded.Mnemonic, std::move(text), decoded});
        offset += length;
        if (decoded.Terminal) { break; }
    }
    return result;
}
std::vector<FMbcInstruction> FMbcDisassembler::DisassembleProgram(const FMbcModule& module, const FMbcProgram& program, size_t maxInstructions) const {
    std::vector<FMbcInstruction> result;
    uint32 offset = program.Start;
    const FByteArray& code = module.Code();
    while (offset < code.size() && result.size() < maxInstructions) {
        FMbcDecodedOpcode decoded = DecodeMbcOpcode(code, offset);
        uint32 length = std::max<uint32>(decoded.Length, 1);
        std::string text = Hex(offset) + " " + decoded.Mnemonic + RenderOperands(decoded);
        result.push_back({offset, offset + Mbc::CodeFileOffset, code[offset], decoded.Mnemonic, std::move(text), decoded});
        offset += length;
        if (decoded.Terminal || offset > program.End) { break; }
    }
    return result;
}
}
