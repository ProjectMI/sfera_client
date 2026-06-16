#include "SferaCommandLine.h"
#include <algorithm>
#include <cctype>

static std::string ToLowerAscii(std::string Value)
{
    std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Value;
}

void SferaCommandLine::Parse(const std::string& CommandLine)
{
    Raw = CommandLine;
    const std::string Lower = ToLowerAscii(Raw);

    // Original WinMain references: /login, /gamexp_sid, connect.cfg, CONNECT_TYPE.
    bLogin = Lower.find("/login") != std::string::npos;
    bGameXpSid = Lower.find("/gamexp_sid") != std::string::npos;
}
