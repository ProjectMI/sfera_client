#pragma once
#include "Core/Types.h"

namespace Sfera {
enum class ECompressionMethod { Raw, LegacyLzss, ZlibDeflate, LegacySphr, Unsupported };
struct FCompressionProbe { ECompressionMethod Method = ECompressionMethod::Raw; size_t HeaderSize = 0; size_t ExpectedSize = 0; std::string Commentary; };

class FCompressionService {
public:
    FCompressionProbe Probe(const FByteArray& bytes) const;
    TResult<FByteArray> DecompressAuto(const FByteArray& bytes) const;
    TResult<FByteArray> Decompress(ECompressionMethod method, const uint8* data, size_t size, size_t expectedSize = 0) const;
};
}
