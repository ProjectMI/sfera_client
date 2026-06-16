#include "SferaStringUtil.h"
#include <cctype>

std::string SferaStringUtil::Trim(const std::string& Value)
{
    size_t Begin = 0;
    while (Begin < Value.size() && std::isspace(static_cast<unsigned char>(Value[Begin])))
    {
        ++Begin;
    }

    size_t End = Value.size();
    while (End > Begin && std::isspace(static_cast<unsigned char>(Value[End - 1])))
    {
        --End;
    }

    return Value.substr(Begin, End - Begin);
}

std::string SferaStringUtil::ToLower(std::string Value)
{
    std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Value;
}

std::string SferaStringUtil::NormalizeSlashes(std::string Value)
{
    std::replace(Value.begin(), Value.end(), '/', '\\');
    return Value;
}

std::string SferaStringUtil::NormalizeLogicalPath(const std::string& Value)
{
    std::string Result = NormalizeSlashes(Trim(Value));
    while (!Result.empty() && (Result[0] == '.' || Result[0] == '\\'))
    {
        if (Result.size() >= 2 && Result[0] == '.' && Result[1] == '\\')
        {
            Result.erase(0, 2);
        }
        else if (Result[0] == '\\')
        {
            Result.erase(0, 1);
        }
        else
        {
            break;
        }
    }
    return Result;
}

std::string SferaStringUtil::GetExtensionLower(const std::string& Value)
{
    const std::string Normalized = NormalizeSlashes(Value);
    const size_t Slash = Normalized.find_last_of('\\');
    const size_t Dot = Normalized.find_last_of('.');
    if (Dot == std::string::npos || (Slash != std::string::npos && Dot < Slash))
    {
        return std::string();
    }
    return ToLower(Normalized.substr(Dot));
}

bool SferaStringUtil::StartsWithIgnoreCase(const std::string& Value, const std::string& Prefix)
{
    if (Prefix.size() > Value.size())
    {
        return false;
    }
    return ToLower(Value.substr(0, Prefix.size())) == ToLower(Prefix);
}

bool SferaStringUtil::EndsWithIgnoreCase(const std::string& Value, const std::string& Suffix)
{
    if (Suffix.size() > Value.size())
    {
        return false;
    }
    return ToLower(Value.substr(Value.size() - Suffix.size())) == ToLower(Suffix);
}

bool SferaStringUtil::WildcardMatchIgnoreCase(const std::string& Pattern, const std::string& Value)
{
    const std::string P = ToLower(NormalizeSlashes(Pattern));
    const std::string V = ToLower(NormalizeSlashes(Value));

    size_t Pi = 0;
    size_t Vi = 0;
    size_t Star = std::string::npos;
    size_t Match = 0;

    while (Vi < V.size())
    {
        if (Pi < P.size() && (P[Pi] == '?' || P[Pi] == V[Vi]))
        {
            ++Pi;
            ++Vi;
        }
        else if (Pi < P.size() && P[Pi] == '*')
        {
            Star = Pi++;
            Match = Vi;
        }
        else if (Star != std::string::npos)
        {
            Pi = Star + 1;
            Vi = ++Match;
        }
        else
        {
            return false;
        }
    }

    while (Pi < P.size() && P[Pi] == '*')
    {
        ++Pi;
    }

    return Pi == P.size();
}
