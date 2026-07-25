#include "Components/Network/GamePacketDispatcher.h"
#include "Common/SferaGameConstants.h"
#include "Common/TextEncoding.h"
#include "Network/SphereEmuProtocol.h"
#include <numbers>

namespace
{
FServerPacketRoute ParseRoute(const std::vector<uint8>& frame)
{
    FServerPacketRoute route;
    if (frame.size() >= 2) { route.Length = static_cast<uint16>(frame[0] | (static_cast<uint16>(frame[1]) << 8)); }
    if (frame.size() >= 4) { route.Opcode = static_cast<uint16>(frame[2] | (static_cast<uint16>(frame[3]) << 8)); }
    if (frame.size() >= 13)
    {
        route.Channel = frame[9];
        route.Family = frame[10];
        route.Service = frame[11];
        route.Method = frame[12];
        route.HasRoute = true;
    }
    return route;
}

int32 ReadI32(const std::vector<uint8>& data, size_t offset)
{
    const uint32 value = static_cast<uint32>(data[offset]) | (static_cast<uint32>(data[offset + 1]) << 8) | (static_cast<uint32>(data[offset + 2]) << 16) | (static_cast<uint32>(data[offset + 3]) << 24);
    return static_cast<int32>(value);
}

uint32 ReadBitsLE(const FByteArray& data, size_t bitOffset, size_t bitCount)
{
    uint32 value = 0;
    for (size_t bit = 0; bit < bitCount && bit < 32; ++bit)
    {
        const size_t absolute = bitOffset + bit;
        const size_t byteIndex = absolute / 8;
        if (byteIndex >= data.size()) { break; }
        if ((data[byteIndex] & static_cast<uint8>(1U << (absolute % 8))) != 0) { value |= 1U << bit; }
    }
    return value;
}

double DecodeServerCoordinateBits(const FByteArray& frame, size_t bitOffset)
{
    FByteArray encoded(4, 0);
    const uint32 value = ReadBitsLE(frame, bitOffset, 32);
    for (size_t index = 0; index < encoded.size(); ++index) { encoded[index] = static_cast<uint8>((value >> (index * 8)) & 0xff); }
    const int32 scale = encoded[3] & 0x7f;
    if (scale == 58) { return 0.0; }
    const int32 sign = (encoded[3] & 0x80) != 0 ? -1 : 1;
    const bool oddStep = (encoded[2] & 0x80) != 0;
    const int32 mantissaBits = ((encoded[2] & 0x7f) << 16) | (encoded[1] << 8) | encoded[0];
    const double mantissa = 1.0 + static_cast<double>(mantissaBits) / 8388608.0;
    int32 exponent = 0;
    if (scale < 69) { exponent = 11 - (2 * (69 - scale) - (oddStep ? 1 : 0)); }
    else { exponent = 11 + (2 * (scale - 69) + (oddStep ? 1 : 0)); }
    return static_cast<double>(sign) * std::ldexp(mantissa, exponent);
}

bool IsSaneWorldPosition(const FServerWorldPosition& position)
{
    return std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z) && std::isfinite(position.Angle) && std::abs(position.X) < 20000.0 && std::abs(position.Y) < 20000.0 && std::abs(position.Z) < 20000.0;
}

int32 DecodeAppearanceIndex(uint32 modelValue, bool female, int32 count, bool face)
{
    const int32 value = static_cast<int32>(modelValue);
    const int32 direct = value - Sfera::CharacterModelBase;
    if (direct >= 0 && direct < count) { return direct; }
    const int32 decoded = (face ? 256 : 255) - value - Sfera::CharacterModelBase;
    return female && decoded >= 0 && decoded < count ? decoded : 0;
}

