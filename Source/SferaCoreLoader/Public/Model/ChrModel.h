#pragma once
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include <cstddef>
#include <string>
#include <vector>

namespace Sfera {
struct FChrChunk { int32 Type = 0; size_t Offset = 0; size_t Size = 0; };
struct FChrInfo { FPath SourcePath; size_t FileSize = 0; int32 VersionOrFlags = 0; int32 ChunkCount = 0; std::vector<FChrChunk> Chunks; std::vector<std::string> BoneNames; };
struct FChrVertex { float X = 0.0f; float Y = 0.0f; float Z = 0.0f; float NX = 0.0f; float NY = 0.0f; float NZ = 0.0f; float U = 0.0f; float V = 0.0f; uint8 Bone0 = 0; uint8 Bone1 = 0; float Blend = 1.0f; };
struct FChrBounds { float MinX = 0.0f; float MinY = 0.0f; float MinZ = 0.0f; float MaxX = 0.0f; float MaxY = 0.0f; float MaxZ = 0.0f; };
struct FChrMesh { FChrInfo Info; std::vector<FChrVertex> Vertices; std::vector<uint16> Indices; FChrBounds Bounds; };
TResult<FChrInfo> LoadChrInfoFromBytes(const FByteArray& bytes, std::string_view sourceName);
TResult<FChrMesh> LoadChrMeshFromBytes(const FByteArray& bytes, std::string_view sourceName);
TResult<FChrInfo> LoadChrInfoFromResource(const FResourceManager& resources, std::string_view logicalName);
TResult<FChrMesh> LoadChrMeshFromResource(const FResourceManager& resources, std::string_view logicalName);
}
