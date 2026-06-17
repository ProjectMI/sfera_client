#include "Core/Application.h"
#include "FileSystem/PathUtils.h"
#include "MBC/MbcDisassembler.h"
#include "MBC/MbcEngineBridge.h"
#include <algorithm>
#include <memory>

namespace Sfera {
FApplication::FApplication() : ExecutableRoot(PathUtils::GetExecutableDirectory()), Logger(ExecutableRoot / "core_loader.log"), FileSystem(ExecutableRoot) {}

int FApplication::Run() {
    Logger.Info("SferaCoreLoader x64 bootstrap");
    Logger.Info("executable resource root: " + ExecutableRoot.string());
    FileSystem.BuildCatalog(&Logger);
    Config = std::make_unique<FConfigService>(FileSystem);
    Config->LoadKnownConfigs(&Logger);
    Resources = std::make_unique<FResourceManager>(FileSystem, Compression);
    Resources->BuildCatalog(&Logger);
    LoadWorldAndObjects();
    RegisterRecoveredNatives();
    ProbeMbcModules();
    BuildMbcRuntime();
    Network = std::make_unique<FConnectManager>(*Config);
    auto endpoint = Network->ReadEndpointFromConfig();
    if (endpoint) { Logger.Info("network endpoint recovered from cfg: " + endpoint->Host + ":" + std::to_string(endpoint->Port)); }
    else { Logger.Warning("network endpoint not found; connect manager initialized but not connected"); }
    Logger.Info("core-loader finished bootstrap");
    return 0;
}

void FApplication::LoadWorldAndObjects() {
    GameObjects = std::make_unique<FGameObjectService>(*Resources);
    GameObjects->Initialize(&Logger);
    WorldScene = std::make_unique<FWorldScene>(*Resources, GameObjects->Registry());
    WorldScene->LoadBootstrapScene(&Logger);
}

void FApplication::RegisterRecoveredNatives() {
    if (GameObjects) { GameObjects->RegisterMbcNatives(MbcNatives, &Logger); }
    FMbcEngineBridge::Register(MbcNatives, GameObjects.get(), WorldScene.get(), Resources.get(), &Logger);
    MbcNatives.RegisterNoOp("QueryShowWebShop", true);
    MbcNatives.RegisterNoOp("WaitForAsk", true);
    MbcNatives.RegisterNoOp("GetPictsPointer", true);
    MbcNatives.RegisterNoOp("PutMoney", false);
    MbcNatives.RegisterNoOp("GetMoney", true);
    MbcNatives.Register("Log", [this](FMbcNativeContext& ctx) { Logger.Info("MBC native Log reached runtime boundary: " + ctx.Name); return FStatus::Ok(); });
}

void FApplication::ProbeMbcModules() {
    auto mbcFiles = Resources->Catalog().FindByKind(EResourceKind::Mbc);
    Logger.Info("MBC candidates: " + std::to_string(mbcFiles.size()));
    size_t loaded = 0;
    size_t failed = 0;
    for (const auto& file : mbcFiles) {
        auto blob = Resources->Load(file.RelativePath.generic_string());
        if (!blob.IsOk()) { ++failed; Logger.Warning("failed to load MBC " + file.RelativePath.string() + ": " + blob.Status().Message()); continue; }
        FStatus status = MbcProject.AddModule(file.RelativePath.generic_string(), std::move(blob.Value().Bytes));
        if (!status.IsOk()) { ++failed; Logger.Warning("invalid MBC " + file.RelativePath.string() + ": " + status.Message()); continue; }
        ++loaded;
    }
    Logger.Info("MBC parsed modules: " + std::to_string(loaded) + ", failed=" + std::to_string(failed));
    FMbcDisassembler disassembler;
    size_t sampleCount = std::min<size_t>(8, MbcProject.Modules().size());
    for (size_t i = 0; i < sampleCount; ++i) {
        const FMbcModule& module = MbcProject.Modules()[i];
        auto instructions = disassembler.Disassemble(module, 16);
        Logger.Info("MBC loaded: " + module.Name() + ", programs=" + std::to_string(module.Programs().size()) + ", functions=" + std::to_string(module.Functions().size()) + ", strings=" + std::to_string(module.Strings().size()) + ", first_ops=" + std::to_string(instructions.size()) + ", header=" + module.Header().Commentary);
    }
}

void FApplication::BuildMbcRuntime() {
    MbcProject.BuildLinker();
    Logger.Info("MBC project linked: modules=" + std::to_string(MbcProject.Modules().size()) + ", programs=" + std::to_string(MbcProject.ProgramCount()) + ", functions=" + std::to_string(MbcProject.FunctionCount()) + ", imports=" + std::to_string(MbcProject.ImportCount()));
    Logger.Info("MBC runtime links: script_patches=" + std::to_string(MbcProject.Linker().Patches().size()) + ", engine_natives=" + std::to_string(MbcProject.Linker().NativeImports().size()) + ", unresolved_imports=" + std::to_string(MbcProject.Linker().UnresolvedImports().size()));
    size_t shown = 0;
    for (const auto& patch : MbcProject.Linker().Patches()) { Logger.Info("MBC link patch: " + patch.SourceModule + "." + patch.SourceName + " -> " + patch.TargetModule + "." + patch.TargetName + ", rel32=" + std::to_string(patch.Rel32)); if (++shown >= 6) { break; } }
    shown = 0;
    for (const auto& import : MbcProject.Linker().UnresolvedImports()) { Logger.Warning("MBC unresolved import: " + import.QualifiedName); if (++shown >= 8) { break; } }
    FMbcVirtualMachine vm(MbcNatives);
    FStatus status = vm.LoadProject(&MbcProject);
    if (!status.IsOk()) { Logger.Warning("MBC runtime not initialized: " + status.Message()); return; }
    Logger.Info("MBC runtime initialized: opcode_table=" + std::to_string(MbcOpcodeTable().size()) + ", builtin_table=" + std::to_string(MbcBuiltinTable().size()) + ", native_registry=" + std::to_string(MbcNatives.Names().size()));
}
}
