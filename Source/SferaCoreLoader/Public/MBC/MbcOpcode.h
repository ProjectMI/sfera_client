#pragma once
#include "MBC/MbcTypes.h"

enum class EMbcOperandFormat 
{
	None, 
	Trap,
	U8,
	U16, 
	ProgramI16,
	Rel16, 
	Rel32, 
	JFalseRel16,
	JFalseRel32, 
	LogicalOrRel16, 
	LogicalAndRel16, 
	CallRel32, DataRef, 
	TypedImm32, 
	TypedImmU16, 
	TypedImmI8, 
	ImportStubU32,
	InlineSpan, 
	TypedSpanRef, 
	TypedSpanInline, 
	ArrayAbs, 
	Array2Checked, 
	Array2,
	SliceOffsetRef, 
	SliceOffsetSpan, 
	Prologue, 
	Builtin
};

struct FMbcOpcodeSpec
{ 
	uint8 Opcode = 0; 
	uint8 Char = 0;
	uint32 TableEa = 0; 
	uint32 HandlerEa = 0; 
	std::string_view HandlerName; 
	std::string_view Mnemonic;
	EMbcOperandFormat Format = EMbcOperandFormat::None; 
	std::string_view Semantic; 
};

struct FMbcBuiltinSpec 
{ 
	uint8 SubOpcode = 0; 
	uint32 TableEa = 0; 
	uint32 TargetEa = 0;
	std::string_view TargetName; 
	std::string_view Mnemonic;
	std::string_view Semantic;
	std::string_view Confidence = "unverified"; 
};

struct FMbcDecodedEdge 
{
	std::string Kind;
	std::optional<uint32> Dst; 
	std::string Note; 
};

struct FMbcDecodedOpcode 
{ 
	std::string Mnemonic;
	uint32 Length = 1; 
	std::unordered_map<std::string, std::string> Operands;
	bool Terminal = false; 
	bool Known = true;
	std::vector<FMbcDecodedEdge> Edges;
	const FMbcOpcodeSpec* Spec = nullptr;
	const FMbcBuiltinSpec* Builtin = nullptr; 
};

const FMbcOpcodeSpec* FindMbcOpcode(uint8 opcode);
const FMbcBuiltinSpec* FindMbcBuiltin(uint8 subOpcode);
const std::vector<FMbcOpcodeSpec>& MbcOpcodeTable();
const std::vector<FMbcBuiltinSpec>& MbcBuiltinTable();
FMbcDecodedOpcode DecodeMbcOpcode(const FByteArray& code, uint32 offset);
