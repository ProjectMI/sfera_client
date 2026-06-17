#include "UI/FontCatalog.h"
#include "Core/NumericParse.h"
#include "ResourceLoader/ResourceTypes.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace Sfera {
namespace {
std::string LowerCopy(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; }
std::string Trim(std::string s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), isSpace));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), isSpace).base(), s.end());
    return s;
}
std::string ReadCString(const std::vector<uint8>& bytes, size_t& offset) {
    size_t start = offset;
    while (offset < bytes.size() && bytes[offset] != 0) { ++offset; }
    std::string value(reinterpret_cast<const char*>(bytes.data() + start), offset - start);
    if (offset < bytes.size()) { ++offset; }
    return value;
}
int16 ReadInt16Le(const std::vector<uint8>& bytes, size_t offset) {
    if (offset + 2 > bytes.size()) { return 0; }
    uint16 value = static_cast<uint16>(bytes[offset]) | (static_cast<uint16>(bytes[offset + 1]) << 8);
    return static_cast<int16>(value);
}

float ReadFloatLe(const std::vector<uint8>& bytes, size_t offset) {
    if (offset + 4 > bytes.size()) { return 0.0f; }
    uint32 value = static_cast<uint32>(bytes[offset]) | (static_cast<uint32>(bytes[offset + 1]) << 8) | (static_cast<uint32>(bytes[offset + 2]) << 16) | (static_cast<uint32>(bytes[offset + 3]) << 24);
    float out = 0.0f;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

int ReadInt32Le(const std::vector<uint8>& bytes, size_t offset) {
    if (offset + 4 > bytes.size()) { return 0; }
    uint32 value = static_cast<uint32>(bytes[offset]) | (static_cast<uint32>(bytes[offset + 1]) << 8) | (static_cast<uint32>(bytes[offset + 2]) << 16) | (static_cast<uint32>(bytes[offset + 3]) << 24);
    return static_cast<int32>(value);
}
}

std::vector<std::string> FUiFontCatalog::ParseConfiguredFonts(std::string_view text) {
    std::vector<std::string> names;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        size_t comment = line.find("//");
        if (comment != std::string::npos) { line.resize(comment); }
        line = Trim(line);
        if (line.empty()) { continue; }
        std::string lower = LowerCopy(line);
        if (lower.rfind("new_font_", 0) != 0) { continue; }
        size_t quote1 = line.find('"');
        size_t quote2 = quote1 == std::string::npos ? std::string::npos : line.find('"', quote1 + 1);
        if (quote1 == std::string::npos || quote2 == std::string::npos || quote2 <= quote1 + 1) { continue; }
        size_t idxStart = std::strlen("NEW_FONT_");
        size_t idxEnd = line.find_first_of(" \t", idxStart);
        int32 index = -1;
        NumericParse::TryParseInt32Strict(line.substr(idxStart, idxEnd == std::string::npos ? std::string::npos : idxEnd - idxStart), index);
        if (index < 0) { continue; }
        if (names.size() <= static_cast<size_t>(index)) { names.resize(static_cast<size_t>(index) + 1); }
        names[static_cast<size_t>(index)] = line.substr(quote1 + 1, quote2 - quote1 - 1);
    }
    return names;
}

std::string FUiFontCatalog::ResolveResource(const FResourceManager& resources, std::string_view name, std::initializer_list<std::string_view> prefixes, std::initializer_list<std::string_view> extensions) {
    if (name.empty()) { return {}; }
    std::string baseName(name);
    bool hasExt = baseName.find('.') != std::string::npos;
    for (std::string_view prefix : prefixes) {
        for (std::string_view ext : extensions) {
            if (hasExt && !ext.empty()) { continue; }
            std::string candidate(prefix);
            candidate += baseName;
            candidate += ext;
            if (auto record = resources.Catalog().FindByLogicalName(candidate)) { return record->RelativePath.generic_string(); }
        }
    }
    std::string wanted = LowerCopy(baseName);
    FPath wantedPath{baseName};
    std::string wantedStem = LowerCopy(wantedPath.stem().string());
    std::vector<std::string> allowedExts;
    for (std::string_view ext : extensions) { if (!ext.empty()) { allowedExts.push_back(LowerCopy(std::string(ext))); } }
    for (const auto& record : resources.Catalog().All()) {
        std::string ext = LowerCopy(record.RelativePath.extension().string());
        if (!allowedExts.empty() && std::find(allowedExts.begin(), allowedExts.end(), ext) == allowedExts.end()) { continue; }
        std::string stem = LowerCopy(record.RelativePath.stem().string());
        std::string filename = LowerCopy(record.RelativePath.filename().string());
        if (stem == wanted || filename == wanted || (!wantedStem.empty() && stem == wantedStem)) { return record.RelativePath.generic_string(); }
    }
    return {};
}

