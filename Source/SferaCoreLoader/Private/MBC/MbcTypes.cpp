#include "MBC/MbcTypes.h"
#include <bit>
#include <stdexcept>

namespace Mbc
{
    int8 Sign8(uint8 value) { return value >= 0x80 ? static_cast<int8>(int(value) - 0x100) : static_cast<int8>(value); }
    static void Need(const FByteArray& bytes, uint32 offset, uint32 count)
    {
        if (offset > bytes.size() || count > bytes.size() - offset)
        {
            throw std::runtime_error("MBC read past end");
        }
    }
    int16 ReadI16(const FByteArray& bytes, uint32 offset)
    {
        Need(bytes, offset, 2);
        return static_cast<int16>(uint16(bytes[offset]) | (uint16(bytes[offset + 1]) << 8));
    }
    uint16 ReadU16(const FByteArray& bytes, uint32 offset)
    {
        Need(bytes, offset, 2);
        return uint16(bytes[offset]) | (uint16(bytes[offset + 1]) << 8);
    }
    uint32 ReadU32(const FByteArray& bytes, uint32 offset)
    {
        Need(bytes, offset, 4);
        return uint32(bytes[offset]) | (uint32(bytes[offset + 1]) << 8) | (uint32(bytes[offset + 2]) << 16) | (uint32(bytes[offset + 3]) << 24);
    }
    int32 ReadI32(const FByteArray& bytes, uint32 offset) { return static_cast<int32>(ReadU32(bytes, offset)); }
    float FloatFromU32(uint32 value)
    {
        return std::bit_cast<float>(value);
    }
    std::string ReadCString(const FByteArray& bytes, uint32& offset)
    {
        uint32 start = offset;

        while (offset < bytes.size() && bytes[offset] != 0)
        {
            ++offset;
        }

        if (offset >= bytes.size())
        {
            throw std::runtime_error("unterminated MBC string");
        }

        std::string result;
        result.reserve(offset - start);

        for (uint32 i = start; i < offset; ++i)
        {
            result.push_back(static_cast<char>(bytes[i]));
        }
        ++offset;
        return result;
    }
    std::string TypeName(uint8 typeId)
    {
        switch (typeId)
        {
        case TypeChar: return "i8/char";
        case TypeString: return "span/string";
        case TypeStringRef: return "span/string_ref";
        case TypeInt: return "int32";
        case TypeIntRef: return "int32_ref_or_span";
        case TypeIntRefRef: return "int32_ref_or_span_ref";
        case TypeFloat: return "float32";
        case TypeFloatRef: return "float32_ref_or_span";
        case TypeFloatRefRef: return "float32_ref_or_span_ref";
        case TypeSlice: return "slice_descriptor";
        case TypeSliceRef: return "slice_descriptor_ref";
        default:
            {
                uint8 base = typeId & 0xF0;
                uint8 depth = typeId & 0x0F;
                std::string name = base == 0x00 ? "span/string" : base == 0x10 ? "int32" : base == 0x20 ? "float32" : base == 0x30 ? "slice_descriptor" : "unknown";

                for (uint8 i = 0; i < depth; ++i)
                {
                    name += "_ref";
                }

                return name;
            }
        }
    }
    bool IsReferenceType(uint8 typeId) { return (typeId & 0x0F) != 0; }
    uint32 StorageSizeForType(uint8 typeId)
    {
        if (typeId == TypeChar)
        {
            return 1;
        }

        if (typeId == TypeInt || typeId == TypeFloat)
        {
            return 4;
        }

        return typeId != 0 ? 0x0C : 1;
    }
    uint32 DereferencedStorageSizeForType(uint8 typeId)
    {
        if ((typeId & 0x0F) != 0)
        {
            return 0x0C;
        }

        return typeId == TypeChar ? 1 : 4;
    }
}
FMbcValue FMbcValue::Int(int32 value)
{
    FMbcValue v;
    v.Type = Mbc::TypeInt;
    v.IntValue = value;
    v.FloatValue = static_cast<float>(value);
    return v;
}
FMbcValue FMbcValue::Float(float value)
{
    FMbcValue v;
    v.Type = Mbc::TypeFloat;
    v.FloatValue = value;
    v.IntValue = static_cast<int32>(value);
    return v;
}
FMbcValue FMbcValue::Char(uint8 value)
{
    FMbcValue v;
    v.Type = Mbc::TypeChar;
    v.IntValue = value;
    v.FloatValue = static_cast<float>(value);
    return v;
}
FMbcValue FMbcValue::SliceValue(uint32 offset, uint32 length, uint32 capacity)
{
    FMbcValue v;
    v.Type = Mbc::TypeSlice;
    v.Slice =
    {
        offset, length, capacity
    };
    return v;
}
FMbcValue FMbcValue::Ref(uint8 type, uint32 dataOffset)
{
    FMbcValue v;
    v.Type = type;
    v.DataOffset = dataOffset;
    v.IsLValue = true;
    return v;
}
