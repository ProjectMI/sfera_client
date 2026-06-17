#pragma once
#include "MBC/MbcDisassembler.h"
#include "MBC/MbcNativeRegistry.h"

namespace Sfera {
class FMbcVirtualMachine {
public:
    explicit FMbcVirtualMachine(FMbcNativeRegistry& registry);
    FStatus LoadModule(const FMbcModule* module);
    FStatus ValidateLoadedModule() const;
    FStatus RunSymbol(std::string_view symbolName);
private:
    FMbcNativeRegistry& Registry;
    const FMbcModule* LoadedModule = nullptr;
};
}
