#include "Config/ConfigDocument.h"
#include <algorithm>
#include <cctype>
#include <sstream>

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

FStatus FConfigDocument::Parse(std::string text, std::string sourceName) {
    Source = std::move(sourceName);
    ParsedEntries.clear();
    Index.clear();
    std::vector<std::string> scope;
    std::istringstream input(text);
    std::string line;
    size_t lineNo = 0;
    while (std::getline(input, line)) {
        ++lineNo;
        line = Trim(StripComment(line));
        if (line.empty()) { continue; }
        if (line == "}" || line == "]") { if (!scope.empty()) { scope.pop_back(); } continue; }
        if (line.front() == '[' && line.back() == ']') { scope.clear(); scope.push_back(Trim(line.substr(1, line.size() - 2))); continue; }
        bool opensBlock = line.back() == '{';
        if (opensBlock) { line = Trim(line.substr(0, line.size() - 1)); }
        size_t split = line.find('=');
        if (split == std::string::npos) { split = line.find(':'); }
        if (split == std::string::npos) { split = line.find_first_of(" \t"); }
        std::string key = split == std::string::npos ? line : Trim(line.substr(0, split));
        std::string value = split == std::string::npos ? std::string() : Trim(line.substr(split + 1));
        if (key.empty()) { continue; }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') { value = value.substr(1, value.size() - 2); }
        std::string scopeText;
        for (size_t i = 0; i < scope.size(); ++i) { scopeText += (i ? "." : "") + scope[i]; }
        FConfigEntry entry{key, value, scopeText, lineNo};
        std::string lookup = Lower(scopeText.empty() ? key : scopeText + "." + key);
        Index[lookup] = ParsedEntries.size();
        Index[Lower(key)] = ParsedEntries.size();
        ParsedEntries.push_back(std::move(entry));
        if (opensBlock) { scope.push_back(key); }
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
    try { return std::stoll(*value); } catch (...) { return std::nullopt; }
}
}
