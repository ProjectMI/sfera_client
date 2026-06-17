#pragma once
#include "Core/Types.h"
#include <unordered_map>
#include <variant>

namespace Sfera {
namespace Mbc {
inline constexpr uint32 CodeFileOffset = 0x20;
inline constexpr char MagicText[16] = {'M','B','L',' ','s','c','r','i','p','t',' ','v','4','.','0','\0'};
inline constexpr uint8 TypeChar = 0x00;
inline constexpr uint8 TypeString = 0x01;
inline constexpr uint8 TypeStringRef = 0x02;
inline constexpr uint8 TypeInt = 0x10;
inline constexpr uint8 TypeIntRef = 0x11;
inline constexpr uint8 TypeIntRefRef = 0x12;
inline constexpr uint8 TypeFloat = 0x20;
inline constexpr uint8 TypeFloatRef = 0x21;
inline constexpr uint8 TypeFloatRefRef = 0x22;
inline constexpr uint8 TypeSlice = 0x30;
inline constexpr uint8 TypeSliceRef = 0x31;
int8 Sign8(uint8 value);
int16 ReadI16(const FByteArray& bytes, uint32 offset);
uint16 ReadU16(const FByteArray& bytes, uint32 offset);
int32 ReadI32(const FByteArray& bytes, uint32 offset);
uint32 ReadU32(const FByteArray& bytes, uint32 offset);
float FloatFromU32(uint32 value);
std::string ReadCString(const FByteArray& bytes, uint32& offset);
std::string TypeName(uint8 typeId);
bool IsReferenceType(uint8 typeId);
uint32 StorageSizeForType(uint8 typeId);
uint32 DereferencedStorageSizeForType(uint8 typeId);
}

struct FMbcHeader { std::string Magic; uint32 ChecksumOrTag = 0; uint32 ModuleTag = 0; uint32 CodeSize = 0; uint32 DataSize = 0; uint32 CodeFileOffset = Mbc::CodeFileOffset; };
struct FMbcProgram { uint32 Index = 0; std::string Name; uint32 Start = 0; uint32 End = 0; uint8 StateRaw = 0; uint8 QueueId = 0; uint32 Unknown48 = 0; int8 State() const { return Mbc::Sign8(StateRaw); } uint32 FileStart() const { return Mbc::CodeFileOffset + Start; } uint32 FileEnd() const { return Mbc::CodeFileOffset + End; } bool Contains(uint32 codeOffset) const { return Start <= codeOffset && codeOffset <= End; } };
struct FMbcFunction { uint32 Index = 0; std::string Name; uint32 CodeOffset = 0; uint32 ProgramIndexRaw = 0; uint32 FlagsOrModule = 0; int32 ProgramIndex() const { return static_cast<int32>(ProgramIndexRaw); } bool IsImport() const { return ProgramIndex() < 0; } uint32 FileOffset() const { return Mbc::CodeFileOffset + CodeOffset; } };
struct FMbcArrayGuard { uint32 Index = 0; uint32 BeginGuardOffset = 0; uint32 EndGuardOffset = 0; };
struct FMbcSlice { uint32 Offset = 0; uint32 Length = 0; uint32 Capacity = 0; };
struct FMbcValue { uint8 Type = Mbc::TypeInt; int32 IntValue = 0; float FloatValue = 0.0f; FMbcSlice Slice; uint32 DataOffset = 0; bool IsLValue = false; static FMbcValue Int(int32 value); static FMbcValue Float(float value); static FMbcValue Char(uint8 value); static FMbcValue SliceValue(uint32 offset, uint32 length, uint32 capacity); static FMbcValue Ref(uint8 type, uint32 dataOffset); };
struct FMbcRuntimeEvent { std::string Kind; std::string Name; uint32 ModuleIndex = 0; uint32 ProgramIndex = 0; uint32 CodeOffset = 0; std::string Commentary; };
}
