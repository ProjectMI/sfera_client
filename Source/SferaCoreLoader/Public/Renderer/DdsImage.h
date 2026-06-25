#pragma once
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"

struct FDdsImage 
{ 
	int32 Width = 0; 
	int32 Height = 0;
	int32 Stride = 0; 
	bool HasAlpha = false;
	FByteArray BgraPixels;
	explicit operator bool() const { return Width > 0 && Height > 0 && !BgraPixels.empty(); } 
};

TResult<FDdsImage> DecodeDdsRgbImageFromBytes(const FByteArray& bytes, std::string_view sourceName);
TResult<FDdsImage> DecodeDdsRgbImageFromResource(const FResourceManager& resources, std::string_view logicalName);
