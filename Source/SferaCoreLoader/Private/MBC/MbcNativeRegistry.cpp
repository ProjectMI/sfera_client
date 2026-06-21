#include "MBC/MbcNativeRegistry.h"
#include <algorithm>

void FMbcNativeRegistry::Register(std::string name, FMbcNative function)
{
    Functions[std::move(name)] = std::move(function);
}
void FMbcNativeRegistry::RegisterNoOp(std::string name, bool pushesValue)
{
    Register(std::move(name), [pushesValue](FMbcNativeContext& ctx)
    {
        ctx.Commentary = "native boundary stub";

        if (pushesValue)
        {
            ctx.ReturnValue = FMbcValue::Int(0);
        }

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
