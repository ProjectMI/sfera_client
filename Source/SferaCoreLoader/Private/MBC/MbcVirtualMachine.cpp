#include "MBC/MbcVirtualMachine.h"

namespace Sfera {
FMbcVirtualMachine::FMbcVirtualMachine(FMbcNativeRegistry& registry) : Registry(registry) {}
FStatus FMbcVirtualMachine::LoadModule(const FMbcModule* module) { LoadedModule = module; return ValidateLoadedModule(); }
FStatus FMbcVirtualMachine::ValidateLoadedModule() const { return LoadedModule && !LoadedModule->Bytes().empty() ? FStatus::Ok() : FStatus::Error(EStatusCode::InvalidData, "no MBC module loaded"); }
FStatus FMbcVirtualMachine::RunSymbol(std::string_view symbolName) {
    FStatus status = ValidateLoadedModule();
    if (!status.IsOk()) { return status; }
    const FMbcNative* native = Registry.Find(symbolName);
    if (!native) { return FStatus::Error(EStatusCode::Unsupported, "MBC symbol is not mapped to a recovered native: " + std::string(symbolName)); }
    return (*native)({});
}
}
