#pragma once
#include "Core/Types.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiDocumentParser.h"

TResult<FUiStringTable> LoadUiStringTableFromResource(const FResourceManager& resources, std::string_view logicalName);
TResult<FUiWindowDef> LoadUiWindowFromResource(const FResourceManager& resources, std::string_view logicalName);
