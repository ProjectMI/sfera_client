#pragma once
#include "Core/Types.h"
#include "Network/SphereEmuProtocol.h"
#include "ResourceLoader/ResourceManager.h"
#include <Windows.h>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct IDirect3DDevice9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DTexture9;
class FLogger;

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

struct FVec3 
{ 
    float X = 0.0f;
    float Y = 0.0f; 
    float Z = 0.0f;
};

struct FQuat 
{ 
    float W = 1.0f;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

struct FMatrix4 
{ 
    float M[16]{}; 
};

struct FXaddSubobject 
{ 
    std::string Code;
    std::string MeshName;
    std::vector<std::string> TextureNames;
};

class FD3D9CharacterScene 
{
public:
    FD3D9CharacterScene();
    ~FD3D9CharacterScene();
    FD3D9CharacterScene(const FD3D9CharacterScene&) = delete;
    FD3D9CharacterScene& operator=(const FD3D9CharacterScene&) = delete;
    bool EnsureInitialized(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger);
    bool Draw(IDirect3DDevice9* device, const FResourceManager& resources, const FCharacterCreationAppearance& appearance, float characterAngle, int32 cameraFocusId, const RECT& clientRect, FLogger* logger);
    void Shutdown();
    bool IsReady() const { return Initialized; }

private:
    void ReleaseBatches(std::vector<FSceneBatch>& batches);
    void ReleaseBuffers();
    void ReleaseCharacterResources();
    bool LoadGroundMesh(const FResourceManager& resources, std::string& error);
    bool LoadCharacterMesh(const FResourceManager& resources, const FCharacterCreationAppearance& appearance, std::string& error);
    bool UploadGroundBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error);
    bool UploadCharacterBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error);
    bool UploadBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error);
    bool UpdateCharacterAppearance(IDirect3DDevice9* device, const FResourceManager& resources, const FCharacterCreationAppearance& appearance, FLogger* logger);
    void UpdateCharacterAnimation(IDirect3DDevice9* device);
    void ConfigureRenderState(IDirect3DDevice9* device);
    void SetCameraFocus(int32 focusId, bool snap);
    void UpdateCamera();
    void UpdateViewProjection(IDirect3DDevice9* device, const RECT& clientRect);
    void DrawGround(IDirect3DDevice9* device);
    void DrawCharacter(IDirect3DDevice9* device);
    IDirect3DTexture9* LoadDdsTexture(IDirect3DDevice9* device, const FResourceManager& resources, const std::string& logicalName, FLogger* logger, std::string& error);
    std::vector<FMatrix4> BuildSkeletonMatrices(size_t frame) const;
    void UpdateCharacterVerticesForFrame(size_t frame);
    bool Initialized = false;
    bool GroundUploaded = false;
    bool CharacterUploaded = false;
    FCharacterCreationAppearance CurrentAppearance{};
    std::vector<FSceneVertex> CharacterVertices;
    std::vector<uint16> CharacterIndices;
    std::vector<FSceneBatch> CharacterBatches;
    std::vector<FSkinnedVertexSource> CharacterSources;
    std::vector<FSceneVertex> GroundVertices;
    std::vector<uint16> GroundIndices;
    std::vector<FSceneBatch> GroundBatches;
    std::vector<int32> SkeletonParents;
    std::vector<std::string> SkeletonBoneNames;
    std::vector<float> SkeletonTransforms;
    std::vector<int32> SkeletonAnimationFrameCounts;
    int32 SkeletonBoneCount = 0;
    int32 SkeletonFrameCount = 0;
    size_t CharacterRootBone = 0;
    float CharacterRootBindX = 0.0f;
    float CharacterRootBindY = 0.0f;
    float CharacterRootBindZ = 0.0f;
    float CharacterCenterX = 0.0f;
    float CharacterMinY = 0.0f;
    float CharacterCenterZ = 0.0f;
    float CharacterScale = 1.0f;
    size_t CharacterAnimationStart = 0;
    size_t CharacterAnimationFrames = 1;
    unsigned long CharacterAnimationTick = 0;
    float CharacterAngle = 3.1415926535f;
    int32 CameraFocusId = -1;
    float CameraFocusX = 0.0f;
    float CameraFocusY = 1.34f;
    float CameraFocusZ = 0.0f;
    float CameraYaw = 0.0f;
    float CameraDistance = 4.45f;
    float CameraPitch = 0.02f;
    float CameraFovDegrees = 50.0f;
    float CameraFocusXTarget = 0.0f;
    float CameraFocusYTarget = 1.34f;
    float CameraFocusZTarget = 0.0f;
    float CameraYawTarget = 0.0f;
    float CameraDistanceTarget = 4.45f;
    float CameraPitchTarget = 0.02f;
    float CameraFovDegreesTarget = 50.0f;
    IDirect3DVertexBuffer9* CharacterVertexBuffer = nullptr;
    IDirect3DIndexBuffer9* CharacterIndexBuffer = nullptr;
    IDirect3DVertexBuffer9* GroundVertexBuffer = nullptr;
    IDirect3DIndexBuffer9* GroundIndexBuffer = nullptr;
};