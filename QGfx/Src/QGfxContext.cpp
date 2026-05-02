#include "QGfxContext.h"

namespace QGfx
{
    Context::Context(Driver *driver)
        : m_driver(driver)
    {
    }

    bool Context::initialize()
    {
        return m_driver && m_driver->initialize();
    }

    DriverCapabilities Context::capabilities() const
    {
        return m_driver ? m_driver->capabilities() : DriverCapabilities{};
    }

    bool Context::createSurface(Surface &surface)
    {
        return m_driver && m_driver->createSurface(surface);
    }

    void Context::destroySurface(Surface &surface)
    {
        if (m_driver)
            m_driver->destroySurface(surface);
    }

    bool Context::uploadSurfaceRegion(const Surface &surface,
                                      const QC::Rect &rect,
                                      const QC::u32 *pixels,
                                      QC::u32 stridePixels)
    {
        return m_driver && m_driver->uploadSurfaceRegion(surface, rect, pixels, stridePixels);
    }

    bool Context::submitBatch(Batch &batch)
    {
        if (!m_driver)
            return false;

        batch.optimize();
        return m_driver->submitBatch(batch);
    }

    bool Context::present()
    {
        return m_driver && m_driver->present();
    }
}
