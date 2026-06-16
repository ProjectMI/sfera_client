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
    return true;
}

void SferaInterfaceResourceManager::Shutdown()
{
    UiFiles.clear();
    FontFiles.clear();
    LanguageFiles.clear();
    ResourceManager = nullptr;
}
