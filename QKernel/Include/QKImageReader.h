#pragma once

#include "QCTypes.h"
#include "QG/Image.h"

namespace QK::ImageReader
{
    enum class Format : QC::u8
    {
        Unknown = 0,
        BMP,
        PNG,
        ICO,
    };

    struct LoadResult
    {
        Format format = Format::Unknown;
        QG::ImageSurface surface;
    };

    QC::Status loadAsset(const char *path, LoadResult &out);
    const char *formatName(Format format);
}
