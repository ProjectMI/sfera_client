#pragma once
#include "MBC/MbcNativeRegistry.h"
#include "MBC/MbcOpcode.h"
#include "MBC/MbcProject.h"

namespace Sfera {
struct FMbcExecutionLimits { uint32 MaxInstructions = 100000; bool AllowNativeSideEffects = false; };
struct FMbcExecutionResult { FStatus Status = FStatus::Ok(); uint32 Instructions = 0; bool Halted = false; std::vector<FMbcRuntimeEvent> Events; };
struct FMbcRuntimeFrame { uint32 ModuleIndex = 0; uint32 ProgramIndex = 0; uint32 Pc = 0; std::vector<uint32> ReturnStack; std::vector<FMbcValue> Stack; FByteArray Data; bool Running = false; };
class FMbcVirtualMachine {
public:
    explicit FMbcVirtualMachine(FMbcNativeRegistry& registry);
    FStatus LoadProject(const FMbcProject* project);
    FStatus LoadModule(const FMbcModule* module);
    FStatus ValidateLoadedProject() const;
    FStatus ValidateLoadedModule() const;
    FStatus RunSymbol(std::string_view symbolName);
    FMbcExecutionResult RunModuleProgram(std::string_view moduleName, std::string_view programName, FMbcExecutionLimits limits = {});
    const std::vector<FMbcRuntimeEvent>& LastEvents() const { return Events; }
private:
    FMbcExecutionResult RunFrame(FMbcRuntimeFrame& frame, FMbcExecutionLimits limits);
    FStatus ExecuteDecoded(FMbcRuntimeFrame& frame, const FMbcDecodedOpcode& decoded);
    FStatus ExecuteBuiltin(FMbcRuntimeFrame& frame, const FMbcDecodedOpcode& decoded);
    void Push(FMbcRuntimeFrame& frame, FMbcValue value);
    FMbcValue Pop(FMbcRuntimeFrame& frame);
    bool Truthy(const FMbcValue& value) const;
    void AddEvent(std::string kind, std::string name, const FMbcRuntimeFrame& frame, std::string commentary = {});
    FMbcNativeRegistry& Registry;
    const FMbcProject* Project = nullptr;
    FMbcProject SingleModuleProject;
    const FMbcModule* LoadedModule = nullptr;
    std::vector<FMbcRuntimeEvent> Events;
};
}
