#pragma once
#include "Core/Types.h"
#include <functional>
#include <unordered_map>

namespace Sfera {
using FMbcNative = std::function<FStatus(const std::vector<int64>&)>;
class FMbcNativeRegistry {
public:
    void Register(std::string name, FMbcNative function);
    const FMbcNative* Find(std::string_view name) const;
    std::vector<std::string> Names() const;
private:
    std::unordered_map<std::string, FMbcNative> Functions;
};
}
