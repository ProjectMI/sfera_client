#pragma once
#include "Core/Types.h"

namespace Sfera::PathUtils {
FPath GetExecutablePath();
FPath GetExecutableDirectory();
std::string NormalizeForLookup(const FPath& path);
}
