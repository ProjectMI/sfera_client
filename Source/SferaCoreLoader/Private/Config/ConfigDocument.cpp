#include "Config/ConfigDocument.h"
#include "Common/StringUtils.h"
#include "Core/NumericParse.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>



static std::string StripConfigComment(std::string line)
{
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '"') { quoted = !quoted; }
        if (!quoted && (line[i] == ';' || line[i] == '#')) { line.resize(i); break; }
        if (!quoted && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') { line.resize(i); break; }
    }
    return line;
}

static void TrimTrailingComma(std::string& value)
{
    value = Common::Trim(std::move(value));
    while (!value.empty() && value.back() == ',')
    {
        value.pop_back();
        value = Common::Trim(std::move(value));
    }
}

static std::string ScopeText(const std::vector<std::string>& scope) { return Common::Join(scope, "."); }

static std::pair<std::string, std::string> SplitTypeTag(std::string key)
{
    key = Common::Trim(std::move(key));
    size_t begin = key.find('<');
    size_t end = begin == std::string::npos ? std::string::npos : key.find('>', begin + 1);

    if (begin != std::string::npos && end != std::string::npos && end > begin)
    {
        std::string type = key.substr(begin + 1, end - begin - 1);
        key.erase(begin, end - begin + 1);
        return
        {
            Common::Trim(std::move(key)), Common::Trim(std::move(type))
        };
    }

    return
    {
        Common::Trim(std::move(key)), {}
    };
}


FStatus FConfigDocument::Parse(std::string text, std::string sourceName)
{
    Source = std::move(sourceName);
    ParsedEntries.clear();
    Index.clear();

    std::vector<std::string> scope;
    std::unordered_map<size_t, std::string> arrayContextByDepth;
    std::unordered_map<std::string, uint32> arrayCounters;
    std::optional<std::string> pendingArrayName;

    auto addEntry = [&](std::string key, std::string typeTag, std::string value, size_t lineNo, std::string rawLine)
    {
        if (key.empty()) { return; }

        std::string scopeText = ScopeText(scope);
        FConfigEntry entry
        {
            std::move(key), Common::Unquote(std::move(value)), scopeText, std::move(typeTag), std::move(rawLine), lineNo
        };
        std::string lookup = Common::ToLower(scopeText.empty() ? entry.Key : scopeText + "." + entry.Key);
        Index[lookup] = ParsedEntries.size();
        Index[Common::ToLower(entry.Key)] = ParsedEntries.size();
        ParsedEntries.push_back(std::move(entry));
    };

    auto openAnonymousBlock = [&]()
    {
        size_t depth = scope.size();
        std::string name;

        if (pendingArrayName)
        {
            name = *pendingArrayName;
            arrayContextByDepth[depth] = name;
            pendingArrayName.reset();
        }
        else
        {
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

    while (std::getline(input, line))
    {
        ++lineNo;
        std::string rawLine = line;
        line = Common::Trim(StripConfigComment(line));
        TrimTrailingComma(line);

        if (line.empty()) { continue; }

        if (line == "}" || line == "]")
        {
            if (!scope.empty())
            {
                scope.pop_back();
            }

            pendingArrayName.reset();
            continue;
        }

        if (line == "{") { openAnonymousBlock(); continue; }

        if (line.front() == '[' && line.back() == ']') { scope.clear(); pendingArrayName.reset(); scope.push_back(Common::Trim(line.substr(1, line.size() - 2))); continue; }

        bool opensBlock = false;

        if (!line.empty() && line.back() == '{')
        {
            opensBlock = true;
            line = Common::Trim(line.substr(0, line.size() - 1));
            TrimTrailingComma(line);
        }

        size_t split = line.find('=');

        if (split == std::string::npos)
        {
            split = line.find(':');
        }

        if (split == std::string::npos)
        {
            split = line.find_first_of(" \t");
        }

        std::string keyPart = split == std::string::npos ? line : Common::Trim(line.substr(0, split));
        std::string value = split == std::string::npos ? std::string() : Common::Trim(line.substr(split + 1));
        auto [key, typeTag] = SplitTypeTag(std::move(keyPart));

        if (key.empty()) { continue; }

        addEntry(key, typeTag, value, lineNo, rawLine);

        bool arrayLike = !typeTag.empty() && typeTag.find('a') != std::string::npos;

        if (arrayLike || value.empty() || value == "{")
        {
            pendingArrayName = key;
            arrayContextByDepth[scope.size()] = key;
        }

        if (opensBlock)
        {
            openAnonymousBlock();
        }
    }

    return FStatus::Ok();
}

std::optional<std::string> FConfigDocument::FindString(std::string_view key) const
{
    auto it = Index.find(Common::ToLower(std::string(key)));

    if (it == Index.end()) { return std::nullopt; }

    return ParsedEntries[it->second].Value;
}

std::optional<int64> FConfigDocument::FindInt(std::string_view key) const
{
    auto value = FindString(key);

    if (!value) { return std::nullopt; }

    int64 parsed = 0;
    return NumericParse::TryParseInt64Strict(*value, parsed) ? std::optional<int64>(parsed) : std::nullopt;
}
