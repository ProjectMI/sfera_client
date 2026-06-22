#pragma once
#include "Core/Types.h"
#include "Common/BinaryData.h"
#include <stdexcept>

namespace Binary
{
inline void RequireRange(const FByteArray& data, size_t offset, size_t size, std::string_view what)
{
    if (offset > data.size() || size > data.size() - offset)
    {
        throw std::runtime_error(std::string("truncated data while reading ") + std::string(what));
    }
}

inline std::string BytesToString(const FByteArray& data, size_t offset, size_t size)
{
    RequireRange(data, offset, size, "string");

    std::string out;
    out.reserve(size);

    for (size_t i = 0; i < size; ++i)
    {
        out.push_back(static_cast<char>(data[offset + i]));
    }

    return out;
}

inline uint8 U8(const FByteArray& data, size_t offset)
{
    RequireRange(data, offset, 1, "u8");
    return data[offset];
}

inline uint16 U16LE(const FByteArray& data, size_t offset)
{
    RequireRange(data, offset, 2, "u16");
    return Common::U16LEUnchecked(data, offset);
}

inline uint32 U32LE(const FByteArray& data, size_t offset)
{
    RequireRange(data, offset, 4, "u32");
    return Common::U32LEUnchecked(data, offset);
}

inline uint64 U64LE(const FByteArray& data, size_t offset)
{
    RequireRange(data, offset, 8, "u64");
    return Common::U64LEUnchecked(data, offset);
}

inline float F32LE(const FByteArray& data, size_t offset)
{
    RequireRange(data, offset, 4, "f32");
    return Common::F32LEUnchecked(data, offset);
}

inline std::string ReadCString(const FByteArray& data, size_t& offset)
{
    if (offset >= data.size())
    {
        throw std::runtime_error("truncated data while reading cstring");
    }

    size_t start = offset;

    while (offset < data.size() && data[offset] != 0)
    {
        ++offset;
    }

    if (offset >= data.size())
    {
        throw std::runtime_error("unterminated cstring");
    }

    std::string out = BytesToString(data, start, offset - start);
    ++offset;
    return out;
}

inline std::string ReadLengthPrefixedString(const FByteArray& data, size_t& cursor, size_t end, std::string_view what)
{
    RequireRange(data, cursor, 1, what);
    const size_t length = data[cursor++];
    if (cursor > end || length > end - cursor) { throw std::runtime_error(std::string("truncated data while reading ") + std::string(what)); }
    std::string out = BytesToString(data, cursor, length);
    cursor += length;
    while (!out.empty() && out.back() == '\0') { out.pop_back(); }
    return out;
}

inline std::string ReadFixedString(const FByteArray& data, size_t offset, size_t size)
{
    RequireRange(data, offset, size, "fixed string");

    size_t length = 0;

    while (length < size && data[offset + length] != 0)
    {
        ++length;
    }

    return BytesToString(data, offset, length);
}

inline int16 I16LE(const FByteArray& data, size_t offset) { return static_cast<int16>(U16LE(data, offset)); }
inline int32 I32LE(const FByteArray& data, size_t offset) { return static_cast<int32>(U32LE(data, offset)); }
}
