#include "FileSystem/PathUtils.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <iterator>

namespace Sfera::PathUtils {
FPath GetExecutablePath() {
    wchar_t buffer[32768] = {};
    DWORD size = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (size == 0 || size >= std::size(buffer)) { return std::filesystem::current_path(); }
    return FPath(std::wstring(buffer, buffer + size));
}

FPath GetExecutableDirectory() {
    FPath exePath = GetExecutablePath();
    return exePath.has_parent_path() ? exePath.parent_path() : std::filesystem::current_path();
}

std::string NormalizeForLookup(const FPath& path) {
    std::string value = path.generic_string();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}
}
