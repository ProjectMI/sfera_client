#include "Core/Application.h"
#include "Client/ClientFrontendRuntime.h"
#include "Client/ClientSettings.h"
#include "FileSystem/PathUtils.h"
#include "MBC/MbcDisassembler.h"
#include "MBC/MbcEngineBridge.h"
#include "Model/ModelRepository.h"
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
        Logger.Info("cataloging files");
        FileSystem.BuildCatalog(&Logger);
        stage("loading configs", 0.10f);
        Logger.Info("loading configs");
        Config = std::make_unique<FConfigService>(FileSystem);
        Config->LoadKnownConfigs(&Logger);
        clientSettings = LoadClientSettings(*Config);
        Logger.Info("client settings: forced fullscreen; config XRES/YRES are kept only as UI/runtime hints");
        stage("building resource catalog", 0.18f);
        Resources = std::make_unique<FResourceManager>(FileSystem, Compression);
        Resources->BuildCatalog(&Logger);
        stage("loading game objects and model catalog", 0.30f);
        Logger.Info("loading game objects and world scene");
        LoadWorldAndObjects();
        stage("registering script natives", 0.50f);
        Logger.Info("registering script natives");
        RegisterRecoveredNatives();
        stage("loading script modules", 0.60f);
        Logger.Info("loading script modules");
        ProbeMbcModules();
        stage("linking script runtime", 0.74f);
        Logger.Info("linking script runtime");
        BuildMbcRuntime();
        stage("preparing network endpoint", 0.82f);
        Network = std::make_unique<FConnectManager>(*Config);
        auto endpoint = Network->ReadEndpointFromConfig();

        if (endpoint)
        {
            Logger.Info("network endpoint recovered from cfg: " + endpoint->Host + ":" + std::to_string(endpoint->Port));
        }
        else
        {
            Logger.Warning("network endpoint not found; login button will report configuration error");
        }

        if (!clientSettings.Endpoint && endpoint)
        {
            clientSettings.Endpoint = endpoint;
            clientSettings.Title = "Sphere - " + endpoint->Host + ":" + std::to_string(endpoint->Port);
        }

        stage("loading UI resources", 0.88f);
        FUiBootstrapDesc uiDesc = MakeUiBootstrapDesc(clientSettings);
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

        if (!frontendStatus.IsOk())
        {
            Logger.Error("frontend event loop failed: " + frontendStatus.Message());
        }

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

void FApplication::RegisterRecoveredNatives()
{
    if (GameObjects)
    {
        GameObjects->RegisterMbcNatives(MbcNatives, &Logger);
    }

    FMbcEngineBridge::Register(MbcNatives, GameObjects.get(), WorldScene.get(), Resources.get(), &Logger);
    MbcNatives.RegisterRecoveredBoundary("QueryShowWebShop", EMbcNativeBoundaryReturn::IntZero);
    MbcNatives.RegisterRecoveredBoundary("WaitForAsk", EMbcNativeBoundaryReturn::IntZero);
    MbcNatives.RegisterRecoveredBoundary("GetPictsPointer", EMbcNativeBoundaryReturn::IntZero);
    MbcNatives.RegisterRecoveredBoundary("PutMoney");
    MbcNatives.RegisterRecoveredBoundary("GetMoney", EMbcNativeBoundaryReturn::IntZero);
    MbcNatives.Register("Log", [this](FMbcNativeContext& ctx)
    {
        Logger.Info("MBC native Log reached runtime boundary: " + ctx.Name);
        return FStatus::Ok();
    });
}

