#pragma once
#include "SferaBase.h"

class SferaBinaryReader
{
public:
    SferaBinaryReader();
    explicit SferaBinaryReader(const SferaByteBuffer& InBytes);

    void Reset(const SferaByteBuffer& InBytes);
    bool IsValidOffset(size_t Offset, size_t Size) const;
    bool Seek(size_t NewOffset);
    size_t Tell() const { return Offset; }
    size_t Size() const { return Bytes ? Bytes->size() : 0; }
    SferaUInt8 ToUInt8(SferaByte Value) { return static_cast<SferaUInt8>(Value); }
    size_t Remaining() const;

    bool ReadUInt8(SferaUInt8& OutValue);
    bool ReadUInt16LE(SferaUInt16& OutValue);
    bool ReadUInt32LE(SferaUInt32& OutValue);
    bool ReadInt32LE(SferaInt32& OutValue);
    bool ReadFloat32LE(float& OutValue);
    bool ReadBytes(size_t Count, SferaByteBuffer& OutBytes);
    bool ReadCString(size_t MaxBytes, std::string& OutString);

private:
    const SferaByteBuffer* Bytes = nullptr;
    size_t Offset = 0;
};
