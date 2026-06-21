#pragma once
#include "Core/Types.h"
#include <cstring>
#include <stdexcept>

namespace Sfera::Binary {
inline void RequireRange(const FByteArray& data, size_t offset, size_t size, std::string_view what) { if (offset > data.size() || size > data.size() - offset) { throw std::runtime_error(std::string("truncated data while reading ") + std::string(what)); } }
inline uint8 U8(const FByteArray& data, size_t offset) { RequireRange(data, offset, 1, "u8"); return data[offset]; }
inline uint16 U16LE(const FByteArray& data, size_t offset) { RequireRange(data, offset, 2, "u16"); return static_cast<uint16>(data[offset] | (static_cast<uint16>(data[offset + 1]) << 8)); }
inline int16 I16LE(const FByteArray& data, size_t offset) { return static_cast<int16>(U16LE(data, offset)); }
inline uint32 U32LE(const FByteArray& data, size_t offset) { RequireRange(data, offset, 4, "u32"); return static_cast<uint32>(data[offset]) | (static_cast<uint32>(data[offset + 1]) << 8) | (static_cast<uint32>(data[offset + 2]) << 16) | (static_cast<uint32>(data[offset + 3]) << 24); }
inline int32 I32LE(const FByteArray& data, size_t offset) { return static_cast<int32>(U32LE(data, offset)); }
inline uint64 U64LE(const FByteArray& data, size_t offset) { RequireRange(data, offset, 8, "u64"); return static_cast<uint64>(U32LE(data, offset)) | (static_cast<uint64>(U32LE(data, offset + 4)) << 32); }
inline float F32LE(const FByteArray& data, size_t offset) { uint32 raw = U32LE(data, offset); float out = 0.0f; std::memcpy(&out, &raw, sizeof(out)); return out; }
inline std::string ReadCString(const FByteArray& data, size_t& offset) { if (offset >= data.size()) { throw std::runtime_error("truncated data while reading cstring"); } size_t start = offset; while (offset < data.size() && data[offset] != 0) { ++offset; } if (offset >= data.size()) { throw std::runtime_error("unterminated cstring"); } std::string out(reinterpret_cast<const char*>(data.data() + start), offset - start); ++offset; return out; }
inline std::string ReadFixedString(const FByteArray& data, size_t offset, size_t size) { RequireRange(data, offset, size, "fixed string"); const auto* begin = reinterpret_cast<const char*>(data.data() + offset); size_t length = 0; while (length < size && begin[length] != '\0') { ++length; } return std::string(begin, length); }
}
