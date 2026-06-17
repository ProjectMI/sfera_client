#include "MBC/MbcNativeRegistry.h"
#include <algorithm>

namespace Sfera {
void FMbcNativeRegistry::Register(std::string name, FMbcNative function) { Functions[std::move(name)] = std::move(function); }
const FMbcNative* FMbcNativeRegistry::Find(std::string_view name) const { auto it = Functions.find(std::string(name)); return it == Functions.end() ? nullptr : &it->second; }
std::vector<std::string> FMbcNativeRegistry::Names() const { std::vector<std::string> names; for (const auto& pair : Functions) { names.push_back(pair.first); } std::sort(names.begin(), names.end()); return names; }
}
