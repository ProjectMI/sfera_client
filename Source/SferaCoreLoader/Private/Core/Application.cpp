#include "Core/Application.h"
#include "FileSystem/PathUtils.h"
#include "MBC/MbcDisassembler.h"
#include "MBC/MbcModule.h"
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
    RegisterRecoveredNatives();
    ProbeMbcModules();
    Network = std::make_unique<FConnectManager>(*Config);
    auto endpoint = Network->ReadEndpointFromConfig();
    if (endpoint) { Logger.Info("network endpoint recovered from cfg: " + endpoint->Host + ":" + std::to_string(endpoint->Port)); }
    else { Logger.Warning("network endpoint not found; connect manager initialized but not connected"); }
    Logger.Info("core-loader finished bootstrap");
    return 0;
}

void FApplication::RegisterRecoveredNatives() {
    MbcNatives.Register("send", [this](const std::vector<int64>&) { Logger.Warning("MBC native send reached placeholder boundary"); return FStatus::Ok(); });
    MbcNatives.Register("Connect", [this](const std::vector<int64>&) { Logger.Info("MBC native Connect reached placeholder boundary"); return FStatus::Ok(); });
    MbcNatives.Register("Log", [this](const std::vector<int64>&) { Logger.Info("MBC native Log reached placeholder boundary"); return FStatus::Ok(); });
}

void FApplication::ProbeMbcModules() {
    auto mbcFiles = Resources->Catalog().FindByKind(EResourceKind::Mbc);
    Logger.Info("MBC candidates: " + std::to_string(mbcFiles.size()));
    FMbcDisassembler disassembler;
    size_t loaded = 0;
    for (const auto& file : mbcFiles) {
        auto blob = Resources->Load(file.RelativePath.generic_string());
        if (!blob.IsOk()) { Logger.Warning("failed to load MBC " + file.RelativePath.string() + ": " + blob.Status().Message()); continue; }
        FMbcModule module;
        FStatus status = module.Load(file.RelativePath.generic_string(), std::move(blob.Value().Bytes));
        if (!status.IsOk()) { Logger.Warning("invalid MBC " + file.RelativePath.string()); continue; }
        auto instructions = disassembler.Disassemble(module, 16);
        Logger.Info("MBC loaded: " + module.Name() + ", strings=" + std::to_string(module.Strings().size()) + ", first_ops=" + std::to_string(instructions.size()) + ", header=" + module.Header().Commentary);
        ++loaded;
        if (loaded >= 8) { break; }
    }
}
}
