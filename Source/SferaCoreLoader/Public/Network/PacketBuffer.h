#pragma once
#include "Core/Types.h"

enum class EPacketLengthMode 
{ 
    UInt16LE,
    UInt32LE 
};

class FPacketBuffer 
{
public:
    explicit FPacketBuffer(EPacketLengthMode mode = EPacketLengthMode::UInt16LE);
    void Append(const uint8* data, size_t size);
    bool TryPopFrame(FByteArray& outFrame);
    FByteArray MakeFrame(const FByteArray& payload) const;
private:
    EPacketLengthMode Mode;
    FByteArray Buffer;
};