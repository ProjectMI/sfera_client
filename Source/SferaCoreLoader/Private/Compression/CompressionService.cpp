#include "Compression/CompressionService.h"
#include "Compression/LzssCodec.h"
#include "Compression/SphrCodec.h"
#include "Compression/ZlibInflate.h"
#include <algorithm>
#include <string_view>

namespace
{
    bool StartsWithBytes(const FByteArray& bytes, std::string_view marker)
    {
        if (bytes.size() < marker.size()) { return false; }

        return std::equal(marker.begin(), marker.end(), bytes.begin(), bytes.begin() + marker.size(), [](char expected, uint8 value)
        {
            return static_cast<uint8>(expected) == value;
        });
    }

    uint32 ReadLe32(const FByteArray& bytes, size_t offset)
    {
        return static_cast<uint32>(bytes[offset])
            | (static_cast<uint32>(bytes[offset + 1]) << 8)
            | (static_cast<uint32>(bytes[offset + 2]) << 16)
            | (static_cast<uint32>(bytes[offset + 3]) << 24);
    }
}

FCompressionProbe FCompressionService::Probe(const FByteArray& bytes) const
{
    if (bytes.size() >= 14 && StartsWithBytes(bytes, "SPHR"))
    {
        auto p = FSphrCodec::Probe(bytes);
        return {ECompressionMethod::LegacySphr, 0, p.ExpectedSize, "legacy SPHR zlib config wrapper"};
    }

    if (bytes.size() >= 12 && StartsWithBytes(bytes, "LZSS"))
    {
        return {ECompressionMethod::LegacyLzss, 8, ReadLe32(bytes, 4), "explicit LZSS marker"};
    }

    if (bytes.size() >= 12 && StartsWithBytes(bytes, "LZ77"))
    {
        return {ECompressionMethod::LegacyLzss, 8, ReadLe32(bytes, 4), "explicit LZ77 marker mapped to legacy LZSS"};
    }

    if (bytes.size() >= 2 && bytes[0] == 0x78) { return {ECompressionMethod::ZlibDeflate, 0, 0, "zlib/deflate stream"}; }

    if (bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B)
    {
        return
        {
            ECompressionMethod::Unsupported, 0, 0, "gzip-like stream; external decoder not linked"
        };
    }

    return
    {
        ECompressionMethod::Raw, 0, bytes.size(), "raw payload"
    };
}

TResult<FByteArray> FCompressionService::DecompressAuto(const FByteArray& bytes) const
{
    FCompressionProbe probe = Probe(bytes);
    return Decompress(probe.Method, bytes.data() + probe.HeaderSize, bytes.size() - probe.HeaderSize, probe.ExpectedSize);
}

TResult<FByteArray> FCompressionService::Decompress(ECompressionMethod method, const uint8* data, size_t size, size_t expectedSize) const
{
    if (method == ECompressionMethod::Raw) { return FByteArray(data, data + size); }

    if (method == ECompressionMethod::LegacyLzss) { return FLzssCodec::DecodeLegacyWindow(data, size, expectedSize); }

    if (method == ECompressionMethod::ZlibDeflate) { return FZlibInflate::DecodeZlib(data, size, expectedSize); }

    if (method == ECompressionMethod::LegacySphr) { FByteArray bytes(data, data + size); return FSphrCodec::Decode(bytes); }

    return FStatus::Error(EStatusCode::Unsupported, "unsupported compression method in recovered slice");
}
