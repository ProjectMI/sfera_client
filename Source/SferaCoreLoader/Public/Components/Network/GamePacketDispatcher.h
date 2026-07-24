#pragma once
#include "Components/Network/ServerEvent.h"

class FGamePacketDispatcher
{
public:
    std::vector<FServerEvent> DecodeFrames(const std::vector<std::vector<uint8>>& frames, const FPacketDispatchContext& context = {});
    std::vector<FServerEvent> DecodeFrame(const std::vector<uint8>& frame, const FPacketDispatchContext& context = {});
    std::vector<FServerEvent> BuildLoginSnapshot(const FLoginProbeResult& result);
    FServerEvent BuildCharacterActivated(int32 slot, std::optional<FServerWorldPosition> initialPosition);
    FServerEvent BuildSessionClosed(std::string reason);
private:
    FServerEvent MakeEvent(EServerEventSubsystem subsystem, FServerPacketRoute route, FServerEventPayload payload);
    std::atomic<uint64> NextSequence{1};
};
