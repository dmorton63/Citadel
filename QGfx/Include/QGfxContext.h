#pragma once

#include "QGfxBatch.h"
#include "QGfxDriver.h"
#include "QCVector.h"

namespace QGfx
{
    class Context
    {
    public:
        explicit Context(Driver *driver = nullptr);

        void setDriver(Driver *driver) { m_driver = driver; }
        Driver *driver() const { return m_driver; }

        bool initialize();
        DriverCapabilities capabilities() const;

        bool createSurface(Surface &surface);
        void destroySurface(Surface &surface);
        bool uploadSurfaceRegion(const Surface &surface,
                                 const QC::Rect &rect,
                                 const QC::u32 *pixels,
                                 QC::u32 stridePixels);
        bool submitBatch(Batch &batch);
        bool present();

    private:
        Driver *m_driver;
    };
}
