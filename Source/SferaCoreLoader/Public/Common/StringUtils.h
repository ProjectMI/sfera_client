#pragma once
#include "Core/Types.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>
#include <vector>

namespace Common
{
inline std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline std::string ToLower(std::string_view value) { return ToLower(std::string(value)); }
inline std::string ToLowerPath(FPath path) { return ToLower(path.generic_string()); }

inline std::string Trim(std::string value)
{
    auto keep = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), keep));
    value.erase(std::find_if(value.rbegin(), value.rend(), keep).base(), value.end());
    return value;
}

inline std::string NormalizePathKey(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    value = ToLower(std::move(value));
    while (!value.empty() && (value.front() == '/' || std::isspace(static_cast<unsigned char>(value.front())))) { value.erase(value.begin()); }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) { value.pop_back(); }
    return value;
}

inline std::string NormalizePathKey(const FPath& path) { return NormalizePathKey(path.generic_string()); }
inline bool EqualsNoCase(std::string_view a, std::string_view b) { return ToLower(a) == ToLower(b); }
inline bool EndsWith(std::string_view text, std::string_view suffix) { return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix; }

inline std::string StripCppComment(std::string line)
{
    bool quoted = false;
    for (size_t i = 0; i + 1 < line.size(); ++i)
    {
        if (line[i] == '"') { quoted = !quoted; }
        if (!quoted && line[i] == '/' && line[i + 1] == '/') { line.resize(i); break; }
    }
    return line;
}

inline std::string Join(const std::vector<std::string>& values, std::string_view separator)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) { if (i) { out += separator; } out += values[i]; }
    return out;
}

inline std::string Unquote(std::string value)
{
    value = Trim(std::move(value));
    const bool quoted = value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''));
    return quoted ? value.substr(1, value.size() - 2) : value;
}

inline std::vector<std::string> SplitTokens(std::string_view text, std::string_view separators, bool stripQuotes = true)
{
    std::string normalized(text);
    for (char& ch : normalized) { if (separators.find(ch) != std::string_view::npos) { ch = ' '; } }
    std::istringstream input(normalized);
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) { tokens.push_back(stripQuotes ? Unquote(std::move(token)) : std::move(token)); }
    return tokens;
}

inline std::string StripExtension(std::string value)
{
    size_t slash = value.find_last_of("/\\");
    size_t dot = value.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) { value.erase(dot); }
    return value;
}

inline std::string BaseNameWithoutExtension(std::string_view path)
{
    std::string value(path);
    size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) { value.erase(0, slash + 1); }
    return StripExtension(std::move(value));
}
}
