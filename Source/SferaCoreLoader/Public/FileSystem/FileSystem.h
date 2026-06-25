#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"

struct FFileRecord 
{
    FPath AbsolutePath;
    FPath RelativePath;
    uint64 Size = 0;
};

class FFileSystem
{
public:
    explicit FFileSystem(FPath root = {});
    void SetRoot(FPath root);
    const FPath& GetRoot() const { return Root; }
    void BuildCatalog(FLogger* logger = nullptr);
    std::optional<FPath> Resolve(std::string_view logicalPath) const;
    TResult<FByteArray> ReadBytes(std::string_view logicalPath) const;
    TResult<std::string> ReadText(std::string_view logicalPath) const;
    const std::vector<FFileRecord>& GetFiles() const { return Files; }
private:
    FPath Root;
    std::vector<FFileRecord> Files;
    std::unordered_map<std::string, size_t> Lookup;
};
