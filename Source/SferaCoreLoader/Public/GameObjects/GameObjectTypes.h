#pragma once
#include "Core/Types.h"
#include "WorldScene/WorldTypes.h"
#include <unordered_map>
#include <variant>

enum class EGameObjectKind
{ 
	Unknown, 
	StaticObject,
	DynamicObject, 
	Character, 
	Item, 
	EffectProxy,
	ScriptProxy 
};

enum class EObjectParamType 
{
	Missing,
	Integer, 
	Float, 
	String 
};

struct FObjectParamValue
{
	EObjectParamType Type = EObjectParamType::Missing; 
	int32 IntValue = 0;
	float FloatValue = 0.0f; 
	std::string StringValue; 
	static FObjectParamValue Missing(); 
	static FObjectParamValue Int(int32 value);
	static FObjectParamValue Float(float value); 
	static FObjectParamValue String(std::string value);
};

struct FGameObjectDescriptor 
{ 
	std::string Archetype; 
	std::string SourceConfig;
	std::unordered_map<std::string, FObjectParamValue> Params;
};

struct FGameObjectState
{ 
	uint32 Handle = 0; 
	EGameObjectKind Kind = EGameObjectKind::Unknown;
	std::string Archetype;
	FVector3 Position; 
	FVector3 Direction; 
	FVector3 Scale{1.0f, 1.0f, 1.0f};
	float Speed = 0.0f; 
	int32 Trigger = 0; 
	bool Alive = true; 
};