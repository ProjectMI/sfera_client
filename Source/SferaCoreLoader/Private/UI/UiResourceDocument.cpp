#include "UI/UiResourceDocument.h"
#include "Core/NumericParse.h"
#include "FileSystem/PathUtils.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace Sfera {
namespace {
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return s;
}

bool EqualsNoCase(std::string_view a, std::string_view b) { return Lower(std::string(a)) == Lower(std::string(b)); }

std::string Trim(std::string s) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

bool IsMostlyText(const FByteArray& bytes) {
    if (bytes.empty()) { return false; }
    size_t printable = 0;
    size_t checked = 0;
    for (uint8 b : bytes) {
        if (b == 0) { continue; }
        ++checked;
        if (b == '\r' || b == '\n' || b == '\t' || (b >= 0x20 && b < 0x7F) || b >= 0xC0) { ++printable; }
    }
    return checked > 0 && printable * 100 / checked >= 85;
}

std::string BytesToText(const FByteArray& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (uint8 b : bytes) { if (b != 0) { out.push_back(static_cast<char>(b)); } }
    return out;
}

std::string RemoveLineComment(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '"') { inQuote = !inQuote; out.push_back(ch); continue; }
        if (!inQuote && ch == '/' && i + 1 < line.size() && line[i + 1] == '/') { break; }
        out.push_back(ch);
    }
    return Trim(std::move(out));
}

std::vector<std::string> TokenizeLine(std::string_view line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (inQuote) {
            if (ch == '"') { tokens.push_back(current); current.clear(); inQuote = false; }
            else { current.push_back(ch); }
            continue;
        }
        if (ch == '"') { if (!current.empty()) { tokens.push_back(current); current.clear(); } inQuote = true; continue; }
        if (std::isspace(static_cast<unsigned char>(ch))) { if (!current.empty()) { tokens.push_back(current); current.clear(); } continue; }
        if (ch == '{' || ch == '}' || ch == '|' || ch == '=') { if (!current.empty()) { tokens.push_back(current); current.clear(); } tokens.emplace_back(1, ch); continue; }
        current.push_back(ch);
    }
    if (!current.empty()) { tokens.push_back(current); }
    return tokens;
}

bool TryReadInt(const std::vector<std::string>& values, size_t index, int32& out) {
    return index < values.size() && NumericParse::TryParseInt32Strict(values[index], out);
}

int32 ReadIntOr(const std::vector<std::string>& values, size_t index, int32 fallback = 0) {
    int32 out = fallback;
    TryReadInt(values, index, out);
    return out;
}

