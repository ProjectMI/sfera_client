#include "Compression/CompressionService.h"
#include "Common/BinaryData.h"
#include "Compression/LzssCodec.h"
#include "Compression/SphrCodec.h"
#include "Compression/ZlibInflate.h"


FCompressionProbe FCompressionService::Probe(const FByteArray& bytes) const
{
    if (bytes.size() >= 14 && Common::StartsWithBytes(bytes, "SPHR"))
    {
        auto p = FSphrCodec::Probe(bytes);
        return {ECompressionMethod::LegacySphr, 0, p.ExpectedSize, "legacy SPHR zlib config wrapper"};
    }

    if (bytes.size() >= 12 && Common::StartsWithBytes(bytes, "LZSS"))
    {
        return {ECompressionMethod::LegacyLzss, 8, Common::U32LEUnchecked(bytes, 4), "explicit LZSS marker"};
    }

    if (bytes.size() >= 12 && Common::StartsWithBytes(bytes, "LZ77"))
    {
        return {ECompressionMethod::LegacyLzss, 8, Common::U32LEUnchecked(bytes, 4), "explicit LZ77 marker mapped to legacy LZSS"};
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
