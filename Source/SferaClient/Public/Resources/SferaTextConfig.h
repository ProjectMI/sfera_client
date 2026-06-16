#pragma once
#include "SferaBase.h"

class SferaTextConfig
{
public:
    bool LoadFromText(const std::string& Text);
    void Clear();

    bool HasValue(const std::string& Key) const;
    std::string GetString(const std::string& Key, const std::string& DefaultValue = std::string()) const;
    int GetInt(const std::string& Key, int DefaultValue) const;
    bool GetBool(const std::string& Key, bool DefaultValue) const;
    const std::unordered_map<std::string, std::string>& GetValues() const { return Values; }

private:
    std::unordered_map<std::string, std::string> Values;
};
