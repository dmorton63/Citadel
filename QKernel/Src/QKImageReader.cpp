#include "QKImageReader.h"

#include "QFSFile.h"
#include "QFSVFS.h"
#include "QCString.h"

namespace QK::ImageReader
{
    namespace
    {
        static bool hasExtIgnoreCase(const char *path, const char *ext)
        {
            if (!path || !ext)
                return false;
            const QC::usize pathLen = QC::String::strlen(path);
            const QC::usize extLen = QC::String::strlen(ext);
            if (pathLen < extLen)
                return false;

            const char *p = path + (pathLen - extLen);
            for (QC::usize i = 0; i < extLen; ++i)
            {
                char a = p[i];
                char b = ext[i];
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    return false;
            }
            return true;
        }

        static bool readAll(const char *path, QC::Vector<QC::u8> &out)
        {
            out.clear();
            if (!path || *path == '\0')
                return false;

            QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!f)
                return false;

            const QC::isize sizeSigned = f->size();
            if (sizeSigned <= 0)
            {
                QFS::VFS::instance().close(f);
                return false;
            }

            const QC::usize size = static_cast<QC::usize>(sizeSigned);
            out.resize(size);

            QC::usize off = 0;
            while (off < size)
            {
                const QC::isize n = f->read(out.data() + off, size - off);
                if (n <= 0)
                    break;
                off += static_cast<QC::usize>(n);
            }

            QFS::VFS::instance().close(f);
            if (off != size)
            {
                out.clear();
                return false;
            }

            return true;
        }

        static inline QC::u16 rd16(const QC::u8 *p)
        {
            return static_cast<QC::u16>(p[0]) | (static_cast<QC::u16>(p[1]) << 8);
        }

        static inline QC::u32 rd32(const QC::u8 *p)
        {
            return static_cast<QC::u32>(p[0]) |
                   (static_cast<QC::u32>(p[1]) << 8) |
                   (static_cast<QC::u32>(p[2]) << 16) |
                   (static_cast<QC::u32>(p[3]) << 24);
        }

        static bool decodeBmp(const QC::Vector<QC::u8> &buf, QG::ImageSurface &out)
        {
            out.reset();
            if (buf.size() < 54)
                return false;
            const QC::u8 *p = buf.data();
            if (p[0] != 'B' || p[1] != 'M')
                return false;

            const QC::u32 pixelOffset = rd32(p + 10);
            const QC::u32 dibSize = rd32(p + 14);
            if (dibSize < 40)
                return false;

            const QC::u32 width = rd32(p + 18);
            const QC::u32 height = rd32(p + 22);
            const QC::u16 planes = rd16(p + 26);
            const QC::u16 bpp = rd16(p + 28);
            const QC::u32 compression = rd32(p + 30);

            if (width == 0 || height == 0 || planes != 1)
                return false;
            if (compression != 0)
                return false;
            if (bpp != 24 && bpp != 32)
                return false;

            const QC::u32 bytesPerPixel = (bpp == 24) ? 3u : 4u;
            const QC::u32 rowRaw = width * bytesPerPixel;
            const QC::u32 rowStride = (rowRaw + 3u) & ~3u;
            const QC::u64 needed = static_cast<QC::u64>(pixelOffset) + static_cast<QC::u64>(rowStride) * static_cast<QC::u64>(height);
            if (needed > buf.size())
                return false;

            out.width = width;
            out.height = height;
            out.pixels.resize(static_cast<QC::usize>(width) * static_cast<QC::usize>(height));

            for (QC::u32 y = 0; y < height; ++y)
            {
                const QC::u32 srcY = (height - 1u - y);
                const QC::u8 *row = p + pixelOffset + static_cast<QC::usize>(srcY) * rowStride;
                for (QC::u32 x = 0; x < width; ++x)
                {
                    const QC::u8 *px = row + static_cast<QC::usize>(x) * bytesPerPixel;
                    const QC::u8 b = px[0];
                    const QC::u8 g = px[1];
                    const QC::u8 r = px[2];
                    const QC::u8 a = (bytesPerPixel == 4) ? px[3] : 0xFF;
                    out.pixels[static_cast<QC::usize>(y) * width + x] =
                        (static_cast<QC::u32>(a) << 24) |
                        (static_cast<QC::u32>(r) << 16) |
                        (static_cast<QC::u32>(g) << 8) |
                        static_cast<QC::u32>(b);
                }
            }

            return true;
        }

