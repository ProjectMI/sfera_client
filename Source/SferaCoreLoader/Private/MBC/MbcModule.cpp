#include "MBC/MbcModule.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Sfera {
FStatus FMbcModule::Load(std::string name, FByteArray bytes) {
    ModuleName = std::move(name);
    RawBytes = std::move(bytes);
    CodeBytes.clear();
    DataBytes.clear();
    MetadataBytes.clear();
    ProgramTable.clear();
    FunctionTable.clear();
    StringsFound.clear();
    HeaderProbe = {};
    ParsedHeader = {};
    Valid = false;
    return ParseImage();
}

FStatus FMbcModule::ParseImage() {
    try {
        if (RawBytes.size() < Mbc::CodeFileOffset) { HeaderProbe.Commentary = "too small for MBC header"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
        HeaderProbe.HasKnownMagic = std::memcmp(RawBytes.data(), Mbc::MagicText, 16) == 0;
        if (!HeaderProbe.HasKnownMagic) { HeaderProbe.Commentary = "bad MBC magic; expected MBL script v4.0"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
        ParsedHeader.Magic = "MBL script v4.0";
        ParsedHeader.ChecksumOrTag = Mbc::ReadU32(RawBytes, 0x10);
        ParsedHeader.ModuleTag = Mbc::ReadU32(RawBytes, 0x14);
        ParsedHeader.CodeSize = Mbc::ReadU32(RawBytes, 0x18);
        ParsedHeader.DataSize = Mbc::ReadU32(RawBytes, 0x1C);
        uint32 codeStart = Mbc::CodeFileOffset;
        uint32 codeEnd = codeStart + ParsedHeader.CodeSize;
        uint32 dataEnd = codeEnd + ParsedHeader.DataSize;
        if (codeEnd < codeStart || dataEnd < codeEnd || dataEnd > RawBytes.size()) { HeaderProbe.Commentary = "truncated MBC sections"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
        CodeBytes.assign(RawBytes.begin() + codeStart, RawBytes.begin() + codeEnd);
        DataBytes.assign(RawBytes.begin() + codeEnd, RawBytes.begin() + dataEnd);
        uint32 off = dataEnd;
        if (off + 4 > RawBytes.size()) { HeaderProbe.Commentary = "missing MBC program table"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
        uint32 programCount = Mbc::ReadU32(RawBytes, off);
        off += 4;
        ProgramTable.reserve(programCount);
        for (uint32 i = 0; i < programCount; ++i) {
            FMbcProgram program;
            program.Index = i;
            program.Name = Mbc::ReadCString(RawBytes, off);
            if (off + 14 > RawBytes.size()) { HeaderProbe.Commentary = "truncated MBC program record"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
            program.Start = Mbc::ReadU32(RawBytes, off);
            program.End = Mbc::ReadU32(RawBytes, off + 4);
            program.StateRaw = RawBytes[off + 8];
            program.QueueId = RawBytes[off + 9];
            program.Unknown48 = Mbc::ReadU32(RawBytes, off + 10);
            off += 14;
            ProgramTable.push_back(std::move(program));
        }
        if (off + 4 > RawBytes.size()) { HeaderProbe.Commentary = "missing MBC function table"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
        uint32 functionCount = Mbc::ReadU32(RawBytes, off);
        off += 4;
        FunctionTable.reserve(functionCount);
        for (uint32 i = 0; i < functionCount; ++i) {
            FMbcFunction fn;
            fn.Index = i;
            fn.Name = Mbc::ReadCString(RawBytes, off);
            if (off + 12 > RawBytes.size()) { HeaderProbe.Commentary = "truncated MBC function record"; return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
            fn.CodeOffset = Mbc::ReadU32(RawBytes, off);
            fn.ProgramIndexRaw = Mbc::ReadU32(RawBytes, off + 4);
            fn.FlagsOrModule = Mbc::ReadU32(RawBytes, off + 8);
            off += 12;
            FunctionTable.push_back(std::move(fn));
        }
        MetadataBytes.assign(RawBytes.begin() + off, RawBytes.end());
        ExtractStringsFromData();
        HeaderProbe.CodeSize = ParsedHeader.CodeSize;
        HeaderProbe.Commentary = "MBL script v4.0; code=" + std::to_string(ParsedHeader.CodeSize) + ", data=" + std::to_string(ParsedHeader.DataSize) + ", programs=" + std::to_string(ProgramTable.size()) + ", functions=" + std::to_string(FunctionTable.size());
        Valid = true;
        return FStatus::Ok();
    } catch (const std::exception& ex) { HeaderProbe.Commentary = ex.what(); return FStatus::Error(EStatusCode::InvalidData, HeaderProbe.Commentary); }
}

void FMbcModule::ExtractStringsFromData() {
    uint32 start = 0;
    uint32 run = 0;
    for (uint32 i = 0; i <= DataBytes.size(); ++i) {
        bool printable = i < DataBytes.size() && ((DataBytes[i] >= 32 && DataBytes[i] < 127) || DataBytes[i] >= 0xC0);
        if (printable) { if (run == 0) { start = i; } ++run; continue; }
        if (i < DataBytes.size() && DataBytes[i] == 0 && run >= 3) { std::string s(reinterpret_cast<const char*>(DataBytes.data() + start), run); StringsFound.push_back({start, std::move(s)}); }
        run = 0;
    }
}

const FMbcProgram* FMbcModule::ProgramByName(std::string_view name) const { for (const auto& p : ProgramTable) { if (p.Name == name) { return &p; } } return nullptr; }
const FMbcProgram* FMbcModule::ProgramForOffset(uint32 codeOffset) const { for (const auto& p : ProgramTable) { if (p.Contains(codeOffset)) { return &p; } } return nullptr; }
const FMbcFunction* FMbcModule::FunctionByName(std::string_view name) const { for (const auto& f : FunctionTable) { if (f.Name == name) { return &f; } } return nullptr; }
const FMbcFunction* FMbcModule::FunctionAtOffset(uint32 codeOffset) const { for (const auto& f : FunctionTable) { if (f.CodeOffset == codeOffset) { return &f; } } return nullptr; }
}
