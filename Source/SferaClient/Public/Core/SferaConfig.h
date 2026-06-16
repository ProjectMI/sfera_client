#pragma once
#include "SferaBase.h"
#include "SferaCommandLine.h"
#include "SferaTextConfig.h"

class SferaResourceManager;

class SferaConfig
{
public:
    bool LoadBootstrapConfig(const SferaCommandLine& CommandLine);
    bool LoadRuntimeConfig(const SferaResourceManager& Resources);

    const SferaSize2D& GetWindowSize() const { return WindowSize; }
    bool IsRuntimeMode() const { return bRuntimeMode; }
    bool IsLoginMode() const { return bLoginMode; }
    bool UseHardwareCursor() const { return bHardwareCursor; }
    const std::string& GetLanguageSuffix() const { return LanguageSuffix; }
    const SferaTextConfig& GetConfigFile() const { return ConfigFile; }
    const SferaTextConfig& GetConnectFile() const { return ConnectFile; }
    const SferaTextConfig& GetControlFile() const { return ControlFile; }

private:
    void ApplyConfigValues();

private:
    // Original sub_499B60 clamps to at least 0x320 x 0x258.
    SferaSize2D WindowSize = { 800, 600 };
    bool bRuntimeMode = true;
    bool bLoginMode = false;
    bool bHardwareCursor = true;
    std::string LanguageSuffix;
    SferaTextConfig ConfigFile;
    SferaTextConfig ConnectFile;
    SferaTextConfig ControlFile;
};
