#pragma once
#include "WorldScene/WorldTypes.h"

struct FGameWorldSkyState
{
    float Time = 0.0f;
    int ClearRed = 0;
    int ClearGreen = 0;
    int ClearBlue = 0;
    int AmbientRed = 0;
    int AmbientGreen = 0;
    int AmbientBlue = 0;
    int SunRed = 0;
    int SunGreen = 0;
    int SunBlue = 0;
    int CloudRed = 0;
    int CloudGreen = 0;
    int CloudBlue = 0;
};

struct FGameWorldConfig
{
    bool Ok = false;
    std::vector<std::wstring> ModelDirs;
    std::vector<std::wstring> StaticObjectDirs;
    std::array<std::vector<std::wstring>, 31> GrassPatterns{};
    std::array<std::vector<std::wstring>, 31> GrassFlowerPatterns{};
    std::vector<std::wstring> GrassDetailModels;
    std::vector<FVector2> GrassSampleOffsets;
    std::wstring TerrainMicrotexture;
    std::wstring GrassmapDir = L"landscape/grassmap";
    int OriginRow = 40;
    int OriginColumn = 39;
    int VisibleRadius = 3;
    float TileSize = 100.0f;
    float StaticObjectRadius = 250.0f;
    int GrassmapGridSize = 16;
    int GrassmapTileResolution = 256;
    float GrassmapWorldOffsetX = 4000.0f;
    float GrassmapWorldOffsetZ = 4000.0f;
    float GrassmapWorldScale = 0.5120000243186951f;
    int GrassmapWorldSignX = 1;
    int GrassmapWorldSignZ = -1;
    float GrassHighlandMinY = 300.0f;
    float GrassHighlandMaxY = 800.0f;
    int GrassHighlandPatternOffset = 15;
    int GrassQuality = 1;
    float GrassRadius = 120.0f;
    float GrassSpacing = 8.3333f;
    int GrassDetailCount = 8;
    int GrassFlowerCountMax = 20;
    float GrassJitterFraction = 0.25f;
    float GrassScaleMin = 0.6f;
    float GrassScaleMax = 1.1f;
    float GrassFlatnessRadius = 2.45f;
    float GrassFlatnessThreshold = 0.5f;
    float GrassFlatnessNormalY = 0.75f;
    float GrassGenerationMargin = 16.0f;
    float GrassWindAmplitude = 1.8f;
    float GrassWindSpeed = 1.35f;
    float GrassColorGain = 1.8f;
    float GrassFadeStart = 42.0f;
    float GrassFadeEnd = 54.0f;
    float GrassGustRadiusScale = 0.12f;
    float GrassBreeze = 0.30f;
    std::wstring CameraMode;
    std::wstring SkyTexture;
    float SkyRadius = 220.0f;
    float SkyHeightScale = 0.55f;
    float SkyScrollSpeed = 0.002f;
    int SkyRed = 200;
    int SkyGreen = 200;
    int SkyBlue = 200;
    std::vector<FGameWorldSkyState> SkyStates;
    float CameraEyeHeight = 2.0f;
    float CameraLookDistance = 8.0f;
    float CameraTurnSpeed = 0.003f;
    float CameraPitchSpeed = 0.003f;
    float CameraMinPitch = -1.40f;
    float CameraMaxPitch = 1.40f;
    float CameraFov = 60.0f;
    float WalkSpeed = 5.1f;
    float RunMultiplier = 2.2f;
    float PlayerCollisionRadius = 0.32f;
    float PlayerCollisionHeight = 1.8f;
    float MaxStepHeight = 0.6f;
    float MovementCollisionStep = 0.2f;
    float CollisionFloorNormalThreshold = 0.2f;
    float SlopeSlideNormalY = 0.72f;
    float SlopeSlideFactor = 0.6f;
    float JumpImpulse = -5.0f;
    float JumpGravity = 9.8f;
    float WaterDayStart = 0.34f;
    float WaterDayEnd = 0.66f;
    float WaterNightBefore = 0.19f;
    float WaterNightAfter = 0.81f;
    float WaterTransitionWidth = 0.15f;
    float WaterReflectNight = 0.3f;
    float WaterReflectTransition = 0.5f;
    float WaveAmp = 1.0f;
    float WaveScale = 0.12f;
    float WaveFreqX = 3.93f;
    float WaveFreqZ = 2.02f;
    float WaveCellStep = 8.33f;
    float WaveSpeed = 1.5f;
    int WaterReflectionEnabled = 1;
    int PositionSendIntervalMs = 100;
    float NearClip = 0.2f;
    float FarClip = 260.0f;
    float FogStart = 70.0f;
    float FogEnd = 170.0f;
    int ClearRed = 105;
    int ClearGreen = 157;
    int ClearBlue = 205;
};

struct FGameMovementInput
{
    bool Forward = false;
    bool Backward = false;
    bool StrafeLeft = false;
    bool StrafeRight = false;
    bool Run = false;
};

struct FGameWorldPosition
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double Angle = 0.0;
};
