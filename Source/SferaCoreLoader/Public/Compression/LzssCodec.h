#pragma once
#include "Core/Types.h"

namespace Sfera {
class FLzssCodec {
public:
    static TResult<FByteArray> DecodeLegacyWindow(const uint8* data, size_t size, size_t expectedSize = 0);
};
}
