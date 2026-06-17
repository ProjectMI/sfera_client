#pragma once
#include "Config/ConfigService.h"
#include "Network/PacketBuffer.h"
#include "Network/TcpSocket.h"

namespace Sfera {
class FConnectManager {
public:
    explicit FConnectManager(const FConfigService& config);
    std::optional<FEndpoint> ReadEndpointFromConfig() const;
    FStatus ConnectConfigured();
    void Close();
    bool IsConnected() const { return Socket.IsConnected(); }
private:
    const FConfigService& Config;
    FTcpSocket Socket;
    FPacketBuffer Packets;
};
}
