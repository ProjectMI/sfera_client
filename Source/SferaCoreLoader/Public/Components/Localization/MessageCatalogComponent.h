#pragma once
#include "ResourceLoader/ResourceManager.h"

class FMessageCatalogComponent
{
public:
    explicit FMessageCatalogComponent(const FResourceManager& resources, FLogger* logger = nullptr);
    bool LoadGroup(std::string_view name);
    void UnloadGroups();
    void SetItemGroup(int32 group) { ItemGroup = group; }
    int32 ItemGroupValue() const { return ItemGroup; }
    std::string Message(int32 id) const;
    size_t GroupCount() const { return Groups.size(); }
private:
    using FMessageMap = std::unordered_map<uint64, std::string>;
    static uint64 MakeKey(int32 group, int32 id);
    static std::string Trim(std::string value);
    bool ParseGroup(const FByteArray& bytes, FMessageMap& messages) const;
    const FResourceManager& Resources;
    FLogger* Log = nullptr;
    std::vector<FMessageMap> Groups;
    int32 ItemGroup = -1;
    std::string Fallback = "???";
};
