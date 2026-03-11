#include "QG/Image.h"

#include "QCColor.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QGPainter.h"
#include "miniz_tinfl.h"

namespace QG
{
    namespace
    {
        constexpr const char *LOG_MODULE = "QGImage";

        inline QC::u32 readU32(const QC::u8 *ptr)
        {
            return (static_cast<QC::u32>(ptr[0]) << 24) |
                   (static_cast<QC::u32>(ptr[1]) << 16) |
                   (static_cast<QC::u32>(ptr[2]) << 8) |
                   static_cast<QC::u32>(ptr[3]);
        }

        inline QC::u8 paeth(QC::u8 a, QC::u8 b, QC::u8 c)
        {
            int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
            int pa = p > static_cast<int>(a) ? p - static_cast<int>(a) : static_cast<int>(a) - p;
            int pb = p > static_cast<int>(b) ? p - static_cast<int>(b) : static_cast<int>(b) - p;
            int pc = p > static_cast<int>(c) ? p - static_cast<int>(c) : static_cast<int>(c) - p;
            if (pa <= pb && pa <= pc)
                return a;
            if (pb <= pc)
                return b;
            return c;
        }

        struct PNGHeader
        {
            QC::u32 width = 0;
            QC::u32 height = 0;
            QC::u8 bitDepth = 0;
            QC::u8 colorType = 0;
            QC::u8 compression = 0;
            QC::u8 filter = 0;
            QC::u8 interlace = 0;
        };

        enum class PNGColorType : QC::u8
        {
            Grayscale = 0,
            RGB = 2,
            Palette = 3,
            GrayscaleAlpha = 4,
            RGBA = 6
        };

        bool parseIHDR(const QC::u8 *data, QC::usize length, PNGHeader &out)
        {
            if (length < 13)
                return false;
            out.width = readU32(data);
            out.height = readU32(data + 4);
            out.bitDepth = data[8];
            out.colorType = data[9];
            out.compression = data[10];
            out.filter = data[11];
            out.interlace = data[12];
            return true;
        }

        bool loadChunk(const QC::u8 *pngData,
                       QC::usize pngSize,
                       QC::usize &offset,
                       QC::u32 &length,
                       QC::u32 &type,
                       const QC::u8 *&chunkData)
        {
            if (offset + 8 > pngSize)
                return false;
            length = readU32(pngData + offset);
            type = readU32(pngData + offset + 4);
            offset += 8;
            if (offset + length + 4 > pngSize)
                return false;
            chunkData = pngData + offset;
            offset += length + 4; // Skip data + CRC
            return true;
        }