bool ReadBool(const std::vector<std::string>& values, bool fallback = false) {
    if (values.empty()) { return fallback; }
    std::string v = Lower(values[0]);
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

std::string JoinValues(const std::vector<std::string>& values, size_t start = 0) {
    std::string out;
    for (size_t i = start; i < values.size(); ++i) { if (!out.empty()) { out += ' '; } out += values[i]; }
    return out;
}

FUiProperty MakeProperty(const std::vector<std::string>& tokens, int32 line) {
    FUiProperty prop;
    if (!tokens.empty()) { prop.Key = tokens[0]; }
    for (size_t i = 1; i < tokens.size(); ++i) { if (tokens[i] != "=") { prop.Values.push_back(tokens[i]); } }
    prop.Line = line;
    return prop;
}

enum class EUiBlock { Root, Window, SpritesDef, Sprite, Control };

struct FUiParseState {
    FUiDocument Document;
    std::vector<EUiBlock> Stack{EUiBlock::Root};
    EUiBlock Pending = EUiBlock::Root;
    bool HasPending = false;
    FUiWindow* CurrentWindow = nullptr;
    FUiSprite* CurrentSprite = nullptr;
    FUiControl* CurrentControl = nullptr;
};

EUiBlock BlockFromToken(const std::string& token) {
    std::string key = Lower(token);
    if (key == "windowui") { return EUiBlock::Window; }
    if (key == "spritesdef") { return EUiBlock::SpritesDef; }
    if (key == "sprite") { return EUiBlock::Sprite; }
    if (key == "control") { return EUiBlock::Control; }
    return EUiBlock::Root;
}

const char* BlockName(EUiBlock block) {
    switch (block) { case EUiBlock::Window: return "windowUI"; case EUiBlock::SpritesDef: return "spritesDef"; case EUiBlock::Sprite: return "sprite"; case EUiBlock::Control: return "control"; default: return "root"; }
}

void StartBlock(FUiParseState& state, EUiBlock block, int32 line) {
    state.Stack.push_back(block);
    if (block == EUiBlock::Window) {
        state.Document.Windows.emplace_back();
        state.CurrentWindow = &state.Document.Windows.back();
        state.CurrentWindow->Line = line;
    } else if (block == EUiBlock::Sprite) {
        if (state.CurrentWindow) { state.CurrentWindow->Sprites.emplace_back(); state.CurrentSprite = &state.CurrentWindow->Sprites.back(); }
        else { state.Document.GlobalSprites.emplace_back(); state.CurrentSprite = &state.Document.GlobalSprites.back(); }
        state.CurrentSprite->Line = line;
    } else if (block == EUiBlock::Control) {
        if (!state.CurrentWindow) { state.Document.Warnings.push_back("control block outside windowUI at line " + std::to_string(line)); return; }
        state.CurrentWindow->Controls.emplace_back();
        state.CurrentControl = &state.CurrentWindow->Controls.back();
        state.CurrentControl->Id = static_cast<int32>(state.CurrentWindow->Controls.size());
        state.CurrentControl->Line = line;
    }
}

void CloseBlock(FUiParseState& state, int32 line) {
    if (state.Stack.size() <= 1) { state.Document.Warnings.push_back("extra closing brace at line " + std::to_string(line)); return; }
    EUiBlock block = state.Stack.back();
    state.Stack.pop_back();
    if (block == EUiBlock::Control) { state.CurrentControl = nullptr; }
    else if (block == EUiBlock::Sprite) { state.CurrentSprite = nullptr; }
    else if (block == EUiBlock::Window) { state.CurrentWindow = nullptr; }
}

void ApplySpriteProperty(FUiParseState& state, const FUiProperty& prop) {
    if (!state.CurrentSprite) { return; }
    std::string key = Lower(prop.Key);
    if (key == "name" && !prop.Values.empty()) { state.CurrentSprite->Name = prop.Values[0]; }
    else if (key == "size") { state.CurrentSprite->Size.X = ReadIntOr(prop.Values, 0); state.CurrentSprite->Size.Y = ReadIntOr(prop.Values, 1); }
    else if (key == "texture" && prop.Values.size() >= 9) {
        FUiTextureSlice slice;
        slice.TextureName = prop.Values[0];
        slice.Source.X = ReadIntOr(prop.Values, 1);
        slice.Source.Y = ReadIntOr(prop.Values, 2);
        slice.Source.W = ReadIntOr(prop.Values, 3) - slice.Source.X;
        slice.Source.H = ReadIntOr(prop.Values, 4) - slice.Source.Y;
        slice.Dest.X = ReadIntOr(prop.Values, 5);
        slice.Dest.Y = ReadIntOr(prop.Values, 6);
        slice.Dest.W = ReadIntOr(prop.Values, 7) - slice.Dest.X;
        slice.Dest.H = ReadIntOr(prop.Values, 8) - slice.Dest.Y;
        slice.Line = prop.Line;
        state.CurrentSprite->TextureSlices.push_back(slice);
        state.Document.TextureNames.insert(Lower(slice.TextureName));
    } else if (key == "tcoords" && prop.Values.size() >= 9) {
        int32 sliceIndex = ReadIntOr(prop.Values, 0, -1);
        if (sliceIndex >= 0 && static_cast<size_t>(sliceIndex) < state.CurrentSprite->TextureSlices.size()) {
            FUiTextureSlice& slice = state.CurrentSprite->TextureSlices[static_cast<size_t>(sliceIndex)];
            slice.HasCustomTexCoords = true;
            for (int32 i = 0; i < 4; ++i) {
                slice.TexCoords[i].X = ReadIntOr(prop.Values, 1 + static_cast<size_t>(i) * 2);
                slice.TexCoords[i].Y = ReadIntOr(prop.Values, 2 + static_cast<size_t>(i) * 2);
            }
        } else {
            state.Document.Warnings.push_back("tcoords references missing sprite slice at line " + std::to_string(prop.Line));
        }
    }
    state.CurrentSprite->Properties.push_back(prop);
}

void ApplyControlProperty(FUiParseState& state, const FUiProperty& prop) {
    if (!state.CurrentControl) { return; }
    std::string key = Lower(prop.Key);
    if (key == "classid" && !prop.Values.empty()) { state.CurrentControl->ClassId = Upper(prop.Values[0]); }
    else if (key == "position") { state.CurrentControl->Position.X = ReadIntOr(prop.Values, 0); state.CurrentControl->Position.Y = ReadIntOr(prop.Values, 1); }
    else if (key == "size") { state.CurrentControl->Size.X = ReadIntOr(prop.Values, 0); state.CurrentControl->Size.Y = ReadIntOr(prop.Values, 1); }
    else if (key == "font") { state.CurrentControl->Font = ReadIntOr(prop.Values, 0, -1); }
    else if (key == "image" && !prop.Values.empty()) { state.CurrentControl->Image = prop.Values[0]; }
    else if (key == "drawmethod" && !prop.Values.empty()) { state.CurrentControl->DrawMethod = Upper(prop.Values[0]); if (prop.Values.size() > 1) { state.CurrentControl->DrawSprite = prop.Values[1]; } }
    else if (key == "hidden") { state.CurrentControl->Hidden = ReadBool(prop.Values); }
    else if (key == "disabled") { state.CurrentControl->Disabled = ReadBool(prop.Values); }
    state.CurrentControl->Properties.push_back(prop);
}

void ApplyWindowProperty(FUiParseState& state, const FUiProperty& prop) {
    if (!state.CurrentWindow) { return; }
    std::string key = Lower(prop.Key);
    if (key == "windowname" && !prop.Values.empty()) { state.CurrentWindow->Name = prop.Values[0]; }
    else if (key == "windowtext" && !prop.Values.empty()) { state.CurrentWindow->Text = JoinValues(prop.Values); }
    else if (key == "position") { state.CurrentWindow->Position.X = ReadIntOr(prop.Values, 0); state.CurrentWindow->Position.Y = ReadIntOr(prop.Values, 1); }
    else if (key == "size") { state.CurrentWindow->Size.X = ReadIntOr(prop.Values, 0); state.CurrentWindow->Size.Y = ReadIntOr(prop.Values, 1); }
    else if (key == "font") { state.CurrentWindow->Font = ReadIntOr(prop.Values, 0, -1); }
    else if (key == "drawmethod" && !prop.Values.empty()) { state.CurrentWindow->DrawMethod = Upper(prop.Values[0]); if (prop.Values.size() > 1) { state.CurrentWindow->DrawSprite = prop.Values[1]; } }
    state.CurrentWindow->Properties.push_back(prop);
}

void ApplyProperty(FUiParseState& state, const FUiProperty& prop) {
    EUiBlock block = state.Stack.empty() ? EUiBlock::Root : state.Stack.back();
    if (block == EUiBlock::Sprite) { ApplySpriteProperty(state, prop); }
    else if (block == EUiBlock::Control) { ApplyControlProperty(state, prop); }
    else if (block == EUiBlock::Window) { ApplyWindowProperty(state, prop); }
}

FUiDocumentStats MakeStats(const FUiDocument& doc) { return doc.Stats(); }
}

