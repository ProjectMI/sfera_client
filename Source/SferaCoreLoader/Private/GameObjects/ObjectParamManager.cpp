#include "GameObjects/ObjectParamManager.h"
#include "Common/StringUtils.h"
#include "Core/NumericParse.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool IsAsciiDigit(char ch) { return ch >= '0' && ch <= '9'; }
bool IsAsciiAlpha(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }

bool HasNumericSuffixAfterPrefix(std::string_view name, std::string_view prefix)
{
    if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix || !IsAsciiDigit(name[prefix.size()])) { return false; }
    size_t i = prefix.size();
    while (i < name.size() && IsAsciiDigit(name[i])) { ++i; }
    return i == name.size() || name[i] == '_' || name[i] == '-' || IsAsciiAlpha(name[i]);
}

bool IsQuoted(std::string_view value)
{
    return value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''));
}

std::vector<std::string> TokenizeRow(std::string_view line)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char ch : line)
    {
        if (ch == '"') { quoted = !quoted; cur.push_back(ch); continue; }
        const bool separator = !quoted && (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == ';');
        if (separator) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } continue; }
        cur.push_back(ch);
    }
    if (!cur.empty()) { out.push_back(cur); }
    return out;
}
}

FStatus FObjectParamManager::Open(const FResourceManager& resources, std::string_view logicalName, FLogger* logger)
{
    auto blob = resources.Load(logicalName);

    if (!blob.IsOk()) { return blob.Status(); }

    std::string text(blob.Value().Bytes.begin(), blob.Value().Bytes.end());
    FConfigDocument doc;
    FStatus status = doc.Parse(std::move(text), std::string(logicalName));

    if (!status.IsOk()) { return status; }

    EObjectParamSourceKind kind = ClassifySourcePath(Normalize(logicalName));

    if (kind == EObjectParamSourceKind::None)
    {
        kind = EObjectParamSourceKind::ExplicitDocument;
    }

    LoadedSources.push_back(IndexDocument(doc, kind, logger));
    Documents.push_back(std::move(doc));
    return FStatus::Ok();
}

FStatus FObjectParamManager::OpenKnownConfigs(const FResourceManager& resources, FLogger* logger)
{
    Documents.clear();
    Objects.clear();
    LoadedSources.clear();

    size_t manifestMatches = 0;
    size_t loaded = 0;
    size_t modelTables = 0;
    size_t entityFiles = 0;
    size_t groupTables = 0;

    for (const auto& item : resources.Catalog().All())
    {
        std::string logical = item.RelativePath.generic_string();
        std::string lower = Normalize(logical);
        EObjectParamSourceKind kind = ClassifySourcePath(lower);

        if (kind == EObjectParamSourceKind::None) { continue; }

        ++manifestMatches;

        auto blob = resources.Load(logical);

        if (!blob.IsOk()) { continue; }

        std::string text(blob.Value().Bytes.begin(), blob.Value().Bytes.end());
        FConfigDocument doc;
        FStatus status = doc.Parse(std::move(text), logical);

        if (!status.IsOk()) { continue; }

        FObjectParamSource source = IndexDocument(doc, kind, nullptr);
        LoadedSources.push_back(std::move(source));
        Documents.push_back(std::move(doc));
        ++loaded;

        switch (kind)
        {
        case EObjectParamSourceKind::ModelParamTable: ++modelTables;
            break;
        case EObjectParamSourceKind::EntityParamFile: ++entityFiles;
            break;
        case EObjectParamSourceKind::GroupTable: ++groupTables;
            break;
        default: break;
        }
    }

    std::sort(LoadedSources.begin(), LoadedSources.end(), [](const FObjectParamSource& a, const FObjectParamSource& b)
    {
        if (a.Kind != b.Kind) { return static_cast<int>(a.Kind) < static_cast<int>(b.Kind); }

        return a.LogicalName < b.LogicalName;
    });

    if (logger)
    {
        logger->Info("GameObjects config registry: manifest_matches=" + std::to_string(manifestMatches) +
        ", loaded=" + std::to_string(loaded) +
        ", model_tables=" + std::to_string(modelTables) +
        ", entity_param_files=" + std::to_string(entityFiles) +
        ", group_tables=" + std::to_string(groupTables) +
        ", indexed_objects=" + std::to_string(Objects.size()));
        size_t shown = 0;

        for (const auto& source : LoadedSources)
        {
            if (shown >= 12) { break; }

            logger->Info("GameObjects source[" + std::to_string(shown) + "]: kind=" + std::string(SourceKindName(source.Kind)) +
            ", entries=" + std::to_string(source.EntryCount) +
            ", fields=" + std::to_string(source.IndexedFields) +
            ", objects_delta=" + std::to_string(source.IndexedObjectsAfter - source.IndexedObjectsBefore) +
            ", file=" + source.LogicalName);
            ++shown;
        }

        if (LoadedSources.size() > shown)
        {
            logger->Info("GameObjects source log suppressed: " + std::to_string(LoadedSources.size() - shown) + " additional registry documents");
        }
    }

    return FStatus::Ok();
}

