#pragma once
#include "Core/Types.h"

namespace Sfera {
class FNativeFile {
public:
    static TResult<FByteArray> ReadAllBytes(const FPath& path);
    static TResult<std::string> ReadAllText(const FPath& path);
    static FStatus WriteAllBytes(const FPath& path, const FByteArray& bytes);
    static bool Exists(const FPath& path);
};
}
