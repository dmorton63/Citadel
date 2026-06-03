#include "QGfxVmwareSVGADriver.h"

#include "QGfxBatch.h"
#include "QDrvDisplayBootstrap.h"

namespace QGfx
{
    namespace
    {
        inline QC::Rect unionRects(const QC::Rect &a, const QC::Rect &b)
        {
            const QC::i32 left = (a.x < b.x) ? a.x : b.x;
            const QC::i32 top = (a.y < b.y) ? a.y : b.y;
            const QC::i32 right = (a.right() > b.right()) ? a.right() : b.right();
            const QC::i32 bottom = (a.bottom() > b.bottom()) ? a.bottom() : b.bottom();
            return QC::Rect{left,
                            top,
                            static_cast<QC::u32>(right - left),
                            static_cast<QC::u32>(bottom - top)};
        }
    }

    VmwareSVGADriver::VmwareSVGADriver() = default;

    void VmwareSVGADriver::setScanoutUploadCallback(ScanoutUploadCallback callback, void *userData)
    {
        m_scanoutUploadCallback = callback;
        m_scanoutUploadUserData = userData;
    }

    bool VmwareSVGADriver::initialize()
    {
        const bool available = QDrv::Display::cvd_has_accelerated_present();

        m_capabilities = DriverCapabilities{};
        m_capabilities.supportsScanoutUploads = available;
        m_capabilities.supportsPresent = available;
        m_capabilities.supportsScreenRectCopy = QDrv::Display::cvd_has_accelerated_rect_copy();

        m_initialized = available;
        return m_initialized;
    }

    DriverCapabilities VmwareSVGADriver::capabilities() const
    {
        return m_capabilities;
    }

    bool VmwareSVGADriver::createSurface(Surface &surface)
    {
        if (!m_initialized || surface.width == 0 || surface.height == 0 || surface.format == PixelFormat::Unknown)
            return false;

        surface.id.value = m_nextSurfaceId++;

        // Current QEMU VMware path does not expose a general offscreen surface API here.
        // Only scanout-like surfaces are given a hardware handle placeholder.
        if (surface.usage == SurfaceUsage::Scanout)
            surface.gpuHandle.value = surface.id.value;
        else
            surface.gpuHandle.value = 0;

        return true;
    }

    void VmwareSVGADriver::destroySurface(Surface &surface)
    {
        surface.id.value = 0;
        surface.gpuHandle.value = 0;
        surface.clearDirtyRegion();
    }

    bool VmwareSVGADriver::uploadSurfaceRegion(const Surface &surface,
                                               const QC::Rect &rect,
                                               const QC::u32 *pixels,
                                               QC::u32 stridePixels)
    {
        if (!m_initialized || !surface.id.isValid() || rect.isEmpty())
            return false;

        if (surface.usage != SurfaceUsage::Scanout || !m_scanoutUploadCallback)
            return false;

        if (!m_scanoutUploadCallback(rect, pixels, stridePixels, m_scanoutUploadUserData))
            return false;

        if (surface.displayDevice && surface.displaySwapchain && surface.displaySurfaceId.isValid())
        {
            m_presentDevice = surface.displayDevice;
            m_presentSwapchain = surface.displaySwapchain;
            m_presentSurface = surface.displaySurfaceId;
            m_presentReadyValue = surface.displayReadyValue;
        }

        queueDirtyRect(rect);
        return true;
    }

    bool VmwareSVGADriver::submitBatch(const Batch &batch)
    {
        if (!m_initialized)
            return false;

        if (batch.ops().size() == 0)
            return true;

        // Conservative MVP: only screen-to-screen copies are currently exposed.
        for (QC::usize i = 0; i < batch.ops().size(); ++i)
        {
            const DrawOp &op = batch.ops()[i];
            if (!op.srcSurface.isValid())
                return false;
            if (op.opacity != 255)
                return false;
            if (op.transform != Transform::None)
                return false;
            if (op.srcRect.width != op.dstRect.width || op.srcRect.height != op.dstRect.height)
                return false;
        }

        for (QC::usize i = 0; i < batch.ops().size(); ++i)
        {
            const DrawOp &op = batch.ops()[i];
            if (!m_presentDevice || !m_presentSwapchain || !m_presentSurface.isValid())
                return false;

            if (QDrv::Display::cvd_rect_copy(m_presentDevice,
                                             m_presentSwapchain,
                                             m_presentSurface,
                                             op.srcRect,
                                             op.dstRect) != QDrv::Display::CVD_OK)
            {
                return false;
            }
        }

        return true;
    }

    bool VmwareSVGADriver::present()
    {
        if (!m_initialized)
            return false;

        if (m_pendingDirtyCount == 0)
            return true;

        if (!m_presentDevice || !m_presentSwapchain || !m_presentSurface.isValid())
            return false;

        const QDrv::Display::cvd_result_t presentResult =
            QDrv::Display::cvd_present_regions(m_presentDevice,
                                               m_presentSwapchain,
                                               m_presentSurface,
                                               m_presentReadyValue,
                                               m_pendingDirtyRects,
                                               m_pendingDirtyCount,
                                               QDrv::Display::CVD_PRESENT_VSYNC);
        if (presentResult != QDrv::Display::CVD_OK)
            return false;

        m_pendingDirtyCount = 0;
        return true;
    }

    void VmwareSVGADriver::queueDirtyRect(const QC::Rect &rect)
    {
        if (m_pendingDirtyCount < (sizeof(m_pendingDirtyRects) / sizeof(m_pendingDirtyRects[0])))
        {
            m_pendingDirtyRects[m_pendingDirtyCount++] = rect;
            return;
        }

        m_pendingDirtyRects[0] = unionRects(m_pendingDirtyRects[0], rect);
        m_pendingDirtyCount = 1;
    }
}