FUiDocumentStats FUiDocument::Stats() const {
    FUiDocumentStats stats;
    stats.LogicalName = LogicalName;
    stats.ByteSize = ByteSize;
    stats.Loaded = ByteSize > 0;
    stats.TextLike = true;
    stats.Parsed = true;
    stats.WasCompressed = WasCompressed;
    stats.WindowCount = Windows.size();
    stats.SpriteCount = GlobalSprites.size();
    stats.TextureNameCount = TextureNames.size();
    for (const auto& sprite : GlobalSprites) { stats.TextureSliceCount += sprite.TextureSlices.size(); }
    for (const auto& window : Windows) {
        stats.ControlCount += window.Controls.size();
        stats.SpriteCount += window.Sprites.size();
        for (const auto& sprite : window.Sprites) { stats.TextureSliceCount += sprite.TextureSlices.size(); }
    }
    stats.WarningCount = Warnings.size();
    return stats;
}

const FUiWindow* FUiDocument::FindWindowByName(std::string_view name) const {
    for (const auto& window : Windows) { if (EqualsNoCase(window.Name, name)) { return &window; } }
    return nullptr;
}

TResult<FUiDocument> FUiResourceDocument::ParseText(std::string_view logicalName, std::string_view sourcePath, const FByteArray& bytes, bool wasCompressed) {
    if (!IsMostlyText(bytes)) { return FStatus::Error(EStatusCode::Unsupported, "UI resource is not text-like: " + std::string(logicalName)); }
    FUiParseState state;
    state.Document.LogicalName = std::string(logicalName);
    state.Document.SourcePath = std::string(sourcePath);
    state.Document.ByteSize = bytes.size();
    state.Document.WasCompressed = wasCompressed;
    std::string text = BytesToText(bytes);
    std::istringstream stream(text);
    std::string line;
    int32 lineNo = 0;
    while (std::getline(stream, line)) {
        ++lineNo;
        std::string cleaned = RemoveLineComment(line);
        if (cleaned.empty()) { continue; }
        std::vector<std::string> tokens = TokenizeLine(cleaned);
        if (tokens.empty()) { continue; }
        size_t index = 0;
        while (index < tokens.size()) {
            const std::string& token = tokens[index];
            if (token == "}") { CloseBlock(state, lineNo); ++index; continue; }
            if (token == "{") {
                if (!state.HasPending) { state.Document.Warnings.push_back("anonymous opening brace at line " + std::to_string(lineNo)); ++index; continue; }
                StartBlock(state, state.Pending, lineNo);
                state.HasPending = false;
                ++index;
                continue;
            }
            EUiBlock block = BlockFromToken(token);
            if (block != EUiBlock::Root) {
                if (index + 1 < tokens.size() && tokens[index + 1] == "{") { StartBlock(state, block, lineNo); index += 2; continue; }
                state.Pending = block;
                state.HasPending = true;
                ++index;
                continue;
            }
            std::vector<std::string> propTokens(tokens.begin() + static_cast<std::ptrdiff_t>(index), tokens.end());
            ApplyProperty(state, MakeProperty(propTokens, lineNo));
            break;
        }
    }
    if (state.Stack.size() > 1) { state.Document.Warnings.push_back("UI parse finished with unclosed block: " + std::string(BlockName(state.Stack.back()))); }
    return state.Document;
}

