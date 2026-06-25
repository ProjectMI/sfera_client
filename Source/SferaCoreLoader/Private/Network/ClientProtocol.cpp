#include "Network/ClientProtocol.h"
#include "Network/SphereEmuProtocol.h"

FProtocolProbeResult FClientProtocolProbe::Inspect(const FByteArray& bytes) const
{
    FProtocolProbeResult result;
    result.Bytes = bytes.size();

    if (bytes.size() >= 2)
    {
        size_t n = size_t(bytes[0] | (bytes[1] << 8));
        result.LooksLikeU16Frame = (n + 2) <= bytes.size();
    }

    if (bytes.size() >= 4)
    {
        size_t n = size_t(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
        result.LooksLikeU32Frame = (n + 4) <= bytes.size();
    }

    result.LooksLikeCannotConnect = FSphereEmuProtocol::LooksLikeCannotConnect(bytes);
    result.LooksLikeCharacterSelectStart = FSphereEmuProtocol::LooksLikeCharacterSelectStart(bytes);
    result.LooksLikeEmptyCharacterSlot = FSphereEmuProtocol::LooksLikeEmptyCharacterSlot(bytes);
    result.LooksLikeCharacterSlot = FSphereEmuProtocol::LooksLikeCharacterSlot(bytes);

    for (uint8 ch : bytes)
    {
        if (result.AsciiPreview.size() >= 80)
        {
            break;
        }

        result.AsciiPreview.push_back(std::isprint(ch) ? static_cast<char>(ch) : '.');
    }

    return result;
}

std::string FClientProtocolProbe::Describe(const FProtocolProbeResult& result)
{
    return "bytes=" + std::to_string(result.Bytes) + ", u16=" + std::string(result.LooksLikeU16Frame ? "yes" : "no") + ", u32=" + std::string(result.LooksLikeU32Frame ? "yes" : "no") + ", cannot_connect=" + std::string(result.LooksLikeCannotConnect ? "yes" : "no") + ", char_select=" + std::string(result.LooksLikeCharacterSelectStart ? "yes" : "no") + ", char_slot=" + std::string(result.LooksLikeCharacterSlot ? "yes" : "no") + ", empty_slot=" + std::string(result.LooksLikeEmptyCharacterSlot ? "yes" : "no") + ", ascii=\"" + result.AsciiPreview + "\"";
}
