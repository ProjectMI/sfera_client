#pragma once
#include "Core/Types.h"
#include <array>
#include <string>
#include <vector>

namespace Sfera {
struct FProtocolProbeResult {
    size_t Bytes = 0;
    bool LooksLikeU16Frame = false;
    bool LooksLikeU32Frame = false;
    std::string AsciiPreview;
};

struct FCharacterSlotInfo {
    bool Present = false;
    bool CanCreate = true;
    int32 Slot = 0;
    std::string Name;
    bool Female = false;
    int32 MaxHp = 0;
    int32 MaxMp = 0;
    int32 CurrentHp = 0;
    int32 CurrentMp = 0;
    int32 Strength = 10;
    int32 Dexterity = 10;
    int32 Accuracy = 10;
    int32 Endurance = 10;
    int32 Fire = 0;
    int32 Water = 0;
    int32 Earth = 0;
    int32 Air = 0;
    int32 PhysicalDefense = 0;
    int32 MagicalDefense = 0;
};

struct FLoginHandshakeResult {
    bool Connected = false;
    bool LegacyHandshake = false;
    bool LoginSent = false;
    bool LoginRejected = false;
    bool CharacterSelectReady = false;
    uint16 LocalId = 0;
    int32 FirstOpcode = 0;
    int32 NextOpcode = 0;
    int32 FirstLength = 0;
    int32 NextLength = 0;
    int32 CharacterSelectPackets = 0;
    int32 CharacterSelectBytes = 0;
    std::array<FCharacterSlotInfo, 3> CharacterSlots{};
    std::string Message;
};

class FClientProtocolProbe {
public:
    FProtocolProbeResult Inspect(const FByteArray& bytes) const;
    static std::string Describe(const FProtocolProbeResult& result);
};

uint16 ReadU16LE(const FByteArray& data, size_t offset);
void WriteU16LE(FByteArray& data, size_t offset, uint16 value);
FByteArray BuildLegacyPacket(uint16 opcode, const FByteArray& payload, uint16& sequence, uint16 xorKey);
FByteArray BuildSphereEmuLoginPacket(uint16 localId, std::string_view login, std::string_view password);
FByteArray EncodeClientPacketForSphereEmu(const FByteArray& decoded);
bool LooksLikeCannotConnect(const FByteArray& frame);
bool LooksLikeCharacterSelectStart(const FByteArray& frame);
bool LooksLikeCharacterSlot(const FByteArray& frame);
FCharacterSlotInfo ParseCharacterSlot(const FByteArray& frame, int32 slot);
}