bool DecodeRemotePlayerSpawn(const FByteArray& frame, FRemotePlayerSpawnEvent& event)
{
    constexpr size_t contentBit = 7 * 8;
    if (frame.size() < 31 || FSphereEmuProtocol::ReadU16LE(frame, 2) != SferaProtocol::ServerFrameOpcode || FSphereEmuProtocol::ReadU16LE(frame, 0) != frame.size()) { return false; }
    const uint32 objectType = ReadBitsLE(frame, contentBit + 18, 10);
    const uint32 actionType = ReadBitsLE(frame, contentBit + 29, 8);
    if ((objectType != 2 && objectType != 4) || actionType != 124) { return false; }
    event.EntityId = ReadBitsLE(frame, contentBit, 16);
    if (event.EntityId == 0) { return false; }
    event.Position.EntityId = event.EntityId;
    event.Position.CharacterEntity = true;
    event.Position.X = DecodeServerCoordinateBits(frame, contentBit + 37);
    event.Position.Y = DecodeServerCoordinateBits(frame, contentBit + 69);
    event.Position.Z = DecodeServerCoordinateBits(frame, contentBit + 101);
    event.Position.Angle = static_cast<double>(ReadBitsLE(frame, contentBit + 133, 8)) * std::numbers::pi / 128.0;
    if (!IsSaneWorldPosition(event.Position)) { return false; }
    const size_t nameLength = ReadBitsLE(frame, contentBit + 173, 8);
    const size_t nameBit = contentBit + 181;
    const size_t femaleBit = nameBit + nameLength * 8 + 41;
    if (nameLength > 64 || femaleBit >= frame.size() * 8) { return false; }
    FByteArray nameBytes;
    nameBytes.reserve(nameLength);
    for (size_t index = 0; index < nameLength; ++index) { nameBytes.push_back(static_cast<uint8>(ReadBitsLE(frame, nameBit + index * 8, 8))); }
    event.Name = Common::WideToUtf8(Common::Cp1251BytesToWide(nameBytes));
    event.Appearance.Female = ReadBitsLE(frame, femaleBit, 1) != 0;
    event.Appearance.ModelBase = Sfera::CharacterModelBase;
    event.Appearance.Face = DecodeAppearanceIndex(ReadBitsLE(frame, contentBit + 141, 8), event.Appearance.Female, event.Appearance.Female ? 12 : 13, true);
    event.Appearance.Hair = DecodeAppearanceIndex(ReadBitsLE(frame, contentBit + 149, 8), event.Appearance.Female, event.Appearance.Female ? 5 : 3, false);
    event.Appearance.HairColor = DecodeAppearanceIndex(ReadBitsLE(frame, contentBit + 157, 8), event.Appearance.Female, 4, false);
    event.Appearance.Tattoo = DecodeAppearanceIndex(ReadBitsLE(frame, contentBit + 165, 8), event.Appearance.Female, 4, false);
    return !event.Name.empty();
}

int32 DecodeWrappedFraction(uint32 encoded, int32 base)
{
    int32 value = static_cast<int32>(encoded) - base;
    if (value < -2048) { value += 4096; }
    else if (value > 2047) { value -= 4096; }
    return value;
}

bool DecodeRemotePlayerMove(const FByteArray& frame, FRemotePlayerMoveEvent& event)
{
    if (frame.size() != 23 || frame[0] != 0x17 || frame[1] != 0 || FSphereEmuProtocol::ReadU16LE(frame, 2) != SferaProtocol::ServerFrameOpcode || frame[11] != 0x2d || frame[15] != 0x6a || frame[16] != 0x10 || (frame[5] & 1) == 0 || (frame[12] & 0x1f) != 0x11) { return false; }
    const uint32 xInteger = ((frame[5] >> 1) & 0x7f) | (static_cast<uint32>(frame[6]) << 7) | (static_cast<uint32>(frame[7] & 1) << 15);
    const uint32 yInteger = (frame[7] >> 1) | (static_cast<uint32>(frame[8] & 0x3f) << 7);
    const uint32 zInteger = (frame[8] >> 6) | (static_cast<uint32>(frame[9]) << 2) | (static_cast<uint32>(frame[10]) << 10);
    event.EntityId = (frame[12] >> 5) | (static_cast<uint32>(frame[13]) << 3) | (static_cast<uint32>(frame[14] & 0x1f) << 11);
    if (event.EntityId == 0) { return false; }
    const uint32 xFraction = (frame[17] >> 2) | (static_cast<uint32>(frame[18] & 0x3f) << 6);
    const uint32 yFraction = (frame[18] >> 6) | (static_cast<uint32>(frame[19]) << 2) | (static_cast<uint32>(frame[20] & 3) << 10);
    const uint32 zFraction = (frame[20] >> 2) | (static_cast<uint32>(frame[21] & 0x3f) << 6);
    const uint8 angleByte = static_cast<uint8>((frame[21] >> 6) | (frame[22] << 2));
    event.Position.EntityId = event.EntityId;
    event.Position.CharacterEntity = true;
    event.Position.X = static_cast<int32>(xInteger) - 32768 + static_cast<double>(DecodeWrappedFraction(xFraction, 4094)) / 64.0;
    event.Position.Y = static_cast<int32>(yInteger) - 1200 + static_cast<double>(2047 - static_cast<int32>(yFraction)) / 64.0;
    event.Position.Z = static_cast<int32>(zInteger) - 32768 + static_cast<double>(DecodeWrappedFraction(zFraction, 4094)) / 64.0;
    event.Position.Angle = static_cast<double>(static_cast<int8>(angleByte)) * 2.0 * std::numbers::pi / 256.0;
    return IsSaneWorldPosition(event.Position);
}

