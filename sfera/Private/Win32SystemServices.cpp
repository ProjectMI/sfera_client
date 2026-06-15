#include "../Public/Win32SystemServices.h"

#include <string>
#include <utility>

namespace sfera::platform::win32
{
    UniqueHandle::UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}

    UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    UniqueHandle::~UniqueHandle()
    {
        reset();
    }

    bool UniqueHandle::isValid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE UniqueHandle::get() const noexcept
    {
        return handle_;
    }

    HANDLE UniqueHandle::release() noexcept
    {
        return std::exchange(handle_, INVALID_HANDLE_VALUE);
    }

    void UniqueHandle::reset(HANDLE handle) noexcept
    {
        if (isValid())
            CloseHandle(handle_);
        handle_ = handle;
    }

    void SystemServices::pinCurrentThreadToProcessor(DWORD_PTR affinityMask) const
    {
        SetThreadAffinityMask(GetCurrentThread(), affinityMask);
    }

    void SystemServices::ensureDirectoryExists(std::string_view path) const
    {
        if (path.empty())
            return;
        const std::string pathString(path);
        UniqueHandle existing(CreateFileA(pathString.c_str(), FILE_READ_DATA, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!existing.isValid())
            CreateDirectoryA(pathString.c_str(), nullptr);
    }

    int SystemServices::showMessage(const MessageBoxRequest& request) const
    {
        return MessageBoxA(static_cast<HWND>(request.owner), request.text ? request.text : "", request.title ? request.title : "Sphere", request.flags);
    }

    void SystemServices::initializeCriticalSection(void* criticalSection) const
    {
        if (criticalSection)
            InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(criticalSection));
    }

    void SystemServices::deleteCriticalSection(void* criticalSection) const
    {
        if (criticalSection)
            DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(criticalSection));
    }
}
