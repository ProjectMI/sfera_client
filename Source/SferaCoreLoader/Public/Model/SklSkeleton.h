#pragma once
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"

struct FSklTransform 
{ 
	float QX = 0.0f; 
	float QY = 0.0f;
	float QZ = 0.0f;
	float QW = 1.0f;
	float TX = 0.0f; 
	float TY = 0.0f; 
	float TZ = 0.0f; 
};

struct FSklSkeleton
{ 
	FPath SourcePath;
	int32 BoneCount = 0; 
	int32 FrameCount = 0; 
	std::vector<int32> Parents;
	std::vector<std::string> BoneNames; 
	std::vector<FSklTransform> Transforms;
	std::vector<int32> AnimationFrameCounts; 
};

TResult<FSklSkeleton> LoadSklSkeletonFromBytes(const FByteArray& bytes, std::string_view sourceName);
TResult<FSklSkeleton> LoadSklSkeletonFromResource(const FResourceManager& resources, std::string_view logicalName);
