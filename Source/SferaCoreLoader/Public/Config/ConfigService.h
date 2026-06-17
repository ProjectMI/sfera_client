#pragma once
#include "Config/ConfigDocument.h"
#include "FileSystem/FileSystem.h"
#include <unordered_map>

namespace Sfera {
class FConfigService {
public:
    explicit FConfigService(const FFileSystem& fileSystem);
    FStatus LoadKnownConfigs(FLogger* logger = nullptr);
    FStatus LoadConfig(std::string logicalPath);
    const FConfigDocument* FindConfig(std::string_view logicalPath) const;
    std::optional<std::string> FindString(std::string_view key) const;
    const std::unordered_map<std::string, FConfigDocument>& DocumentsByName() const { return Documents; }
private:
    const FFileSystem& FileSystem;
    std::unordered_map<std::string, FConfigDocument> Documents;
};
}
