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
        QDrv::Display::cvd_device_t m_presentDevice = nullptr;
        QDrv::Display::cvd_swapchain_t m_presentSwapchain = nullptr;
        QDrv::Display::cvd_surface_id_t m_presentSurface{};
        QDrv::Display::cvd_sync_value_t m_presentReadyValue = 0;
        QC::u32 m_nextSurfaceId = 1;
        bool m_initialized = false;
        ScanoutUploadCallback m_scanoutUploadCallback = nullptr;
        void *m_scanoutUploadUserData = nullptr;
        QC::Rect m_pendingDirtyRects[128] = {};
        QC::usize m_pendingDirtyCount = 0;
    };
}