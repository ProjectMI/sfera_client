#pragma once
#include "Compression/CompressionService.h"
#include "Config/ConfigService.h"
#include "Core/Logger.h"
#include "FileSystem/FileSystem.h"
#include "MBC/MbcNativeRegistry.h"
#include "MBC/MbcProject.h"
#include "MBC/MbcVirtualMachine.h"
#include "Model/ModelRepository.h"
#include "GameObjects/GameObjectService.h"
#include "Network/ConnectManager.h"
#include "WorldScene/WorldScene.h"
#include "ResourceLoader/ResourceManager.h"

class FApplication 
{
public:
    FApplication();
    ~FApplication();
    int Run();
private:
    void RegisterRecoveredNatives();
    void LoadWorldAndObjects();
    void ProbeMbcModules();
    void BuildMbcRuntime();
    FPath ExecutableRoot;
    FLogger Logger;
    FFileSystem FileSystem;
    FCompressionService Compression;
    std::unique_ptr<FConfigService> Config;
    std::unique_ptr<FResourceManager> Resources;
    std::unique_ptr<FGameObjectService> GameObjects;
    std::unique_ptr<FModelRepository> Models;
    std::unique_ptr<FWorldScene> WorldScene;
    std::unique_ptr<FConnectManager> Network;
    FMbcNativeRegistry MbcNatives;
    FMbcProject MbcProject;
};
