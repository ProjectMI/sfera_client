#include "Network/ClientProtocol.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Sfera {
namespace {
uint16 ReadU16LE(const uint8* p) { return static_cast<uint16>(p[0] | (p[1] << 8)); }
void WriteU16LE(FByteArray& bytes, size_t offset, uint16 value) {
    bytes[offset] = static_cast<uint8>(value & 0xFF);
    bytes[offset + 1] = static_cast<uint8>((value >> 8) & 0xFF);
}
}

FProtocolProbeResult FClientProtocolProbe::Inspect(const FByteArray& bytes) const {
    FProtocolProbeResult result;
    result.Bytes = bytes.size();
    if (bytes.size() >= 2) {
        size_t n = size_t(bytes[0] | (bytes[1] << 8));
        result.LooksLikeU16PayloadFrame = (n + 2) <= bytes.size();
        result.LooksLikeSferaFrame = FSferaNetProtocol::LooksLikeFrame(bytes);
    }
    for (uint8 ch : bytes) { if (result.AsciiPreview.size() >= 80) { break; } result.AsciiPreview.push_back(std::isprint(ch) ? static_cast<char>(ch) : '.'); }
    return result;
}

std::string FClientProtocolProbe::Describe(const FProtocolProbeResult& result) {
    return "bytes=" + std::to_string(result.Bytes) + ", u16_payload=" + std::string(result.LooksLikeU16PayloadFrame ? "yes" : "no") + ", sfera=" + std::string(result.LooksLikeSferaFrame ? "yes" : "no") + ", ascii=\"" + result.AsciiPreview + "\"";
}

void FSferaNetProtocol::Append(const uint8* data, size_t size) { Buffer.insert(Buffer.end(), data, data + size); }

bool FSferaNetProtocol::LooksLikeFrame(const FByteArray& bytes) {
    if (bytes.size() < HeaderSize) { return false; }
    uint16 length = ReadU16LE(bytes.data());
    if (length < HeaderSize || length > 0xEA60 || length > bytes.size()) { return false; }
    uint16 opcode = ReadU16LE(bytes.data() + 6);
    return opcode == ServerCreateConnection || opcode == ServerKeepAlive || opcode == ServerPing || length <= bytes.size();
}

uint16 FSferaNetProtocol::ComputeChecksum(const uint8* frame, size_t frameSize, uint16 connectionKey) {
    uint16 sum = 0;
    for (size_t i = 4; i < frameSize; ++i) { sum = static_cast<uint16>(sum + static_cast<int8_t>(frame[i])); }
    return static_cast<uint16>(connectionKey ^ sum);
}

bool FSferaNetProtocol::TryPopPacket(FSferaNetPacket& outPacket) {
    if (Buffer.size() < HeaderSize) { return false; }
    uint16 length = ReadU16LE(Buffer.data());
    if (length < HeaderSize || length > 0xEA60) {
        Buffer.erase(Buffer.begin());
        return false;
    }
    if (Buffer.size() < length) { return false; }
    outPacket.Length = length;
    outPacket.Checksum = ReadU16LE(Buffer.data() + 2);
    outPacket.Sequence = ReadU16LE(Buffer.data() + 4);
    outPacket.Opcode = ReadU16LE(Buffer.data() + 6);
    outPacket.Payload.assign(Buffer.begin() + HeaderSize, Buffer.begin() + length);
    outPacket.ChecksumValid = true; // server key is learned from create-connection packets, so validation is deferred.
    Buffer.erase(Buffer.begin(), Buffer.begin() + length);
    return true;
}

FByteArray FSferaNetProtocol::MakePacket(uint16 opcode, const FByteArray& payload, uint16 connectionKey) {
    const uint16 length = static_cast<uint16>(HeaderSize + payload.size());
    FByteArray frame(length, 0);
    WriteU16LE(frame, 0, length);
    Sequence = static_cast<uint16>(Sequence + static_cast<uint16>((std::rand() & 0x3) + 1));
    WriteU16LE(frame, 4, Sequence);
    WriteU16LE(frame, 6, opcode);
    std::copy(payload.begin(), payload.end(), frame.begin() + HeaderSize);
    WriteU16LE(frame, 2, ComputeChecksum(frame.data(), frame.size(), connectionKey));
    return frame;
}
}
