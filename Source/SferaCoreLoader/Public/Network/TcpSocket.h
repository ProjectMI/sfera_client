#pragma once
#include "Network/NetworkTypes.h"
#include <memory>

struct FNativeTcpSocket;

class FTcpSocket
{
public:
    FTcpSocket();
    ~FTcpSocket();

    FStatus Connect(const FEndpoint& endpoint);
    FStatus Connect(const FEndpoint& endpoint, uint32 timeoutMs);
    FStatus SetNonBlocking(bool enabled);
    FStatus Send(const FByteArray& bytes);
    TResult<FByteArray> Receive(size_t maxBytes);
    TResult<FByteArray> ReceiveAvailable(size_t maxBytes);
    void Close();
    bool IsConnected() const { return State == EConnectionState::Connected; }

private:
    EConnectionState State = EConnectionState::Closed;
    std::unique_ptr<FNativeTcpSocket> Native;
};