std::optional<FObjectParamValue> FObjectParamManager::GetParam(std::string_view objectName, std::string_view paramName) const
{
    auto it = Objects.find(Normalize(objectName));

    if (it == Objects.end()) { return std::nullopt; }

    auto paramIt = it->second.Params.find(Normalize(paramName));

    if (paramIt == it->second.Params.end()) { return std::nullopt; }

    return paramIt->second;
}

std::optional<FGameObjectDescriptor> FObjectParamManager::GetDescriptor(std::string_view objectName) const
{
    auto it = Objects.find(Normalize(objectName));

    if (it == Objects.end()) { return std::nullopt; }

    return it->second;
}

std::string FObjectParamManager::Normalize(std::string_view text) { return Common::NormalizePathKey(std::string(text)); }

bool FObjectParamManager::IsEntityConfigPath(std::string_view normalizedPath)
{
    if (!Common::EndsWith(normalizedPath, ".cfg") && !Common::EndsWith(normalizedPath, ".txt")) { return false; }

    if (normalizedPath.find("language/") == 0 || normalizedPath.find("landscape/") == 0 || normalizedPath.find("logs/") == 0) { return false; }

    std::string name = Common::BaseNameWithoutExtension(normalizedPath);
    bool entityName = HasNumericSuffixAfterPrefix(name, "npc") || HasNumericSuffixAfterPrefix(name, "char") ||
    HasNumericSuffixAfterPrefix(name, "crt") || HasNumericSuffixAfterPrefix(name, "mob") ||
    HasNumericSuffixAfterPrefix(name, "item") || HasNumericSuffixAfterPrefix(name, "obj");

    if (!entityName) { return false; }

    return StartsWith(normalizedPath, "params/") || normalizedPath.find('/') == std::string::npos;
}

bool FObjectParamManager::IsModelParamTable(std::string_view normalizedPath)
{
    return normalizedPath == "models/mdlparam.txt" || Common::BaseNameWithoutExtension(normalizedPath) == "mdlparam";
}

bool FObjectParamManager::IsGroupTable(std::string_view normalizedPath)
{
    if (!Common::EndsWith(normalizedPath, ".cfg") && !Common::EndsWith(normalizedPath, ".txt")) { return false; }

    std::string name = Common::BaseNameWithoutExtension(normalizedPath);
    return StartsWith(name, "group_") || name == "randbox" || name == "grouppref";
}

EObjectParamSourceKind FObjectParamManager::ClassifySourcePath(std::string_view normalizedPath)
{
    if (IsModelParamTable(normalizedPath)) { return EObjectParamSourceKind::ModelParamTable; }

    if (IsGroupTable(normalizedPath)) { return EObjectParamSourceKind::GroupTable; }

    if (IsEntityConfigPath(normalizedPath)) { return EObjectParamSourceKind::EntityParamFile; }

    return EObjectParamSourceKind::None;
}

std::string_view FObjectParamManager::SourceKindName(EObjectParamSourceKind kind)
{
    switch (kind)
    {
    case EObjectParamSourceKind::ExplicitDocument: return "explicit_object_param_document";
    case EObjectParamSourceKind::ModelParamTable: return "models_mdlparam_table";
    case EObjectParamSourceKind::EntityParamFile: return "entity_param_file";
    case EObjectParamSourceKind::GroupTable: return "group_table";
    default: return "none";
    }
}

std::string FObjectParamManager::ObjectKeyForEntry(const FConfigEntry& entry)
{
    std::string scope = Common::Trim(entry.Scope);

    if (!scope.empty())
    {
        size_t dot = scope.find_last_of('.');
        std::string leaf = dot == std::string::npos ? scope : scope.substr(dot + 1);

        if (!leaf.empty()) { return leaf; }
    }

    std::string key = Common::Trim(entry.Key);
    size_t dot = key.find_first_of('.');

    if (dot != std::string::npos && dot > 0) { return key.substr(0, dot); }

    size_t bracket = key.find('[');

    if (bracket != std::string::npos && bracket > 0) { return key.substr(0, bracket); }

    return {};
}

FObjectParamValue FObjectParamManager::ParseValue(std::string value)
{
    std::string trimmed = Common::Trim(std::move(value));

    if (IsQuoted(trimmed))
    {
        return FObjectParamValue::String(Common::Unquote(std::move(trimmed)));
    }

    bool hasFloatMarker = trimmed.find('.') != std::string::npos || trimmed.find('e') != std::string::npos || trimmed.find('E') != std::string::npos;

    if (hasFloatMarker)
    {
        float parsed = 0.0f;

        if (NumericParse::TryParseFloatStrict(trimmed, parsed))
        {
            return FObjectParamValue::Float(parsed);
        }
    }

    int32 parsedInt = 0;

    if (NumericParse::TryParseInt32Strict(trimmed, parsedInt)) { return FObjectParamValue::Int(parsedInt); }

    return FObjectParamValue::String(trimmed);
}

