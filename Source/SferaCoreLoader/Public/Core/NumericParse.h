#pragma once
#include "Core/Types.h"

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

inline int DetectIntegerBase(std::string_view& text)
{
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text.remove_prefix(2);
        return 16;
    }

    if (text.size() > 1 && text.front() == '0')
    {
        text.remove_prefix(1);
        return 8;
    }

    return 10;
}

inline bool TryParseUnsignedMagnitude(std::string_view text, uint64& out)
{
    const int base = DetectIntegerBase(text);
    if (text.empty())
    {
        out = 0;
        return true;
    }

    uint64 value = 0;
    const auto begin = text.data();
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value, base);

    if (result.ec != std::errc{} || result.ptr != end) { return false; }

    out = value;
    return true;
}

inline bool TryParseInt64Strict(std::string_view text, int64& out)
{
    text = TrimView(text);
    if (text.empty()) { return false; }

    bool negative = false;

    if (text.front() == '+' || text.front() == '-')
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }

    uint64 magnitude = 0;
    if (!TryParseUnsignedMagnitude(text, magnitude)) { return false; }

    constexpr uint64 minMagnitude = static_cast<uint64>(std::numeric_limits<int64>::max()) + 1ULL;

    if (negative)
    {
        if (magnitude > minMagnitude) { return false; }

        out = magnitude == minMagnitude ? std::numeric_limits<int64>::min() : -static_cast<int64>(magnitude);
        return true;
    }

    if (magnitude > static_cast<uint64>(std::numeric_limits<int64>::max())) { return false; }

    out = static_cast<int64>(magnitude);
    return true;
}

inline bool TryParseUInt64Strict(std::string_view text, uint64& out)
{
    text = TrimView(text);
    if (text.empty() || text.front() == '-') { return false; }

    if (text.front() == '+')
    {
        text.remove_prefix(1);
    }

    return TryParseUnsignedMagnitude(text, out);
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

    float value = 0.0f;
    const auto begin = text.data();
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);

    if (result.ec != std::errc{} || result.ptr != end) { return false; }

    out = value;
    return true;
}

inline bool TryParseFloatPrefix(std::string_view text, float& out)
{
    text = TrimView(text);
    if (text.empty()) { return false; }

    float value = 0.0f;
    const auto begin = text.data();
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);

    if (result.ec != std::errc{} || result.ptr == begin) { return false; }

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
