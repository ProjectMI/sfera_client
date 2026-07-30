#include "Client/ClientSettings.h"
#include "Common/StringUtils.h"
#include "FileSystem/PathUtils.h"

namespace
{
    int32 ReadIntSetting(const FConfigService& config, std::string_view key, int32 fallback)
    {
        auto value = config.FindInt(key);
        return value ? static_cast<int32>(*value) : fallback;
    }
    std::string ReadStringSetting(const FConfigService& config, std::string_view key, std::string fallback = {})
    {
        auto value = config.FindString(key);
        return value ? Common::Trim(*value) : fallback;
    }
    float ReadVolumeSetting(const FConfigService& config, std::string_view key, float fallback)
    {
        const FConfigDocument* audioConfig = config.FindConfig("sfera_audio.cfg");
        auto value = audioConfig ? audioConfig->FindInt(key) : std::nullopt;
        if (!value) { value = config.FindInt(key); }
        if (!value) { return fallback; }
        const float normalized = static_cast<float>(*value) / 100.0f;
        return std::clamp(normalized, 0.0f, 1.0f);
    }
    std::optional<uint16> ReadPort(const FConfigService& config)
    {
        auto port = config.FindInt("PORT");

        if (!port)
        {
            port = config.FindInt("SERVERPORT");
        }

        if (!port)
        {
            port = config.FindInt("LOGINPORT");
        }

        if (!port || *port <= 0 || *port > 65535)
        {
            return std::nullopt;
        }

        return static_cast<uint16>(*port);
    }
    std::optional<std::string> ReadHost(const FConfigService& config)
    {
        constexpr std::array<std::string_view, 7> keys = {
            "MAIN_URL", "SERVER", "HOST", "IP", "ADDRESS", "LAST_URL", "SRV00"
        };

        for (std::string_view key : keys)
        {
            auto host = config.FindString(key);

            if (host && !Common::Trim(*host).empty())
            {
                return Common::Trim(*host);
            }
        }

        return std::nullopt;
    }
}

FClientSettings LoadClientSettings(const FConfigService& config)
{
    FClientSettings settings;
    settings.Width = std::clamp(ReadIntSetting(config, "XRES", settings.Width), 640, 8192);
    settings.Height = std::clamp(ReadIntSetting(config, "YRES", settings.Height), 480, 8192);
    settings.Lang = ReadIntSetting(config, "LANG", settings.Lang);
    settings.ConnectType = ReadIntSetting(config, "CONNECT_TYPE", settings.ConnectType);
    settings.Depth = ReadIntSetting(config, "DEPTH", settings.Depth);
    settings.SoundVolume = ReadVolumeSetting(config, "SNDVOL", settings.SoundVolume);
    settings.MusicVolume = ReadVolumeSetting(config, "MUSVOL", settings.MusicVolume);
    settings.RegistrationUrl = ReadStringSetting(config, "REG_SRV");
    auto host = ReadHost(config);
    auto port = ReadPort(config);

    if (host && port)
    {
        settings.Endpoint = FEndpoint
        {
            *host, *port
        };
    }

    settings.Title = settings.Endpoint ? "Sphere - " + settings.Endpoint->Host + ":" + std::to_string(settings.Endpoint->Port) : "Sphere";
    return settings;
}

FUiBootstrapDesc MakeUiBootstrapDesc(const FClientSettings& settings)
{
    FUiBootstrapDesc desc;
    desc.DesignWidth = 1024;
    desc.DesignHeight = 768;
    desc.Lang = settings.Lang;
    desc.StringsResource = settings.Lang == 1 ? "language/strings_e.ui" : settings.Lang == 2 ? "language/strings_i.ui" : settings.Lang == 3 ? "language/strings_p.ui" : "language/strings.ui";
    const bool english = settings.Lang == 1;
    const bool sphereOne = settings.ConnectType == 1;
    desc.LoginBackgroundTexture = english ? (sphereOne ? "xadd/login_eng_s1.dds" : "xadd/login_eng_sp.dds") : (sphereOne ? "xadd/login_rus_s1.dds" : "xadd/login_rus_sp.dds");
    desc.ConnectionWindowResource = "effects/connection.ui";
    desc.PickPersonWindowResource = "effects/pickpers.ui";
    desc.SoundVolume = settings.SoundVolume;
    desc.MusicVolume = settings.MusicVolume;
    return desc;
}

FStatus SaveClientAudioSettings(float soundVolume, float musicVolume)
{
    const FPath path = PathUtils::GetExecutableDirectory() / "sfera_audio.cfg";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) { return FStatus::Error(EStatusCode::IoError, "cannot open audio settings for writing: " + path.string()); }
    const int32 soundPercent = static_cast<int32>(std::lround(std::clamp(soundVolume, 0.0f, 1.0f) * 100.0f));
    const int32 musicPercent = static_cast<int32>(std::lround(std::clamp(musicVolume, 0.0f, 1.0f) * 100.0f));
    output << "SNDVOL = " << soundPercent << '\n' << "MUSVOL = " << musicPercent << '\n';
    output.flush();
    if (!output) { return FStatus::Error(EStatusCode::IoError, "cannot save audio settings: " + path.string()); }
    return FStatus::Ok();
}
