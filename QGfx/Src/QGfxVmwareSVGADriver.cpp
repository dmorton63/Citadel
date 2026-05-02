#include "QGfxVmwareSVGADriver.h"

#include "QGfxBatch.h"
#include "QDrvVmwareSVGA.h"

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
        auto &svga = QDrv::VmwareSVGA::instance();
        const bool available = svga.initialize() && (svga.has2D() || svga.initialize2D());

        m_capabilities = DriverCapabilities{};
        m_capabilities.supportsScanoutUploads = available;
        m_capabilities.supportsPresent = available;
        m_capabilities.supportsScreenRectCopy = available;

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

        auto &svga = QDrv::VmwareSVGA::instance();
        for (QC::usize i = 0; i < batch.ops().size(); ++i)
        {
            const DrawOp &op = batch.ops()[i];
            svga.rectCopy(static_cast<QC::u32>(op.srcRect.x),
                          static_cast<QC::u32>(op.srcRect.y),
                          static_cast<QC::u32>(op.dstRect.x),
                          static_cast<QC::u32>(op.dstRect.y),
                          op.srcRect.width,
                          op.srcRect.height);
        }

        return true;
    }

    bool VmwareSVGADriver::present()
    {
        if (!m_initialized)
            return false;

        if (m_pendingDirtyCount == 0)
            return true;

        auto &svga = QDrv::VmwareSVGA::instance();
        if (m_pendingDirtyCount == 1)
        {
            const QC::Rect &rect = m_pendingDirtyRects[0];
            svga.updateRect(static_cast<QC::u32>(rect.x),
                            static_cast<QC::u32>(rect.y),
                            rect.width,
                            rect.height);
        }
        else
        {
            svga.updateRects(m_pendingDirtyRects, m_pendingDirtyCount);
        }

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