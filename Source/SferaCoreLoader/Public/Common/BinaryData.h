#pragma once
#include "Core/Types.h"
#include <algorithm>
#include <bit>
#include <string_view>

namespace Common
{
inline bool HasRange(const FByteArray& bytes, size_t offset, size_t size) { return offset <= bytes.size() && size <= bytes.size() - offset; }
inline uint16 U16LEUnchecked(const FByteArray& bytes, size_t offset) { return static_cast<uint16>(bytes[offset] | (static_cast<uint16>(bytes[offset + 1]) << 8)); }
inline int16 I16LEUnchecked(const FByteArray& bytes, size_t offset) { return static_cast<int16>(U16LEUnchecked(bytes, offset)); }
inline uint32 U32LEUnchecked(const FByteArray& bytes, size_t offset) { return static_cast<uint32>(bytes[offset]) | (static_cast<uint32>(bytes[offset + 1]) << 8) | (static_cast<uint32>(bytes[offset + 2]) << 16) | (static_cast<uint32>(bytes[offset + 3]) << 24); }
inline uint64 U64LEUnchecked(const FByteArray& bytes, size_t offset) { return static_cast<uint64>(U32LEUnchecked(bytes, offset)) | (static_cast<uint64>(U32LEUnchecked(bytes, offset + 4)) << 32); }
inline float F32FromU32(uint32 value) { return std::bit_cast<float>(value); }
inline float F32LEUnchecked(const FByteArray& bytes, size_t offset) { return F32FromU32(U32LEUnchecked(bytes, offset)); }
inline uint16 U16LEOr(const FByteArray& bytes, size_t offset, uint16 fallback = 0) { return HasRange(bytes, offset, 2) ? U16LEUnchecked(bytes, offset) : fallback; }
inline uint32 U32LEOr(const FByteArray& bytes, size_t offset, uint32 fallback = 0) { return HasRange(bytes, offset, 4) ? U32LEUnchecked(bytes, offset) : fallback; }
inline int32 I32LEOr(const FByteArray& bytes, size_t offset, int32 fallback = 0) { return static_cast<int32>(U32LEOr(bytes, offset, static_cast<uint32>(fallback))); }
inline float F32LEOr(const FByteArray& bytes, size_t offset, float fallback = 0.0f) { return HasRange(bytes, offset, sizeof(float)) ? F32LEUnchecked(bytes, offset) : fallback; }

inline bool StartsWithBytes(const FByteArray& bytes, std::string_view marker)
{
    if (bytes.size() < marker.size()) { return false; }
    return std::equal(marker.begin(), marker.end(), bytes.begin(), bytes.begin() + marker.size(), [](char expected, uint8 value) { return static_cast<uint8>(expected) == value; });
}

inline std::string FixedCString(const FByteArray& bytes, size_t offset, size_t maxLen)
{
    std::string out;
    for (size_t i = 0; i < maxLen && offset + i < bytes.size(); ++i)
    {
        char ch = static_cast<char>(bytes[offset + i]);
        if (ch == '\0') { break; }
        out.push_back(ch);
    }
    return out;
}
}
