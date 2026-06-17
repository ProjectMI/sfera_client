#pragma once
#include "Network/NetworkTypes.h"
#include <cstdint>

namespace Sfera {
class FTcpSocket {
public:
    FTcpSocket();
    ~FTcpSocket();
    FStatus Connect(const FEndpoint& endpoint);
    FStatus Send(const FByteArray& bytes);
    TResult<FByteArray> Receive(size_t maxBytes);
    void Close();
    bool IsConnected() const { return State == EConnectionState::Connected; }
private:
    EConnectionState State = EConnectionState::Closed;
    uintptr_t SocketHandle = ~uintptr_t(0);
};
}
