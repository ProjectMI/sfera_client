#include "Network/ConnectManager.h"

namespace Sfera {
FConnectManager::FConnectManager(const FConfigService& config) : Config(config) {}

std::optional<FEndpoint> FConnectManager::ReadEndpointFromConfig() const {
    auto host = Config.FindString("SERVER");
    if (!host) { host = Config.FindString("HOST"); }
    auto portText = Config.FindString("PORT");
    if (!host || !portText) { return std::nullopt; }
    try { return FEndpoint{*host, static_cast<uint16>(std::stoi(*portText))}; } catch (...) { return std::nullopt; }
}

FStatus FConnectManager::ConnectConfigured() {
    auto endpoint = ReadEndpointFromConfig();
    if (!endpoint) { return FStatus::Error(EStatusCode::NotFound, "network endpoint not found in connect.cfg/connectn.cfg"); }
    return Socket.Connect(*endpoint);
}

void FConnectManager::Close() { Socket.Close(); }
}
