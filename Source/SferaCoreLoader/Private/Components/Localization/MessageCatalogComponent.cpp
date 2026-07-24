#include "Components/Localization/MessageCatalogComponent.h"
#include "Common/TextEncoding.h"

FMessageCatalogComponent::FMessageCatalogComponent(const FResourceManager& resources, FLogger* logger) : Resources(resources), Log(logger) {}

uint64 FMessageCatalogComponent::MakeKey(int32 group, int32 id)
{
    return (static_cast<uint64>(static_cast<uint32>(group)) << 32) | static_cast<uint32>(id);
}

std::string FMessageCatalogComponent::Trim(std::string value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { return {}; }
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool FMessageCatalogComponent::LoadGroup(std::string_view name)
{
    if (Groups.size() >= 10 || name.empty()) { return false; }
    std::string logicalName(name);
    if (!Resources.Catalog().FindByLogicalName(logicalName)) { logicalName = "language/" + logicalName + ".txt"; }
    TResult<FResourceBlob> blob = Resources.Load(logicalName);
    if (!blob.IsOk())
    {
        if (Log) { Log->Warning("message group load failed: " + logicalName + "; " + blob.Status().Message()); }
        return false;
    }
    FMessageMap messages;
    if (!ParseGroup(blob.Value().Bytes, messages)) { return false; }
    Groups.push_back(std::move(messages));
    return true;
}

void FMessageCatalogComponent::UnloadGroups()
{
    Groups.clear();
    ItemGroup = -1;
}

std::string FMessageCatalogComponent::Message(int32 id) const
{
    for (const FMessageMap& group : Groups)
    {
        auto found = group.find(MakeKey(ItemGroup, id));
        if (found != group.end()) { return found->second; }
    }
    for (const FMessageMap& group : Groups)
    {
        auto found = group.find(MakeKey(-1, id));
        if (found != group.end()) { return found->second; }
    }
    return Fallback;
}

bool FMessageCatalogComponent::ParseGroup(const FByteArray& bytes, FMessageMap& messages) const
{
    const std::string text = Common::WideToUtf8(Common::Cp1251BytesToWide(bytes));
    std::istringstream input(text);
    std::string line;
    int32 groupBegin = -1;
    int32 groupEnd = -1;
    while (std::getline(input, line))
    {
        line = Trim(std::move(line));
        if (line.empty()) { continue; }
        if (line.front() == '#')
        {
            std::string range = Trim(line.substr(1));
            const size_t separator = range.find('-');
            try
            {
                groupBegin = std::stoi(separator == std::string::npos ? range : range.substr(0, separator));
                groupEnd = separator == std::string::npos ? groupBegin : std::stoi(range.substr(separator + 1));
            }
            catch (...) { groupBegin = -1; groupEnd = -1; }
            if (groupEnd < groupBegin) { std::swap(groupBegin, groupEnd); }
            continue;
        }
        std::istringstream row(line);
        int32 id = -1;
        if (!(row >> id)) { continue; }
        std::string message;
        std::getline(row, message);
        message = Trim(std::move(message));
        for (int32 group = groupBegin; group <= groupEnd; ++group) { messages[MakeKey(group, id)] = message; }
    }
    return true;
}
