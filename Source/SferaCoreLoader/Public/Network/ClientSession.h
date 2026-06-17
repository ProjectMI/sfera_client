#pragma once
#include "Core/Logger.h"
#include "Network/ClientProtocol.h"
#include "Network/TcpSocket.h"
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace Sfera {
enum class EClientSessionStage { Idle, EndpointResolved, Connecting, Connected, ProbeReceiving, Failed, Closed };

struct FClientSessionSnapshot {
    EClientSessionStage Stage = EClientSessionStage::Idle;
    std::string StageText;
    std::optional<FEndpoint> Endpoint;
    size_t BytesReceived = 0;
    size_t FramesReceived = 0;
    size_t PacketsSent = 0;
    uint32 ConnectAttempts = 0;
    uint16 ConnectionId = 0;
    uint16 SessionToken = 0;
    uint16 LastOpcode = 0;
    std::string LastError;
};

class FClientSession {
public:
    explicit FClientSession(FLogger* logger = nullptr);
    void SetLogger(FLogger* logger) { Log = logger; }
    void Configure(std::optional<FEndpoint> endpoint);
    void Configure(std::vector<FEndpoint> endpoints);
    FStatus StartProbe(uint32 timeoutMs = 5000);
    void Tick();
    void Close();
    const FClientSessionSnapshot& Snapshot() const { return State; }
    static const char* StageName(EClientSessionStage stage);
private:
    void SetStage(EClientSessionStage stage, std::string text);
    FStatus TryConnect(uint32 timeoutMs);
    FLogger* Log = nullptr;
    FTcpSocket Socket;
    FSferaNetProtocol Protocol;
    FClientSessionSnapshot State;
    std::vector<FEndpoint> EndpointCandidates;
    size_t ActiveEndpointIndex = 0;
    uint32 ConnectTimeoutMs = 5000;
    std::chrono::steady_clock::time_point NextReconnectAttempt = std::chrono::steady_clock::now();
};
}