bool DecodeEntityDespawn(const FByteArray& frame, FRemotePlayerDespawnEvent& event)
{
    if (frame.size() != 11 || frame[0] != 0x0b || frame[1] != 0 || FSphereEmuProtocol::ReadU16LE(frame, 2) != SferaProtocol::ServerFrameOpcode || frame[4] != 0 || frame[5] != 0 || frame[6] != 0 || frame[9] != 0 || frame[10] != 0) { return false; }
    event.EntityId = static_cast<uint64>(frame[7]) | (static_cast<uint64>(frame[8]) << 8);
    return event.EntityId != 0;
}

bool IsChatHeader(const FByteArray& frame)
{
    return frame.size() >= 20 && FSphereEmuProtocol::ReadU16LE(frame, 0) == frame.size() && FSphereEmuProtocol::ReadU16LE(frame, 2) == SferaProtocol::ServerFrameOpcode && frame[5] == 0x22 && frame[6] == 0xe4 && frame[7] == 0x45 && frame[8] == 0xf0 && frame[9] == 0x14 && frame[10] == 0x80;
}

bool IsChatTextFrame(const FByteArray& frame)
{
    return frame.size() >= 14 && FSphereEmuProtocol::ReadU16LE(frame, 0) == frame.size() && FSphereEmuProtocol::ReadU16LE(frame, 2) == SferaProtocol::ServerFrameOpcode && frame[5] == 0x22 && frame[6] == 0xe4 && frame[7] == 0x45 && frame[8] == 0xf0 && frame[9] == 0x14 && frame[10] == 0xc0;
}

std::string DecodeCp1251Utf8(const FByteArray& bytes)
{
    return Common::WideToUtf8(Common::Cp1251BytesToWide(bytes));
}

constexpr char kAppearanceSenderPrefix = '~';
constexpr char kAppearanceSenderDelimiter = '|';

int32 AppearanceValue(char value)
{
    return value >= 'A' && value <= 'P' ? value - 'A' : -1;
}

std::optional<FRemotePlayerAppearanceEvent> DecodeAppearanceSender(std::string_view sender)
{
    if (sender.size() < 8 || sender.front() != kAppearanceSenderPrefix) { return std::nullopt; }
    const size_t delimiter = sender.rfind(kAppearanceSenderDelimiter);
    if (delimiter == std::string_view::npos || delimiter <= 1 || sender.size() - delimiter - 1 != 5) { return std::nullopt; }
    const std::string_view name = sender.substr(1, delimiter - 1);
    const std::wstring wideName = Common::Utf8ToWide(name);
    const FByteArray nameBytes = Common::WideToCp1251Bytes(wideName);
    if (wideName.empty() || nameBytes.empty() || nameBytes.size() > 19) { return std::nullopt; }
    const std::string_view fields = sender.substr(delimiter + 1);
    std::array<int32, 5> values{};
    for (size_t index = 0; index < values.size(); ++index)
    {
        values[index] = AppearanceValue(fields[index]);
        if (values[index] < 0) { return std::nullopt; }
    }
    const bool female = values[0] != 0;
    if (values[0] > 1 || values[1] >= (female ? 12 : 13) || values[2] >= (female ? 5 : 3) || values[3] >= 4 || values[4] >= 4) { return std::nullopt; }
    FRemotePlayerAppearanceEvent event;
    event.Name.assign(name);
    event.Appearance = FCharacterCreationAppearance{female, Sfera::CharacterModelBase, values[1], values[2], values[3], values[4]};
    return event;
}

std::string ExtractVisibleChatText(std::string text)
{
    while (!text.empty() && text.back() == '\0') { text.pop_back(); }
    const size_t marker = text.find("</l>: ");
    if (marker != std::string::npos) { text.erase(0, marker + 6); }
    return text;
}

