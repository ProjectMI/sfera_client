#pragma once
#include "Core/Types.h"
#include "MBC/MbcTypes.h"

class FMbcVirtualMachine;

struct FMbcNativeContext
{ 
    FMbcVirtualMachine* Vm = nullptr; 
    const FByteArray* Data = nullptr;
    std::string Name;
    std::vector<FMbcValue> Args;
    std::optional<FMbcValue> ReturnValue;
    std::string Commentary; 
};

using FMbcNative = std::function<FStatus(FMbcNativeContext&)>;

enum class EMbcNativeBoundaryReturn
{
    None,
    IntZero
};

class FMbcNativeRegistry 
{
public:
    void Register(std::string name, FMbcNative function);
    void RegisterRecoveredBoundary(std::string name, EMbcNativeBoundaryReturn returnValue = EMbcNativeBoundaryReturn::None);
    const FMbcNative* Find(std::string_view name) const;
    std::vector<std::string> Names() const;
private:
    std::unordered_map<std::string, FMbcNative> Functions;
};
