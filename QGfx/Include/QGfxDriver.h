#pragma once

#include "QGfxSurface.h"

namespace QGfx
{
    class Batch;

    class Driver
    {
    public:
        virtual ~Driver() = default;

        virtual bool initialize() = 0;
        virtual DriverCapabilities capabilities() const = 0;

        virtual bool createSurface(Surface &surface) = 0;
        virtual void destroySurface(Surface &surface) = 0;
        virtual bool uploadSurfaceRegion(const Surface &surface,
                                         const QC::Rect &rect,
                                         const QC::u32 *pixels,
                                         QC::u32 stridePixels) = 0;
        virtual bool submitBatch(const Batch &batch) = 0;
        virtual bool present() = 0;
    };
}
