#pragma once

#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sfera::platform::win32
{
    struct MessageBoxRequest
    {
        void* owner = nullptr;
        const char* text = "";
        const char* title = "Sphere";
        unsigned int flags = 0;
    };

    class UniqueHandle final
    {
    public:
        UniqueHandle() noexcept = default;
        explicit UniqueHandle(HANDLE handle) noexcept;
        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;
        UniqueHandle(UniqueHandle&& other) noexcept;
        UniqueHandle& operator=(UniqueHandle&& other) noexcept;
        ~UniqueHandle();
        bool isValid() const noexcept;
        HANDLE get() const noexcept;
        HANDLE release() noexcept;
        void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept;

    private:
        HANDLE handle_ = INVALID_HANDLE_VALUE;
    };

    class SystemServices final
    {
    public:
        void pinCurrentThreadToProcessor(DWORD_PTR affinityMask) const;
        void ensureDirectoryExists(std::string_view path) const;
        int showMessage(const MessageBoxRequest& request) const;
        void initializeCriticalSection(void* criticalSection) const;
        void deleteCriticalSection(void* criticalSection) const;
    };
}