void FUiFontCatalog::ParseSfnt(FUiFontFace& face, const std::vector<uint8>& bytes) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "SFNT", 4) != 0) { return; }
    size_t offset = 4;
    std::string internalName = ReadCString(bytes, offset);
    std::string textureName = ReadCString(bytes, offset);
    if (!internalName.empty()) { face.InternalName = internalName; }
    if (!textureName.empty()) { face.TextureName = textureName; }
    if (offset + 8 > bytes.size()) { return; }
    face.NativeHeight = std::max(0, ReadInt32Le(bytes, offset));
    face.Baseline = std::max(0, ReadInt32Le(bytes, offset + 4));
    size_t glyphOffset = offset + 8;
    constexpr size_t GlyphRecordSize = 28;
    for (int code = 32; code < 256; ++code) {
        if (glyphOffset + GlyphRecordSize > bytes.size()) { break; }
        FUiFontGlyph glyph;
        glyph.SourceW = ReadInt16Le(bytes, glyphOffset + 0);
        glyph.SourceH = ReadInt16Le(bytes, glyphOffset + 2);
        glyph.BearingX = ReadInt16Le(bytes, glyphOffset + 4);
        glyph.BearingY = ReadInt16Le(bytes, glyphOffset + 6);
        glyph.Advance = ReadInt16Le(bytes, glyphOffset + 8);
        glyph.U1 = ReadFloatLe(bytes, glyphOffset + 12);
        glyph.V1 = ReadFloatLe(bytes, glyphOffset + 16);
        glyph.U2 = ReadFloatLe(bytes, glyphOffset + 20);
        glyph.V2 = ReadFloatLe(bytes, glyphOffset + 24);
        glyph.Valid = glyph.SourceW > 0 && glyph.SourceH > 0 && glyph.U2 > glyph.U1 && glyph.V2 > glyph.V1;
        if (glyph.Advance <= 0) { glyph.Advance = std::max(1, glyph.SourceW + glyph.BearingX); }
        face.Glyphs[code] = glyph;
        glyphOffset += GlyphRecordSize;
    }
}

namespace {
std::string RecoveredSystemFaceForFontIndex(int index) {
    switch (index) {
    case 0: return "Tahoma";
    case 1: return "Tahoma";
    case 2: return "Tahoma";
    case 3: return "Arial";
    case 4: return "Calibri";
    case 5: return "Century";
    case 6: return "Consolas";
    case 7: return "Garamond";
    case 8: return "Georgia";
    case 9: return "Microsoft Sans Serif";
    case 10: return "Times New Roman";
    case 11: return "Verdana";
    default: return "Arial";
    }
}
}

void FUiFontCatalog::Load(const FResourceManager& resources, FLogger* logger) {
    if (Loaded) { return; }
    Loaded = true;
    FontFaces.clear();
    auto cfg = resources.Load("fonts.cfg");
    if (!cfg.IsOk()) {
        if (logger) { logger->Warning("UI fonts: fonts.cfg not found; text renderer will use GDI fallback"); }
        return;
    }
    std::string cfgText(reinterpret_cast<const char*>(cfg.Value().Bytes.data()), cfg.Value().Bytes.size());
    std::vector<std::string> names = ParseConfiguredFonts(cfgText);
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) { continue; }
        FUiFontFace face;
        face.Index = static_cast<int>(i);
        face.Name = names[i];
        face.SystemFace = RecoveredSystemFaceForFontIndex(face.Index);
        face.DescriptorResource = ResolveResource(resources, names[i], {"effects/", "Effects/", ""}, {".sfn"});
        if (!face.DescriptorResource.empty()) {
            auto sfnt = resources.Load(face.DescriptorResource);
            if (sfnt.IsOk()) { ParseSfnt(face, sfnt.Value().Bytes); }
        }
        face.TextureResource = ResolveResource(resources, face.TextureName.empty() ? face.Name : face.TextureName, {"xadd/", "effects/", "Effects/", "textures/", ""}, {".dds", ".tga", ".png", ""});
        if (face.TextureResource.empty()) { face.TextureResource = ResolveResource(resources, face.Name, {"xadd/", "effects/", "Effects/", "textures/", ""}, {".dds", ".tga", ".png", ""}); }
        FontFaces.push_back(face);
        if (logger) { logger->Info("UI font attached: id=" + std::to_string(face.Index) + ", name=" + face.Name + ", internal=" + face.InternalName + ", system=" + face.SystemFace + ", descriptor=" + face.DescriptorResource + ", texture=" + face.TextureResource); }
    }
    if (logger) { logger->Info("UI fonts attached: " + std::to_string(FontFaces.size())); }
}

const FUiFontFace* FUiFontCatalog::Find(int index) const {
    for (const auto& face : FontFaces) { if (face.Index == index) { return &face; } }
    return nullptr;
}
}