        bool decodePNGInternal(const QC::u8 *data, QC::usize size, ImageSurface &out)
        {
            out.reset();
            if (!data || size < 8)
                return false;

            static const QC::u8 signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (QC::String::memcmp(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(signature), 8) != 0)
            {
                QC_LOG_WARN(LOG_MODULE, "PNG signature mismatch");
                return false;
            }

            PNGHeader header;
            bool headerSeen = false;
            QC::Vector<QC::u8> compressed;

            QC::usize offset = 8;
            while (offset < size)
            {
                QC::u32 chunkLength = 0;
                QC::u32 chunkType = 0;
                const QC::u8 *chunkPtr = nullptr;
                if (!loadChunk(data, size, offset, chunkLength, chunkType, chunkPtr))
                    return false;

                switch (chunkType)
                {
                case 0x49484452: // IHDR
                    if (!parseIHDR(chunkPtr, chunkLength, header))
                        return false;
                    headerSeen = true;
                    break;
                case 0x49444154: // IDAT
                    if (!headerSeen)
                        return false;
                    if (chunkLength > 0)
                    {
                        const QC::usize oldSize = compressed.size();
                        compressed.resize(oldSize + chunkLength);
                        QC::String::memcpy(compressed.data() + oldSize, chunkPtr, chunkLength);
                    }
                    break;
                case 0x49454E44:   // IEND
                    offset = size; // Exit loop
                    break;
                default:
                    break; // Skip chunk
                }
            }

            if (!headerSeen || compressed.empty())
                return false;

            if (header.bitDepth != 8)
            {
                QC_LOG_WARN(LOG_MODULE, "Unsupported PNG bit depth %u", header.bitDepth);
                return false;
            }

            if (header.compression != 0 || header.filter != 0 || header.interlace != 0)
            {
                QC_LOG_WARN(LOG_MODULE, "Unsupported PNG compression/filter/interlace settings");
                return false;
            }

            QC::u32 bytesPerPixel = 0;
            switch (static_cast<PNGColorType>(header.colorType))
            {
            case PNGColorType::Grayscale:
                bytesPerPixel = 1;
                break;
            case PNGColorType::RGB:
                bytesPerPixel = 3;
                break;
            case PNGColorType::RGBA:
                bytesPerPixel = 4;
                break;
            default:
                QC_LOG_WARN(LOG_MODULE, "Unsupported PNG color type %u", header.colorType);
                return false;
            }

            QC::usize stride = static_cast<QC::usize>(header.width) * bytesPerPixel;
            QC::usize expectedSize = (stride + 1) * header.height;
            if (expectedSize == 0)
                return false;

            QC::Vector<QC::u8> decompressed;
            decompressed.resize(expectedSize);
            size_t actualSize = expectedSize;
            const int flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
            size_t result = tinfl_decompress_mem_to_mem(decompressed.data(), actualSize, compressed.data(), compressed.size(), flags);
            if (result != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
            {
                actualSize = result;
            }
            if (result == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || actualSize != expectedSize)
            {
                QC_LOG_WARN(LOG_MODULE, "PNG zlib inflate failed (expected %zu, got %zu)", static_cast<size_t>(expectedSize), actualSize);
                return false;
            }

            QC::Vector<QC::u8> reconPrev;
            QC::Vector<QC::u8> reconCur;
            reconPrev.resize(stride);
            reconCur.resize(stride);

            out.width = header.width;
            out.height = header.height;
            out.pixels.clear();
            out.pixels.resize(static_cast<QC::usize>(header.width) * header.height);

            const QC::u8 *src = decompressed.data();
            for (QC::u32 y = 0; y < header.height; ++y)
            {
                QC::u8 filterType = *src++;
                for (QC::usize x = 0; x < stride; ++x)
                {
                    QC::u8 raw = *src++;
                    QC::u8 left = (x >= bytesPerPixel) ? reconCur[x - bytesPerPixel] : 0;
                    QC::u8 up = (y > 0) ? reconPrev[x] : 0;
                    QC::u8 upLeft = (y > 0 && x >= bytesPerPixel) ? reconPrev[x - bytesPerPixel] : 0;

                    switch (filterType)
                    {
                    case 0:
                        reconCur[x] = raw;
                        break;
                    case 1:
                        reconCur[x] = static_cast<QC::u8>(raw + left);
                        break;
                    case 2:
                        reconCur[x] = static_cast<QC::u8>(raw + up);
                        break;
                    case 3:
                        reconCur[x] = static_cast<QC::u8>(raw + static_cast<QC::u8>((static_cast<int>(left) + static_cast<int>(up)) / 2));
                        break;
                    case 4:
                        reconCur[x] = static_cast<QC::u8>(raw + paeth(left, up, upLeft));
                        break;
                    default:
                        QC_LOG_WARN(LOG_MODULE, "Unsupported PNG filter type %u", filterType);
                        return false;
                    }
                }

                // Convert to ARGB8888. Hot path: avoid per-pixel QC::Color construction
                // and avoid per-row vector copies.
                QC::u32 *dstRow = out.pixels.data() + static_cast<QC::usize>(y) * header.width;
                const QC::u8 *row = reconCur.data();

                if (header.colorType == static_cast<QC::u8>(PNGColorType::RGBA))
                {
                    const QC::u8 *p = row;
                    for (QC::u32 x = 0; x < header.width; ++x)
                    {
                        const QC::u32 r = p[0];
                        const QC::u32 g = p[1];
                        const QC::u32 b = p[2];
                        const QC::u32 a = p[3];
                        dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
                        p += 4;
                    }
                }
                else if (header.colorType == static_cast<QC::u8>(PNGColorType::RGB))
                {
                    const QC::u8 *p = row;
                    for (QC::u32 x = 0; x < header.width; ++x)
                    {
                        const QC::u32 r = p[0];
                        const QC::u32 g = p[1];
                        const QC::u32 b = p[2];
                        dstRow[x] = (255u << 24) | (r << 16) | (g << 8) | b;
                        p += 3;
                    }
                }
                else // Grayscale
                {
                    for (QC::u32 x = 0; x < header.width; ++x)
                    {
                        const QC::u32 v = row[x];
                        dstRow[x] = (255u << 24) | (v << 16) | (v << 8) | v;
                    }
                }

                // Keep reconPrev as previous row for next scanline filter.
                // Using memcpy avoids per-element construction overhead in QC::Vector assignment.
                QC::String::memcpy(reconPrev.data(), reconCur.data(), stride);
            }

            return out.isValid();
        }

        QC::Rect computeTargetRect(const QC::Rect &dest,
                                   QC::u32 sourceWidth,
                                   QC::u32 sourceHeight,
                                   ImageScaleMode mode)
        {
            QC::Rect result = dest;
            if (mode == ImageScaleMode::Stretch || mode == ImageScaleMode::Tile)
            {
                return result;
            }

            if (mode == ImageScaleMode::Original)
            {
                result.width = sourceWidth < dest.width ? sourceWidth : dest.width;
                result.height = sourceHeight < dest.height ? sourceHeight : dest.height;
                return result;
            }

            if (mode == ImageScaleMode::Center)
            {
                result.width = sourceWidth;
                result.height = sourceHeight;
            }
            else
            {
                if (sourceWidth == 0 || sourceHeight == 0)
                {
                    result.width = 0;
                    result.height = 0;
                    return result;
                }

                QC::u64 scaledW = static_cast<QC::u64>(dest.width);
                QC::u64 scaledH = static_cast<QC::u64>(dest.height);
                QC::u64 srcW = sourceWidth;
                QC::u64 srcH = sourceHeight;

                QC::u64 scaleX = (scaledW << 32) / srcW;
                QC::u64 scaleY = (scaledH << 32) / srcH;
                QC::u64 scale = (mode == ImageScaleMode::Fit) ? (scaleX < scaleY ? scaleX : scaleY)
                                                              : (scaleX > scaleY ? scaleX : scaleY);
                if (scale == 0)
                    scale = 1;

                QC::u32 targetW = static_cast<QC::u32>((srcW * scale) >> 32);
                QC::u32 targetH = static_cast<QC::u32>((srcH * scale) >> 32);

                if (targetW == 0)
                    targetW = 1;
                if (targetH == 0)
                    targetH = 1;

                result.width = mode == ImageScaleMode::Fill ? dest.width : (targetW > dest.width && mode != ImageScaleMode::Fill ? dest.width : targetW);
                result.height = mode == ImageScaleMode::Fill ? dest.height : (targetH > dest.height && mode != ImageScaleMode::Fill ? dest.height : targetH);
            }

            QC::i32 offsetX = static_cast<QC::i32>(dest.width > result.width ? (dest.width - result.width) / 2 : 0);
            QC::i32 offsetY = static_cast<QC::i32>(dest.height > result.height ? (dest.height - result.height) / 2 : 0);
            result.x += offsetX;
            result.y += offsetY;
            return result;
        }

        void blitScaledNearest(IPainter *painter,
                               const ImageSurface &surface,
                               const QC::Rect &target,
                               QC::Vector<QC::u32> &scratch)
        {
            if (!painter)
                return;

            // Respect clip rect to avoid rescaling/blitting the entire image when only a
            // small region is dirty (e.g., button hover on a wallpaper-backed desktop).
            const QC::Point origin = painter->origin();
            QC::Rect clipLocal = painter->clipRect().offset(-origin.x, -origin.y);

            QC::i32 ix1 = target.x;
            QC::i32 iy1 = target.y;
            QC::i32 ix2 = target.x + static_cast<QC::i32>(target.width);
            QC::i32 iy2 = target.y + static_cast<QC::i32>(target.height);
            if (!clipLocal.isEmpty())
            {
                const QC::i32 cx1 = clipLocal.x;
                const QC::i32 cy1 = clipLocal.y;
                const QC::i32 cx2 = clipLocal.x + static_cast<QC::i32>(clipLocal.width);
                const QC::i32 cy2 = clipLocal.y + static_cast<QC::i32>(clipLocal.height);

                if (ix1 < cx1)
                    ix1 = cx1;
                if (iy1 < cy1)
                    iy1 = cy1;
                if (ix2 > cx2)
                    ix2 = cx2;
                if (iy2 > cy2)
                    iy2 = cy2;
            }

            if (ix2 <= ix1 || iy2 <= iy1)
                return;

            const QC::u32 xStart = static_cast<QC::u32>(ix1 - target.x);
            const QC::u32 yStart = static_cast<QC::u32>(iy1 - target.y);
            const QC::u32 outW = static_cast<QC::u32>(ix2 - ix1);
            const QC::u32 yEnd = static_cast<QC::u32>(iy2 - target.y);

            if (target.width == surface.width && target.height == surface.height)
            {
                painter->blitAlpha(target.x + static_cast<QC::i32>(xStart),
                                   target.y + static_cast<QC::i32>(yStart),
                                   surface.data() + (static_cast<QC::usize>(yStart) * surface.width) + xStart,
                                   outW,
                                   yEnd - yStart,
                                   surface.width);
                return;
            }

            scratch.resize(outW);
            QC::Vector<QC::u32> srcXMap;
            srcXMap.resize(outW);

            for (QC::u32 i = 0; i < outW; ++i)
            {
                const QC::u32 destX = xStart + i;
                srcXMap[i] = (static_cast<QC::u64>(destX) * surface.width) / target.width;
            }

            const QC::u32 *source = surface.data();

            for (QC::u32 y = yStart; y < yEnd; ++y)
            {
                QC::u32 srcY = (static_cast<QC::u64>(y) * surface.height) / target.height;
                const QC::u32 *srcRow = source + srcY * surface.width;
                bool rowOpaque = true;
                for (QC::u32 i = 0; i < outW; ++i)
                {
                    const QC::u32 px = srcRow[srcXMap[i]];
                    scratch[i] = px;
                    if (((px >> 24) & 0xFF) != 255)
                        rowOpaque = false;
                }

                if (rowOpaque)
                {
                    painter->blit(target.x + static_cast<QC::i32>(xStart),
                                 target.y + static_cast<QC::i32>(y),
                                 scratch.data(),
                                 outW,
                                 1,
                                 outW);
                }
                else
                {
                    painter->blitAlpha(target.x + static_cast<QC::i32>(xStart),
                                       target.y + static_cast<QC::i32>(y),
                                       scratch.data(),
                                       outW,
                                       1,
                                       outW);
                }
            }
        }
    } // namespace

