#include "SferaTextConfig.h"
#include "SferaStringUtil.h"
#include <sstream>
#include <cstdlib>

void SferaTextConfig::Clear()
{
    Values.clear();
}

bool SferaTextConfig::LoadFromText(const std::string& Text)
{
    Clear();

    std::istringstream Stream(Text);
    std::string Line;
    while (std::getline(Stream, Line))
    {
        Line = SferaStringUtil::Trim(Line);
        if (Line.empty() || Line[0] == '#' || Line[0] == ';')
        {
            continue;
        }

        const size_t CommentHash = Line.find('#');
        const size_t CommentSemi = Line.find(';');
        const size_t Comment = (CommentHash == std::string::npos) ? CommentSemi : ((CommentSemi == std::string::npos) ? CommentHash : (std::min)(CommentHash, CommentSemi));
        if (Comment != std::string::npos)
        {
            Line = SferaStringUtil::Trim(Line.substr(0, Comment));
        }

        size_t Separator = Line.find('=');
        if (Separator == std::string::npos)
        {
            Separator = Line.find_first_of(" \t");
        }

        if (Separator == std::string::npos)
        {
            Values[SferaStringUtil::ToLower(Line)] = "1";
            continue;
        }

        std::string Key = SferaStringUtil::ToLower(SferaStringUtil::Trim(Line.substr(0, Separator)));
        std::string Value = SferaStringUtil::Trim(Line.substr(Separator + 1));
        if (Value.size() >= 2 && ((Value.front() == '"' && Value.back() == '"') || (Value.front() == '\'' && Value.back() == '\'')))
        {
            Value = Value.substr(1, Value.size() - 2);
        }

        if (!Key.empty())
        {
            Values[Key] = Value;
        }
    }

    return true;
}

bool SferaTextConfig::HasValue(const std::string& Key) const
{
    return Values.find(SferaStringUtil::ToLower(Key)) != Values.end();
}

std::string SferaTextConfig::GetString(const std::string& Key, const std::string& DefaultValue) const
{
    auto It = Values.find(SferaStringUtil::ToLower(Key));
    return It == Values.end() ? DefaultValue : It->second;
}

int SferaTextConfig::GetInt(const std::string& Key, int DefaultValue) const
{
    auto It = Values.find(SferaStringUtil::ToLower(Key));
    if (It == Values.end())
    {
        return DefaultValue;
    }
    return std::atoi(It->second.c_str());
}

bool SferaTextConfig::GetBool(const std::string& Key, bool DefaultValue) const
{
    auto It = Values.find(SferaStringUtil::ToLower(Key));
    if (It == Values.end())
    {
        return DefaultValue;
    }

    const std::string Value = SferaStringUtil::ToLower(It->second);
    if (Value == "1" || Value == "true" || Value == "yes" || Value == "on") return true;
    if (Value == "0" || Value == "false" || Value == "no" || Value == "off") return false;
    return DefaultValue;
}
