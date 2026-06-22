#pragma once
#include "Core/Types.h"
#include <Windows.h>
#include <bit>
#include <string>
#include <string_view>

namespace Common
{
inline std::wstring MultiByteToWide(UINT codePage, std::string_view text, DWORD flags = 0)
{
    if (text.empty()) { return {}; }
    const int required = MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) { return {}; }
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

inline std::string WideToMultiByte(UINT codePage, std::wstring_view text, DWORD flags = 0, const char* fallback = nullptr)
{
    if (text.empty()) { return {}; }
    const int required = WideCharToMultiByte(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0, fallback, nullptr);
    if (required <= 0) { return {}; }
    std::string bytes(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(codePage, flags, text.data(), static_cast<int>(text.size()), bytes.data(), required, fallback, nullptr);
    return bytes;
}

inline std::wstring Utf8ToWide(std::string_view text) { return MultiByteToWide(CP_UTF8, text, MB_ERR_INVALID_CHARS); }
inline std::string WideToUtf8(std::wstring_view text) { return WideToMultiByte(CP_UTF8, text); }

inline std::wstring Cp1251BytesToWide(const FByteArray& bytes)
{
    if (bytes.empty()) { return {}; }
    return MultiByteToWide(1251, std::string_view(std::bit_cast<const char*>(bytes.data()), bytes.size()));
}


inline FByteArray WideToCp1251Bytes(std::wstring_view text)
{
    const std::string bytes = WideToMultiByte(1251, text, 0, "?");
    return FByteArray(bytes.begin(), bytes.end());
}
}
