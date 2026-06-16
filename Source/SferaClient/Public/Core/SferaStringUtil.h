#pragma once
#include "SferaBase.h"

class SferaStringUtil
{
public:
    static std::string Trim(const std::string& Value);
    static std::string ToLower(std::string Value);
    static std::string NormalizeSlashes(std::string Value);
    static std::string NormalizeLogicalPath(const std::string& Value);
    static std::string GetExtensionLower(const std::string& Value);
    static bool StartsWithIgnoreCase(const std::string& Value, const std::string& Prefix);
    static bool EndsWithIgnoreCase(const std::string& Value, const std::string& Suffix);
    static bool WildcardMatchIgnoreCase(const std::string& Pattern, const std::string& Value);
};