void FApplication::ProbeMbcModules()
{
    auto mbcFiles = Resources->Catalog().FindByKind(EResourceKind::Mbc);
    Logger.Info("MBC candidates: " + std::to_string(mbcFiles.size()));

    struct FMbcLoadResult
    {
        std::string Name;
        FMbcModule Module;
        FStatus Status;
    };

    std::vector<std::optional<FMbcLoadResult>> results(mbcFiles.size());
    const size_t hardware = static_cast<size_t>(std::max(1u, std::thread::hardware_concurrency()));
    const size_t threadCount = mbcFiles.empty() ? 0 : std::min(std::clamp(hardware - 1, size_t{1}, size_t{6}), mbcFiles.size());
    std::atomic_size_t next{0};
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        workers.emplace_back([this, &mbcFiles, &results, &next]()
        {
            for (;;)
            {
                const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= mbcFiles.size())
                {
                    break;
                }
                const auto& file = mbcFiles[index];
                FMbcLoadResult result;
                result.Name = file.RelativePath.generic_string();
                auto blob = Resources->Load(result.Name);
                if (!blob.IsOk())
                {
                    result.Status = blob.Status();
                    results[index] = std::move(result);
                    continue;
                }
                result.Status = result.Module.Load(result.Name, std::move(blob.Value().Bytes));
                results[index] = std::move(result);
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    size_t loaded = 0;
    size_t failed = 0;
    for (auto& result : results)
    {
        if (!result)
        {
            continue;
        }
        if (!result->Status.IsOk())
        {
            ++failed;
            Logger.Warning("invalid MBC " + result->Name + ": " + result->Status.Message());
            continue;
        }
        MbcProject.Modules().push_back(std::move(result->Module));
        ++loaded;
    }

    Logger.Info("MBC parsed modules: " + std::to_string(loaded) + ", failed=" + std::to_string(failed) + ", threads=" + std::to_string(threadCount));
    FMbcDisassembler disassembler;
    size_t sampleCount = std::min<size_t>(8, MbcProject.Modules().size());

    for (size_t i = 0; i < sampleCount; ++i)
    {
        const FMbcModule& module = MbcProject.Modules()[i];
        auto instructions = disassembler.Disassemble(module, 16);
        Logger.Info("MBC loaded: " + module.Name() + ", programs=" + std::to_string(module.Programs().size()) + ", functions=" + std::to_string(module.Functions().size()) + ", strings=" + std::to_string(module.Strings().size()) + ", first_ops=" + std::to_string(instructions.size()) + ", header=" + module.Header().Commentary);
    }
}

void FApplication::BuildMbcRuntime()
{
    MbcProject.BuildLinker();
    Logger.Info("MBC project linked: modules=" + std::to_string(MbcProject.Modules().size()) + ", programs=" + std::to_string(MbcProject.ProgramCount()) + ", functions=" + std::to_string(MbcProject.FunctionCount()) + ", imports=" + std::to_string(MbcProject.ImportCount()));
    Logger.Info("MBC runtime links: script_patches=" + std::to_string(MbcProject.Linker().Patches().size()) + ", engine_natives=" + std::to_string(MbcProject.Linker().NativeImports().size()) + ", unresolved_imports=" + std::to_string(MbcProject.Linker().UnresolvedImports().size()));
    size_t shown = 0;

    for (const auto& patch : MbcProject.Linker().Patches())
    {
        Logger.Info("MBC link patch: " + patch.SourceModule + "." + patch.SourceName + " -> " + patch.TargetModule + "." + patch.TargetName + ", rel32=" + std::to_string(patch.Rel32));

        if (++shown >= 6)
        {
            break;
        }
    }

    shown = 0;

    for (const auto& import : MbcProject.Linker().UnresolvedImports())
    {
        Logger.Warning("MBC unresolved import: " + import.QualifiedName);

        if (++shown >= 8)
        {
            break;
        }
    }

    FMbcVirtualMachine vm(MbcNatives);
    FStatus status = vm.LoadProject(&MbcProject);

    if (!status.IsOk()) { Logger.Warning("MBC runtime not initialized: " + status.Message()); return; }

    Logger.Info("MBC runtime initialized: opcode_table=" + std::to_string(MbcOpcodeTable().size()) + ", builtin_table=" + std::to_string(MbcBuiltinTable().size()) + ", native_registry=" + std::to_string(MbcNatives.Names().size()));
}
