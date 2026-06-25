#pragma once
#include "Core/Types.h"
#include "Renderer/GameWorld/SkinnedCharacterModel.h"
#include "Renderer/CharacterSceneTypes.h"
#include "Model/SklSkeleton.h"
#include "Network/SphereEmuProtocol.h"
#include "ResourceLoader/ResourceManager.h"

struct IDirect3DDevice9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DTexture9;
class FLogger;

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
    bool Draw(IDirect3DDevice9* device, const FResourceManager& resources, const FCharacterCreationAppearance& appearance, float characterAngle, int32 cameraFocusId, const RECT& clientRect, float deltaSeconds, FLogger* logger);
    FSkinnedCharacterModel ExportSkinnedModel() const;
    void Shutdown();
    bool IsReady() const { return Initialized; }

private:
    void ReleaseBatches(std::vector<FSceneBatch>& Batches);
    void ReleaseBuffers();
    void ReleaseCharacterResources();
    bool LoadGroundMesh(const FResourceManager& resources, std::string& error);
    void LoadPlayerAnimationTable(const FResourceManager& resources, bool female);
    bool LoadCharacterMesh(const FResourceManager& resources, const FCharacterCreationAppearance& appearance, std::string& error);
    bool UploadGroundBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error);
    bool UploadCharacterBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error);
    bool UploadBuffers(IDirect3DDevice9* device, const FResourceManager& resources, FLogger* logger, std::string& error);
    bool UpdateCharacterAppearance(IDirect3DDevice9* device, const FResourceManager& resources, const FCharacterCreationAppearance& appearance, FLogger* logger);
    void UpdateCharacterAnimation(IDirect3DDevice9* device, float deltaSeconds);
    void ConfigureRenderState(IDirect3DDevice9* device);
    void SetCameraFocus(int32 focusId, bool snap);
    void UpdateCamera();
    void UpdateViewProjection(IDirect3DDevice9* device, const RECT& clientRect);
    void DrawGround(IDirect3DDevice9* device);
    void DrawCharacter(IDirect3DDevice9* device);
    IDirect3DTexture9* LoadDdsTexture(IDirect3DDevice9* device, const FResourceManager& resources, const std::string& logicalName, FLogger* logger, std::string& error);
    std::vector<FSceneMatrix4> BuildSkeletonMatrices(size_t frame) const;
    std::vector<FSceneMatrix4> BuildSkeletonMatrices(size_t frameA, size_t frameB, float alpha) const;
    void UpdateCharacterVerticesForFrame(size_t frame);
    void UpdateCharacterVerticesForFrame(size_t frameA, size_t frameB, float alpha);
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
    FSklSkeleton Skeleton;
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
    float CharacterAnimationTime = 0.0f;
    int32 CharacterAnimIdle = 20;
    int32 CharacterAnimWalk = 20;
    int32 CharacterAnimRun = 20;
    float CharacterAngle = Sfera::InitialCharacterSceneAngle;
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
