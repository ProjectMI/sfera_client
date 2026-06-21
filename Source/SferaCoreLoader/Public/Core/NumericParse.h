#pragma once
#include "Core/Types.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <limits>

namespace NumericParse

{
inline std::string_view TrimView(std::string_view text) 
{
    while (!text.empty() && static_cast<unsigned char>(text.front()) <= ' ') 
    {
        text.remove_prefix(1); 
    }

    while (!text.empty() && static_cast<unsigned char>(text.back()) <= ' ') 
    { 
        text.remove_suffix(1);
    }

    return text;
}

inline bool IsFullyConsumed(const char* end, const char* stop) 
{
    while (end < stop && static_cast<unsigned char>(*end) <= ' ')
    { 
        ++end; 
    }

    return end == stop;
}

inline bool TryParseInt64Strict(std::string_view text, int64& out) 
{
    text = TrimView(text);
    if (text.empty()) { return false; }

    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    long long value = std::strtoll(owned.c_str(), &end, 0);
    if (end == owned.c_str() || errno == ERANGE || !IsFullyConsumed(end, owned.c_str() + owned.size())) { return false; }

    out = static_cast<int64>(value);
    return true;
}

inline bool TryParseUInt64Strict(std::string_view text, uint64& out)
{
    text = TrimView(text);
    if (text.empty() || text.front() == '-') { return false; }

    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    unsigned long long value = std::strtoull(owned.c_str(), &end, 0);
    if (end == owned.c_str() || errno == ERANGE || !IsFullyConsumed(end, owned.c_str() + owned.size())) { return false; }

    out = static_cast<uint64>(value);
    return true;
}

inline bool TryParseInt32Strict(std::string_view text, int32& out) 
{
    int64 value = 0;
    if (!TryParseInt64Strict(text, value)) { return false; }
    if (value < std::numeric_limits<int32>::min() || value > std::numeric_limits<int32>::max()) { return false; }

    out = static_cast<int32>(value);
    return true;
}

inline bool TryParseUInt32Strict(std::string_view text, uint32& out)
{
    uint64 value = 0;
    if (!TryParseUInt64Strict(text, value)) { return false; }
    if (value > std::numeric_limits<uint32>::max()) { return false; }

    out = static_cast<uint32>(value);
    return true;
}

inline bool TryParseFloatStrict(std::string_view text, float& out) 
{
    text = TrimView(text);
    if (text.empty()) { return false; }

    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    float value = std::strtof(owned.c_str(), &end);
    if (end == owned.c_str() || errno == ERANGE || !IsFullyConsumed(end, owned.c_str() + owned.size())) { return false; }

    out = value;
    return true;
}

inline bool TryParseFloatPrefix(std::string_view text, float& out)
{
    text = TrimView(text);
    if (text.empty()) { return false; }

    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    float value = std::strtof(owned.c_str(), &end);
    if (end == owned.c_str() || errno == ERANGE) { return false; }

    out = value;
    return true;
}

inline int32 Int32Or(std::string_view text, int32 fallback = 0)
{
    int32 value = fallback;
    return TryParseInt32Strict(text, value) ? value : fallback;
}

inline uint32 UInt32Or(std::string_view text, uint32 fallback = 0) 
{
    uint32 value = fallback;
    return TryParseUInt32Strict(text, value) ? value : fallback;
}

inline float FloatOr(std::string_view text, float fallback = 0.0f)
{
    float value = fallback;
    return TryParseFloatPrefix(text, value) ? value : fallback;
}
}
