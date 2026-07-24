#include "Components/UI/ClanRuntimeComponent.h"
#include "Common/SferaGameConstants.h"
#include "Common/TextEncoding.h"

namespace
{
std::string DecodeClanName(const std::vector<uint8>& frame, size_t offset)
{
    FByteArray bytes;
    while (offset < frame.size() && frame[offset] != 0) { bytes.push_back(frame[offset++]); }
    return Common::WideToUtf8(Common::Cp1251BytesToWide(bytes));
}
}

bool FClanRuntimeComponent::InspectFrames(const std::vector<std::vector<uint8>>& frames)
{
    bool changed = false;
    for (const std::vector<uint8>& frame : frames) { changed = InspectFrame(frame) || changed; }
    return changed;
}

void FClanRuntimeComponent::Reset()
{
    Current = {};
}

bool FClanRuntimeComponent::InspectFrame(const std::vector<uint8>& frame)
{
    if (frame.size() < 14 || static_cast<uint16>(frame[2] | (static_cast<uint16>(frame[3]) << 8)) != SferaProtocol::ServerFrameOpcode || frame[9] != SferaProtocol::ServerChannelByte || frame[10] != SferaProtocol::ServerFamilyByte || frame[11] != 12 || frame[12] != 22) { return false; }
    FClanRuntimeState next = Current;
    const uint8 code = frame[13];
    next.Known = true;
    if (frame.size() == 14)
    {
        if (code == 0 || code == 0xff) { next.Available = false; next.Rank = -1; next.Name.clear(); }
        else if (code >= 1 && code <= 4) { next.Available = true; next.Rank = static_cast<int32>(code) - 1; }
    }
    else if (code == 5) { next.Available = false; next.Rank = -1; next.Name.clear(); }
    else if (code == 6) { next.Available = true; next.Rank = 3; next.Name = DecodeClanName(frame, 14); }
    else if (code <= 3) { next.Available = true; next.Rank = static_cast<int32>(code); }
    const bool changed = next.Known != Current.Known || next.Available != Current.Available || next.Rank != Current.Rank || next.Name != Current.Name;
    Current = std::move(next);
    return changed;
}
