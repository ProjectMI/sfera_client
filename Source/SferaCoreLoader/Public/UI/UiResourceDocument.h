#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Sfera {
struct FUiVec2i { int32 X = 0; int32 Y = 0; };
struct FUiRecti { int32 X = 0; int32 Y = 0; int32 W = 0; int32 H = 0; };

struct FUiProperty {
    std::string Key;
    std::vector<std::string> Values;
    int32 Line = 0;
};

struct FUiTextureCoord {
    int32 X = 0;
    int32 Y = 0;
};

struct FUiTextureSlice {
    std::string TextureName;
    FUiRecti Source;
    FUiRecti Dest;
    bool HasCustomTexCoords = false;
    FUiTextureCoord TexCoords[4];
    int32 Line = 0;
};

struct FUiSprite {
    std::string Name;
    FUiVec2i Size;
    std::vector<FUiTextureSlice> TextureSlices;
    std::vector<FUiProperty> Properties;
    int32 Line = 0;
};

struct FUiControl {
    int32 Id = 0;
    std::string ClassId;
    FUiVec2i Position;
    FUiVec2i Size;
    int32 Font = -1;
    std::string Image;
    std::string DrawMethod;
    std::string DrawSprite;
    bool Hidden = false;
    bool Disabled = false;
    std::vector<FUiProperty> Properties;
    int32 Line = 0;
};

struct FUiWindow {
    std::string Name;
    std::string Text;
    FUiVec2i Position;
    FUiVec2i Size;
    int32 Font = -1;
    std::string DrawMethod;
    std::string DrawSprite;
    std::vector<FUiSprite> Sprites;
    std::vector<FUiControl> Controls;
    std::vector<FUiProperty> Properties;
    int32 Line = 0;
};

struct FUiDocumentStats {
    std::string LogicalName;
    size_t ByteSize = 0;
    bool Loaded = false;
    bool TextLike = false;
    bool Parsed = false;
    bool WasCompressed = false;
    size_t WindowCount = 0;
    size_t ControlCount = 0;
    size_t SpriteCount = 0;
    size_t TextureSliceCount = 0;
    size_t TextureNameCount = 0;
    size_t WarningCount = 0;
};

struct FUiDocument {
    std::string LogicalName;
    std::string SourcePath;
    size_t ByteSize = 0;
    bool WasCompressed = false;
    std::vector<FUiWindow> Windows;
    std::vector<FUiSprite> GlobalSprites;
    std::vector<std::string> Warnings;
    std::set<std::string> TextureNames;
    FUiDocumentStats Stats() const;
    const FUiWindow* FindWindowByName(std::string_view name) const;
};

struct FUiResourceManifest {
    size_t DocumentCount = 0;
    size_t LoadedCount = 0;
    size_t ParsedCount = 0;
    size_t WindowCount = 0;
    size_t ControlCount = 0;
    size_t SpriteCount = 0;
    size_t TextureSliceCount = 0;
    size_t TextureNameCount = 0;
    std::unordered_map<std::string, size_t> ClassIdCounts;
    std::vector<FUiDocumentStats> Documents;
};

class FUiResourceDocument {
public:
    FStatus LoadFromResource(const FResourceManager& resources, std::string_view logicalName, FLogger* logger = nullptr);
    const FUiDocument& Document() const { return ParsedDocument; }
    const FUiDocumentStats& Stats() const { return ParsedStats; }
    bool IsLoaded() const { return ParsedStats.Loaded; }
    bool IsParsed() const { return ParsedStats.Parsed; }
    static TResult<FUiDocument> ParseText(std::string_view logicalName, std::string_view sourcePath, const FByteArray& bytes, bool wasCompressed);
private:
    FUiDocument ParsedDocument;
    FUiDocumentStats ParsedStats;
};

class FUiResourceSystem {
public:
    FStatus BuildManifest(const FResourceManager& resources, FLogger* logger = nullptr);
    TResult<FUiDocument> LoadDocument(const FResourceManager& resources, std::string_view logicalName, FLogger* logger = nullptr) const;
    const FUiResourceManifest& Manifest() const { return UiManifest; }
private:
    FUiResourceManifest UiManifest;
};
}
