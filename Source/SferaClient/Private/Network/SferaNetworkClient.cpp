#include "SferaNetworkClient.h"

#include <utility>

bool SferaNetworkClient::Initialize()
{
    if (State == EConnectionState::Running || State == EConnectionState::Starting)
    {
        AppendDiagnostic("TCP-IP manager is already initialized.");
        return true;
    }
    State = EConnectionState::Starting;
    SendQueue.clear();
    PendingSendBytes = 0;
    AppendDiagnostic("Init TCP-IP manager boundary.");
    State = EConnectionState::Running;
    return true;
}

void SferaNetworkClient::Shutdown()
{
    if (State == EConnectionState::Stopped) return;
    State = EConnectionState::Stopping;
    SendQueue.clear();
    PendingSendBytes = 0;
    AppendDiagnostic("Deinit TCP-IP manager boundary.");
    State = EConnectionState::Stopped;
}

void SferaNetworkClient::Tick()
{
    if (State != EConnectionState::Running) return;
    DrainSendQueue(4096u);
}

bool SferaNetworkClient::QueuePayload(const SferaByteBuffer& Payload)
{
    if (State != EConnectionState::Running)
    {
        AppendDiagnostic("TCP-IP payload rejected because manager is not running.");
        return false;
    }
    if (Payload.empty()) return true;
    if (Payload.size() > MaxSendBufferBytes || PendingSendBytes > MaxSendBufferBytes - Payload.size())
    {
        AppendDiagnostic("TCP-IP send buffer overload.");
        return false;
    }
    SendQueue.push_back(Payload);
    PendingSendBytes += Payload.size();
    return true;
}

bool SferaNetworkClient::QueuePayload(std::string_view PayloadText)
{
    SferaByteBuffer payload;
    payload.reserve(PayloadText.size());
    for (char value : PayloadText) payload.push_back(static_cast<SferaByte>(static_cast<unsigned char>(value)));
    return QueuePayload(payload);
}

std::size_t SferaNetworkClient::GetPendingSendBytes() const
{
    return PendingSendBytes;
}

SferaNetworkClient::EConnectionState SferaNetworkClient::GetState() const
{
    return State;
}

const std::vector<std::string>& SferaNetworkClient::GetDiagnostics() const
{
    return Diagnostics;
}

void SferaNetworkClient::AppendDiagnostic(std::string Message)
{
    Diagnostics.push_back(std::move(Message));
    if (Diagnostics.size() > 64u) Diagnostics.erase(Diagnostics.begin());
}

void SferaNetworkClient::DrainSendQueue(std::size_t MaxBytesToDrain)
{
    std::size_t drained = 0;
    while (!SendQueue.empty() && drained < MaxBytesToDrain)
    {
        const std::size_t packetSize = SendQueue.front().size();
        if (drained + packetSize > MaxBytesToDrain) break;
        drained += packetSize;
        PendingSendBytes -= packetSize;
        SendQueue.pop_front();
    }
}
