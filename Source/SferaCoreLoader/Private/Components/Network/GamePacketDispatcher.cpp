#include "Components/Network/GamePacketDispatcher.h"
#include "Common/SferaGameConstants.h"
#include "Common/TextEncoding.h"
#include "Network/SphereEmuProtocol.h"

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
    FGameClockEvent clock;
    if (DecodeGameClock(frame, clock)) { events.push_back(MakeEvent(EServerEventSubsystem::Clock, route, clock)); }
    FClanStateEvent clan;
    if (DecodeClan(frame, clan)) { events.push_back(MakeEvent(EServerEventSubsystem::Clan, route, std::move(clan))); }
    FMapDescriptorEvent map;
    if (DecodeMapDescriptor(frame, map)) { events.push_back(MakeEvent(EServerEventSubsystem::Map, route, std::move(map))); }
    const std::optional<FServerWorldPosition> position = context.LocalEntityId != 0 ? FSphereEmuProtocol::TryParseServerWorldPosition(frame, context.LocalEntityId) : FSphereEmuProtocol::TryParseServerWorldPosition(frame);
    if (position) { events.push_back(MakeEvent(EServerEventSubsystem::World, route, FWorldPositionEvent{*position})); }
    if (events.empty()) { events.push_back(MakeEvent(EServerEventSubsystem::Diagnostics, route, FUnhandledServerPacketEvent{1, frame.size()})); }
    return events;
}

std::vector<FServerEvent> FGamePacketDispatcher::BuildLoginSnapshot(const FLoginProbeResult& result)
{
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
    return MakeEvent(EServerEventSubsystem::Session, {}, FSessionClosedEvent{std::move(reason)});
}
