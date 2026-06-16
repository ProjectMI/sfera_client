#pragma once
#include "SferaBase.h"
#include "SferaResourceTypes.h"

class SferaResourceManager;

class SferaTextureManager
{
public:
    bool Initialize(const SferaResourceManager& Resources);
    void Shutdown();

    const std::vector<std::string>& GetTextureFolders() const { return TextureFolders; }
    const std::vector<const SferaResourceRecord*>& GetTextures() const { return Textures; }
    const SferaResourceRecord* FindTextureByName(const std::string& Name) const;

private:
    void AddTextureFolder(const std::string& Folder);

private:
    const SferaResourceManager* ResourceManager = nullptr;
    std::vector<std::string> TextureFolders;
    std::vector<const SferaResourceRecord*> Textures;
};
