#pragma once

#include "QGfxDriver.h"

namespace QGfx
{
    using ScanoutUploadCallback = bool (*)(const QC::Rect &rect,
                                           const QC::u32 *pixels,
                                           QC::u32 stridePixels,
                                           void *userData);

    class VmwareSVGADriver : public Driver
    {
    public:
        VmwareSVGADriver();

        void setScanoutUploadCallback(ScanoutUploadCallback callback, void *userData);

        bool initialize() override;
        DriverCapabilities capabilities() const override;

        bool createSurface(Surface &surface) override;
        void destroySurface(Surface &surface) override;
        bool uploadSurfaceRegion(const Surface &surface,
                                 const QC::Rect &rect,
                                 const QC::u32 *pixels,
                                 QC::u32 stridePixels) override;
        bool submitBatch(const Batch &batch) override;
        bool present() override;

    private:
        void queueDirtyRect(const QC::Rect &rect);

        DriverCapabilities m_capabilities;
        QC::u32 m_nextSurfaceId = 1;
        bool m_initialized = false;
        ScanoutUploadCallback m_scanoutUploadCallback = nullptr;
        void *m_scanoutUploadUserData = nullptr;
        QC::Rect m_pendingDirtyRects[128] = {};
        QC::usize m_pendingDirtyCount = 0;
    };
}