#pragma once
#include "SferaBase.h"
#include <deque>

class SferaNetworkClient
{
public:
    enum class EConnectionState
    {
        Stopped,
        Starting,
        Running,
        Stopping
    };

    bool Initialize();
    void Shutdown();
    void Tick();

    bool QueuePayload(const SferaByteBuffer& Payload);
    bool QueuePayload(std::string_view PayloadText);
    std::size_t GetPendingSendBytes() const;
    EConnectionState GetState() const;
    const std::vector<std::string>& GetDiagnostics() const;

private:
    void AppendDiagnostic(std::string Message);
    void DrainSendQueue(std::size_t MaxBytesToDrain);

private:
    static constexpr std::size_t MaxSendBufferBytes = 64u * 1024u;
    EConnectionState State = EConnectionState::Stopped;
    std::deque<SferaByteBuffer> SendQueue;
    std::size_t PendingSendBytes = 0;
    std::vector<std::string> Diagnostics;
};