        static bool decodeIco(const QC::Vector<QC::u8> &buf, QG::ImageSurface &out)
        {
            out.reset();
            if (buf.size() < 22)
                return false;
            const QC::u8 *p = buf.data();
            if (rd16(p + 0) != 0 || rd16(p + 2) != 1)
                return false;
            const QC::u16 count = rd16(p + 4);
            if (count == 0)
                return false;

            QC::u32 bestW = 0;
            QC::u32 bestH = 0;
            QC::u32 bestOff = 0;
            QC::u32 bestSize = 0;

            for (QC::u16 i = 0; i < count; ++i)
            {
                const QC::usize off = 6 + static_cast<QC::usize>(i) * 16;
                if (off + 16 > buf.size())
                    break;

                const QC::u32 w = (p[off + 0] == 0) ? 256u : static_cast<QC::u32>(p[off + 0]);
                const QC::u32 h = (p[off + 1] == 0) ? 256u : static_cast<QC::u32>(p[off + 1]);
                const QC::u32 bytesInRes = rd32(p + off + 8);
                const QC::u32 imageOff = rd32(p + off + 12);
                const QC::u64 end = static_cast<QC::u64>(imageOff) + static_cast<QC::u64>(bytesInRes);
                if (bytesInRes == 0 || end > buf.size())
                    continue;

                if (w * h > bestW * bestH)
                {
                    bestW = w;
                    bestH = h;
                    bestOff = imageOff;
                    bestSize = bytesInRes;
                }
            }

            if (bestSize == 0)
                return false;

            QC::Vector<QC::u8> payload;
            payload.resize(bestSize);
            QC::String::memcpy(payload.data(), p + bestOff, bestSize);

            // ICO commonly embeds PNG payloads; BMP payloads are also attempted.
            if (QG::decodePNG(payload, out))
                return true;

            if (decodeBmp(payload, out))
                return true;

            return false;
        }
    }

    QC::Status loadAsset(const char *path, LoadResult &out)
    {
        out = {};

        QC::Vector<QC::u8> bytes;
        if (!readAll(path, bytes))
            return QC::Status::NotFound;

        const bool isPng = hasExtIgnoreCase(path, ".png");
        const bool isBmp = hasExtIgnoreCase(path, ".bmp");
        const bool isIco = hasExtIgnoreCase(path, ".ico");

        if (isPng)
        {
            if (!QG::decodePNG(bytes, out.surface))
                return QC::Status::Error;
            out.format = Format::PNG;
            return QC::Status::Success;
        }

        if (isBmp)
        {
            if (!decodeBmp(bytes, out.surface))
                return QC::Status::Error;
            out.format = Format::BMP;
            return QC::Status::Success;
        }

        if (isIco)
        {
            if (!decodeIco(bytes, out.surface))
                return QC::Status::Error;
            out.format = Format::ICO;
            return QC::Status::Success;
        }

        if (QG::decodePNG(bytes, out.surface))
        {
            out.format = Format::PNG;
            return QC::Status::Success;
        }
        if (decodeBmp(bytes, out.surface))
        {
            out.format = Format::BMP;
            return QC::Status::Success;
        }
        if (decodeIco(bytes, out.surface))
        {
            out.format = Format::ICO;
            return QC::Status::Success;
        }

        return QC::Status::NotSupported;
    }

    const char *formatName(Format format)
    {
        switch (format)
        {
        case Format::BMP:
            return "BMP";
        case Format::PNG:
            return "PNG";
        case Format::ICO:
            return "ICO";
        default:
            return "UNKNOWN";
        }
    }
}
