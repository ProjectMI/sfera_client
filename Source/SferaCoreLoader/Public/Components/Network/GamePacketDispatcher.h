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
    struct FPendingChatMessage
    {
        size_t ExpectedBytes = 0;
        uint8 Channel = 1;
        std::string Sender;
        bool Suppress = false;
        FByteArray Bytes;
    };
    FServerEvent MakeEvent(EServerEventSubsystem subsystem, FServerPacketRoute route, FServerEventPayload payload);
    std::optional<FPendingChatMessage> PendingChat;
    std::atomic<uint64> NextSequence{1};
};
