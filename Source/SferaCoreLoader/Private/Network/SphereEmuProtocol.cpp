#include "Network/SphereEmuProtocol.h"
#include "Common/BinaryData.h"
#include "Common/TextEncoding.h"
#include "Common/ValueUtils.h"
#include <Windows.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <random>

namespace
{
    void WriteByteLsb(FByteArray& data, int bitPosition, uint8 value)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((value & (1U << bit)) == 0)
            {
                continue;
            }

            const int absoluteBit = bitPosition + bit;
            data[static_cast<size_t>(absoluteBit / 8)] |= static_cast<uint8>(1U << (absoluteBit % 8));
        }
    }
    bool IsRussianNameSymbol(wchar_t ch) { return (ch >= static_cast<wchar_t>(0x0410) && ch <= static_cast<wchar_t>(0x042f)) || (ch >= static_cast<wchar_t>(0x0430) && ch <= static_cast<wchar_t>(0x044f)) || ch == static_cast<wchar_t>(0x0401) || ch == static_cast<wchar_t>(0x0451); }
    uint8 EncodeCharacterNameSymbol(wchar_t ch)
    {
        if (ch >= static_cast<wchar_t>(0x0410) && ch <= static_cast<wchar_t>(0x042f))
        {
            return static_cast<uint8>(129U + static_cast<unsigned>(ch - static_cast<wchar_t>(0x0410)) * 2U);
        }

        if (ch >= static_cast<wchar_t>(0x0430) && ch <= static_cast<wchar_t>(0x044f))
        {
            return static_cast<uint8>(193U + static_cast<unsigned>(ch - static_cast<wchar_t>(0x0430)) * 2U);
        }

        if (ch == static_cast<wchar_t>(0x0401))
        {
            return static_cast<uint8>(129U + static_cast<unsigned>(static_cast<wchar_t>(0x0415) - static_cast<wchar_t>(0x0410)) * 2U);
        }

        if (ch == static_cast<wchar_t>(0x0451))
        {
            return static_cast<uint8>(193U + static_cast<unsigned>(static_cast<wchar_t>(0x0435) - static_cast<wchar_t>(0x0430)) * 2U);
        }

        if (ch >= 0x20 && ch <= 0x7f)
        {
            return static_cast<uint8>(static_cast<unsigned>(ch) * 2U);
        }

        return static_cast<uint8>(L'?' * 2U);
    }
    FByteArray EncodeCharacterNameCheck(const std::wstring& name)
    {
        FByteArray bytes(name.size() + 1, 0);
        const bool needsServerCyrillicLeadCorrection = name.size() > 1 && IsRussianNameSymbol(name[0]) && IsRussianNameSymbol(name[1]);

        for (size_t i = 1; i <= name.size(); ++i)
        {
            auto code = static_cast<unsigned>(EncodeCharacterNameSymbol(name[i - 1]));

            if (i == 1 && needsServerCyrillicLeadCorrection && (code & 1U) != 0)
            {
                --code;
            }

            bytes[i - 1] = static_cast<uint8>((bytes[i - 1] & 0x1fU) | ((code & 0x07U) << 5));
            bytes[i] = static_cast<uint8>((bytes[i] & 0xe0U) | ((code >> 3) & 0x1fU));
        }

        return bytes;
    }
    int32 AppearanceModelValue(int32 localIndex, int32 modelBase)
    {
        modelBase = std::clamp(modelBase, 0, 255);
        localIndex = std::clamp(localIndex, 0, 255 - modelBase);
        return modelBase + localIndex;
    }
    int32 AppearanceLocalIndex(int32 storedValue, int32 count, int32 modelBase)
    {
        if (count <= 0)
        {
            return -1;
        }

        if (storedValue >= modelBase && storedValue < modelBase + count)
        {
            return storedValue - modelBase;
        }

        return -1;
    }
    void WriteClientCoordinate(FByteArray& packet, size_t offset, double value)
    {
        constexpr double fractionBase = 8388608.0;
        uint32 fraction = 0;
        int32 scale = 126;
        bool negative = false;

        if (std::abs(value) > 0.0000001)
        {
            negative = value < 0.0;
            const double absolute = std::abs(value);
            const int32 exponent = static_cast<int32>(std::floor(std::log2(absolute)));
            scale = std::clamp(exponent + 127, 0, 255);
            const double normalized = absolute / std::ldexp(1.0, exponent);
            fraction = static_cast<uint32>((normalized - 1.0) * fractionBase) & 0x7fffffU;
        }

        packet[offset] = static_cast<uint8>((packet[offset] & 0x3fU) | ((fraction & 0x3U) << 6));
        packet[offset + 1] = static_cast<uint8>((fraction >> 2) & 0xffU);
        packet[offset + 2] = static_cast<uint8>((fraction >> 10) & 0xffU);
        packet[offset + 3] = static_cast<uint8>(((fraction >> 18) & 0x1fU) | ((scale & 0x7) << 5));
        packet[offset + 4] = static_cast<uint8>((packet[offset + 4] & 0xc0U) | ((scale >> 3) & 0x1fU) | (negative ? 0x20U : 0U));
    }
    uint32 ReadBitsLE(const FByteArray& data, int32 bitOffset, int32 bitCount)
    {
        uint32 value = 0;

        for (int32 bit = 0; bit < bitCount; ++bit)
        {
            const int32 absolute = bitOffset + bit;
            const size_t byteIndex = static_cast<size_t>(absolute / 8);

            if (byteIndex >= data.size())
            {
                break;
            }

            if ((data[byteIndex] & (1U << (absolute % 8))) != 0)
            {
                value |= 1U << bit;
            }
        }

        return value;
    }
    int32 ReadPacked14(const FByteArray& data, size_t offset)
    {
        if (offset + 1 >= data.size())
        {
            return 0;
        }

        return static_cast<int32>((data[offset] >> 2) | (data[offset + 1] << 6));
    }
    std::wstring DecodeSlotName(const FByteArray& frame)
    {
        FByteArray bytes;
        bytes.reserve(19);

        for (int32 i = 0; i < 19; ++i)
        {
            const uint8 decoded = static_cast<uint8>(ReadBitsLE(frame, 538 + i * 8, 8));

            if (decoded == 0)
            {
                break;
            }

            bytes.push_back(decoded);
        }

        return FSphereEmuProtocol::FromCp1251(bytes);
    }

    bool MatchesServerFrame(const FByteArray& frame, size_t size, uint8 marker, std::optional<uint8> state = std::nullopt)
    {
        if (frame.size() != size || Common::U16LEOr(frame, 2) != SferaProtocol::ServerFrameOpcode) { return false; }
        if (frame[9] != SferaProtocol::ServerChannelByte || frame[10] != SferaProtocol::ServerFamilyByte || frame[11] != marker) { return false; }
        return !state || frame[12] == *state;
    }
}
void FSphereEmuProtocol::WriteU16LE(FByteArray& data, size_t offset, uint16 value)
{
    if (offset + 2 > data.size())
    {
        data.resize(offset + 2);
    }

    data[offset] = static_cast<uint8>(value & 0xff);
    data[offset + 1] = static_cast<uint8>((value >> 8) & 0xff);
}
uint16 FSphereEmuProtocol::ReadU16LE(const FByteArray& data, size_t offset)
{
    if (offset + 2 > data.size())
    {
        return 0;
    }

    return Common::U16LEOr(data, offset);
}
FByteArray FSphereEmuProtocol::ToCp1251(const std::wstring& text)
{
    return Common::WideToCp1251Bytes(text);
}
std::wstring FSphereEmuProtocol::FromCp1251(const FByteArray& bytes)
{
    return Common::Cp1251BytesToWide(bytes);
}
FByteArray FSphereEmuProtocol::BuildLegacyPacket(uint16 opcode, const FByteArray& payload, uint16& sequence, uint16 xorKey)
{
    const uint16 length = static_cast<uint16>(payload.size() + 8);
    FByteArray packet(length, 0);
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> delta(1, 4);
    sequence = static_cast<uint16>(sequence + static_cast<uint16>(delta(rng)));
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 4, sequence);
    WriteU16LE(packet, 6, opcode);
    std::copy(payload.begin(), payload.end(), packet.begin() + 8);
    int16 sum = 0;

    for (size_t i = 4; i < packet.size(); ++i)
    {
        sum = static_cast<int16>(sum + static_cast<int8>(packet[i]));
    }

    WriteU16LE(packet, 2, static_cast<uint16>(xorKey ^ static_cast<uint16>(sum)));
    return packet;
}
FByteArray FSphereEmuProtocol::BuildLoginPacket(uint16 localId, const std::wstring& login, const std::wstring& password)
{
    FByteArray strings = ToCp1251(login);
    strings.push_back(1);
    const auto passwordBytes = ToCp1251(password);
    strings.insert(strings.end(), passwordBytes.begin(), passwordBytes.end());
    strings.push_back(0);
    constexpr int stringBitOffset = 18 * 8 + 2;
    const int packetLength = (stringBitOffset + static_cast<int>(strings.size()) * 8 + 7) / 8;
    FByteArray packet(static_cast<size_t>(packetLength), 0);
    WriteU16LE(packet, 0, static_cast<uint16>(packetLength));
    WriteU16LE(packet, 2, SferaProtocol::GameFrameOpcode);
    const auto major = static_cast<uint8>((localId >> 8) & 0xff);
    const auto minor = static_cast<uint8>(localId & 0xff);
    packet[7] = major;
    packet[8] = minor;
    packet[11] = major;
    packet[12] = minor;
    int bitPosition = stringBitOffset;

    for (uint8 byte : strings)
    {
        WriteByteLsb(packet, bitPosition, byte);
        bitPosition += 8;
    }

    return packet;
}
std::string FSphereEmuProtocol::MakeCharacterName(const std::wstring& login)
{
    std::string name;

    for (wchar_t ch : login)
    {
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))
        {
            name.push_back(static_cast<char>(ch));
        }

        if (name.size() >= 12)
        {
            break;
        }
    }

    if (name.empty())
    {
        name = "CodexHero";
    }

    if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')))
    {
        name.insert(name.begin(), 'C');
    }

    if (name.size() < 3)
    {
        name += "Hero";
    }

    if (name.size() > 12)
    {
        name.resize(12);
    }

    return name;
}
std::array<uint8, 5> FSphereEmuProtocol::BuildCharacterCreationBytes(const FCharacterCreationAppearance& appearance)
{
    const int modelBase = std::clamp(appearance.ModelBase, 0, 255);
    const int modelFace = AppearanceModelValue(appearance.Face, modelBase);
    const int modelHair = AppearanceModelValue(appearance.Hair, modelBase);
    const int modelHairColor = AppearanceModelValue(appearance.HairColor, modelBase);
    const int modelTattoo = AppearanceModelValue(appearance.Tattoo, modelBase);
    int wireFace = modelFace;
    int wireHair = modelHair;
    int wireHairColor = modelHairColor;
    int wireTattoo = modelTattoo;

    if (appearance.Female)
    {
        wireFace = 256 - modelFace;
        wireHair = 255 - modelHair;
        wireHairColor = 255 - modelHairColor;
        wireTattoo = 255 - modelTattoo;
    }

    std::array<uint8, 5> bytes{};
    bytes[0] = static_cast<uint8>((wireFace & 0x03) << 6);
    bytes[1] = static_cast<uint8>(((wireFace >> 2) & 0x3f) | ((wireHair & 0x03) << 6));
    bytes[2] = static_cast<uint8>(((wireHair >> 2) & 0x3f) | ((wireHairColor & 0x03) << 6));
    bytes[3] = static_cast<uint8>(((wireHairColor >> 2) & 0x3f) | ((wireTattoo & 0x03) << 6));
    bytes[4] = static_cast<uint8>((wireTattoo >> 2) & 0x3f);
    return bytes;
}
FByteArray FSphereEmuProtocol::BuildCharacterSelectPacket(uint16 localId, int32 slot)
{
    constexpr uint16 length = 0x15;
    FByteArray packet(length, 0);
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 2, SferaProtocol::GameFrameOpcode);
    packet[6] = SferaProtocol::ClientActionByte;
    packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
    packet[8] = static_cast<uint8>(localId & 0xff);
    packet[9] = SferaProtocol::ServerChannelByte;
    packet[10] = SferaProtocol::ServerFamilyByte;
    packet[17] = static_cast<uint8>((Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount) + 1) * SferaProtocol::SlotWireStride);
    return packet;
}
FByteArray FSphereEmuProtocol::BuildCreateCharacterPacket(uint16 localId, int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance)
{
    const auto nameCheck = EncodeCharacterNameCheck(name);
    const auto length = static_cast<uint16>(25 + nameCheck.size());
    FByteArray packet(length, 0);
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 2, SferaProtocol::GameFrameOpcode);
    packet[6] = SferaProtocol::ClientActionByte;
    packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
    packet[8] = static_cast<uint8>(localId & 0xff);
    packet[9] = SferaProtocol::ServerChannelByte;
    packet[10] = SferaProtocol::ServerFamilyByte;
    packet[13] = SferaProtocol::ServerChannelByte;
    packet[14] = SferaProtocol::ServerFamilyByte;
    packet[15] = SferaProtocol::CharacterSelectMarker;
    packet[16] = SferaProtocol::CreateCharacterAction;
    packet[17] = static_cast<uint8>((Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount) + 1) * SferaProtocol::SlotWireStride);
    std::copy(nameCheck.begin(), nameCheck.end(), packet.begin() + 20);
    const auto charData = BuildCharacterCreationBytes(appearance);
    std::copy(charData.begin(), charData.end(), packet.end() - charData.size());
    return packet;
}
FByteArray FSphereEmuProtocol::BuildDeleteCharacterPacket(uint16 localId, int32 slot)
{
    constexpr uint16 length = 0x2a;
    FByteArray packet(length, 0);
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 2, SferaProtocol::GameFrameOpcode);
    packet[6] = SferaProtocol::ClientActionByte;
    packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
    packet[8] = static_cast<uint8>(localId & 0xff);
    packet[9] = SferaProtocol::ServerChannelByte;
    packet[10] = SferaProtocol::ServerFamilyByte;
    packet[13] = SferaProtocol::ServerChannelByte;
    packet[14] = SferaProtocol::ServerFamilyByte;
    packet[15] = SferaProtocol::CharacterSelectMarker;
    packet[16] = SferaProtocol::DeleteCharacterAction;
    packet[17] = static_cast<uint8>((Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount) + 1) * SferaProtocol::SlotWireStride);
    return packet;
}
FByteArray FSphereEmuProtocol::BuildIngameAckPacket(uint16 localId)
{
    constexpr uint16 length = 0x13;
    FByteArray packet(length, 0);
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 2, SferaProtocol::GameFrameOpcode);
    packet[6] = SferaProtocol::ClientActionByte;
    packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
    packet[8] = static_cast<uint8>(localId & 0xff);
    packet[9] = SferaProtocol::ServerChannelByte;
    packet[10] = SferaProtocol::ServerFamilyByte;
    return packet;
}
FByteArray FSphereEmuProtocol::BuildPositionPacket(uint16 localId, uint8 sequence, double x, double y, double z, double angle)
{
    constexpr uint16 length = 0x26;
    FByteArray packet(length, 0);
    WriteU16LE(packet, 0, length);
    WriteU16LE(packet, 2, SferaProtocol::GameFrameOpcode);
    packet[6] = SferaProtocol::ClientActionByte;
    packet[9] = SferaProtocol::ServerChannelByte;
    packet[10] = SferaProtocol::ServerFamilyByte;
    packet[11] = static_cast<uint8>((localId >> 8) & 0xff);
    packet[12] = static_cast<uint8>(localId & 0xff);
    packet[17] = sequence;
    WriteClientCoordinate(packet, 21, x);
    WriteClientCoordinate(packet, 25, y);
    WriteClientCoordinate(packet, 29, z);
    WriteClientCoordinate(packet, 33, angle);
    return packet;
}
FByteArray FSphereEmuProtocol::EncodeClientPacket(const FByteArray& decoded)
{
    constexpr std::array<uint8, 9> encodingMask =
    {
        0x4B, 0x0D, 0xEF, 0x60, 0xC9, 0x9A, 0x70, 0x0E, 0x03
    };
    constexpr size_t start = 9;

    if (decoded.size() <= start)
    {
        return decoded;
    }

    FByteArray encoded = decoded;
    uint8 mask3 = 0;

    for (size_t i = 0; i + start < decoded.size(); ++i)
    {
        const uint8 current = decoded[i + start];
        encoded[i + start] = static_cast<uint8>(current ^ encodingMask[i % encodingMask.size()] ^ mask3);
        mask3 = static_cast<uint8>(current * i + 2 * mask3);
    }

    return encoded;
}
bool FSphereEmuProtocol::LooksLikeCannotConnect(const FByteArray& frame) { return MatchesServerFrame(frame, SferaProtocol::ShortStatusFrameSize, SferaProtocol::CannotConnectMarker); }
bool FSphereEmuProtocol::LooksLikeCharacterSelectStart(const FByteArray& frame) { return MatchesServerFrame(frame, SferaProtocol::CharacterSelectStartFrameSize, SferaProtocol::CharacterSelectMarker, SferaProtocol::CharacterSelectStartState); }
bool FSphereEmuProtocol::LooksLikeEmptyCharacterSlot(const FByteArray& frame) { return MatchesServerFrame(frame, SferaProtocol::CharacterSlotFrameSize, SferaProtocol::CharacterSlotMarker, SferaProtocol::EmptySlotState); }
bool FSphereEmuProtocol::LooksLikeCharacterSlot(const FByteArray& frame) { return MatchesServerFrame(frame, SferaProtocol::CharacterSlotFrameSize, SferaProtocol::CharacterSlotMarker); }
bool FSphereEmuProtocol::LooksLikeNameCheckPassed(const FByteArray& frame) { return MatchesServerFrame(frame, SferaProtocol::ShortStatusFrameSize, SferaProtocol::CharacterSelectMarker, SferaProtocol::NameCheckPassedState); }
bool FSphereEmuProtocol::LooksLikeNameAlreadyExists(const FByteArray& frame) { return MatchesServerFrame(frame, SferaProtocol::ShortStatusFrameSize, SferaProtocol::NameAlreadyExistsMarker, SferaProtocol::NameAlreadyExistsState); }
FCharacterSlot FSphereEmuProtocol::ParseCharacterSlot(const FByteArray& frame, int32 slot, const FCharacterAppearanceRules& rules)
{
    FCharacterSlot out;
    out.Slot = slot;

    if (!LooksLikeCharacterSlot(frame))
    {
        out.CanCreate = false;
        return out;
    }

    out.Name = DecodeSlotName(frame);
    out.Present = !out.Name.empty();
    out.CanCreate = !out.Present && frame.size() == 108 && frame[12] == 0x79;
    out.Female = ReadBitsLE(frame, 530, 8) != 0;
    out.MaxHp = static_cast<int32>(ReadBitsLE(frame, 106, 16));
    out.MaxMp = static_cast<int32>(ReadBitsLE(frame, 122, 16));
    out.Strength = static_cast<int32>(ReadBitsLE(frame, 138, 16));
    out.Dexterity = static_cast<int32>(ReadBitsLE(frame, 154, 16));
    out.Accuracy = static_cast<int32>(ReadBitsLE(frame, 170, 16));
    out.Endurance = static_cast<int32>(ReadBitsLE(frame, 186, 16));
    out.Earth = static_cast<int32>(ReadBitsLE(frame, 202, 16));
    out.Air = static_cast<int32>(ReadBitsLE(frame, 218, 16));
    out.Water = static_cast<int32>(ReadBitsLE(frame, 234, 16));
    out.Fire = static_cast<int32>(ReadBitsLE(frame, 250, 16));
    out.PhysicalDefense = static_cast<int32>(ReadBitsLE(frame, 266, 16));
    out.MagicalDefense = static_cast<int32>(ReadBitsLE(frame, 282, 16));
    out.Karma = static_cast<int32>(ReadBitsLE(frame, 298, 8));
    out.MaxSatiety = static_cast<int32>(ReadBitsLE(frame, 306, 16));
    out.TitleId = static_cast<int32>(ReadBitsLE(frame, 322, 16));
    out.DegreeId = static_cast<int32>(ReadBitsLE(frame, 338, 16));
    out.TitleXp = static_cast<int32>(ReadBitsLE(frame, 354, 32));
    out.DegreeXp = static_cast<int32>(ReadBitsLE(frame, 386, 32));
    out.CurrentSatiety = static_cast<int32>(ReadBitsLE(frame, 418, 16));
    out.CurrentHp = static_cast<int32>(ReadBitsLE(frame, 434, 16));
    out.CurrentMp = static_cast<int32>(ReadBitsLE(frame, 450, 16));
    out.TitleStats = static_cast<int32>(ReadBitsLE(frame, 466, 16));
    out.DegreeStats = static_cast<int32>(ReadBitsLE(frame, 482, 16));
    out.PhysicalAttack = 0;
    out.MagicalAttack = 0;
    out.TitleLevel = std::max(1, (out.TitleXp / std::max(1, out.TitleNextXp)) + 1);
    out.DegreeLevel = std::max(1, (out.DegreeXp / std::max(1, out.DegreeNextXp)) + 1);
    out.Face = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 690, 8)), out.Female ? rules.FemaleFaceCount : rules.MaleFaceCount, rules.ModelBase);
    out.Hair = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 698, 8)), out.Female ? rules.FemaleHairCount : rules.MaleHairCount, rules.ModelBase);
    out.HairColor = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 706, 8)), rules.HairColorCount, rules.ModelBase);
    out.Tattoo = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 714, 8)), rules.TattooCount, rules.ModelBase);
    return out;
}
