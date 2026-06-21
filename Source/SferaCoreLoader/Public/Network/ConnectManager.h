#pragma once
#include "Config/ConfigService.h"
#include "Network/PacketBuffer.h"
#include "Network/TcpSocket.h"

class FConnectManager 
{
public:
    explicit FConnectManager(const FConfigService& config);
    std::optional<FEndpoint> ReadEndpointFromConfig() const;
    FStatus ConnectConfigured();
    void Close();
    bool IsConnected() const { return Socket.IsConnected(); }
private:
    static std::vector<std::string> TokenizeEndpointLine(std::string_view text);
    static bool LooksLikeHost(std::string_view token);
    static bool TryParsePort(std::string_view token, uint16& outPort);
    static std::optional<FEndpoint> EndpointFromTokens(const std::vector<std::string>& tokens);
    const FConfigService& Config;
    FTcpSocket Socket;
    FPacketBuffer Packets;
};