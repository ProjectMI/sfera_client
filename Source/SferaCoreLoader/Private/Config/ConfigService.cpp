#include "Config/ConfigService.h"
#include "FileSystem/PathUtils.h"

namespace Sfera {
FConfigService::FConfigService(const FFileSystem& fileSystem) : FileSystem(fileSystem) {}

FStatus FConfigService::LoadKnownConfigs(FLogger* logger) {
    const char* names[] = {"config.cfg", "connect.cfg", "connectn.cfg", "debug.cfg", "control.cfg", "fonts.cfg", "Models/Materials.cfg", "Landscape/zoning.cfg", "Landscape/zoningHaron.cfg"};
    for (const char* name : names) {
        FStatus status = LoadConfig(name);
        if (logger && status.IsOk()) { logger->Info(std::string("loaded cfg: ") + name); }
    }
    return FStatus::Ok();
}

FStatus FConfigService::LoadConfig(std::string logicalPath) {
    auto text = FileSystem.ReadText(logicalPath);
    if (!text.IsOk()) { return text.Status(); }
    FConfigDocument doc;
    FStatus status = doc.Parse(text.Value(), logicalPath);
    if (!status.IsOk()) { return status; }
    Documents[PathUtils::NormalizeForLookup(logicalPath)] = std::move(doc);
    return FStatus::Ok();
}

const FConfigDocument* FConfigService::FindConfig(std::string_view logicalPath) const {
    auto it = Documents.find(PathUtils::NormalizeForLookup(FPath{std::string(logicalPath)}));
    return it == Documents.end() ? nullptr : &it->second;
}

std::optional<std::string> FConfigService::FindString(std::string_view key) const {
    for (const auto& pair : Documents) { if (auto value = pair.second.FindString(key)) { return value; } }
    return std::nullopt;
}
}
