#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include <string>
#include <vector>

namespace Sfera {
struct FUiFontFace {
    int Index = -1;
    std::string Name;
    std::string DescriptorResource;
    std::string TextureName;
    std::string TextureResource;
    int NativeHeight = 0;
};

class FUiFontCatalog {
public:
    void Load(const FResourceManager& resources, FLogger* logger = nullptr);
    bool IsLoaded() const { return Loaded; }
    const FUiFontFace* Find(int index) const;
    const std::vector<FUiFontFace>& Faces() const { return FontFaces; }
private:
    static std::vector<std::string> ParseConfiguredFonts(std::string_view text);
    static void ParseSfnt(FUiFontFace& face, const std::vector<uint8>& bytes);
    static std::string ResolveResource(const FResourceManager& resources, std::string_view name, std::initializer_list<std::string_view> prefixes, std::initializer_list<std::string_view> extensions);
    bool Loaded = false;
    std::vector<FUiFontFace> FontFaces;
};
}
