#include "Renderer/DdsImage.h"
#include "Core/BinaryReader.h"
#include <algorithm>
#include <stdexcept>

namespace
{
    FDdsImage Decode(const FByteArray& data, std::string_view sourceName)
    {
        if (data.size() < 128 || data[0] != 'D' || data[1] != 'D' || data[2] != 'S' || data[3] != ' ')
        {
            throw std::runtime_error("not a DDS file: " + std::string(sourceName));
        }

        const auto headerSize = Binary::U32LE(data, 4);
        const int32 height = static_cast<int32>(Binary::U32LE(data, 12));
        const int32 width = static_cast<int32>(Binary::U32LE(data, 16));
        const auto pixelFormatSize = Binary::U32LE(data, 76);
        const auto pixelFormatFlags = Binary::U32LE(data, 80);
        const auto rgbBitCount = Binary::U32LE(data, 88);
        const auto rMask = Binary::U32LE(data, 92);
        const auto gMask = Binary::U32LE(data, 96);
        const auto bMask = Binary::U32LE(data, 100);
        const auto aMask = Binary::U32LE(data, 104);

        if (headerSize != 124 || pixelFormatSize != 32 || width <= 0 || height <= 0)
        {
            throw std::runtime_error("unsupported DDS header: " + std::string(sourceName));
        }

        if ((pixelFormatFlags & 0x40U) == 0 || rMask != 0x00FF0000U || gMask != 0x0000FF00U || bMask != 0x000000FFU)
        {
            throw std::runtime_error("unsupported DDS pixel format: " + std::string(sourceName));
        }

        if (rgbBitCount != 24 && rgbBitCount != 32)
        {
            throw std::runtime_error("only uncompressed RGB24/RGB32 DDS is supported: " + std::string(sourceName));
        }

        const bool hasAlpha = rgbBitCount == 32 && (pixelFormatFlags & 0x1U) != 0 && aMask == 0xFF000000U;
        const size_t bytesPerPixel = rgbBitCount / 8;
        const size_t sourceStride = static_cast<size_t>(width) * bytesPerPixel;
        const size_t imageSize = sourceStride * static_cast<size_t>(height);
        Binary::RequireRange(data, 128, imageSize, "DDS top mip level");
        FDdsImage image;
        image.Width = width;
        image.Height = height;
        image.Stride = width * 4;
        image.HasAlpha = hasAlpha;
        image.BgraPixels.assign(static_cast<size_t>(image.Stride) * static_cast<size_t>(height), 0);
        const auto* src = data.data() + 128;
        auto* dst = image.BgraPixels.data();

        for (int32 y = 0; y < height; ++y)
        {
            const auto* rowSrc = src + static_cast<size_t>(y) * sourceStride;
            auto* rowDst = dst + static_cast<size_t>(y) * static_cast<size_t>(image.Stride);

            for (int32 x = 0; x < width; ++x)
            {
                const auto* p = rowSrc + static_cast<size_t>(x) * bytesPerPixel;
                uint8 alpha = hasAlpha ? p[3] : 255;
                rowDst[x * 4 + 0] = hasAlpha ? static_cast<uint8>((static_cast<unsigned>(p[0]) * alpha) / 255U) : p[0];
                rowDst[x * 4 + 1] = hasAlpha ? static_cast<uint8>((static_cast<unsigned>(p[1]) * alpha) / 255U) : p[1];
                rowDst[x * 4 + 2] = hasAlpha ? static_cast<uint8>((static_cast<unsigned>(p[2]) * alpha) / 255U) : p[2];
                rowDst[x * 4 + 3] = alpha;
            }
        }

        return image;
    }
    FStatus DdsError(const std::exception& e) { return FStatus::Error(EStatusCode::InvalidData, std::string("DDS decode failed: ") + e.what()); }
}
TResult<FDdsImage> DecodeDdsRgbImageFromBytes(const FByteArray& bytes, std::string_view sourceName)
{
    try
    {
        return Decode(bytes, sourceName);
    }
    catch (const std::exception& e)
    {
        return DdsError(e);
    }
}
TResult<FDdsImage> DecodeDdsRgbImageFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    auto blob = resources.Load(logicalName);

    if (!blob.IsOk())
    {
        return blob.Status();
    }

    return DecodeDdsRgbImageFromBytes(blob.Value().Bytes, blob.Value().SourcePath.generic_string());
}
