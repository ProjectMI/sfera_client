#include "../Public/SphereApplication.h"
#include "../Public/Win32SystemServices.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace sfera::client
{
    namespace
    {
        constexpr DWORD_PTR PrimaryProcessorAffinity = 1;

        std::string_view cStringView(const char* text) noexcept
        {
            return text ? std::string_view(text) : std::string_view();
        }

        const char* cStringData(std::string_view text) noexcept
        {
            return text.data() ? text.data() : "";
        }

        std::string_view nullTerminatedBufferView(MutableBuffer buffer) noexcept
        {
            if (buffer.empty())
                return {};
            const void* terminator = std::memchr(buffer.data, '\0', buffer.size);
            const std::size_t length = terminator ? static_cast<const char*>(terminator) - buffer.data : buffer.size;
            return std::string_view(buffer.data, length);
        }

        bool contains(std::string_view text, std::string_view token) noexcept
        {
            return !token.empty() && text.find(token) != std::string_view::npos;
        }

        bool isWhitespace(char value) noexcept
        {
            return value == ' ' || value == '\t';
        }

        std::string_view readFollowingToken(std::string_view commandLine, std::size_t valueOffset) noexcept
        {
            while (valueOffset < commandLine.size() && isWhitespace(commandLine[valueOffset]))
                ++valueOffset;
            const std::size_t valueBegin = valueOffset;
            while (valueOffset < commandLine.size() && !isWhitespace(commandLine[valueOffset]) && valueOffset - valueBegin < MaxGameXpSessionIdLength)
                ++valueOffset;
            return commandLine.substr(valueBegin, valueOffset - valueBegin);
        }

        void copyToBuffer(MutableBuffer destination, std::string_view source) noexcept
        {
            if (destination.empty())
                return;
            const std::size_t maxBytes = destination.size - 1;
            const std::size_t bytesToCopy = std::min(source.size(), maxBytes);
            if (bytesToCopy != 0)
                std::memcpy(destination.data, source.data(), bytesToCopy);
            destination.data[bytesToCopy] = '\0';
        }

        void writeFlag(char* destination, bool value) noexcept
        {
            if (destination)
                *destination = value ? 1 : 0;
        }

        void writeInt(int* destination, int value) noexcept
        {
            if (destination)
                *destination = value;
        }

        void writePointer(void** destination, void* value) noexcept
        {
            if (destination)
                *destination = value;
        }

        void decodeGuardToken(MutableBuffer token, std::uintptr_t xorKey) noexcept
        {
            if (token.empty() || xorKey == 0 || token.size < sizeof(std::uint32_t))
                return;
            for (std::size_t offset = 0; offset + sizeof(std::uint32_t) <= token.size; offset += 3)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, token.data + offset, sizeof(value));
                value ^= static_cast<std::uint32_t>(xorKey);
                std::memcpy(token.data + offset, &value, sizeof(value));
            }
        }

        class CriticalSectionScope final
        {
        public:
            CriticalSectionScope(const ClientMemory& memory, const platform::win32::SystemServices& system) : memory_(memory), system_(system) {}
            CriticalSectionScope(const CriticalSectionScope&) = delete;
            CriticalSectionScope& operator=(const CriticalSectionScope&) = delete;
            ~CriticalSectionScope()
            {
                if (!ownsSections_)
                    return;
                for (void* section : memory_.criticalSections)
                    system_.deleteCriticalSection(section);
            }
            void initialize()
            {
                for (void* section : memory_.criticalSections)
                    system_.initializeCriticalSection(section);
                ownsSections_ = true;
            }
            void release() noexcept
            {
                ownsSections_ = false;
            }

        private:
            const ClientMemory& memory_;
            const platform::win32::SystemServices& system_;
            bool ownsSections_ = false;
        };
    }

    ParsedCommandLine parseCommandLine(std::string_view commandLine) noexcept
    {
        if (!contains(commandLine, LoginSwitch))
            return {};
        const std::size_t sidSwitchOffset = commandLine.find(GameXpSidSwitch);
        if (sidSwitchOffset == std::string_view::npos)
            return { LaunchMode::InvalidLogin, {} };
        return { LaunchMode::GameXpLogin, readFollowingToken(commandLine, sidSwitchOffset + GameXpSidSwitch.size()) };
    }

    RuntimeConfig makeRuntimeConfig(const SferaClientRuntimeConfig& config) noexcept
    {
        RuntimeConfig result;
        result.memory.primaryInstance = static_cast<void**>(config.memory.primaryInstanceGlobal);
        result.memory.idaCompatibilityInstance = static_cast<void**>(config.memory.idaCompatibilityInstanceGlobal);
        result.memory.showCommand = config.memory.showCommandGlobal;
        result.memory.commandLine = { config.memory.commandLine.data, config.memory.commandLine.size };
        result.memory.gameXpSession = { config.memory.gameXpSession.data, config.memory.gameXpSession.size };
        result.memory.connectTypeFlag = config.memory.connectTypeFlag;
        result.memory.noLoginFlag = config.memory.noLoginFlag;
        result.memory.requiredDirectories = config.memory.requiredDirectories;
        result.memory.requiredDirectoryCount = config.memory.requiredDirectoryCount;
        for (std::size_t index = 0; index < CriticalSectionCount; ++index)
            result.memory.criticalSections[index] = config.memory.criticalSections[index];
        result.memory.guardToken = { config.memory.guardToken.data, config.memory.guardToken.size };
        result.memory.guardTokenXorKey = config.memory.guardTokenXorKey;
        result.memory.guardMessageText = cStringView(config.memory.guardMessageText);
        result.memory.guardMessageTitle = cStringView(config.memory.guardMessageTitle);
        result.memory.missingLoginSidText = cStringView(config.memory.missingLoginSidText);
        result.memory.missingLoginSidTitle = cStringView(config.memory.missingLoginSidTitle);
        result.callbacks.fatalMessageHandler = config.callbacks.fatalMessageHandler;
        result.callbacks.installFatalMessageHandler = config.callbacks.installFatalMessageHandler;
        result.callbacks.cleanupCallback = config.callbacks.cleanupCallback;
        result.callbacks.installCleanupCallback = config.callbacks.installCleanupCallback;
        result.callbacks.openConfigFile = config.callbacks.openConfigFile;
        result.callbacks.readIntegerConfigValue = config.callbacks.readIntegerConfigValue;
        result.callbacks.initializeSubsystems = config.callbacks.initializeSubsystems;
        result.callbacks.initializeTiming = config.callbacks.initializeTiming;
        result.callbacks.loadConfig = config.callbacks.loadConfig;
        result.callbacks.loadDebugConfig = config.callbacks.loadDebugConfig;
        result.callbacks.enterMainLoop = config.callbacks.enterMainLoop;
        return result;
    }

    StartupInfo makeStartupInfo(const SferaClientStartupInfo& startup) noexcept
    {
        return { startup.instance, startup.previousInstance, cStringView(startup.commandLine), startup.showCommand };
    }

    SphereApplication::SphereApplication(RuntimeConfig config) noexcept : config_(config) {}

    int SphereApplication::run(const StartupInfo& startup)
    {
        platform::win32::SystemServices system;
        system.pinCurrentThreadToProcessor(PrimaryProcessorAffinity);
        installCallbacks();
        decodeGuardToken(config_.memory.guardToken, config_.memory.guardTokenXorKey);
        if (rejectGuardLaunch(startup.commandLine))
            return 0;
        if (!applyCommandLine(startup.commandLine))
            return 0;
        loadConnectionMode();
        initializeSubsystems();
        CriticalSectionScope criticalSections(config_.memory, system);
        criticalSections.initialize();
        initializeRequiredDirectories();
        initializeTiming();
        publishStartupInfo(startup);
        loadConfiguration();
        criticalSections.release();
        enterMainLoop();
        return 0;
    }

    void SphereApplication::installCallbacks() const
    {
        const ClientCallbacks& callbacks = config_.callbacks;
        if (callbacks.installFatalMessageHandler && callbacks.fatalMessageHandler)
            callbacks.installFatalMessageHandler(callbacks.fatalMessageHandler);
        if (callbacks.installCleanupCallback && callbacks.cleanupCallback)
            callbacks.installCleanupCallback(callbacks.cleanupCallback);
    }

    bool SphereApplication::rejectGuardLaunch(std::string_view commandLine) const
    {
        const std::string_view guardToken = nullTerminatedBufferView(config_.memory.guardToken);
        const std::size_t occurrence = guardToken.empty() ? std::string_view::npos : commandLine.find(guardToken);
        if (occurrence == std::string_view::npos)
            return false;
        char* guardWindow = const_cast<char*>(commandLine.data() + occurrence);
        platform::win32::SystemServices system;
        system.showMessage({ guardWindow, cStringData(config_.memory.guardMessageText), cStringData(config_.memory.guardMessageTitle), static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(guardWindow)) });
        return true;
    }

    bool SphereApplication::applyCommandLine(std::string_view commandLine) const
    {
        const ParsedCommandLine parsed = parseCommandLine(commandLine);
        if (parsed.mode == LaunchMode::InvalidLogin)
        {
            platform::win32::SystemServices system;
            system.showMessage({ nullptr, cStringData(config_.memory.missingLoginSidText), cStringData(config_.memory.missingLoginSidTitle), ErrorMessageBoxFlags });
            return false;
        }
        if (parsed.mode == LaunchMode::LocalWithoutLogin)
            writeFlag(config_.memory.noLoginFlag, true);
        if (parsed.mode == LaunchMode::GameXpLogin)
            copyToBuffer(config_.memory.gameXpSession, parsed.gameXpSessionId);
        return true;
    }

    void SphereApplication::loadConnectionMode() const
    {
        const ClientCallbacks& callbacks = config_.callbacks;
        if (!callbacks.openConfigFile || !callbacks.readIntegerConfigValue)
            return;
        if (callbacks.openConfigFile(cStringData(ConnectConfigFile)) != 0)
            return;
        int connectType = 0;
        const bool readSucceeded = callbacks.readIntegerConfigValue(cStringData(ConnectTypeKey), &connectType) == 0;
        writeFlag(config_.memory.connectTypeFlag, readSucceeded && connectType == 1);
    }

    void SphereApplication::initializeSubsystems() const
    {
        if (config_.callbacks.initializeSubsystems)
            config_.callbacks.initializeSubsystems();
    }

    void SphereApplication::initializeRequiredDirectories() const
    {
        platform::win32::SystemServices system;
        for (std::size_t index = 0; index < config_.memory.requiredDirectoryCount; ++index)
        {
            const char* directory = config_.memory.requiredDirectories ? config_.memory.requiredDirectories[index] : nullptr;
            system.ensureDirectoryExists(cStringView(directory));
        }
    }

    void SphereApplication::initializeTiming() const
    {
        if (config_.callbacks.initializeTiming)
            config_.callbacks.initializeTiming(TimingScale, TimingStep);
    }

    void SphereApplication::publishStartupInfo(const StartupInfo& startup) const
    {
        writePointer(config_.memory.primaryInstance, startup.instance);
        writePointer(config_.memory.idaCompatibilityInstance, startup.instance);
        writeInt(config_.memory.showCommand, startup.showCommand);
        copyToBuffer(config_.memory.commandLine, startup.commandLine);
    }

    void SphereApplication::loadConfiguration() const
    {
        if (config_.callbacks.loadConfig)
            config_.callbacks.loadConfig();
        if (config_.callbacks.loadDebugConfig)
            config_.callbacks.loadDebugConfig();
    }

    void SphereApplication::enterMainLoop() const
    {
        if (config_.callbacks.enterMainLoop)
            config_.callbacks.enterMainLoop();
    }
}

extern "C" int sfera_run_client_application(const SferaClientRuntimeConfig* config, SferaClientStartupInfo startup)
{
    if (!config)
        return 0;
    sfera::client::SphereApplication application(sfera::client::makeRuntimeConfig(*config));
    return application.run(sfera::client::makeStartupInfo(startup));
}
