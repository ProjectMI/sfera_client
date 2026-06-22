#pragma once
#include "Core/Types.h"
#include <exception>
#include <string>
#include <string_view>

namespace StatusUtils
{
inline FStatus InvalidDataFromException(std::string_view prefix, const std::exception& e)
{
    return FStatus::Error(EStatusCode::InvalidData, std::string(prefix) + e.what());
}
}
