#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include <string>
#include <vector>

namespace Sfera {
struct FUiFontGlyph {
    bool Valid = false;
    int SourceW = 0;
    int SourceH = 0;
    int BearingX = 0;
    int BearingY = 0;
    int Advance = 0;
    float U1 = 0.0f;
    float V1 = 0.0f;
    float U2 = 0.0f;
    float V2 = 0.0f;
};

struct FUiFontFace {
    int Index = -1;
    std::string Name;
    std::string InternalName;
    std::string SystemFace;
    std::string DescriptorResource;
    std::string TextureName;
    std::string TextureResource;
    int NativeHeight = 0;
    int Baseline = 0;
    FUiFontGlyph Glyphs[256];
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
