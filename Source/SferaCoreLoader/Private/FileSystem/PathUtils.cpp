#include "FileSystem/PathUtils.h"
#include "Common/StringUtils.h"

namespace PathUtils
{
    FPath GetExecutablePath()
    {
        std::array<wchar_t, 32768> buffer{};
        DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (size == 0 || size >= buffer.size()) { return std::filesystem::current_path(); }

        return FPath(std::wstring(buffer.data(), buffer.data() + size));
    }

    FPath GetExecutableDirectory()
    {
        FPath exePath = GetExecutablePath();
        return exePath.has_parent_path() ? exePath.parent_path() : std::filesystem::current_path();
    }

    std::string NormalizeForLookup(const FPath& path) { return Common::ToLowerPath(path); }
}
