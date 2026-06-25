#pragma once
#include "Core/Types.h"

namespace StatusUtils
{
inline FStatus InvalidDataFromException(std::string_view prefix, const std::exception& e)
{
    return FStatus::Error(EStatusCode::InvalidData, std::string(prefix) + e.what());
}
}
