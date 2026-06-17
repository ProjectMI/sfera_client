#pragma once
#include "SferaBase.h"
#include "SferaResourceTypes.h"
#include <initializer_list>

class SferaResourceManager;

struct SferaUiControlFamily
{
    std::string Name;
    std::string SourceFile;
    std::vector<std::string> ResourceKeys;
    int FunctionCount = 0;
};

struct SferaUiStartupBinding
{
    std::string LogicalPath;
    ESferaResourceKind Kind = ESferaResourceKind::Unknown;
    std::string Purpose;
};

class SferaUiControlCatalog
{
public:
    void Build(const SferaResourceManager& Resources);
    void Clear();
    const std::vector<SferaUiControlFamily>& GetFamilies() const { return Families; }
    const std::vector<SferaUiStartupBinding>& GetStartupBindings() const { return StartupBindings; }
    std::string BuildSummaryText() const;

private:
    void AddFamily(const char* Name, const char* SourceFile, std::initializer_list<const char*> Keys, int FunctionCount);
    void BindResources(const SferaResourceManager& Resources);
    void AddBinding(const SferaResourceRecord& Record, const std::string& Purpose);
    std::string ClassifyUiResource(const SferaResourceRecord& Record) const;

private:
    std::vector<SferaUiControlFamily> Families;
    std::vector<SferaUiStartupBinding> StartupBindings;
};
