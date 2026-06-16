#include "SferaBinaryReader.h"
#include <cstring>

namespace
{
    SferaUInt8 ToUInt8(SferaByte Value)
    {
        return static_cast<SferaUInt8>(Value);
    }
}

SferaBinaryReader::SferaBinaryReader()
{
}

SferaBinaryReader::SferaBinaryReader(const SferaByteBuffer& InBytes)
{
    Reset(InBytes);
}

void SferaBinaryReader::Reset(const SferaByteBuffer& InBytes)
{
    Bytes = &InBytes;
    Offset = 0;
}

bool SferaBinaryReader::IsValidOffset(size_t InOffset, size_t InSize) const
{
    if (!Bytes)
    {
        return false;
    }
    return InOffset <= Bytes->size() && InSize <= Bytes->size() - InOffset;
}

bool SferaBinaryReader::Seek(size_t NewOffset)
{
    if (!Bytes || NewOffset > Bytes->size())
    {
        return false;
    }
    Offset = NewOffset;
    return true;
}

size_t SferaBinaryReader::Remaining() const
{
    if (!Bytes || Offset > Bytes->size())
    {
        return 0;
    }
    return Bytes->size() - Offset;
}

bool SferaBinaryReader::ReadUInt8(SferaUInt8& OutValue)
{
    if (!IsValidOffset(Offset, 1))
    {
        return false;
    }
    OutValue = ToUInt8((*Bytes)[Offset++]);
    return true;
}

bool SferaBinaryReader::ReadUInt16LE(SferaUInt16& OutValue)
{
    if (!IsValidOffset(Offset, 2))
    {
        return false;
    }
    OutValue = static_cast<SferaUInt16>(ToUInt8((*Bytes)[Offset])) |
        static_cast<SferaUInt16>(ToUInt8((*Bytes)[Offset + 1]) << 8);
    Offset += 2;
    return true;
}

bool SferaBinaryReader::ReadUInt32LE(SferaUInt32& OutValue)
{
    if (!IsValidOffset(Offset, 4))
    {
        return false;
    }
    OutValue = static_cast<SferaUInt32>(ToUInt8((*Bytes)[Offset])) |
        (static_cast<SferaUInt32>(ToUInt8((*Bytes)[Offset + 1])) << 8) |
        (static_cast<SferaUInt32>(ToUInt8((*Bytes)[Offset + 2])) << 16) |
        (static_cast<SferaUInt32>(ToUInt8((*Bytes)[Offset + 3])) << 24);
    Offset += 4;
    return true;
}

bool SferaBinaryReader::ReadInt32LE(SferaInt32& OutValue)
{
    SferaUInt32 Raw = 0;
    if (!ReadUInt32LE(Raw))
    {
        return false;
    }
    OutValue = static_cast<SferaInt32>(Raw);
    return true;
}

bool SferaBinaryReader::ReadFloat32LE(float& OutValue)
{
    SferaUInt32 Raw = 0;
    if (!ReadUInt32LE(Raw))
    {
        return false;
    }
    static_assert(sizeof(float) == sizeof(SferaUInt32), "float size mismatch");
    std::memcpy(&OutValue, &Raw, sizeof(float));
    return true;
}

bool SferaBinaryReader::ReadBytes(size_t Count, SferaByteBuffer& OutBytes)
{
    if (!IsValidOffset(Offset, Count))
    {
        return false;
    }
    OutBytes.assign(Bytes->begin() + static_cast<std::ptrdiff_t>(Offset), Bytes->begin() + static_cast<std::ptrdiff_t>(Offset + Count));
    Offset += Count;
    return true;
}

bool SferaBinaryReader::ReadCString(size_t MaxBytes, std::string& OutString)
{
    OutString.clear();
    if (!Bytes)
    {
        return false;
    }

    size_t Count = 0;
    while (Offset < Bytes->size() && Count < MaxBytes)
    {
        const SferaUInt8 Byte = ToUInt8((*Bytes)[Offset++]);
        ++Count;
        if (Byte == 0)
        {
            return true;
        }
        OutString.push_back(static_cast<std::string::value_type>(Byte));
    }

    return false;
}
