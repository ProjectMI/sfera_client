#pragma once
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiRuntimeState.h"

class FUiDocumentParser
{
public:
    TResult<FUiStringTable> LoadStringTableFromResource(const FResourceManager& resources, std::string_view logicalName) const;
    TResult<FUiWindowDef> LoadWindowFromResource(const FResourceManager& resources, std::string_view logicalName) const;
};
