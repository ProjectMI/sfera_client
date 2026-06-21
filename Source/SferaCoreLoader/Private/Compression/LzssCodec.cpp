#include "Compression/LzssCodec.h"

TResult<FByteArray> FLzssCodec::DecodeLegacyWindow(const uint8* data, size_t size, size_t expectedSize)
{
    FByteArray output;

    if (expectedSize)
    {
        output.reserve(expectedSize);
    }

    size_t pos = 0;

    while (pos < size)
    {
        uint8 flags = data[pos++];

        for (int bit = 0; bit < 8 && pos < size; ++bit)
        {
            bool literal = (flags & (1u << bit)) != 0;

            if (literal) { output.push_back(data[pos++]); continue; }

            if (pos + 1 >= size) { return FStatus::Error(EStatusCode::InvalidData, "truncated LZSS back-reference"); }

            uint16 token = static_cast<uint16>(data[pos] | (data[pos + 1] << 8));
            pos += 2;
            size_t offset = ((token >> 4) & 0x0FFFu) + 1u;
            size_t length = (token & 0x000Fu) + 3u;

            if (offset > output.size()) { return FStatus::Error(EStatusCode::InvalidData, "invalid LZSS back-reference"); }

            for (size_t i = 0; i < length; ++i)
            {
                output.push_back(output[output.size() - offset]);
            }

            if (expectedSize && output.size() > expectedSize) { return FStatus::Error(EStatusCode::InvalidData, "LZSS output overflow"); }
        }
    }

    if (expectedSize && output.size() != expectedSize) { return FStatus::Error(EStatusCode::InvalidData, "LZSS output size mismatch"); }

    return output;
}
