#pragma once
#include "Core/Types.h"
#include <string>
#include <vector>

namespace Sfera {
struct FProtocolProbeResult {
    size_t Bytes = 0;
    bool LooksLikeU16PayloadFrame = false;
    bool LooksLikeSferaFrame = false;
    std::string AsciiPreview;
};

struct FSferaNetPacket {
    uint16 Length = 0;
    uint16 Checksum = 0;
    uint16 Sequence = 0;
    uint16 Opcode = 0;
    FByteArray Payload;
    bool ChecksumValid = false;
};

class FClientProtocolProbe {
public:
    FProtocolProbeResult Inspect(const FByteArray& bytes) const;
    static std::string Describe(const FProtocolProbeResult& result);
};

class FSferaNetProtocol {
public:
    static constexpr uint16 HeaderSize = 8;
    static constexpr uint16 ServerCreateConnection = 0x00C8;
    static constexpr uint16 ServerKeepAlive = 0x0064;
    static constexpr uint16 ServerPing = 0x01F4;
    static constexpr uint16 ClientCreateConnectionAck = 0x0190;

    void Append(const uint8* data, size_t size);
    bool TryPopPacket(FSferaNetPacket& outPacket);
    FByteArray MakePacket(uint16 opcode, const FByteArray& payload, uint16 connectionKey);
    uint16 LastSequence() const { return Sequence; }

    static bool LooksLikeFrame(const FByteArray& bytes);
    static uint16 ComputeChecksum(const uint8* frame, size_t frameSize, uint16 connectionKey);
private:
    FByteArray Buffer;
    uint16 Sequence = 0;
};
}
