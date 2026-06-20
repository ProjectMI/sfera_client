#pragma once
#include "Core/Logger.h"
#include "Network/ClientProtocol.h"
#include "Network/TcpSocket.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Sfera {
enum class EClientSessionStage { Idle, EndpointResolved, Connecting, Connected, Handshaking, LoginSent, CharacterSelectReady, ProbeReceiving, Failed, Closed };

struct FClientSessionSnapshot {
    EClientSessionStage Stage = EClientSessionStage::Idle;
    std::string StageText;
    std::optional<FEndpoint> Endpoint;
    size_t BytesReceived = 0;
    size_t FramesReceived = 0;
    std::string LastError;
    bool LegacyHandshake = false;
    bool LoginSent = false;
    bool CharacterSelectReady = false;
    uint16 LocalId = 0;
    int32 FirstOpcode = 0;
    int32 NextOpcode = 0;
    int32 CharacterSelectPackets = 0;
    int32 CharacterSelectBytes = 0;
    std::array<FCharacterSlotInfo, 3> CharacterSlots{};
};

class FClientSession {
public:
    explicit FClientSession(FLogger* logger = nullptr);
    void SetLogger(FLogger* logger) { Log = logger; }
    void Configure(std::optional<FEndpoint> endpoint);
    FStatus StartProbe(uint32 timeoutMs = 350, std::string login = {}, std::string password = {});
    void Tick();
    void Close();
    const FClientSessionSnapshot& Snapshot() const { return State; }
    static const char* StageName(EClientSessionStage stage);
private:
    bool ReceiveFrameBlocking(FByteArray& frame, uint32 timeoutMs);
    void ApplyLoginResult(const FLoginHandshakeResult& result);
    void SetStage(EClientSessionStage stage, std::string text);
    FLogger* Log = nullptr;
    FTcpSocket Socket;
    std::vector<uint8> PendingBytes;
    FClientSessionSnapshot State;
};
}