FStatus FUiResourceDocument::LoadFromResource(const FResourceManager& resources, std::string_view logicalName, FLogger* logger) {
    auto blob = resources.Load(logicalName);
    if (!blob.IsOk()) { return blob.Status(); }
    auto parsed = ParseText(blob.Value().Id.LogicalName, blob.Value().SourcePath.string(), blob.Value().Bytes, blob.Value().WasCompressed);
    if (!parsed.IsOk()) { return parsed.Status(); }
    ParsedDocument = std::move(parsed.Value());
    ParsedStats = ParsedDocument.Stats();
    if (logger) {
        logger->Info("UI document parsed: " + ParsedStats.LogicalName + ", windows=" + std::to_string(ParsedStats.WindowCount) + ", controls=" + std::to_string(ParsedStats.ControlCount) + ", sprites=" + std::to_string(ParsedStats.SpriteCount) + ", texture_slices=" + std::to_string(ParsedStats.TextureSliceCount) + ", texture_names=" + std::to_string(ParsedStats.TextureNameCount) + ", warnings=" + std::to_string(ParsedStats.WarningCount));
        size_t shown = 0;
        for (const auto& window : ParsedDocument.Windows) { logger->Info("UI window: file=" + ParsedStats.LogicalName + ", name=" + window.Name + ", size=" + std::to_string(window.Size.X) + "x" + std::to_string(window.Size.Y) + ", controls=" + std::to_string(window.Controls.size()) + ", sprites=" + std::to_string(window.Sprites.size())); if (++shown >= 4) { break; } }
    }
    return FStatus::Ok();
}

