#pragma once
#include "Components/Network/ServerEvent.h"

using FGameStateChangeMask = uint32;

namespace GameStateChange
{
    inline constexpr FGameStateChangeMask None = 0;
    inline constexpr FGameStateChangeMask Session = 1u << 0;
    inline constexpr FGameStateChangeMask CharacterRoster = 1u << 1;
    inline constexpr FGameStateChangeMask ActiveCharacter = 1u << 2;
    inline constexpr FGameStateChangeMask WorldPosition = 1u << 3;
    inline constexpr FGameStateChangeMask Clock = 1u << 4;
    inline constexpr FGameStateChangeMask Map = 1u << 5;
    inline constexpr FGameStateChangeMask Clan = 1u << 6;
    inline constexpr FGameStateChangeMask Diagnostics = 1u << 7;
    inline constexpr FGameStateChangeMask All = Session | CharacterRoster | ActiveCharacter | WorldPosition | Clock | Map | Clan | Diagnostics;
}

struct FGameSessionState
{
    bool Connected = false;
    bool InWorld = false;
    uint16 LocalEntityId = 0;
    std::string LastDisconnectReason;
    uint64 Revision = 0;
};

struct FGameCharacterState
{
    std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount> Slots{};
    int32 SelectedSlot = 0;
    int32 ActiveSlot = -1;
    uint64 RosterRevision = 0;
    uint64 ActiveRevision = 0;
};

struct FGameWorldState
{
    bool HasPosition = false;
    FServerWorldPosition Position;
    uint64 Revision = 0;
};

struct FGameClockState
{
    bool Known = false;
    float DayFraction = 0.0f;
    int32 Day = 0;
    int32 Month = 0;
    int32 Year = 0;
    uint64 Revision = 0;
};

struct FGameMapState
{
    std::string SpriteName = "mhp1";
    std::array<int32, 8> Projection{};
    bool HasProjection = false;
    uint64 Revision = 0;
};

struct FGameClanState
{
    bool Known = false;
    bool Available = false;
    int32 Rank = -1;
    std::string Name;
    uint64 Revision = 0;
};

struct FGameDiagnosticsState
{
    uint64 LastServerSequence = 0;
    uint64 UnhandledPacketCount = 0;
    uint64 UnhandledByteCount = 0;
};

class FGameState
{
public:
    FGameStateChangeMask Apply(const FServerEvent& event);
    FGameStateChangeMask Apply(const std::vector<FServerEvent>& events);
    FGameStateChangeMask ResetAll();
    FGameStateChangeMask ResetWorld();
    FGameStateChangeMask SetSelectedCharacterSlot(int32 slot);
    FGameStateChangeMask CommitStatAllocation(const std::array<int32, 8>& deltas);
    const FGameSessionState& Session() const { return SessionState; }
    const FGameCharacterState& Characters() const { return CharacterState; }
    const FGameWorldState& World() const { return WorldState; }
    const FGameClockState& Clock() const { return ClockState; }
    const FGameMapState& Map() const { return MapState; }
    const FGameClanState& Clan() const { return ClanState; }
    const FGameDiagnosticsState& Diagnostics() const { return DiagnosticsState; }
    const FCharacterSlotInfo* SelectedCharacter() const;
    const FCharacterSlotInfo* ActiveCharacter() const;
private:
    FGameSessionState SessionState;
    FGameCharacterState CharacterState;
    FGameWorldState WorldState;
    FGameClockState ClockState;
    FGameMapState MapState;
    FGameClanState ClanState;
    FGameDiagnosticsState DiagnosticsState;
};
