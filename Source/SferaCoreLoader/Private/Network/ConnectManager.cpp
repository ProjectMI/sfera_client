#include "Network/ConnectManager.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace Sfera {
FConnectManager::FConnectManager(const FConfigService& config) : Config(config) {}

static std::string LowerNet(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; }

static std::optional<std::string> FindConfigString(const FConfigService& config, std::string_view docName, std::string_view key) {
    const FConfigDocument* doc = config.FindConfig(docName);
    return doc ? doc->FindString(key) : std::nullopt;
}

static std::string TrimNet(std::string s) { auto ns = [](unsigned char c) { return !std::isspace(c); }; s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns)); s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end()); return s; }

static void AddUniqueEndpoint(std::vector<FEndpoint>& endpoints, FEndpoint endpoint) {
    endpoint.Host = TrimNet(endpoint.Host);
    if (endpoint.Host.empty() || endpoint.Port == 0) { return; }
    auto exists = std::find_if(endpoints.begin(), endpoints.end(), [&](const FEndpoint& item) { return LowerNet(item.Host) == LowerNet(endpoint.Host) && item.Port == endpoint.Port; });
    if (exists == endpoints.end()) { endpoints.push_back(std::move(endpoint)); }
}

std::vector<std::string> FConnectManager::TokenizeEndpointLine(std::string_view text) {
    std::string s(text);
    for (char& ch : s) { if (ch == ',' || ch == ';' || ch == '=' || ch == ':' || ch == '\t') { ch = ' '; } }
    std::istringstream input(s);
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) { if (token.size() >= 2 && ((token.front() == '"' && token.back() == '"') || (token.front() == '\'' && token.back() == '\''))) { token = token.substr(1, token.size() - 2); } tokens.push_back(token); }
    return tokens;
}

bool FConnectManager::LooksLikeHost(std::string_view token) {
    if (token.empty()) { return false; }
    std::string s(token);
    std::string lower = LowerNet(s);
    if (lower == "server" || lower == "host" || lower == "address" || lower == "ip" || lower == "port") { return false; }
    if (lower == "true" || lower == "false" || lower == "yes" || lower == "no") { return false; }
    bool hasAlpha = false;
    bool hasDot = false;
    bool hasColon = false;
    for (char ch : s) { unsigned char c = static_cast<unsigned char>(ch); if (std::isalpha(c)) { hasAlpha = true; } if (ch == '.') { hasDot = true; } if (ch == ':') { hasColon = true; } if (!(std::isalnum(c) || ch == '.' || ch == '-' || ch == '_' || ch == ':')) { return false; } }
    if (hasDot || hasColon || hasAlpha) { return true; }
    return false;
}

bool FConnectManager::TryParsePort(std::string_view token, uint16& outPort) {
    if (token.empty() || token.size() > 5) { return false; }
    int value = 0;
    for (char ch : token) { if (!std::isdigit(static_cast<unsigned char>(ch))) { return false; } value = value * 10 + (ch - '0'); }
    if (value <= 0 || value > 65535) { return false; }
    outPort = static_cast<uint16>(value);
    return true;
}

std::optional<FEndpoint> FConnectManager::EndpointFromTokens(const std::vector<std::string>& tokens) {
    std::optional<std::string> host;
    std::optional<uint16> port;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string lower = LowerNet(tokens[i]);
        if ((lower == "host" || lower == "server" || lower == "address" || lower == "ip") && i + 1 < tokens.size() && LooksLikeHost(tokens[i + 1])) { host = tokens[i + 1]; }
        if (lower == "port" && i + 1 < tokens.size()) { uint16 p = 0; if (TryParsePort(tokens[i + 1], p)) { port = p; } }
    }
    for (const auto& token : tokens) { if (!host && LooksLikeHost(token)) { host = token; } if (!port) { uint16 p = 0; if (TryParsePort(token, p)) { port = p; } } }
    if (host && port) { return FEndpoint{*host, *port}; }
    return std::nullopt;
}

FConnectSettings FConnectManager::ReadConnectionSettings() const {
    FConnectSettings settings;
    if (auto offline = Config.FindString("OFFLINE")) { settings.Offline = TrimNet(*offline) == "1" || LowerNet(TrimNet(*offline)) == "true"; }
    if (auto connectType = Config.FindString("CONNECT_TYPE")) { settings.ConnectType = std::atoi(TrimNet(*connectType).c_str()); }

    uint16 port = 0;
    for (const char* docName : {"connectn.cfg", "connect.cfg"}) {
        if (auto portText = FindConfigString(Config, docName, "PORT")) { if (TryParsePort(TrimNet(*portText), port)) { break; } }
    }
    if (port == 0) {
        auto portText = Config.FindString("PORT");
        if (!portText) { portText = Config.FindString("SERVERPORT"); }
        if (!portText) { portText = Config.FindString("LOGINPORT"); }
        if (portText) { TryParsePort(TrimNet(*portText), port); }
    }

    for (const char* docName : {"connectn.cfg", "connect.cfg"}) {
        for (const char* key : {"MAIN_URL", "LAST_URL", "SERVER", "HOST", "IP", "ADDRESS", "SRV00"}) {
            if (auto host = FindConfigString(Config, docName, key)) { if (LooksLikeHost(TrimNet(*host)) && port != 0) { AddUniqueEndpoint(settings.EndpointCandidates, FEndpoint{TrimNet(*host), port}); } }
        }
    }

    std::vector<std::vector<std::string>> tokenLines;
    for (const auto& docPair : Config.DocumentsByName()) {
        bool likelyConnectDoc = docPair.first.find("connect") != std::string::npos || docPair.first.find("config") != std::string::npos;
        for (const auto& entry : docPair.second.Entries()) {
            std::string key = LowerNet(entry.Scope.empty() ? entry.Key : entry.Scope + "." + entry.Key);
            if (likelyConnectDoc || key.find("server") != std::string::npos || key.find("connect") != std::string::npos || key.find("login") != std::string::npos) { auto tokens = TokenizeEndpointLine(entry.Key + " " + entry.Value); if (tokens.size() >= 2) { tokenLines.push_back(std::move(tokens)); } }
        }
    }
    for (const auto& tokens : tokenLines) { if (auto endpoint = EndpointFromTokens(tokens)) { AddUniqueEndpoint(settings.EndpointCandidates, *endpoint); } }
    if (!settings.EndpointCandidates.empty()) { settings.PrimaryEndpoint = settings.EndpointCandidates.front(); }
    return settings;
}

std::optional<FEndpoint> FConnectManager::ReadEndpointFromConfig() const {
    return ReadConnectionSettings().PrimaryEndpoint;
}

FStatus FConnectManager::ConnectConfigured() {
    auto endpoint = ReadEndpointFromConfig();
    if (!endpoint) { return FStatus::Error(EStatusCode::NotFound, "network endpoint not found in connect.cfg/connectn.cfg"); }
    return Socket.Connect(*endpoint);
}

void FConnectManager::Close() { Socket.Close(); }
}
