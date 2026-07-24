#include "Components/State/GameState.h"
#include "Common/SferaGameConstants.h"
#include <cmath>
#include <type_traits>

namespace
{
int32 ClampSlot(int32 slot)
{
    return std::clamp(slot, 0, Sfera::CharacterSlotCount - 1);
}

bool SamePosition(const FServerWorldPosition& left, const FServerWorldPosition& right)
{
    return left.EntityId == right.EntityId && left.CharacterEntity == right.CharacterEntity && std::abs(left.X - right.X) <= 0.0001 && std::abs(left.Y - right.Y) <= 0.0001 && std::abs(left.Z - right.Z) <= 0.0001 && std::abs(left.Angle - right.Angle) <= 0.0001;
}
}

FGameStateChangeMask FGameState::Apply(const FServerEvent& event)
{
    DiagnosticsState.LastServerSequence = std::max(DiagnosticsState.LastServerSequence, event.Sequence);
    return std::visit([this](const auto& payload) -> FGameStateChangeMask
    {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, FSessionEstablishedEvent>)
        {
            const bool changed = !SessionState.Connected || SessionState.InWorld || SessionState.LocalEntityId != payload.LocalEntityId;
            SessionState.Connected = true;
            SessionState.InWorld = false;
            SessionState.LocalEntityId = payload.LocalEntityId;
            SessionState.LastDisconnectReason.clear();
            if (changed) { ++SessionState.Revision; }
            return changed ? GameStateChange::Session : GameStateChange::None;
        }
        else if constexpr (std::is_same_v<T, FSessionClosedEvent>)
        {
            const std::string reason = payload.Reason;
            const FGameStateChangeMask changes = ResetAll();
            SessionState.LastDisconnectReason = reason;
            ++SessionState.Revision;
            return changes;
        }
        else if constexpr (std::is_same_v<T, FCharacterRosterEvent>)
        {
            const int32 previousSelection = ClampSlot(CharacterState.SelectedSlot);
            const bool hadRoster = CharacterState.RosterRevision != 0;
            CharacterState.Slots = payload.Slots;
            ++CharacterState.RosterRevision;
            int32 selected = previousSelection;
            const FCharacterSlotInfo& current = CharacterState.Slots[static_cast<size_t>(selected)];
            if (!hadRoster || (!current.Present && !current.CanCreate))
            {
                bool found = false;
                for (int32 index = 0; index < Sfera::CharacterSlotCount; ++index)
                {
                    if (CharacterState.Slots[static_cast<size_t>(index)].Present) { selected = index; found = true; break; }
                }
                if (!found)
                {
                    for (int32 index = 0; index < Sfera::CharacterSlotCount; ++index)
                    {
                        if (CharacterState.Slots[static_cast<size_t>(index)].CanCreate) { selected = index; break; }
                    }
                }
            }
            CharacterState.SelectedSlot = selected;
            FGameStateChangeMask changes = GameStateChange::CharacterRoster;
            if (selected != previousSelection) { ++CharacterState.ActiveRevision; changes |= GameStateChange::ActiveCharacter; }
            return changes;
        }
        else if constexpr (std::is_same_v<T, FCharacterActivatedEvent>)
        {
            CharacterState.ActiveSlot = ClampSlot(payload.Slot);
            CharacterState.SelectedSlot = CharacterState.ActiveSlot;
            ++CharacterState.ActiveRevision;
            SessionState.InWorld = true;
            ++SessionState.Revision;
            FGameStateChangeMask changes = GameStateChange::Session | GameStateChange::ActiveCharacter;
            if (payload.InitialPosition)
            {
                WorldState.HasPosition = true;
                WorldState.Position = *payload.InitialPosition;
                ++WorldState.Revision;
                changes |= GameStateChange::WorldPosition;
            }
            return changes;
        }
        else if constexpr (std::is_same_v<T, FWorldPositionEvent>)
        {
            if (WorldState.HasPosition && SamePosition(WorldState.Position, payload.Position)) { return GameStateChange::None; }
            WorldState.HasPosition = true;
            WorldState.Position = payload.Position;
            ++WorldState.Revision;
            return GameStateChange::WorldPosition;
        }
        else if constexpr (std::is_same_v<T, FGameClockEvent>)
        {
            const bool changed = !ClockState.Known || std::abs(ClockState.DayFraction - payload.DayFraction) > 0.000001f || ClockState.Day != payload.Day || ClockState.Month != payload.Month || ClockState.Year != payload.Year;
            if (!changed) { return GameStateChange::None; }
            ClockState.Known = true;
            ClockState.DayFraction = payload.DayFraction;
            ClockState.Day = payload.Day;
            ClockState.Month = payload.Month;
            ClockState.Year = payload.Year;
            ++ClockState.Revision;
            return GameStateChange::Clock;
        }
        else if constexpr (std::is_same_v<T, FMapDescriptorEvent>)
        {
            const bool changed = MapState.SpriteName != payload.SpriteName || MapState.Projection != payload.Projection || MapState.HasProjection != payload.HasProjection;
            if (!changed) { return GameStateChange::None; }
            MapState.SpriteName = payload.SpriteName;
            MapState.Projection = payload.Projection;
            MapState.HasProjection = payload.HasProjection;
            ++MapState.Revision;
            return GameStateChange::Map;
        }
        else if constexpr (std::is_same_v<T, FClanStateEvent>)
        {
            FGameClanState next = ClanState;
            next.Known = payload.Known;
            if (payload.Available) { next.Available = *payload.Available; }
            if (payload.Rank) { next.Rank = *payload.Rank; }
            if (payload.Name) { next.Name = *payload.Name; }
            const bool changed = next.Known != ClanState.Known || next.Available != ClanState.Available || next.Rank != ClanState.Rank || next.Name != ClanState.Name;
            if (!changed) { return GameStateChange::None; }
            next.Revision = ClanState.Revision + 1;
            ClanState = std::move(next);
            return GameStateChange::Clan;
        }
        else
        {
            DiagnosticsState.UnhandledPacketCount += payload.PacketCount;
            DiagnosticsState.UnhandledByteCount += payload.ByteCount;
            return GameStateChange::Diagnostics;
        }
    }, event.Payload);
}

