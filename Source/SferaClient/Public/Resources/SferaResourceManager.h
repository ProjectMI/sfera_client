#pragma once
#include "SferaBase.h"
#include "SferaResourceTypes.h"

class SferaResourceManager
{
public:
    bool Initialize(const std::string& RootDirectory);
    void Shutdown();

    bool ScanBootstrapResources();
    bool ScanDirectory(const std::string& RelativeDirectory, bool bRecursive);
    bool RegisterExpectedResource(const std::string& LogicalPath, ESferaResourceKind Kind, bool bCritical);

    const std::string& GetRootDirectory() const { return Root; }
    const std::vector<SferaResourceRecord>& GetResources() const { return Resources; }
    const std::vector<std::string>& GetWarnings() const { return Warnings; }
    const SferaResourceStats& GetStats() const { return Stats; }

    bool ResolvePath(const std::string& LogicalPath, std::string& OutDiskPath) const;
    const SferaResourceRecord* FindResource(const std::string& LogicalPath) const;
    std::vector<const SferaResourceRecord*> FindByKind(ESferaResourceKind Kind) const;
    std::vector<const SferaResourceRecord*> FindByWildcard(const std::string& Wildcard) const;

    bool LoadTextFile(const std::string& LogicalPath, std::string& OutText) const;
    bool ReadBinaryFile(const std::string& LogicalPath, SferaByteBuffer& OutBytes) const;

    static ESferaResourceKind ClassifyLogicalPath(const std::string& LogicalPath);

private:
    void ResetCatalog();
    void AddOrUpdateRecord(const SferaResourceRecord& Record);
    void AddDiskFile(const std::string& DiskPath, const std::string& LogicalPath);
    void RebuildStats();
    std::string MakeDiskPath(const std::string& LogicalPath) const;

private:
    std::string Root = ".";
    std::vector<SferaResourceRecord> Resources;
    std::unordered_map<std::string, size_t> ResourceIndexByLowerPath;
    std::vector<std::string> Warnings;
    SferaResourceStats Stats;
};
