#pragma once
#include "Core/Types.h"
#include "MBC/MbcTypes.h"

struct FMbcStringRef
{ 
    uint32 Offset = 0; 
    std::string Value; 
};

struct FMbcHeaderProbe 
{ 
    bool HasKnownMagic = false; 
    uint32 Version = 4;
    uint32 CodeOffset = Mbc::CodeFileOffset;
    uint32 CodeSize = 0; 
    std::string Commentary; 
};

class FMbcModule 
{
public:
    FStatus Load(std::string name, FByteArray bytes);
    const std::string& Name() const { return ModuleName; }
    const FByteArray& Bytes() const { return RawBytes; }
    const FByteArray& Code() const { return CodeBytes; }
    const FByteArray& Data() const { return DataBytes; }
    const FByteArray& Metadata() const { return MetadataBytes; }
    const std::vector<FMbcStringRef>& Strings() const { return StringsFound; }
    const FMbcHeaderProbe& Header() const { return HeaderProbe; }
    const FMbcHeader& ScriptHeader() const { return ParsedHeader; }
    const std::vector<FMbcProgram>& Programs() const { return ProgramTable; }
    const std::vector<FMbcFunction>& Functions() const { return FunctionTable; }
    bool IsValid() const { return Valid; }
    const FMbcProgram* ProgramByName(std::string_view name) const;
    const FMbcProgram* ProgramForOffset(uint32 codeOffset) const;
    const FMbcFunction* FunctionByName(std::string_view name) const;
    const FMbcFunction* FunctionAtOffset(uint32 codeOffset) const;
private:
    FStatus ParseImage();
    void ExtractStringsFromData();
    std::string ModuleName;
    FByteArray RawBytes;
    FByteArray CodeBytes;
    FByteArray DataBytes;
    FByteArray MetadataBytes;
    FMbcHeaderProbe HeaderProbe;
    FMbcHeader ParsedHeader;
    std::vector<FMbcProgram> ProgramTable;
    std::vector<FMbcFunction> FunctionTable;
    std::vector<FMbcStringRef> StringsFound;
    bool Valid = false;
};
