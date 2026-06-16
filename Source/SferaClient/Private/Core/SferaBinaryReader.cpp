#include "SferaBinaryReader.h"
#include <cstring>

SferaBinaryReader::SferaBinaryReader()
{
}

SferaBinaryReader::SferaBinaryReader(const std::vector<SferaUInt8>& InBytes)
{
    Reset(InBytes);
}

void SferaBinaryReader::Reset(const std::vector<SferaUInt8>& InBytes)
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
    OutValue = (*Bytes)[Offset++];
    return true;
}

bool SferaBinaryReader::ReadUInt16LE(SferaUInt16& OutValue)
{
    if (!IsValidOffset(Offset, 2))
    {
        return false;
    }
    OutValue = static_cast<SferaUInt16>((*Bytes)[Offset]) |
        static_cast<SferaUInt16>((*Bytes)[Offset + 1] << 8);
    Offset += 2;
    return true;
}

bool SferaBinaryReader::ReadUInt32LE(SferaUInt32& OutValue)
{
    if (!IsValidOffset(Offset, 4))
    {
        return false;
    }
    OutValue = static_cast<SferaUInt32>((*Bytes)[Offset]) |
        (static_cast<SferaUInt32>((*Bytes)[Offset + 1]) << 8) |
        (static_cast<SferaUInt32>((*Bytes)[Offset + 2]) << 16) |
        (static_cast<SferaUInt32>((*Bytes)[Offset + 3]) << 24);
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

bool SferaBinaryReader::ReadBytes(size_t Count, std::vector<SferaUInt8>& OutBytes)
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
        const char Ch = static_cast<char>((*Bytes)[Offset++]);
        ++Count;
        if (Ch == '\0')
        {
            return true;
        }
        OutString.push_back(Ch);
    }

    return false;
}