FStatus FUiResourceSystem::BuildManifest(const FResourceManager& resources, FLogger* logger) {
    UiManifest = FUiResourceManifest{};
    std::set<std::string> textureNames;
    for (const auto& record : resources.Catalog().All()) {
        std::string ext = Lower(record.RelativePath.extension().string());
        if (ext != ".ui") { continue; }
        ++UiManifest.DocumentCount;
        std::string logical = record.RelativePath.generic_string();
        auto loaded = resources.Load(logical);
        if (!loaded.IsOk()) { continue; }
        ++UiManifest.LoadedCount;
        auto parsed = FUiResourceDocument::ParseText(loaded.Value().Id.LogicalName, loaded.Value().SourcePath.string(), loaded.Value().Bytes, loaded.Value().WasCompressed);
        if (!parsed.IsOk()) { continue; }
        ++UiManifest.ParsedCount;
        FUiDocument doc = std::move(parsed.Value());
        FUiDocumentStats stats = doc.Stats();
        UiManifest.WindowCount += stats.WindowCount;
        UiManifest.ControlCount += stats.ControlCount;
        UiManifest.SpriteCount += stats.SpriteCount;
        UiManifest.TextureSliceCount += stats.TextureSliceCount;
        for (const auto& name : doc.TextureNames) { textureNames.insert(name); }
        for (const auto& window : doc.Windows) { for (const auto& control : window.Controls) { if (!control.ClassId.empty()) { ++UiManifest.ClassIdCounts[control.ClassId]; } } }
        UiManifest.Documents.push_back(std::move(stats));
    }
    UiManifest.TextureNameCount = textureNames.size();
    if (logger) {
        logger->Info("UI resource manifest: documents=" + std::to_string(UiManifest.DocumentCount) + ", loaded=" + std::to_string(UiManifest.LoadedCount) + ", parsed=" + std::to_string(UiManifest.ParsedCount) + ", windows=" + std::to_string(UiManifest.WindowCount) + ", controls=" + std::to_string(UiManifest.ControlCount) + ", sprites=" + std::to_string(UiManifest.SpriteCount) + ", texture_slices=" + std::to_string(UiManifest.TextureSliceCount) + ", texture_names=" + std::to_string(UiManifest.TextureNameCount));
        std::vector<std::pair<std::string, size_t>> classes(UiManifest.ClassIdCounts.begin(), UiManifest.ClassIdCounts.end());
        std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        size_t shown = 0;
        for (const auto& item : classes) { logger->Info("UI class usage: " + item.first + "=" + std::to_string(item.second)); if (++shown >= 12) { break; } }
    }
    return FStatus::Ok();
}

TResult<FUiDocument> FUiResourceSystem::LoadDocument(const FResourceManager& resources, std::string_view logicalName, FLogger* logger) const {
    FUiResourceDocument document;
    FStatus status = document.LoadFromResource(resources, logicalName, logger);
    if (!status.IsOk()) { return status; }
    return document.Document();
}
}
