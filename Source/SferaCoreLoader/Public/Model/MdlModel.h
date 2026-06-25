#pragma once
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct FMdlSection 
{ 
	std::string Name;
	size_t Offset = 0;
	size_t Count = 0; 
	size_t Stride = 0; 
	size_t Size = 0;
};

struct FMdlInfo 
{ 
	FPath SourcePath;
	size_t FileSize = 0;
	uint16 VertexCount = 0;
	uint16 IndexCount = 0; 
	uint16 TriangleGroupCount = 0;
	uint8 MaterialCount = 0; 
	uint16 MaterialNamesSize = 0;
	uint8 StripGroupCount = 0; 
	uint8 SmallBlockSize = 0; 
	uint8 UnknownFlag0F = 0; 
	uint16 SkinRecordCount = 0; 
	uint16 SkinIndexCount = 0; 
	uint8 SkinWeightCount = 0; 
	uint8 AnimationFlag = 0;
	uint16 AnimationTableCount = 0;
	int32 ExtraMode = 0; 
	int32 ExtraRecordCount = 0;
	int32 ExtraBlockCount = 0; 
	std::vector<std::string> Materials;
	std::vector<FMdlSection> Sections; 
	size_t ComputedSize = 0;
};

struct FMdlVertex
{ 
	float X = 0.0f; 
	float Y = 0.0f; 
	float Z = 0.0f; 
	float NX = 0.0f; 
	float NY = 0.0f;
	float NZ = 0.0f; 
	float U = 0.0f; 
	float V = 0.0f; 
};

struct FMdlTriangle 
{
	uint16 A = 0; 
	uint16 B = 0; 
	uint16 C = 0; 
	uint16 Flags = 0;
	uint16 Reserved = 0; 
};

struct FMdlSurface
{ 
	uint8 ObjectIndex = 0; 
	uint8 TextureIndex = 0;
	int16 FirstTriangleIndex = 0;
	int16 TriangleCount = 0;
	int16 FirstVertexIndex = 0;
	int16 VertexCount = 0; 
};

struct FMdlObject 
{
	std::string Name; 
	uint8 BoneType = 0;
	uint8 ConnectedBoneCount = 0; 
	uint8 ObjectIndexOffset = 0; 
	uint8 IsAnimated = 0; 
	int16 KeyIndex = 0; 
	uint8 ParentIndex = 0; 
};

struct FMdlTransformKey 
{
	float X = 0.0f; 
	float Y = 0.0f; 
	float Z = 0.0f;
	float QW = 1.0f; 
	float QX = 0.0f;
	float QY = 0.0f; 
	float QZ = 0.0f;
};

struct FMdlSkinIndex
{
	uint16 Record = 0;
	uint8 Blend = 0;
};

struct FMdlBounds 
{
	float MinX = 0.0f;
	float MinY = 0.0f; 
	float MinZ = 0.0f; 
	float MaxX = 0.0f; 
	float MaxY = 0.0f;
	float MaxZ = 0.0f; 
};

struct FMdlMesh 
{
	FMdlInfo Info; 
	std::vector<FMdlVertex> Vertices; 
	std::vector<FMdlTriangle> Triangles; 
	std::vector<FMdlSurface> Surfaces; 
	std::vector<FMdlObject> Objects; 
	FByteArray ObjectIndices; 
	std::vector<FMdlTransformKey> TransformKeys; 
	std::vector<FMdlSkinIndex> SkinIndices;
	std::vector<uint16> Actions;
	FMdlBounds Bounds; 
};

TResult<FMdlInfo> LoadMdlInfoFromBytes(const FByteArray& bytes, std::string_view sourceName);
TResult<FMdlMesh> LoadMdlMeshFromBytes(const FByteArray& bytes, std::string_view sourceName);
TResult<FMdlInfo> LoadMdlInfoFromResource(const FResourceManager& resources, std::string_view logicalName);
TResult<FMdlMesh> LoadMdlMeshFromResource(const FResourceManager& resources, std::string_view logicalName);

std::shared_ptr<const FMdlMesh> FindCachedMdlMesh(std::string_view sourceName);
void AliasCachedMdlMesh(std::string_view aliasName, std::string_view sourceName);
void ClearMdlMeshCache();
size_t CachedMdlMeshCount();