size_t FObjectParamManager::IndexFlatRow(FGameObjectDescriptor& descriptor, std::string_view row)
{
    size_t fields = 0;
    descriptor.Params["raw"] = FObjectParamValue::String(std::string(row));
    std::vector<std::string> tokens = TokenizeRow(row);

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        std::string token = tokens[i];
        size_t eq = token.find('=');

        if (eq != std::string::npos && eq > 0)
        {
            descriptor.Params[Normalize(token.substr(0, eq))] = ParseValue(token.substr(eq + 1));
            ++fields;
            continue;
        }

        if (i + 1 < tokens.size() && token.find('=') == std::string::npos && tokens[i + 1].find('=') == std::string::npos)
        {
            descriptor.Params[Normalize(token)] = ParseValue(tokens[i + 1]);
            ++fields;
            ++i;
            continue;
        }

        descriptor.Params["field_" + std::to_string(i)] = ParseValue(token);
        ++fields;
    }

    return fields;
}

FObjectParamSource FObjectParamManager::IndexDocument(const FConfigDocument& doc, EObjectParamSourceKind kind, FLogger* logger)
{
    FObjectParamSource source;
    source.LogicalName = doc.SourceName();
    source.Kind = kind;
    source.EntryCount = doc.Entries().size();
    source.IndexedObjectsBefore = Objects.size();
    source.Loaded = true;

    size_t fields = 0;

    if (kind == EObjectParamSourceKind::ModelParamTable)
    {
        for (const auto& entry : doc.Entries())
        {
            std::string name = Common::Trim(entry.Key);

            if (name.empty()) { continue; }

            auto& descriptor = Objects[Normalize(name)];

            if (descriptor.Archetype.empty())
            {
                descriptor.Archetype = name;
                descriptor.SourceConfig = doc.SourceName();
            }

            descriptor.Params["source_kind"] = FObjectParamValue::String("models_mdlparam_row");
            fields += IndexFlatRow(descriptor, entry.Value);
        }
    }
    else if (kind == EObjectParamSourceKind::GroupTable)
    {
        for (const auto& entry : doc.Entries())
        {
            std::vector<std::string> tokens = TokenizeRow(entry.Value);
            std::string objectName = tokens.empty() ? ("group_row_" + entry.Key) : tokens[0];

            if (objectName.empty()) { continue; }

            auto& descriptor = Objects[Normalize(objectName)];

            if (descriptor.Archetype.empty())
            {
                descriptor.Archetype = objectName;
                descriptor.SourceConfig = doc.SourceName();
            }

            descriptor.Params["source_kind"] = FObjectParamValue::String("group_table_row");
            descriptor.Params["group_table"] = FObjectParamValue::String(doc.SourceName());
            descriptor.Params["group_row_id"] = ParseValue(entry.Key);
            descriptor.Params["raw"] = FObjectParamValue::String(entry.Value);

            for (size_t i = 0; i < tokens.size(); ++i)
            {
                descriptor.Params["field_" + std::to_string(i)] = ParseValue(tokens[i]);
                ++fields;
            }
        }
    }
    else
    {
        for (const auto& entry : doc.Entries())
        {
            std::string objectKey = ObjectKeyForEntry(entry);

            if (objectKey.empty()) { continue; }

            std::string normalizedObject = Normalize(objectKey);

            if (normalizedObject == "global" || normalizedObject == "material" || normalizedObject == "materials") { continue; }

            auto& descriptor = Objects[normalizedObject];

            if (descriptor.Archetype.empty())
            {
                descriptor.Archetype = objectKey;
                descriptor.SourceConfig = doc.SourceName();
            }

            std::string paramName = entry.Key;
            size_t dot = paramName.find_first_of('.');

            if (dot != std::string::npos && dot + 1 < paramName.size())
            {
                paramName = paramName.substr(dot + 1);
            }

            descriptor.Params[Normalize(paramName)] = ParseValue(entry.Value);
            ++fields;
        }

        if (kind == EObjectParamSourceKind::EntityParamFile)
        {
            std::string objectName = Common::BaseNameWithoutExtension(Normalize(doc.SourceName()));
            auto& descriptor = Objects[Normalize(objectName)];

            if (descriptor.Archetype.empty())
            {
                descriptor.Archetype = objectName;
                descriptor.SourceConfig = doc.SourceName();
            }

            descriptor.Params["source_kind"] = FObjectParamValue::String("params_entity_file");

            for (const auto& entry : doc.Entries())
            {
                std::string key = entry.Scope.empty() ? entry.Key : entry.Scope + "." + entry.Key;
                descriptor.Params[Normalize(key)] = ParseValue(entry.Value);
                ++fields;
            }
        }
    }

    source.IndexedFields = fields;
    source.IndexedObjectsAfter = Objects.size();

    if (logger)
    {
        logger->Info("GameObjects indexed " + std::string(SourceKindName(kind)) + ": " + doc.SourceName() +
        ", entries=" + std::to_string(source.EntryCount) +
        ", fields=" + std::to_string(source.IndexedFields) +
        ", new_objects=" + std::to_string(source.IndexedObjectsAfter - source.IndexedObjectsBefore));
    }

    return source;
}
