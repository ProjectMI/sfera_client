#pragma once
#include "Core/Types.h"

enum class EResourceKind 
{ 
	Unknown, 
	Config, 
	Mbc, 
	Ui, 
	Texture,
	Model,
	Material,
	Landscape,
	Script, 
	Sound,
	Binary
};

struct FResourceId
{
	std::string LogicalName; 
	EResourceKind Kind = EResourceKind::Unknown; 
};

struct FResourceBlob 
{ 
	FResourceId Id;
	FPath SourcePath; 
	FByteArray Bytes;
	bool WasCompressed = false; 
};

std::string_view ToString(EResourceKind kind);
EResourceKind GuessResourceKind(const FPath& path);
