#pragma once
#include "Core/Types.h"

IDirect3DTexture9* CreateD3D9TextureFromDdsBytes(IDirect3DDevice9* device, const FByteArray& data, const std::string& sourceName);
