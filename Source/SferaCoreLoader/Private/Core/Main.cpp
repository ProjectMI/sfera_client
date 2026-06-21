#include "Core/Application.h"
#include <Windows.h>
#include <array>
#include <bit>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
    std::filesystem::path GetExecutableDirectoryForCrashLog()
    {
        std::array<wchar_t, MAX_PATH> path{};
        DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

        if (len == 0 || len >= path.size()) { return std::filesystem::current_path(); }

        return std::filesystem::path(path.data()).parent_path();
    }

    void WriteCrashLog(const std::string& message)
    {
        try
        {
            auto path = GetExecutableDirectoryForCrashLog() / "core_loader_crash.log";
            std::ofstream out(path, std::ios::out | std::ios::app);
            out << message << std::endl;
        } catch (...) {}
    }

    LONG WINAPI SferaUnhandledExceptionFilter(EXCEPTION_POINTERS* info)
    {
        std::ostringstream out;
        out << "fatal SEH exception in SferaCoreLoader";

        if (info && info->ExceptionRecord)
        {
            out << ": code=0x" << std::hex << std::uppercase << info->ExceptionRecord->ExceptionCode;
            out << ", address=0x" << std::bit_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
        }

        std::string message = out.str();
        WriteCrashLog(message);
        MessageBoxA(nullptr, message.c_str(), "SferaCoreLoader fatal error", MB_OK | MB_ICONERROR);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    int RunSferaCoreLoaderGuarded()
    {
        SetUnhandledExceptionFilter(SferaUnhandledExceptionFilter);

        try
        {
            FApplication app;
            int result = app.Run();

            if (result != 0)
            {
                std::string message = "SferaCoreLoader terminated with an error. See core_loader.log near the executable.";
                WriteCrashLog(message);
                MessageBoxA(nullptr, message.c_str(), "SferaCoreLoader error", MB_OK | MB_ICONERROR);
            }

            return result;
        }
        catch (const std::exception& ex)
        {
            std::string message = std::string("fatal std::exception in SferaCoreLoader: ") + ex.what();
            WriteCrashLog(message);
            MessageBoxA(nullptr, message.c_str(), "SferaCoreLoader fatal error", MB_OK | MB_ICONERROR);
            return 1;
        }
        catch (...)
        {
            std::string message = "fatal unknown exception in SferaCoreLoader";
            WriteCrashLog(message);
            MessageBoxA(nullptr, message.c_str(), "SferaCoreLoader fatal error", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return RunSferaCoreLoaderGuarded();
}
