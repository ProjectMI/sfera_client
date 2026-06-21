#pragma once
#include "Config/ConfigService.h"
#include "Network/NetworkTypes.h"
#include "UI/UiRuntime.h"
#include <optional>
#include <string>

namespace Sfera {
struct FClientSettings {
    int32 Width = 1024;
    int32 Height = 768;
    int32 Lang = 0;
    int32 ConnectType = 1;
    int32 Depth = 32;
    std::optional<FEndpoint> Endpoint;
    std::string RegistrationUrl;
    std::string Title = "Sphere";
};

FClientSettings LoadClientSettings(const FConfigService& config);
FUiBootstrapDesc MakeUiBootstrapDesc(const FClientSettings& settings);
}
