#pragma once
#include "SferaBase.h"
#include "SferaResourceTypes.h"

class SferaResourceManager;

class SferaInterfaceResourceManager
{
public:
    bool Initialize(const SferaResourceManager& Resources);
    void Shutdown();

    const std::vector<const SferaResourceRecord*>& GetUiFiles() const { return UiFiles; }
    const std::vector<const SferaResourceRecord*>& GetFontFiles() const { return FontFiles; }
    const std::vector<const SferaResourceRecord*>& GetLanguageFiles() const { return LanguageFiles; }
    const std::vector<const SferaResourceRecord*>& GetStartupUiFiles() const { return StartupUiFiles; }
    const std::string& GetStartupTitle() const { return StartupTitle; }
    const std::string& GetStartupStatusText() const { return StartupStatusText; }

private:
    void RebuildStartupUiState();

    const SferaResourceManager* ResourceManager = nullptr;
    std::vector<const SferaResourceRecord*> UiFiles;
    std::vector<const SferaResourceRecord*> FontFiles;
    std::vector<const SferaResourceRecord*> LanguageFiles;
    std::vector<const SferaResourceRecord*> StartupUiFiles;
    std::string StartupTitle;
    std::string StartupStatusText;
};
