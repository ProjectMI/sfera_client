#pragma once
#include "SferaBase.h"

class SferaCommandLine
{
public:
    void Parse(const char* CommandLine);

    bool HasLoginFlag() const { return bLogin; }
    bool HasGameXpSid() const { return bGameXpSid; }
    const std::string& GetRaw() const { return Raw; }

private:
    std::string Raw;
    bool bLogin = false;
    bool bGameXpSid = false;
};
