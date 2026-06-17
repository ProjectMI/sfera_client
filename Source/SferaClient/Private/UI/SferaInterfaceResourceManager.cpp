#include "SferaInterfaceResourceManager.h"
#include "SferaResourceManager.h"

bool SferaInterfaceResourceManager::Initialize(const SferaResourceManager& Resources)
{
    ResourceManager = &Resources;
    UiFiles = Resources.FindByKind(ESferaResourceKind::UserInterface);
    FontFiles = Resources.FindByKind(ESferaResourceKind::FontDefinition);
    LanguageFiles = Resources.FindByKind(ESferaResourceKind::HyperText);

    std::vector<const SferaResourceRecord*> Strings = Resources.FindByWildcard("Language\\*.ui");
    LanguageFiles.insert(LanguageFiles.end(), Strings.begin(), Strings.end());
    ControlCatalog.Build(Resources);
    RebuildStartupUiState();
    return true;
}

void SferaInterfaceResourceManager::Shutdown()
{
    UiFiles.clear();
    FontFiles.clear();
    LanguageFiles.clear();
    StartupUiFiles.clear();
    StartupTitle.clear();
    StartupStatusText.clear();
    ControlCatalog.Clear();
    ResourceManager = nullptr;
}

void SferaInterfaceResourceManager::RebuildStartupUiState()
{
    StartupUiFiles.clear();
    for (const SferaResourceRecord* Record : UiFiles)
    {
        if (Record && Record->bExists)
        {
            StartupUiFiles.push_back(Record);
            if (StartupUiFiles.size() >= 8)
            {
                break;
            }
        }
    }

    StartupTitle = "Sphere";
    if (!LanguageFiles.empty())
    {
        StartupTitle += " - localized UI";
    }
    else if (!StartupUiFiles.empty())
    {
        StartupTitle += " - UI resources";
    }

    StartupStatusText = "Interface resources: " + std::to_string(UiFiles.size()) + " ui, " + std::to_string(FontFiles.size()) + " fonts, " + std::to_string(LanguageFiles.size()) + " language files; " + ControlCatalog.BuildSummaryText();
}
