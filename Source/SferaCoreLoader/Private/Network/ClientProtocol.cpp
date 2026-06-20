#include "Network/ClientProtocol.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>

namespace Sfera {
namespace {
FByteArray ToCp1251Bytes(std::string_view text) {
    FByteArray out;
    out.reserve(text.size());
    for (char ch : text) { out.push_back(static_cast<uint8>(ch)); }
    return out;
}

void WriteByteLsb(FByteArray& data, int32 bitPosition, uint8 value) {
    for (int32 bit = 0; bit < 8; ++bit) {
        if ((value & (1U << bit)) == 0) { continue; }
        const int32 absoluteBit = bitPosition + bit;
        data[static_cast<size_t>(absoluteBit / 8)] |= static_cast<uint8>(1U << (absoluteBit % 8));
    }
}

int32 ReadPacked14(const FByteArray& data, size_t offset) {
    if (offset + 1 >= data.size()) { return 0; }
    return static_cast<int32>((data[offset] >> 2) | (data[offset + 1] << 6));
}

std::string DecodeSlotName(const FByteArray& frame) {
    if (frame.size() < 87) { return {}; }
    std::string out;
    out.reserve(19);
    for (size_t i = 0; i < 19; ++i) {
        const uint8 current = frame[67 + i];
        const uint8 next = i == 18 ? frame[86] : frame[68 + i];
        const uint8 decoded = static_cast<uint8>((current >> 2) | ((next & 0x03) << 6));
        if (decoded == 0) { break; }
        out.push_back(static_cast<char>(decoded));
    }
    return out;
}
}

uint16 ReadU16LE(const FByteArray& data, size_t offset) {
    if (offset + 1 >= data.size()) { return 0; }
    return static_cast<uint16>(data[offset] | (data[offset + 1] << 8));
}

void WriteU16LE(FByteArray& data, size_t offset, uint16 value) {
    if (offset + 1 >= data.size()) { return; }
    data[offset] = static_cast<uint8>(value & 0xff);
    data[offset + 1] = static_cast<uint8>((value >> 8) & 0xff);
}

FByteArray BuildLegacyPacket(uint16 opcode, const FByteArray& payload, uint16& sequence, uint16 xorKey) {
    const uint16 length = static_cast<uint16>(payload.size() + 8);
    FByteArray packet(length, 0);
    sequence = static_cast<uint16>(sequence + static_cast<uint16>((std::rand() & 3) + 1));
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 4, sequence);
    WriteU16LE(packet, 6, opcode);
    std::copy(payload.begin(), payload.end(), packet.begin() + 8);
    int16_t sum = 0;
    for (size_t i = 4; i < packet.size(); ++i) { sum = static_cast<int16_t>(sum + static_cast<int8_t>(packet[i])); }
    WriteU16LE(packet, 2, static_cast<uint16>(xorKey ^ static_cast<uint16>(sum)));
    return packet;
}

FByteArray BuildSphereEmuLoginPacket(uint16 localId, std::string_view login, std::string_view password) {
    FByteArray strings = ToCp1251Bytes(login);
    strings.push_back(1);
    FByteArray passwordBytes = ToCp1251Bytes(password);
    strings.insert(strings.end(), passwordBytes.begin(), passwordBytes.end());
    strings.push_back(0);
    constexpr int32 stringBitOffset = 18 * 8 + 2;
    const int32 packetLength = (stringBitOffset + static_cast<int32>(strings.size()) * 8 + 7) / 8;
    FByteArray packet(static_cast<size_t>(packetLength), 0);
    WriteU16LE(packet, 0, static_cast<uint16>(packetLength));
    WriteU16LE(packet, 2, 300);
    const uint8 major = static_cast<uint8>((localId >> 8) & 0xff);
    const uint8 minor = static_cast<uint8>(localId & 0xff);
    packet[7] = major;
    packet[8] = minor;
    packet[11] = major;
    packet[12] = minor;
    int32 bitPosition = stringBitOffset;
    for (uint8 byte : strings) { WriteByteLsb(packet, bitPosition, byte); bitPosition += 8; }
    return packet;
}