    void ImageSurface::reset()
    {
        pixels.clear();
        width = 0;
        height = 0;
    }

    bool ImageSurface::isValid() const
    {
        return width > 0 && height > 0 && pixels.size() == static_cast<QC::usize>(width) * height;
    }

    const QC::u32 *ImageSurface::data() const
    {
        return pixels.empty() ? nullptr : pixels.data();
    }

    bool decodePNG(const QC::u8 *data, QC::usize size, ImageSurface &outSurface)
    {
        return decodePNGInternal(data, size, outSurface);
    }

    bool decodePNG(const QC::Vector<QC::u8> &buffer, ImageSurface &outSurface)
    {
        if (buffer.empty())
            return false;
        return decodePNGInternal(buffer.data(), buffer.size(), outSurface);
    }

    void blitImage(IPainter *painter,
                   const ImageSurface &surface,
                   const QC::Rect &destination,
                   ImageScaleMode scaleMode,
                   QC::Vector<QC::u32> &scratchRow)
    {
        if (!painter || !surface.isValid() || destination.width == 0 || destination.height == 0)
            return;

        if (scaleMode == ImageScaleMode::Tile)
        {
            QC::Rect tileRect = destination;
            tileRect.width = surface.width;
            tileRect.height = surface.height;
            for (QC::i32 y = 0; y < destination.height; y += static_cast<QC::i32>(surface.height))
            {
                for (QC::i32 x = 0; x < destination.width; x += static_cast<QC::i32>(surface.width))
                {
                    painter->blitAlpha(destination.x + x,
                                       destination.y + y,
                                       surface.data(),
                                       surface.width,
                                       surface.height,
                                       surface.width);
                }
            }
            return;
        }

        QC::Rect target = computeTargetRect(destination, surface.width, surface.height, scaleMode);
        if (target.width == 0 || target.height == 0)
            return;

        blitScaledNearest(painter, surface, target, scratchRow);
    }

} // namespace QG
