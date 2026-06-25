#pragma once
#include "ResourceLoader/ResourceManager.h"

namespace ResourceLoader
{
template <class T, class TDecoder>
inline TResult<T> DecodeResource(const FResourceManager& resources, std::string_view logicalName, TDecoder decoder)
{
    auto blob = resources.Load(logicalName);
    if (!blob.IsOk()) { return blob.Status(); }
    return decoder(blob.Value().Bytes, blob.Value().SourcePath.generic_string());
}
}
