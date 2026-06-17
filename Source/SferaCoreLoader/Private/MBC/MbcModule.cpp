#include "MBC/MbcModule.h"
#include <cctype>
#include <cstring>

namespace Sfera {
static uint32 ReadLe32Safe(const FByteArray& b, size_t off) {
    if (off + 4 > b.size()) { return 0; }
    return uint32(b[off]) | (uint32(b[off + 1]) << 8) | (uint32(b[off + 2]) << 16) | (uint32(b[off + 3]) << 24);
}

FStatus FMbcModule::Load(std::string name, FByteArray bytes) {
    ModuleName = std::move(name);
    RawBytes = std::move(bytes);
    ProbeHeader();
    ExtractStrings();
    return RawBytes.empty() ? FStatus::Error(EStatusCode::InvalidData, "empty MBC module") : FStatus::Ok();
}

void FMbcModule::ProbeHeader() {
    HeaderProbe = {};
    if (RawBytes.size() >= 4 && std::memcmp(RawBytes.data(), "MBC", 3) == 0) {
        HeaderProbe.HasKnownMagic = true;
        HeaderProbe.Version = RawBytes[3];
        HeaderProbe.CodeOffset = RawBytes.size() >= 12 ? ReadLe32Safe(RawBytes, 4) : 0;
        HeaderProbe.CodeSize = RawBytes.size() >= 12 ? ReadLe32Safe(RawBytes, 8) : 0;
        HeaderProbe.Commentary = "MBC magic detected";
        return;
    }
    HeaderProbe.CodeOffset = 0;
    HeaderProbe.CodeSize = static_cast<uint32>(RawBytes.size());
    HeaderProbe.Commentary = "unknown header; raw bytecode view";
}

void FMbcModule::ExtractStrings() {
    StringsFound.clear();
    size_t start = 0;
    while (start < RawBytes.size()) {
        while (start < RawBytes.size() && !std::isprint(RawBytes[start])) { ++start; }
        size_t end = start;
        while (end < RawBytes.size() && std::isprint(RawBytes[end])) { ++end; }
        if (end - start >= 4) { StringsFound.push_back({static_cast<uint32>(start), std::string(reinterpret_cast<const char*>(RawBytes.data() + start), end - start)}); }
        start = end + 1;
    }
}
}