FByteArray EncodeClientPacketForSphereEmu(const FByteArray& decoded) {
    static constexpr uint8 encodingMask[] = {0x4B, 0x0D, 0xEF, 0x60, 0xC9, 0x9A, 0x70, 0x0E, 0x03};
    constexpr size_t start = 9;
    if (decoded.size() <= start) { return decoded; }
    FByteArray encoded = decoded;
    uint8 mask3 = 0;
    for (size_t i = 0; i + start < decoded.size(); ++i) {
        const uint8 current = decoded[i + start];
        encoded[i + start] = static_cast<uint8>(current ^ encodingMask[i % std::size(encodingMask)] ^ mask3);
        mask3 = static_cast<uint8>(current * i + 2 * mask3);
    }
    return encoded;
}

bool LooksLikeCannotConnect(const FByteArray& frame) { return frame.size() == 14 && ReadU16LE(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0xA0; }
bool LooksLikeCharacterSelectStart(const FByteArray& frame) { return frame.size() == 82 && ReadU16LE(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0x80 && frame[12] == 0x10; }
bool LooksLikeCharacterSlot(const FByteArray& frame) { return frame.size() == 108 && ReadU16LE(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0x60; }

FCharacterSlotInfo ParseCharacterSlot(const FByteArray& frame, int32 slot) {
    FCharacterSlotInfo out;
    out.Slot = slot;
    out.CanCreate = frame.size() == 108 && frame[12] == 0x79;
    if (!LooksLikeCharacterSlot(frame)) { out.CanCreate = false; return out; }
    out.Name = DecodeSlotName(frame);
    out.Present = !out.Name.empty() && frame[12] == 0x79;
    out.Female = frame.size() > 66 && (((frame[66] >> 2) & 1) != 0);
    out.MaxHp = ReadPacked14(frame, 13);
    out.MaxMp = ReadPacked14(frame, 15);
    out.Strength = ReadPacked14(frame, 17);
    out.Dexterity = ReadPacked14(frame, 19);
    out.Accuracy = ReadPacked14(frame, 21);
    out.Endurance = ReadPacked14(frame, 23);
    out.Earth = ReadPacked14(frame, 25);
    out.Air = ReadPacked14(frame, 27);
    out.Water = ReadPacked14(frame, 29);
    out.Fire = ReadPacked14(frame, 31);
    out.PhysicalDefense = ReadPacked14(frame, 33);
    out.MagicalDefense = ReadPacked14(frame, 35);
    out.CurrentHp = ReadPacked14(frame, 54);
    out.CurrentMp = ReadPacked14(frame, 56);
    return out;
}

FProtocolProbeResult FClientProtocolProbe::Inspect(const FByteArray& bytes) const {
    FProtocolProbeResult result;
    result.Bytes = bytes.size();
    if (bytes.size() >= 2) { size_t n = size_t(bytes[0] | (bytes[1] << 8)); result.LooksLikeU16Frame = n <= bytes.size(); }
    if (bytes.size() >= 4) { size_t n = size_t(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24)); result.LooksLikeU32Frame = n <= bytes.size(); }
    for (uint8 ch : bytes) { if (result.AsciiPreview.size() >= 80) { break; } result.AsciiPreview.push_back(std::isprint(ch) ? static_cast<char>(ch) : '.'); }
    return result;
}

std::string FClientProtocolProbe::Describe(const FProtocolProbeResult& result) {
    return "bytes=" + std::to_string(result.Bytes) + ", u16=" + std::string(result.LooksLikeU16Frame ? "yes" : "no") + ", u32=" + std::string(result.LooksLikeU32Frame ? "yes" : "no") + ", ascii=\"" + result.AsciiPreview + "\"";
}
}
