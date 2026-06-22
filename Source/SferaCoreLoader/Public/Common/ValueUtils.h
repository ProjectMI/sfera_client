#pragma once
#include "Core/Types.h"
#include <algorithm>

namespace Common
{
inline int32 ClampIndexToCount(int32 value, int32 count) { return count <= 0 ? 0 : std::clamp(value, 0, count - 1); }
}
