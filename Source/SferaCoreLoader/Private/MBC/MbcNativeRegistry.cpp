#include "MBC/MbcNativeRegistry.h"

void FMbcNativeRegistry::Register(std::string name, FMbcNative function)
{
    Functions[std::move(name)] = std::move(function);
}
void FMbcNativeRegistry::RegisterRecoveredBoundary(std::string name, EMbcNativeBoundaryReturn returnValue)
{
    Register(std::move(name), [returnValue](FMbcNativeContext& ctx)
    {
        ctx.Commentary = "native boundary reached without side effects";
        if (returnValue == EMbcNativeBoundaryReturn::IntZero) { ctx.ReturnValue = FMbcValue::Int(0); }
        return FStatus::Ok();
    });
}
const FMbcNative* FMbcNativeRegistry::Find(std::string_view name) const
{
    auto it = Functions.find(std::string(name));
    return it == Functions.end() ? nullptr : &it->second;
}
std::vector<std::string> FMbcNativeRegistry::Names() const
{
    std::vector<std::string> names;

    for (const auto& pair : Functions)
    {
        names.push_back(pair.first);
    }

    std::sort(names.begin(), names.end());
    return names;
}
