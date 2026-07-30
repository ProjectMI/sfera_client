#include "Core/Application.h"
#include "Client/ClientFrontendRuntime.h"
#include "Client/ClientSettings.h"
#include "FileSystem/PathUtils.h"
#include "Renderer/D3D9GameWorldScene.h"

FApplication::FApplication() : ExecutableRoot(PathUtils::GetExecutableDirectory()), Logger(ExecutableRoot / "core_loader.log"), FileSystem(ExecutableRoot) {}
FApplication::~FApplication() = default;

int FApplication::Run()
{
    Logger.Info("SferaCoreLoader x64 bootstrap");
    Logger.Info("executable resource root: " + ExecutableRoot.string());
    FClientFrontendRuntime frontend(&Logger);
    FClientSettings clientSettings;
    FStatus frontendStatus = frontend.CreateShell(clientSettings);
    if (!frontendStatus.IsOk()) { Logger.Error("frontend shell creation failed: " + frontendStatus.Message()); return 1; }

    frontend.ShowShell();
    auto stage = [&frontend](const std::string& text, float progress)
    {
        frontend.SetStage(text, progress);
        frontend.PumpUi();
    };

    try
    {
        stage("cataloging files", 0.04f);
        FileSystem.BuildCatalog(&Logger);
        stage("loading configs", 0.10f);
        Config = std::make_unique<FConfigService>(FileSystem);
        Config->LoadKnownConfigs(&Logger);
        clientSettings = LoadClientSettings(*Config);
        Logger.Info("client settings: forced fullscreen; config XRES/YRES are kept only as UI/runtime hints");
        stage("building resource catalog", 0.18f);
        Resources = std::make_unique<FResourceManager>(FileSystem, Compression);
        Resources->BuildCatalog(&Logger);
        stage("initializing audio runtime", 0.28f);
        frontendStatus = frontend.InitializeAudio(*Resources, clientSettings.SoundVolume, clientSettings.MusicVolume);
        if (!frontendStatus.IsOk())
        {
            Logger.Warning("audio runtime initialization failed: " + frontendStatus.Message());
            frontend.AddStatusLine("audio unavailable: " + frontendStatus.Message());
        }
        stage("loading game objects and model catalog", 0.34f);
        LoadWorldAndObjects();
        stage("initializing native components", 0.62f);
        WorldRuntime = std::make_unique<FWorldRuntimeComponent>(*GameObjects, *WorldScene, *Resources, &Logger);
        MessageCatalog = std::make_unique<FMessageCatalogComponent>(*Resources, &Logger);
        FileTransfers = std::make_unique<FFileTransferStateComponent>();
        Logger.Info("native components initialized: UI state, network workflows, file transfer state, localization, world runtime services");
        stage("preparing network endpoint", 0.76f);
        Network = std::make_unique<FConnectManager>(*Config);
        auto endpoint = Network->ReadEndpointFromConfig();
        if (endpoint) { Logger.Info("network endpoint recovered from cfg: " + endpoint->Host + ":" + std::to_string(endpoint->Port)); }
        else { Logger.Warning("network endpoint not found; login button will report configuration error"); }

        if (!clientSettings.Endpoint && endpoint)
        {
            clientSettings.Endpoint = endpoint;
            clientSettings.Title = "Sphere - " + endpoint->Host + ":" + std::to_string(endpoint->Port);
        }

        stage("loading UI resources", 0.86f);
        FUiBootstrapDesc uiDesc = MakeUiBootstrapDesc(clientSettings);
        uiDesc.ChatListFont = static_cast<int32>(std::max<int64>(2, Config->FindInt("CHAT_LIST_FONT").value_or(4)));
        uiDesc.ChatEditFont = static_cast<int32>(std::max<int64>(2, Config->FindInt("CHAT_EDIT_FONT").value_or(4)));
        frontendStatus = frontend.InitializeUiResources(*Resources, uiDesc);
        if (!frontendStatus.IsOk()) { Logger.Error("UI runtime initialization failed: " + frontendStatus.Message()); return 1; }

        stage("initializing renderer", 0.94f);
        frontendStatus = frontend.InitializeD3D9(*Resources, WorldScene.get());
        if (!frontendStatus.IsOk()) { Logger.Error("D3D9 initialization failed: " + frontendStatus.Message()); return 1; }

        FClientFrontendDesc frontendDesc;
        frontendDesc.Settings = clientSettings;
        frontendDesc.Endpoint = clientSettings.Endpoint ? clientSettings.Endpoint : endpoint;
        frontendDesc.TryNetworkProbe = false;
        frontend.ConfigureNetwork(frontendDesc);
        frontend.SetStage("login screen ready", 1.0f);
        frontendStatus = frontend.RunEventLoop();
        if (!frontendStatus.IsOk()) { Logger.Error("frontend event loop failed: " + frontendStatus.Message()); }
        Logger.Info("core-loader finished frontend loop");
        return frontendStatus.IsOk() ? 0 : 1;
    }
    catch (const std::exception& ex)
    {
        Logger.Error(std::string("application bootstrap failed: ") + ex.what());
        frontend.AddStatusLine(std::string("fatal: ") + ex.what());
        return 1;
    }
    catch (...)
    {
        Logger.Error("application bootstrap failed: unknown exception");
        frontend.AddStatusLine("fatal: unknown exception");
        return 1;
    }
}

void FApplication::LoadWorldAndObjects()
{
    GameObjects = std::make_unique<FGameObjectService>(*Resources);
    GameObjects->Initialize(&Logger);
    Models = std::make_unique<FModelRepository>(*Resources);
    Models->BuildCatalog(&Logger);
    const auto modelStats = Models->Stats();
    Logger.Info("model runtime bootstrap: total=" + std::to_string(modelStats.TotalCount) + ", mdl=" + std::to_string(modelStats.MdlCount) + ", chr=" + std::to_string(modelStats.ChrCount) + ", skl=" + std::to_string(modelStats.SklCount));
    WorldScene = std::make_unique<FWorldScene>(*Resources, GameObjects->Registry());
    WorldScene->LoadBootstrapScene(&Logger);
    FD3D9GameWorldScene::PrewarmGrassModelCpuCache(*Resources, FD3D9GameWorldScene::DefaultConfig(), &Logger);
}
