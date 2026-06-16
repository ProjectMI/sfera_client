#include "SferaTextureManager.h"
#include "SferaResourceManager.h"
#include "SferaStringUtil.h"

bool SferaTextureManager::Initialize(const SferaResourceManager& Resources)
{
    ResourceManager = &Resources;
    TextureFolders.clear();
    Textures.clear();

    // From main.cpp strings around sub_4A2790 and texturesset.cpp.
    AddTextureFolder("debug\\pics");
    AddTextureFolder("landscape");
    AddTextureFolder("landscape_hr");
    AddTextureFolder("landscape_ph");
    AddTextureFolder("landscape_rd");
    AddTextureFolder("models\\textures");
    AddTextureFolder("models_hr\\textures");
    AddTextureFolder("models_ph\\textures");
    AddTextureFolder("models_rd\\textures");
    AddTextureFolder("textures");
    AddTextureFolder("textures\\fx");
    AddTextureFolder("textures\\emblems");
    AddTextureFolder("textures\\cursors");
    AddTextureFolder("xadd");
    AddTextureFolder("players");
    AddTextureFolder("Shaders\\Pixel");
    AddTextureFolder("Shaders\\Vertex");

    Textures = Resources.FindByKind(ESferaResourceKind::Texture);
    const std::vector<const SferaResourceRecord*> CursorImages = Resources.FindByKind(ESferaResourceKind::CursorImage);
    Textures.insert(Textures.end(), CursorImages.begin(), CursorImages.end());
    return true;
}

void SferaTextureManager::Shutdown()
{
    Textures.clear();
    TextureFolders.clear();
    ResourceManager = nullptr;
}

const SferaResourceRecord* SferaTextureManager::FindTextureByName(const std::string& Name) const
{
    const std::string Wanted = SferaStringUtil::ToLower(SferaStringUtil::NormalizeSlashes(Name));
    for (const SferaResourceRecord* Record : Textures)
    {
        if (!Record)
        {
            continue;
        }
        const std::string Path = SferaStringUtil::ToLower(Record->LogicalPath);
        if (Path == Wanted || SferaStringUtil::EndsWithIgnoreCase(Path, std::string("\\") + Wanted))
        {
            return Record;
        }
    }
    return nullptr;
}

void SferaTextureManager::AddTextureFolder(const std::string& Folder)
{
    TextureFolders.push_back(SferaStringUtil::NormalizeLogicalPath(Folder));
}
