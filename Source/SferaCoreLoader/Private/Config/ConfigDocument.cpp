#include "Config/ConfigDocument.h"
#include "Core/NumericParse.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace Sfera {
static std::string Trim(std::string s) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

static std::string StripComment(std::string line) {
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') { quoted = !quoted; }
        if (!quoted && line[i] == ';') { return line.substr(0, i); }
        if (!quoted && line[i] == '#') { return line.substr(0, i); }
        if (!quoted && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') { return line.substr(0, i); }
    }
    return line;
}

static std::string ScopeText(const std::vector<std::string>& scope) {
    std::string out;
    for (size_t i = 0; i < scope.size(); ++i) { out += (i ? "." : "") + scope[i]; }
    return out;
}

static void StripTrailingComma(std::string& line) {
    line = Trim(line);
    while (!line.empty() && line.back() == ',') { line.pop_back(); line = Trim(line); }
}

static std::pair<std::string, std::string> SplitTypeTag(std::string key) {
    key = Trim(std::move(key));
    size_t begin = key.find('<');
    size_t end = begin == std::string::npos ? std::string::npos : key.find('>', begin + 1);
    if (begin != std::string::npos && end != std::string::npos && end > begin) {
        std::string type = key.substr(begin + 1, end - begin - 1);
        key.erase(begin, end - begin + 1);
        return {Trim(std::move(key)), Trim(std::move(type))};
    }
    return {Trim(std::move(key)), {}};
}

static std::string Unquote(std::string value) {
    value = Trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) { return value.substr(1, value.size() - 2); }
    return value;
}

FStatus FConfigDocument::Parse(std::string text, std::string sourceName) {
    Source = std::move(sourceName);
    ParsedEntries.clear();
    Index.clear();

    std::vector<std::string> scope;
    std::unordered_map<size_t, std::string> arrayContextByDepth;
    std::unordered_map<std::string, uint32> arrayCounters;
    std::optional<std::string> pendingArrayName;

    auto addEntry = [&](std::string key, std::string typeTag, std::string value, size_t lineNo, std::string rawLine) {
        if (key.empty()) { return; }
        std::string scopeText = ScopeText(scope);
        FConfigEntry entry{std::move(key), Unquote(std::move(value)), scopeText, std::move(typeTag), std::move(rawLine), lineNo};
        std::string lookup = Lower(scopeText.empty() ? entry.Key : scopeText + "." + entry.Key);
        Index[lookup] = ParsedEntries.size();
        Index[Lower(entry.Key)] = ParsedEntries.size();
        ParsedEntries.push_back(std::move(entry));
    };

    auto openAnonymousBlock = [&]() {
        size_t depth = scope.size();
        std::string name;
        if (pendingArrayName) { name = *pendingArrayName; arrayContextByDepth[depth] = name; pendingArrayName.reset(); }
        else {
            auto it = arrayContextByDepth.find(depth);
            name = it == arrayContextByDepth.end() ? "block" : it->second;
        }
        std::string parent = ScopeText(scope);
        std::string counterKey = parent + "/" + name;
        uint32 index = arrayCounters[counterKey]++;
        scope.push_back(name + "[" + std::to_string(index) + "]");
    };

    std::istringstream input(text);
    std::string line;
    size_t lineNo = 0;
    while (std::getline(input, line)) {
        ++lineNo;
        std::string rawLine = line;
        line = Trim(StripComment(line));
        StripTrailingComma(line);
        if (line.empty()) { continue; }

        if (line == "}" || line == "]") {
            if (!scope.empty()) { scope.pop_back(); }
            pendingArrayName.reset();
            continue;
        }
        if (line == "{") { openAnonymousBlock(); continue; }
        if (line.front() == '[' && line.back() == ']') { scope.clear(); pendingArrayName.reset(); scope.push_back(Trim(line.substr(1, line.size() - 2))); continue; }

        bool opensBlock = false;
        if (!line.empty() && line.back() == '{') { opensBlock = true; line = Trim(line.substr(0, line.size() - 1)); StripTrailingComma(line); }

        size_t split = line.find('=');
        if (split == std::string::npos) { split = line.find(':'); }
        if (split == std::string::npos) { split = line.find_first_of(" \t"); }

        std::string keyPart = split == std::string::npos ? line : Trim(line.substr(0, split));
        std::string value = split == std::string::npos ? std::string() : Trim(line.substr(split + 1));
        auto [key, typeTag] = SplitTypeTag(std::move(keyPart));
        if (key.empty()) { continue; }

        addEntry(key, typeTag, value, lineNo, rawLine);

        bool arrayLike = !typeTag.empty() && typeTag.find('a') != std::string::npos;
        if (arrayLike || value.empty() || value == "{") { pendingArrayName = key; arrayContextByDepth[scope.size()] = key; }
        if (opensBlock) { openAnonymousBlock(); }
    }
    return FStatus::Ok();
}

std::optional<std::string> FConfigDocument::FindString(std::string_view key) const {
    auto it = Index.find(Lower(std::string(key)));
    if (it == Index.end()) { return std::nullopt; }
    return ParsedEntries[it->second].Value;
}

std::optional<int64> FConfigDocument::FindInt(std::string_view key) const {
    auto value = FindString(key);
    if (!value) { return std::nullopt; }
    int64 parsed = 0;
    return NumericParse::TryParseInt64Strict(*value, parsed) ? std::optional<int64>(parsed) : std::nullopt;
}
}
