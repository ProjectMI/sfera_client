#include "Network/PacketBuffer.h"
#include <algorithm>

FPacketBuffer::FPacketBuffer(EPacketLengthMode mode) : Mode(mode) {}
void FPacketBuffer::Append(const uint8* data, size_t size)
{
    Buffer.insert(Buffer.end(), data, data + size);
}

bool FPacketBuffer::TryPopFrame(FByteArray& outFrame)
{
    size_t prefix = Mode == EPacketLengthMode::UInt16LE ? 2 : 4;

    if (Buffer.size() < prefix) { return false; }

    uint32 length = Mode == EPacketLengthMode::UInt16LE ? uint32(Buffer[0] | (Buffer[1] << 8)) : uint32(Buffer[0] | (Buffer[1] << 8) | (Buffer[2] << 16) | (Buffer[3] << 24));

    if (Buffer.size() < prefix + length) { return false; }

    outFrame.assign(Buffer.begin() + static_cast<std::ptrdiff_t>(prefix), Buffer.begin() + static_cast<std::ptrdiff_t>(prefix + length));
    Buffer.erase(Buffer.begin(), Buffer.begin() + static_cast<std::ptrdiff_t>(prefix + length));
    return true;
}

FByteArray FPacketBuffer::MakeFrame(const FByteArray& payload) const
{
    FByteArray frame;

    if (Mode == EPacketLengthMode::UInt16LE)
    {
        frame.push_back(uint8(payload.size() & 0xFF));
        frame.push_back(uint8((payload.size() >> 8) & 0xFF));
    }
    else
    {
        frame.push_back(uint8(payload.size() & 0xFF));
        frame.push_back(uint8((payload.size() >> 8) & 0xFF));
        frame.push_back(uint8((payload.size() >> 16) & 0xFF));
        frame.push_back(uint8((payload.size() >> 24) & 0xFF));
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}
