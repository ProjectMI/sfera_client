#include "SferaConfig.h"
#include "SferaResourceManager.h"

bool SferaConfig::LoadBootstrapConfig(const SferaCommandLine& CommandLine)
{
    bLoginMode = CommandLine.HasLoginFlag();
    WindowSize.Width = 800;
    WindowSize.Height = 600;
    bRuntimeMode = true;
    bHardwareCursor = true;
    return true;
}

bool SferaConfig::LoadRuntimeConfig(const SferaResourceManager& Resources)
{
    std::string Text;
    if (Resources.LoadTextFile("config.cfg", Text))
    {
        ConfigFile.LoadFromText(Text);
    }
    if (Resources.LoadTextFile("connect.cfg", Text) || Resources.LoadTextFile("connectn.cfg", Text))
    {
        ConnectFile.LoadFromText(Text);
    }
    if (Resources.LoadTextFile("control.cfg", Text))
    {
        ControlFile.LoadFromText(Text);
    }

    ApplyConfigValues();
    return true;
}

void SferaConfig::ApplyConfigValues()
{
    bHardwareCursor = ConfigFile.GetBool("HARDWARE_CURSOR", bHardwareCursor);
    LanguageSuffix = ConfigFile.GetString("LANG", LanguageSuffix);

    // Original window bootstrap is hard-clamped to 800x600. Keep this invariant even if cfg is malformed.
    const int Width = ConfigFile.GetInt("WIDTH", WindowSize.Width);
    const int Height = ConfigFile.GetInt("HEIGHT", WindowSize.Height);
    WindowSize.Width = (std::max)(800, Width);
    WindowSize.Height = (std::max)(600, Height);
}
