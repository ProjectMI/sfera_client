#include "Network/ConnectManager.h"
#include "Common/StringUtils.h"
#include "Core/NumericParse.h"
#include <algorithm>
#include <cctype>

FConnectManager::FConnectManager(const FConfigService& config) : Config(config) {}

std::vector<std::string> FConnectManager::TokenizeEndpointLine(std::string_view text)
{
    return Common::SplitTokens(text, ",;=:\t");
}

bool FConnectManager::LooksLikeHost(std::string_view token)
{
    if (token.empty()) { return false; }

    std::string s(token);
    std::string lower = Common::ToLower(s);

    if (lower == "server" || lower == "host" || lower == "address" || lower == "ip" || lower == "port") { return false; }

    if (lower == "true" || lower == "false" || lower == "yes" || lower == "no") { return false; }

    bool hasAlpha = false;
    bool hasDot = false;
    bool hasColon = false;

    for (char ch : s)
    {
        unsigned char c = static_cast<unsigned char>(ch);

        if (std::isalpha(c))
        {
            hasAlpha = true;
        }

        if (ch == '.')
        {
            hasDot = true;
        }

        if (ch == ':')
        {
            hasColon = true;
        }

        if (!(std::isalnum(c) || ch == '.' || ch == '-' || ch == '_' || ch == ':'))
        {
            return false;
        }
    }

    if (hasDot || hasColon || hasAlpha) { return true; }

    return false;
}

bool FConnectManager::TryParsePort(std::string_view token, uint16& outPort)
{
    uint32 value = 0;
    if (!NumericParse::TryParseUInt32Strict(token, value) || value == 0 || value > 65535) { return false; }
    outPort = static_cast<uint16>(value);
    return true;
}

std::optional<FEndpoint> FConnectManager::EndpointFromTokens(const std::vector<std::string>& tokens)
{
    std::optional<std::string> host;
    std::optional<uint16> port;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        std::string lower = Common::ToLower(tokens[i]);

        if ((lower == "host" || lower == "server" || lower == "address" || lower == "ip") && i + 1 < tokens.size() && LooksLikeHost(tokens[i + 1]))
        {
            host = tokens[i + 1];
        }

        if (lower == "port" && i + 1 < tokens.size())
        {
            uint16 p = 0;

            if (TryParsePort(tokens[i + 1], p))
            {
                port = p;
            }
        }
    }

    for (const auto& token : tokens)
    {
        if (!host && LooksLikeHost(token))
        {
            host = token;
        }

        if (!port)
        {
            uint16 p = 0;

            if (TryParsePort(token, p))
            {
                port = p;
            }
        }
    }

    if (host && port) { return FEndpoint{*host, *port}; }

    return std::nullopt;
}

std::optional<FEndpoint> FConnectManager::ReadEndpointFromConfig() const
{
    auto host = Config.FindString("SERVER");

    if (!host)
    {
        host = Config.FindString("HOST");
    }

    if (!host)
    {
        host = Config.FindString("IP");
    }

    if (!host)
    {
        host = Config.FindString("ADDRESS");
    }

    if (!host)
    {
        host = Config.FindString("MAIN_URL");
    }

    if (!host)
    {
        host = Config.FindString("LAST_URL");
    }

    if (!host)
    {
        host = Config.FindString("SRV00");
    }

    auto portText = Config.FindString("PORT");

    if (!portText)
    {
        portText = Config.FindString("SERVERPORT");
    }

    if (!portText)
    {
        portText = Config.FindString("LOGINPORT");
    }

    if (host && portText)
    {
        uint16 parsedPort = 0;

        if (TryParsePort(Common::Trim(*portText), parsedPort))
        {
            return FEndpoint
            {
                Common::Trim(*host), parsedPort
            };
        }
    }

    std::vector<std::vector<std::string>> tokenLines;

    for (const auto& docPair : Config.DocumentsByName())
    {
        bool likelyConnectDoc = docPair.first.find("connect") != std::string::npos || docPair.first.find("config") != std::string::npos;

        for (const auto& entry : docPair.second.Entries())
        {
            std::string key = Common::ToLower(entry.Scope.empty() ? entry.Key : entry.Scope + "." + entry.Key);

            if (!host && (key.find("server") != std::string::npos || key.find("host") != std::string::npos || key.find("address") != std::string::npos || key.find("url") != std::string::npos || key == "ip" || key.rfind("srv", 0) == 0))
            {
                if (LooksLikeHost(entry.Value))
                {
                    host = Common::Trim(entry.Value);
                }
            }

            if (!portText && key.find("port") != std::string::npos)
            {
                portText = Common::Trim(entry.Value);
            }

            if (likelyConnectDoc || key.find("server") != std::string::npos || key.find("connect") != std::string::npos || key.find("login") != std::string::npos)
            {
                auto tokens = TokenizeEndpointLine(entry.Key + " " + entry.Value);

                if (tokens.size() >= 2)
                {
                    tokenLines.push_back(std::move(tokens));
                }
            }
        }
    }

    if (host && portText)
    {
        uint16 parsedPort = 0;

        if (TryParsePort(Common::Trim(*portText), parsedPort))
        {
            return FEndpoint
            {
                Common::Trim(*host), parsedPort
            };
        }
    }

    for (const auto& tokens : tokenLines)
    {
        auto endpoint = EndpointFromTokens(tokens);

        if (endpoint)
        {
            return endpoint;
        }
    }

    return std::nullopt;
}

FStatus FConnectManager::ConnectConfigured()
{
    auto endpoint = ReadEndpointFromConfig();

    if (!endpoint) { return FStatus::Error(EStatusCode::NotFound, "network endpoint not found in connect.cfg/connectn.cfg"); }

    return Socket.Connect(*endpoint);
}

void FConnectManager::Close()
{
    Socket.Close();
}
