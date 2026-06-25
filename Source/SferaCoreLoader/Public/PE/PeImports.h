#pragma once
#include "Core/Types.h"

struct FPeImportSymbol 
{ 
	std::optional<uint16> Ordinal; 
	std::string Name; 
	uint32 ThunkRva = 0; 
};

struct FPeImportLibrary 
{
	std::string Name;
	std::vector<FPeImportSymbol> Symbols; 
};

struct FPeSection 
{ 
	std::string Name;
	uint32 VirtualAddress = 0;
	uint32 VirtualSize = 0; 
	uint32 RawOffset = 0; 
	uint32 RawSize = 0;
};

struct FPeImage 
{ 
	FPath SourcePath;
	bool Pe32Plus = false; 
	uint16 Machine = 0; 
	uint32 ImageBase32 = 0; 
	uint64 ImageBase64 = 0; 
	uint32 EntryPointRva = 0;
	std::vector<FPeSection> Sections; 
	std::vector<FPeImportLibrary> Imports; 
};

TResult<FPeImage> ParsePeImportsFromBytes(const FByteArray& bytes, std::string_view sourceName);
