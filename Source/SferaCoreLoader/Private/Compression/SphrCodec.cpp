#include "Compression/SphrCodec.h"
#include "Compression/ZlibInflate.h"
#include <cstring>

FSphrProbe FSphrCodec::Probe(const FByteArray& bytes)
{
    FSphrProbe p;

    if (bytes.size() < 14 || std::memcmp(bytes.data(), "SPHR", 4) != 0) { return p; }

    p.IsSphr = true;
    p.HeaderXorKey = bytes.size() > 8 ? bytes[8] : 0;
    p.SizeXorKey = bytes[14];
    uint8 decoded[4] =
    {
        0,0,0,0
    };

    for (size_t i = 0; i < 4; ++i)
    {
        decoded[i] = static_cast<uint8>(bytes[4 + i] ^ p.SizeXorKey);
    }

    p.ExpectedSize = uint32(decoded[0]) | (uint32(decoded[1]) << 8) | (uint32(decoded[2]) << 16) | (uint32(decoded[3]) << 24);
    return p;
}

TResult<FByteArray> FSphrCodec::Decode(const FByteArray& bytes)
{
    FSphrProbe probe = Probe(bytes);

    if (!probe.IsSphr) { return FStatus::Error(EStatusCode::InvalidData, "not an SPHR legacy config payload"); }

    if (bytes.size() < 14) { return FStatus::Error(EStatusCode::InvalidData, "truncated SPHR payload"); }

    FByteArray patched = bytes;
    // Recovered from sub_4BE2B0: selected zlib header bytes are xor-masked by byte +0x08.

    for (size_t off : {size_t(9), size_t(17), size_t(20)})
    {
        if (off < patched.size())
        {
            patched[off] ^= probe.HeaderXorKey;
        }
    }

    for (size_t i = 0; i < 4; ++i)
    {
        patched[4 + i] ^= probe.SizeXorKey;
    }

    if (patched.size() <= 8) { return FStatus::Error(EStatusCode::InvalidData, "SPHR payload has no zlib body"); }

    return FZlibInflate::DecodeZlib(patched.data() + 8, patched.size() - 8, probe.ExpectedSize);
}
