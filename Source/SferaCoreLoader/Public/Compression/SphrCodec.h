#pragma once
#include "Core/Types.h"

struct FSphrProbe 
{
    bool IsSphr = false;
    uint32 ExpectedSize = 0;
    uint8 HeaderXorKey = 0;
    uint8 SizeXorKey = 0;
};

class FSphrCodec 
{
public:
    static FSphrProbe Probe(const FByteArray& bytes);
    static TResult<FByteArray> Decode(const FByteArray& bytes);
};