bool DecodeGameClock(const std::vector<uint8>& frame, FGameClockEvent& event)
{
    if (frame.size() != 56 || FSphereEmuProtocol::ReadU16LE(frame, 2) != 300 || frame[9] != 0x08 || frame[10] != 0x40 || frame[11] != 0x20 || frame[12] != 0x10) { return false; }
    const int32 seconds = ((frame[13] & 0x0f) - 1) * 12;
    const int32 minutes = ((frame[14] & 0x03) << 4) | (frame[13] >> 4);
    const int32 hours = (frame[14] >> 2) & 0x1f;
    if (seconds < 0 || seconds >= 60 || minutes < 0 || minutes >= 60 || hours < 0 || hours >= 24) { return false; }
    event.DayFraction = static_cast<float>(hours * 3600 + minutes * 60 + seconds) / 86400.0f;
    event.Day = ((frame[15] & 0x0f) << 1) | ((frame[14] >> 7) & 0x01);
    event.Month = (frame[15] >> 4) & 0x0f;
    event.Year = ((((frame[17] & 0x03) << 8) | frame[16]) + 7800);
    return true;
}

std::string DecodeClanName(const std::vector<uint8>& frame, size_t offset)
{
    FByteArray bytes;
    while (offset < frame.size() && frame[offset] != 0) { bytes.push_back(frame[offset++]); }
    return Common::WideToUtf8(Common::Cp1251BytesToWide(bytes));
}

bool DecodeClan(const std::vector<uint8>& frame, FClanStateEvent& event)
{
    if (frame.size() < 14 || static_cast<uint16>(frame[2] | (static_cast<uint16>(frame[3]) << 8)) != SferaProtocol::ServerFrameOpcode || frame[9] != SferaProtocol::ServerChannelByte || frame[10] != SferaProtocol::ServerFamilyByte || frame[11] != 12 || frame[12] != 22) { return false; }
    const uint8 code = frame[13];
    if (frame.size() == 14)
    {
        if (code == 0 || code == 0xff) { event.Available = false; event.Rank = -1; event.Name = std::string{}; }
        else if (code >= 1 && code <= 4) { event.Available = true; event.Rank = static_cast<int32>(code) - 1; }
    }
    else if (code == 5) { event.Available = false; event.Rank = -1; event.Name = std::string{}; }
    else if (code == 6) { event.Available = true; event.Rank = 3; event.Name = DecodeClanName(frame, 14); }
    else if (code <= 3) { event.Available = true; event.Rank = static_cast<int32>(code); }
    return event.Available.has_value() || event.Rank.has_value() || event.Name.has_value();
}

bool IsKnownMapSprite(std::string_view value)
{
    static constexpr std::array<std::string_view, 13> names{"mhp1", "hmp1", "feb1", "smp1", "smp2", "smp3", "smp4", "smp5", "smp6", "cmp1", "cmp2", "cmp3", "cmp4"};
    return std::find(names.begin(), names.end(), value) != names.end();
}

bool ProjectionIsValid(const std::array<int32, 8>& p)
{
    const int64 worldWidth = static_cast<int64>(p[2]) - p[0];
    const int64 worldHeight = static_cast<int64>(p[1]) - p[3];
    const int64 mapWidth = static_cast<int64>(p[6]) - p[4];
    const int64 mapHeight = static_cast<int64>(p[7]) - p[5];
    return worldWidth > 0 && worldHeight > 0 && worldWidth < 10000000 && worldHeight < 10000000 && mapWidth > 0 && mapHeight > 0 && mapWidth <= 4096 && mapHeight <= 4096;
}

bool DecodeMapDescriptor(const std::vector<uint8>& frame, FMapDescriptorEvent& event)
{
    constexpr size_t descriptorSize = 280;
    constexpr size_t projectionOffset = 128;
    constexpr size_t spriteOffset = 244;
    if (frame.size() < descriptorSize) { return false; }
    for (size_t base = 0; base + descriptorSize <= frame.size(); ++base)
    {
        const std::string sprite(reinterpret_cast<const char*>(frame.data() + base + spriteOffset), 4);
        if (!IsKnownMapSprite(sprite)) { continue; }
        std::array<int32, 8> projection{};
        for (size_t index = 0; index < projection.size(); ++index) { projection[index] = ReadI32(frame, base + projectionOffset + (index + 1) * 4); }
        if (!ProjectionIsValid(projection)) { continue; }
        event.SpriteName = sprite;
        event.Projection = projection;
        event.HasProjection = true;
        return true;
    }
    return false;
}
}