FGameStateChangeMask FGameState::Apply(const std::vector<FServerEvent>& events)
{
    FGameStateChangeMask changes = GameStateChange::None;
    for (const FServerEvent& event : events) { changes |= Apply(event); }
    return changes;
}

FGameStateChangeMask FGameState::ResetAll()
{
    SessionState = {};
    CharacterState = {};
    WorldState = {};
    ClockState = {};
    MapState = {};
    ClanState = {};
    return GameStateChange::Session | GameStateChange::CharacterRoster | GameStateChange::ActiveCharacter | GameStateChange::WorldPosition | GameStateChange::Clock | GameStateChange::Map | GameStateChange::Clan;
}

FGameStateChangeMask FGameState::ResetWorld()
{
    SessionState.InWorld = false;
    ++SessionState.Revision;
    CharacterState.ActiveSlot = -1;
    ++CharacterState.ActiveRevision;
    WorldState = {};
    MapState = {};
    ClanState = {};
    return GameStateChange::Session | GameStateChange::ActiveCharacter | GameStateChange::WorldPosition | GameStateChange::Map | GameStateChange::Clan;
}

FGameStateChangeMask FGameState::SetSelectedCharacterSlot(int32 slot)
{
    const int32 next = ClampSlot(slot);
    if (next == CharacterState.SelectedSlot) { return GameStateChange::None; }
    CharacterState.SelectedSlot = next;
    ++CharacterState.ActiveRevision;
    return GameStateChange::ActiveCharacter;
}

FGameStateChangeMask FGameState::CommitStatAllocation(const std::array<int32, 8>& deltas)
{
    const int32 slotIndex = CharacterState.ActiveSlot >= 0 ? CharacterState.ActiveSlot : CharacterState.SelectedSlot;
    if (slotIndex < 0 || slotIndex >= Sfera::CharacterSlotCount) { return GameStateChange::None; }
    FCharacterSlotInfo& slot = CharacterState.Slots[static_cast<size_t>(slotIndex)];
    std::array<int32*, 8> stats{&slot.Strength, &slot.Dexterity, &slot.Accuracy, &slot.Endurance, &slot.Fire, &slot.Water, &slot.Earth, &slot.Air};
    int32 titleSpent = 0;
    int32 degreeSpent = 0;
    bool changed = false;
    for (size_t index = 0; index < deltas.size(); ++index)
    {
        if (deltas[index] == 0) { continue; }
        *stats[index] += deltas[index];
        if (index < 4) { titleSpent += deltas[index]; }
        else { degreeSpent += deltas[index]; }
        changed = true;
    }
    if (!changed) { return GameStateChange::None; }
    slot.TitleStats = std::max(0, slot.TitleStats - titleSpent);
    slot.DegreeStats = std::max(0, slot.DegreeStats - degreeSpent);
    ++CharacterState.RosterRevision;
    return GameStateChange::CharacterRoster | GameStateChange::ActiveCharacter;
}

const FCharacterSlotInfo* FGameState::SelectedCharacter() const
{
    const int32 slot = ClampSlot(CharacterState.SelectedSlot);
    return &CharacterState.Slots[static_cast<size_t>(slot)];
}

const FCharacterSlotInfo* FGameState::ActiveCharacter() const
{
    if (CharacterState.ActiveSlot < 0 || CharacterState.ActiveSlot >= Sfera::CharacterSlotCount) { return nullptr; }
    return &CharacterState.Slots[static_cast<size_t>(CharacterState.ActiveSlot)];
}
