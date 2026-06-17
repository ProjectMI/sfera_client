#pragma once
#include "Compression/CompressionService.h"
#include "Config/ConfigService.h"
#include "Core/Logger.h"
#include "FileSystem/FileSystem.h"
#include "MBC/MbcNativeRegistry.h"
#include "Network/ConnectManager.h"
#include "ResourceLoader/ResourceManager.h"
#include <memory>

namespace Sfera {
class FApplication {
public:
    FApplication();
    int Run();
private:
    void RegisterRecoveredNatives();
    void ProbeMbcModules();
    FPath ExecutableRoot;
    FLogger Logger;
    FFileSystem FileSystem;
    FCompressionService Compression;
    std::unique_ptr<FConfigService> Config;
    std::unique_ptr<FResourceManager> Resources;
    std::unique_ptr<FConnectManager> Network;
    FMbcNativeRegistry MbcNatives;
};
}
