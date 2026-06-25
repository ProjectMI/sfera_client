#pragma once

#include "Core/Types.h"
#include "WorldScene/WorldTypes.h"

struct IDirect3DTexture9;

struct FSceneVertex
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float NX = 0.0f;
    float NY = 1.0f;
    float NZ = 0.0f;
    unsigned long Diffuse = 0xfffffffful;
    float U = 0.0f;
    float V = 0.0f;
};

struct FSceneBatch
{
    uint32 StartIndex = 0;
    uint32 IndexCount = 0;
    std::string TextureLogicalName;
    IDirect3DTexture9* Texture = nullptr;
    bool Sky = false;
    bool Head = false;
};

struct FSkinnedVertexSource
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float NX = 0.0f;
    float NY = 1.0f;
    float NZ = 0.0f;
    float U = 0.0f;
    float V = 0.0f;
    uint8 Bone0 = 0;
    uint8 Bone1 = 0;
    float Blend = 1.0f;
};

using FSceneMatrix4 = std::array<float, 16>;
