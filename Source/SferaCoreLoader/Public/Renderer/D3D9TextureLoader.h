#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d9.h>
#include "Core/Types.h"
#include <string>

IDirect3DTexture9* CreateD3D9TextureFromDdsBytes(IDirect3DDevice9* device, const FByteArray& data, const std::string& sourceName);
