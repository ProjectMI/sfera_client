#pragma once
#include "Core/Logger.h"
#include "Network/PacketBuffer.h"
#include "Network/TcpSocket.h"
#include <optional>
#include <string>

enum class EClientSessionStage 
{ 
    Idle,
    EndpointResolved,
    Connecting, 
    Connected, 
    ProbeReceiving, 
    Failed, 
    Closed 
};

struct FClientSessionSnapshot 
{
    EClientSessionStage Stage = EClientSessionStage::Idle;
    std::string StageText;
    std::optional<FEndpoint> Endpoint;
    size_t BytesReceived = 0;
    size_t FramesReceived = 0;
    std::string LastError;
};

class FClientSession 
{
public:
    explicit FClientSession(FLogger* logger = nullptr);
    void SetLogger(FLogger* logger) { Log = logger; }
    void Configure(std::optional<FEndpoint> endpoint);
    FStatus StartProbe(uint32 timeoutMs = 350);
    void Tick();
    void Close();
    const FClientSessionSnapshot& Snapshot() const { return State; }
    static std::string_view StageName(EClientSessionStage stage);
private:
    void SetStage(EClientSessionStage stage, std::string text);
    FLogger* Log = nullptr;
    FTcpSocket Socket;
    FPacketBuffer U16Frames;
    FClientSessionSnapshot State;
};