FServerEvent FGamePacketDispatcher::MakeEvent(EServerEventSubsystem subsystem, FServerPacketRoute route, FServerEventPayload payload)
{
    FServerEvent event;
    event.Sequence = NextSequence.fetch_add(1);
    event.Subsystem = subsystem;
    event.Route = route;
    event.Payload = std::move(payload);
    return event;
}

std::vector<FServerEvent> FGamePacketDispatcher::DecodeFrames(const std::vector<std::vector<uint8>>& frames, const FPacketDispatchContext& context)
{
    std::vector<FServerEvent> events;
    uint32 unhandledPackets = 0;
    size_t unhandledBytes = 0;
    FServerPacketRoute lastUnhandledRoute;
    for (const std::vector<uint8>& frame : frames)
    {
        std::vector<FServerEvent> decoded = DecodeFrame(frame, context);
        if (decoded.size() == 1 && std::holds_alternative<FUnhandledServerPacketEvent>(decoded.front().Payload))
        {
            ++unhandledPackets;
            unhandledBytes += frame.size();
            lastUnhandledRoute = decoded.front().Route;
            continue;
        }
        events.insert(events.end(), std::make_move_iterator(decoded.begin()), std::make_move_iterator(decoded.end()));
    }
    if (unhandledPackets > 0) { events.push_back(MakeEvent(EServerEventSubsystem::Diagnostics, lastUnhandledRoute, FUnhandledServerPacketEvent{unhandledPackets, unhandledBytes})); }
    return events;
}

std::vector<FServerEvent> FGamePacketDispatcher::DecodeFrame(const std::vector<uint8>& frame, const FPacketDispatchContext& context)
{
    const FServerPacketRoute route = ParseRoute(frame);
    std::vector<FServerEvent> events;
    bool handled = false;

    FRemotePlayerSpawnEvent spawn;
    if (DecodeRemotePlayerSpawn(frame, spawn))
    {
        handled = true;
        if (context.LocalEntityId != 0 && spawn.EntityId == context.LocalEntityId) { events.push_back(MakeEvent(EServerEventSubsystem::World, route, FWorldPositionEvent{spawn.Position})); }
        else { events.push_back(MakeEvent(EServerEventSubsystem::World, route, std::move(spawn))); }
    }

    FRemotePlayerMoveEvent move;
    if (!handled && DecodeRemotePlayerMove(frame, move))
    {
        handled = true;
        if (context.LocalEntityId != 0 && move.EntityId == context.LocalEntityId) { events.push_back(MakeEvent(EServerEventSubsystem::World, route, FWorldPositionEvent{move.Position})); }
        else { events.push_back(MakeEvent(EServerEventSubsystem::World, route, std::move(move))); }
    }

    FRemotePlayerDespawnEvent despawn;
    if (!handled && DecodeEntityDespawn(frame, despawn))
    {
        handled = true;
        if (context.LocalEntityId == 0 || despawn.EntityId != context.LocalEntityId) { events.push_back(MakeEvent(EServerEventSubsystem::World, route, despawn)); }
    }

    if (!handled && IsChatHeader(frame))
    {
        handled = true;
        const size_t encodedNameLength = frame[17];
        if (encodedNameLength > 0 && encodedNameLength <= 65)
        {
            const size_t nameLength = encodedNameLength - 1;
            const size_t channelOffset = 19 + nameLength;
            if (channelOffset < frame.size())
            {
                FByteArray nameBytes(frame.begin() + 18, frame.begin() + static_cast<std::ptrdiff_t>(18 + nameLength));
                FPendingChatMessage pending;
                pending.ExpectedBytes = static_cast<size_t>(frame[12]) + static_cast<size_t>(frame[13]) * 255;
                pending.Channel = frame[channelOffset];
                pending.Sender = DecodeCp1251Utf8(nameBytes);
                if (const std::optional<FRemotePlayerAppearanceEvent> appearance = DecodeAppearanceSender(pending.Sender))
                {
                    pending.Suppress = true;
                    events.push_back(MakeEvent(EServerEventSubsystem::World, route, *appearance));
                }
                pending.Bytes.reserve(pending.ExpectedBytes + 1);
                PendingChat = std::move(pending);
            }
            else { PendingChat.reset(); }
        }
        else { PendingChat.reset(); }
    }
    else if (!handled && IsChatTextFrame(frame))
    {
        handled = true;
        if (PendingChat)
        {
            const size_t chunkLength = ReadBitsLE(frame, 11 * 8 + 5, 8);
            const size_t textBitOffset = 11 * 8 + 13;
            if (textBitOffset + chunkLength * 8 <= frame.size() * 8)
            {
                for (size_t index = 0; index < chunkLength; ++index) { PendingChat->Bytes.push_back(static_cast<uint8>(ReadBitsLE(frame, textBitOffset + index * 8, 8))); }
                if (PendingChat->Bytes.size() >= PendingChat->ExpectedBytes)
                {
                    FByteArray complete = std::move(PendingChat->Bytes);
                    if (complete.size() > PendingChat->ExpectedBytes) { complete.resize(PendingChat->ExpectedBytes); }
                    const bool suppress = PendingChat->Suppress;
                    FChatMessageEvent chat;
                    chat.Channel = PendingChat->Channel;
                    chat.Sender = std::move(PendingChat->Sender);
                    chat.Text = ExtractVisibleChatText(DecodeCp1251Utf8(complete));
                    PendingChat.reset();
                    if (!suppress && !chat.Text.empty()) { events.push_back(MakeEvent(EServerEventSubsystem::Chat, route, std::move(chat))); }
                }
            }
            else { PendingChat.reset(); }
        }
    }

    FGameClockEvent clock;
    if (DecodeGameClock(frame, clock)) { handled = true; events.push_back(MakeEvent(EServerEventSubsystem::Clock, route, clock)); }
    FClanStateEvent clan;
    if (DecodeClan(frame, clan)) { handled = true; events.push_back(MakeEvent(EServerEventSubsystem::Clan, route, std::move(clan))); }
    FMapDescriptorEvent map;
    if (DecodeMapDescriptor(frame, map)) { handled = true; events.push_back(MakeEvent(EServerEventSubsystem::Map, route, std::move(map))); }
    if (!handled)
    {
        const std::optional<FServerWorldPosition> position = context.LocalEntityId != 0 ? FSphereEmuProtocol::TryParseServerWorldPosition(frame, context.LocalEntityId) : FSphereEmuProtocol::TryParseServerWorldPosition(frame);
        if (position) { handled = true; events.push_back(MakeEvent(EServerEventSubsystem::World, route, FWorldPositionEvent{*position})); }
    }
    if (!handled) { events.push_back(MakeEvent(EServerEventSubsystem::Diagnostics, route, FUnhandledServerPacketEvent{1, frame.size()})); }
    return events;
}

