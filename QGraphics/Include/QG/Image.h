#pragma once

#include "QCTypes.h"
#include "QCVector.h"
#include "QCGeometry.h"

namespace QG
{
    class IPainter;

    struct ImageSurface
    {
        QC::u32 width = 0;
        QC::u32 height = 0;
        QC::Vector<QC::u32> pixels;

        void reset();
        bool isValid() const;
        const QC::u32 *data() const;
    };

    enum class ImageScaleMode : QC::u8
    {
        Original,
        Stretch,
        Fit,
        Fill,
        Center,
        Tile
    };

    bool decodePNG(const QC::u8 *data, QC::usize size, ImageSurface &outSurface);
    bool decodePNG(const QC::Vector<QC::u8> &buffer, ImageSurface &outSurface);

    void blitImage(IPainter *painter,
                   const ImageSurface &surface,
                   const QC::Rect &destination,
                   ImageScaleMode scaleMode,
                   QC::Vector<QC::u32> &scratchRow);
}
