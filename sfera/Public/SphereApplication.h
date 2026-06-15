#pragma once

#include "SphereClientRuntimeC.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sfera::client
{
    inline constexpr std::size_t CriticalSectionCount = 3;
    inline constexpr std::size_t DecodedGuardTokenBytes = 4;
    inline constexpr std::size_t MaxGameXpSessionIdLength = 30;
    inline constexpr std::string_view LoginSwitch = "/login";
    inline constexpr std::string_view GameXpSidSwitch = "/gamexp_sid";
    inline constexpr std::string_view ConnectConfigFile = "connect.cfg";
    inline constexpr std::string_view ConnectTypeKey = "CONNECT_TYPE";
    inline constexpr unsigned int ErrorMessageBoxFlags = 0x10u;
    inline constexpr long double TimingScale = 1.0L;
    inline constexpr double TimingStep = 0.300000011920929;

    enum class LaunchMode
    {
        LocalWithoutLogin,
        GameXpLogin,
        InvalidLogin
    };

    struct MutableBuffer
    {
        char* data = nullptr;
        std::size_t size = 0;
        bool empty() const noexcept { return !data || size == 0; }
    };

    struct StartupInfo
    {
        void* instance = nullptr;
        void* previousInstance = nullptr;
        std::string_view commandLine;
        int showCommand = 0;
    };

    struct ClientMemory
    {
        void** primaryInstance = nullptr;
        void** idaCompatibilityInstance = nullptr;
        int* showCommand = nullptr;
        MutableBuffer commandLine;
        MutableBuffer gameXpSession;
        char* connectTypeFlag = nullptr;
        char* noLoginFlag = nullptr;
        char* const* requiredDirectories = nullptr;
        std::size_t requiredDirectoryCount = 0;
        std::array<void*, CriticalSectionCount> criticalSections{};
        MutableBuffer guardToken;
        std::uintptr_t guardTokenXorKey = 0;
        std::string_view guardMessageText;
        std::string_view guardMessageTitle;
        std::string_view missingLoginSidText;
        std::string_view missingLoginSidTitle;
    };

    struct ClientCallbacks
    {
        SferaClientFatalMessageHandler fatalMessageHandler = nullptr;
        SferaClientInstallFatalMessageHandler installFatalMessageHandler = nullptr;
        void* cleanupCallback = nullptr;
        SferaClientInstallRawCallback installCleanupCallback = nullptr;
        SferaClientOpenConfigFile openConfigFile = nullptr;
        SferaClientReadIntegerConfigValue readIntegerConfigValue = nullptr;
        SferaClientInitializeSubsystems initializeSubsystems = nullptr;
        SferaClientInitializeTiming initializeTiming = nullptr;
        SferaClientProcedure loadConfig = nullptr;
        SferaClientProcedure loadDebugConfig = nullptr;
        SferaClientProcedure enterMainLoop = nullptr;
    };

    struct RuntimeConfig
    {
        ClientMemory memory;
        ClientCallbacks callbacks;
    };

    struct ParsedCommandLine
    {
        LaunchMode mode = LaunchMode::LocalWithoutLogin;
        std::string_view gameXpSessionId;
    };

    ParsedCommandLine parseCommandLine(std::string_view commandLine) noexcept;
    RuntimeConfig makeRuntimeConfig(const SferaClientRuntimeConfig& config) noexcept;
    StartupInfo makeStartupInfo(const SferaClientStartupInfo& startup) noexcept;

    class SphereApplication final
    {
    public:
        explicit SphereApplication(RuntimeConfig config) noexcept;
        int run(const StartupInfo& startup);

    private:
        void installCallbacks() const;
        bool rejectGuardLaunch(std::string_view commandLine) const;
        bool applyCommandLine(std::string_view commandLine) const;
        void loadConnectionMode() const;
        void initializeSubsystems() const;
        void initializeRequiredDirectories() const;
        void initializeTiming() const;
        void publishStartupInfo(const StartupInfo& startup) const;
        void loadConfiguration() const;
        void enterMainLoop() const;

        RuntimeConfig config_;
    };
}
