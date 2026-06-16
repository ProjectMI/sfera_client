#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string_view>

#if !defined(_WIN64)
#error SferaClientSnapshot S0003 is x64-only. Build the x64 configuration.
#endif

using SferaInt8 = std::int8_t;
using SferaUInt8 = std::uint8_t;
using SferaByte = std::byte;
using SferaByteBuffer = std::vector<SferaByte>;
using SferaInt16 = std::int16_t;
using SferaUInt16 = std::uint16_t;
using SferaInt32 = std::int32_t;
using SferaUInt32 = std::uint32_t;
using SferaInt64 = std::int64_t;
using SferaUInt64 = std::uint64_t;

static_assert(sizeof(void*) == 8, "S0003 must be built as x64.");
static_assert(sizeof(SferaUInt32) == 4, "fixed-width file ABI must stay stable.");

struct SferaSize2D
{
    int Width = 800;
    int Height = 600;
};

struct SferaPoint2D
{
    int X = 0;
    int Y = 0;
};