std::vector<FServerEvent> FGamePacketDispatcher::BuildLoginSnapshot(const FLoginProbeResult& result)
{
    PendingChat.reset();
    if (!result.CharacterSelectReady || !result.Session) { return {}; }
    std::vector<FServerEvent> events;
    events.push_back(MakeEvent(EServerEventSubsystem::Session, {}, FSessionEstablishedEvent{result.LocalId}));
    events.push_back(MakeEvent(EServerEventSubsystem::Character, {}, FCharacterRosterEvent{result.CharacterSlots}));
    std::vector<FServerEvent> packetEvents = DecodeFrames(result.Frames, FPacketDispatchContext{result.LocalId});
    events.insert(events.end(), std::make_move_iterator(packetEvents.begin()), std::make_move_iterator(packetEvents.end()));
    return events;
}

FServerEvent FGamePacketDispatcher::BuildCharacterActivated(int32 slot, std::optional<FServerWorldPosition> initialPosition)
{
    if (!initialPosition)
    {
        FServerWorldPosition fallback;
        fallback.X = SferaProtocol::DefaultServerSpawnX;
        fallback.Y = SferaProtocol::DefaultServerSpawnY;
        fallback.Z = SferaProtocol::DefaultServerSpawnZ;
        fallback.Angle = SferaProtocol::DefaultServerSpawnAngle;
        initialPosition = fallback;
    }
    return MakeEvent(EServerEventSubsystem::Character, {}, FCharacterActivatedEvent{slot, std::move(initialPosition)});
}

FServerEvent FGamePacketDispatcher::BuildSessionClosed(std::string reason)
{
    PendingChat.reset();
    return MakeEvent(EServerEventSubsystem::Session, {}, FSessionClosedEvent{std::move(reason)});
}
