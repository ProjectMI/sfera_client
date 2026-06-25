#include "Renderer/D3D9TextureLoader.h"
#include "Core/BinaryReader.h"
#include "Renderer/D3D9Utils.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace
{
std::string HResultTextNarrow(const char* action, HRESULT hr)
{
    std::ostringstream out;
    out << action << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
    return out.str();
}
}

IDirect3DTexture9* CreateD3D9TextureFromDdsBytes(IDirect3DDevice9* device, const FByteArray& data, const std::string& sourceName)
{
    if (!device)
    {
        throw std::runtime_error("D3D device is absent");
    }
    if (data.size() < 128 || Binary::U32LE(data, 0) != 0x20534444 || Binary::U32LE(data, 4) != 124 || Binary::U32LE(data, 76) != 32)
    {
        throw std::runtime_error("bad DDS file: " + sourceName);
    }
    const auto height = Binary::U32LE(data, 12);
    const auto width = Binary::U32LE(data, 16);
    const auto mipCountRaw = Binary::U32LE(data, 28);
    const auto pfFlags = Binary::U32LE(data, 80);
    const auto fourcc = Binary::U32LE(data, 84);
    const auto rgbBitCount = Binary::U32LE(data, 88);
    const auto rMask = Binary::U32LE(data, 92);
    const auto gMask = Binary::U32LE(data, 96);
    const auto bMask = Binary::U32LE(data, 100);
    const auto aMask = Binary::U32LE(data, 104);
    if (width == 0 || height == 0)
    {
        throw std::runtime_error("empty DDS texture: " + sourceName);
    }
    D3DFORMAT format = D3DFMT_UNKNOWN;
    uint32 blockBytes = 0;
    uint32 sourcePixelBytes = 0;
    bool expandRgb24 = false;
    if ((pfFlags & 0x4U) != 0)
    {
        if (fourcc == 0x31545844U)
        {
            format = D3DFMT_DXT1;
            blockBytes = 8;
        }
        else if (fourcc == 0x33545844U)
        {
            format = D3DFMT_DXT3;
            blockBytes = 16;
        }
        else if (fourcc == 0x35545844U)
        {
            format = D3DFMT_DXT5;
            blockBytes = 16;
        }
    }
    else if ((pfFlags & 0x40U) != 0 && rgbBitCount == 32 && rMask == 0x00ff0000U && gMask == 0x0000ff00U && bMask == 0x000000ffU && ((pfFlags & 0x1U) == 0 || aMask == 0xff000000U))
    {
        format = D3DFMT_A8R8G8B8;
        sourcePixelBytes = 4;
    }
    else if ((pfFlags & 0x40U) != 0 && rgbBitCount == 16 && rMask == 0x0000f800U && gMask == 0x000007e0U && bMask == 0x0000001fU && aMask == 0)
    {
        format = D3DFMT_R5G6B5;
        sourcePixelBytes = 2;
    }
    else if ((pfFlags & 0x40U) != 0 && rgbBitCount == 24 && rMask == 0x00ff0000U && gMask == 0x0000ff00U && bMask == 0x000000ffU && aMask == 0)
    {
        format = D3DFMT_A8R8G8B8;
        sourcePixelBytes = 3;
        expandRgb24 = true;
    }
    if (format == D3DFMT_UNKNOWN)
    {
        throw std::runtime_error("unsupported DDS texture format: " + sourceName);
    }
    const UINT mipCount = static_cast<UINT>((std::max)(uint32{1}, mipCountRaw));
    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = device->CreateTexture(width, height, mipCount, 0, format, D3DPOOL_MANAGED, &texture, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateTexture", hr));
    }
    std::size_t cursor = 128;
    uint32 levelWidth = width;
    uint32 levelHeight = height;
    for (UINT level = 0; level < mipCount; ++level)
    {
        std::size_t sourcePitch = 0;
        std::size_t sourceRows = 0;
        if (blockBytes != 0)
        {
            sourcePitch = static_cast<std::size_t>((std::max)(uint32{1}, (levelWidth + 3) / 4)) * blockBytes;
            sourceRows = (std::max)(uint32{1}, (levelHeight + 3) / 4);
        }
        else
        {
            sourcePitch = static_cast<std::size_t>(levelWidth) * sourcePixelBytes;
            sourceRows = levelHeight;
        }
        const std::size_t sourceBytes = sourcePitch * sourceRows;
        Binary::RequireRange(data, cursor, sourceBytes, "DDS mip data");
        D3DLOCKED_RECT locked{};
        hr = texture->LockRect(level, &locked, nullptr, 0);
        if (FAILED(hr))
        {
            SafeRelease(texture);
            throw std::runtime_error(HResultTextNarrow("Texture::LockRect", hr));
        }
        for (std::size_t row = 0; row < sourceRows; ++row)
        {
            auto* dest = static_cast<uint8*>(locked.pBits) + row * locked.Pitch;
            const auto* source = data.data() + cursor + row * sourcePitch;
            if (expandRgb24)
            {
                for (std::size_t column = 0; column < levelWidth; ++column)
                {
                    dest[column * 4] = source[column * 3];
                    dest[column * 4 + 1] = source[column * 3 + 1];
                    dest[column * 4 + 2] = source[column * 3 + 2];
                    dest[column * 4 + 3] = 0xff;
                }
            }
            else
            {
                std::copy_n(source, sourcePitch, dest);
            }
        }
        texture->UnlockRect(level);
        cursor += sourceBytes;
        levelWidth = (std::max)(uint32{1}, levelWidth / 2);
        levelHeight = (std::max)(uint32{1}, levelHeight / 2);
    }
    return texture;
}
