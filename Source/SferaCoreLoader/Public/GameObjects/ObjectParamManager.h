#pragma once
#include "Config/ConfigDocument.h"
#include "GameObjects/GameObjectTypes.h"
#include "ResourceLoader/ResourceManager.h"
#include <unordered_map>

enum class EObjectParamSourceKind 
{
    None,
    ExplicitDocument,
    ModelParamTable,
    EntityParamFile,
    GroupTable
};

struct FObjectParamSource 
{
    std::string LogicalName;
    EObjectParamSourceKind Kind = EObjectParamSourceKind::None;
    size_t EntryCount = 0;
    size_t IndexedObjectsBefore = 0;
    size_t IndexedObjectsAfter = 0;
    size_t IndexedFields = 0;
    bool Loaded = false;
};

class FObjectParamManager 
{
public:
    FStatus Open(const FResourceManager& resources, std::string_view logicalName, FLogger* logger = nullptr);
    FStatus OpenKnownConfigs(const FResourceManager& resources, FLogger* logger = nullptr);
    std::optional<FObjectParamValue> GetParam(std::string_view objectName, std::string_view paramName) const;
    std::optional<FGameObjectDescriptor> GetDescriptor(std::string_view objectName) const;
    const std::vector<FObjectParamSource>& Sources() const { return LoadedSources; }
    size_t ConfigCount() const { return Documents.size(); }
    size_t ObjectCount() const { return Objects.size(); }
private:
    static std::string Normalize(std::string_view text);
    static std::string Trim(std::string text);
    static bool EndsWith(std::string_view text, std::string_view suffix);
    static bool StartsWith(std::string_view text, std::string_view prefix);
    static bool IsAsciiDigit(char c);
    static bool HasNumericSuffixAfterPrefix(std::string_view name, std::string_view prefix);
    static bool IsEntityConfigPath(std::string_view normalizedPath);
    static bool IsModelParamTable(std::string_view normalizedPath);
    static bool IsGroupTable(std::string_view normalizedPath);
    static EObjectParamSourceKind ClassifySourcePath(std::string_view normalizedPath);
    static std::string_view SourceKindName(EObjectParamSourceKind kind);
    static std::string BaseNameWithoutExtension(std::string_view path);
    static std::string ObjectKeyForEntry(const FConfigEntry& entry);
    static FObjectParamValue ParseValue(std::string value);
    static std::vector<std::string> TokenizeRow(std::string_view row);
    static size_t IndexFlatRow(FGameObjectDescriptor& descriptor, std::string_view row);
    FObjectParamSource IndexDocument(const FConfigDocument& doc, EObjectParamSourceKind kind, FLogger* logger);
    std::vector<FConfigDocument> Documents;
    std::vector<FObjectParamSource> LoadedSources;
    std::unordered_map<std::string, FGameObjectDescriptor> Objects;
};