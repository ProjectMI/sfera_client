#pragma once
#include "Components/Localization/MessageCatalogComponent.h"
#include "Components/Network/FileTransferStateComponent.h"
#include "Components/World/WorldRuntimeComponent.h"
#include "Compression/CompressionService.h"
#include "Config/ConfigService.h"
#include "Core/Logger.h"
#include "FileSystem/FileSystem.h"
#include "GameObjects/GameObjectService.h"
#include "Model/ModelRepository.h"
#include "Network/ConnectManager.h"
#include "ResourceLoader/ResourceManager.h"
#include "WorldScene/WorldScene.h"

class FApplication
{
public:
    FApplication();
    ~FApplication();
    int Run();
private:
    void LoadWorldAndObjects();
    FPath ExecutableRoot;
    FLogger Logger;
    FFileSystem FileSystem;
    FCompressionService Compression;
    std::unique_ptr<FConfigService> Config;
    std::unique_ptr<FResourceManager> Resources;
    std::unique_ptr<FGameObjectService> GameObjects;
    std::unique_ptr<FModelRepository> Models;
    std::unique_ptr<FWorldScene> WorldScene;
    std::unique_ptr<FWorldRuntimeComponent> WorldRuntime;
    std::unique_ptr<FMessageCatalogComponent> MessageCatalog;
    std::unique_ptr<FFileTransferStateComponent> FileTransfers;
    std::unique_ptr<FConnectManager> Network;
};
