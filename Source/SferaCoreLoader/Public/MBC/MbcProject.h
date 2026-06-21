#pragma once
#include "MBC/MbcLinker.h"
#include "MBC/MbcModule.h"

class FMbcProject 
{
public:
    FStatus AddModule(std::string name, FByteArray bytes);
    void BuildLinker();
    const std::vector<FMbcModule>& Modules() const { return ModulesLoaded; }
    std::vector<FMbcModule>& Modules() { return ModulesLoaded; }
    const FMbcProjectLinker& Linker() const { return ProjectLinker; }
    const FMbcModule* FindModule(std::string_view moduleName) const;
    size_t ProgramCount() const;
    size_t FunctionCount() const;
    size_t ImportCount() const;
private:
    std::vector<FMbcModule> ModulesLoaded;
    FMbcProjectLinker ProjectLinker;
};