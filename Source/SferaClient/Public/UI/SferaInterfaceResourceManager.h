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

private:
    const SferaResourceManager* ResourceManager = nullptr;
    std::vector<const SferaResourceRecord*> UiFiles;
    std::vector<const SferaResourceRecord*> FontFiles;
    std::vector<const SferaResourceRecord*> LanguageFiles;
};
