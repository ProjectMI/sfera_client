#include "Network/ClientProtocol.h"
#include <cctype>

namespace Sfera {
FProtocolProbeResult FClientProtocolProbe::Inspect(const FByteArray& bytes) const {
    FProtocolProbeResult result;
    result.Bytes = bytes.size();
    if (bytes.size() >= 2) { size_t n = size_t(bytes[0] | (bytes[1] << 8)); result.LooksLikeU16Frame = (n + 2) <= bytes.size(); }
    if (bytes.size() >= 4) { size_t n = size_t(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24)); result.LooksLikeU32Frame = (n + 4) <= bytes.size(); }
    for (uint8 ch : bytes) { if (result.AsciiPreview.size() >= 80) { break; } result.AsciiPreview.push_back(std::isprint(ch) ? static_cast<char>(ch) : '.'); }
    return result;
}

std::string FClientProtocolProbe::Describe(const FProtocolProbeResult& result) {
    return "bytes=" + std::to_string(result.Bytes) + ", u16=" + std::string(result.LooksLikeU16Frame ? "yes" : "no") + ", u32=" + std::string(result.LooksLikeU32Frame ? "yes" : "no") + ", ascii=\"" + result.AsciiPreview + "\"";
}
}
