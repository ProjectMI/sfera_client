#pragma once
#include "Core/Types.h"

namespace PathUtils 
{
FPath GetExecutablePath();
FPath GetExecutableDirectory();
std::string NormalizeForLookup(const FPath& path);
}
