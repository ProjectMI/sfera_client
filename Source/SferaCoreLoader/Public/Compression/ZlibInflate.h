#pragma once
#include "Core/Types.h"

class FZlibInflate 
{
public:
    static TResult<FByteArray> DecodeZlib(const uint8* data, size_t size, size_t expectedSize = 0);
    static TResult<FByteArray> DecodeRawDeflate(const uint8* data, size_t size, size_t expectedSize = 0);
};