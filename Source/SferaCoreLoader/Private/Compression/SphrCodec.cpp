#include "Compression/SphrCodec.h"
#include "Compression/ZlibInflate.h"
#include <algorithm>
#include <array>
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
}

FSphrProbe FSphrCodec::Probe(const FByteArray& bytes)
{
    FSphrProbe p;

    if (bytes.size() < 14 || !StartsWithBytes(bytes, "SPHR")) { return p; }

    p.IsSphr = true;
    p.HeaderXorKey = bytes.size() > 8 ? bytes[8] : 0;
    p.SizeXorKey = bytes[14];

    std::array<uint8, 4> decoded{};

    for (size_t i = 0; i < decoded.size(); ++i)
    {
        decoded[i] = static_cast<uint8>(bytes[4 + i] ^ p.SizeXorKey);
    }

    p.ExpectedSize = static_cast<uint32>(decoded[0])
        | (static_cast<uint32>(decoded[1]) << 8)
        | (static_cast<uint32>(decoded[2]) << 16)
        | (static_cast<uint32>(decoded[3]) << 24);

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
