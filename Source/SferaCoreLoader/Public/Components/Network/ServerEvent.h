#pragma once
#include "Core/Types.h"
#include "Network/LoginClient.h"

struct FServerPacketRoute
{
    uint16 Length = 0;
    uint16 Opcode = 0;
    uint8 Channel = 0;
    uint8 Family = 0;
    uint8 Service = 0;
    uint8 Method = 0;
    bool HasRoute = false;
};

enum class EServerEventSubsystem : uint8
{
    Session,
    Character,
    World,
    Chat,
    Clock,
    Map,
    Clan,
    Diagnostics
};

struct FSessionEstablishedEvent
{
    uint16 LocalEntityId = 0;
};

struct FSessionClosedEvent
{
    std::string Reason;
};

struct FCharacterRosterEvent
{
    std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount> Slots{};
};

struct FCharacterActivatedEvent
{
    int32 Slot = 0;
    std::optional<FServerWorldPosition> InitialPosition;
};

struct FWorldPositionEvent
{
    FServerWorldPosition Position;
};

struct FRemotePlayerSpawnEvent
{
    uint64 EntityId = 0;
    std::string Name;
    FServerWorldPosition Position;
    FCharacterCreationAppearance Appearance;
};

struct FRemotePlayerMoveEvent
{
    uint64 EntityId = 0;
    FServerWorldPosition Position;
};

struct FRemotePlayerDespawnEvent
{
    uint64 EntityId = 0;
};

struct FRemotePlayerAppearanceEvent
{
    std::string Name;
    FCharacterCreationAppearance Appearance;
};

struct FChatMessageEvent
{
    uint8 Channel = 1;
    std::string Sender;
    std::string Text;
};

struct FGameClockEvent
{
    float DayFraction = 0.0f;
    int32 Day = 0;
    int32 Month = 0;
    int32 Year = 0;
};

struct FMapDescriptorEvent
{
    std::string SpriteName = "mhp1";
    std::array<int32, 8> Projection{};
    bool HasProjection = false;
};

struct FClanStateEvent
{
    bool Known = true;
    std::optional<bool> Available;
    std::optional<int32> Rank;
    std::optional<std::string> Name;
};

struct FUnhandledServerPacketEvent
{
    uint32 PacketCount = 0;
    size_t ByteCount = 0;
};

using FServerEventPayload = std::variant<FSessionEstablishedEvent, FSessionClosedEvent, FCharacterRosterEvent, FCharacterActivatedEvent, FWorldPositionEvent, FRemotePlayerSpawnEvent, FRemotePlayerMoveEvent, FRemotePlayerDespawnEvent, FRemotePlayerAppearanceEvent, FChatMessageEvent, FGameClockEvent, FMapDescriptorEvent, FClanStateEvent, FUnhandledServerPacketEvent>;

struct FServerEvent
{
    uint64 Sequence = 0;
    EServerEventSubsystem Subsystem = EServerEventSubsystem::Diagnostics;
    FServerPacketRoute Route;
    FServerEventPayload Payload = FUnhandledServerPacketEvent{};
};

struct FPacketDispatchContext
{
    uint16 LocalEntityId = 0;
};
