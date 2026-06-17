#pragma once
#include "Core/Types.h"
#include <unordered_map>

namespace Sfera {
struct FConfigEntry { std::string Key; std::string Value; std::string Scope; size_t Line = 0; };

class FConfigDocument {
public:
    FStatus Parse(std::string text, std::string sourceName);
    std::optional<std::string> FindString(std::string_view key) const;
    std::optional<int64> FindInt(std::string_view key) const;
    const std::vector<FConfigEntry>& Entries() const { return ParsedEntries; }
    const std::string& SourceName() const { return Source; }
private:
    std::string Source;
    std::vector<FConfigEntry> ParsedEntries;
    std::unordered_map<std::string, size_t> Index;
};
}
