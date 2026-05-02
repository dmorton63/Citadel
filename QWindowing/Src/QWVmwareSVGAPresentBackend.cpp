// QWindowing VMware SVGA Present Backend
// Namespace: QW

#include "QWVmwareSVGAPresentBackend.h"

#include "QWFramebuffer.h"
#include "QDrvVmwareSVGA.h"
#include "QCLogger.h"
#include "QCBuiltins.h"

namespace QW
{
    namespace
    {
        QGfx::PixelFormat toQGfxPixelFormat(Framebuffer *fb)
        {
            if (!fb)
                return QGfx::PixelFormat::Unknown;

            switch (fb->format())
            {
            case PixelFormat::ARGB8888:
                return QGfx::PixelFormat::ARGB8888;
            case PixelFormat::ABGR8888:
                return QGfx::PixelFormat::XRGB8888;
            default:
                return QGfx::PixelFormat::Unknown;
            }
        }

        bool uploadScanoutRectThunk(const QC::Rect &rect,
                                    const QC::u32 *pixels,
                                    QC::u32 stridePixels,
                                    void *userData)
        {
            auto *backend = static_cast<VmwareSVGAPresentBackend *>(userData);
            return backend && backend->uploadScanoutRect(rect, pixels, stridePixels);
        }
    }

    void VmwareSVGAPresentBackend::initialize(Framebuffer *fb)
    {
        m_framebuffer = fb;
        (void)QDrv::VmwareSVGA::instance().initialize();
        resetAccelerationStats();
        m_pendingRectCopyBatch.clear();

        m_qgfxReady = false;
        m_qgfxContext.setDriver(&m_qgfxDriver);
        m_qgfxDriver.setScanoutUploadCallback(uploadScanoutRectThunk, this);

        if (!m_framebuffer || m_framebuffer->bpp() != 32)
            return;

        if (!m_qgfxContext.initialize())
            return;

        m_scanoutSurface = QGfx::Surface{};
        m_scanoutSurface.width = m_framebuffer->width();
        m_scanoutSurface.height = m_framebuffer->height();
        m_scanoutSurface.format = toQGfxPixelFormat(m_framebuffer);
        m_scanoutSurface.usage = QGfx::SurfaceUsage::Scanout;
        if (m_scanoutSurface.format == QGfx::PixelFormat::Unknown)
            return;

        m_qgfxReady = m_qgfxContext.createSurface(m_scanoutSurface);
        m_accelerationStats.qgfxActive = m_qgfxReady;
        m_accelerationStats.qgfxScanoutUploadsActive = m_qgfxReady && m_qgfxDriver.capabilities().supportsScanoutUploads;
        m_accelerationStats.qgfxRectCopyActive = m_qgfxReady && m_qgfxDriver.capabilities().supportsScreenRectCopy;
    }

    void VmwareSVGAPresentBackend::resetAccelerationStats()
    {
        m_accelerationStats = PresentAccelerationStats{};
    }

    void VmwareSVGAPresentBackend::present()
    {
        // Full-frame fallback.
        present(nullptr, 0);
    }

    void VmwareSVGAPresentBackend::present(const QC::Rect *dirtyRects, QC::usize dirtyCount)
    {
        static QC::u32 s_presentCount = 0;
        ++s_presentCount;
        if ((s_presentCount % 240u) == 1u)
        {
            QC_LOG_INFO("QWPresent", "VmwareSVGAPresentBackend::present #%u (dirty=%lu)",
                        s_presentCount,
                        static_cast<unsigned long>(dirtyCount));
        }

        if (!m_framebuffer)
            return;

        // We still swap the whole backbuffer today (rendering path is still full-frame).
        auto &svga = QDrv::VmwareSVGA::instance();
        if (!(svga.has2D() || svga.initialize2D()))
        {
            m_framebuffer->swap();
            return;
        }

        const QC::u32 fbW = m_framebuffer->width();
        const QC::u32 fbH = m_framebuffer->height();
        const bool useQGfxUploads = m_qgfxReady && m_scanoutSurface.id.isValid();
        const bool havePendingRectCopies = m_pendingRectCopyBatch.ops().size() > 0;
        m_accelerationStats.qgfxActive = m_qgfxReady;
        m_accelerationStats.qgfxScanoutUploadsActive = useQGfxUploads && m_qgfxDriver.capabilities().supportsScanoutUploads;
        m_accelerationStats.qgfxRectCopyActive = m_qgfxReady && m_qgfxDriver.capabilities().supportsScreenRectCopy;
        if (useQGfxUploads)
            ++m_accelerationStats.qgfxPresentCalls;

        if (havePendingRectCopies)
        {
            (void)m_qgfxContext.submitBatch(m_pendingRectCopyBatch);
            m_pendingRectCopyBatch.clear();
        }

        // If no dirty rects are provided, we don't know what's changed;
        // update the whole frame so QEMU refreshes its surface.
        if (!dirtyRects || dirtyCount == 0)
        {
            const QC::Rect fullRect{0, 0, fbW, fbH};
            if (useQGfxUploads)
            {
                const auto *pixels = static_cast<const QC::u32 *>(m_framebuffer->backBuffer());
                if (m_qgfxContext.uploadSurfaceRegion(m_scanoutSurface, fullRect, pixels, m_framebuffer->pitch() / sizeof(QC::u32)) &&
                    m_qgfxContext.present())
                {
                    ++m_accelerationStats.qgfxPresentSuccesses;
                    return;
                }

                ++m_accelerationStats.qgfxScanoutUploadFallbacks;
            }

            m_framebuffer->swap();
            svga.updateRect(0, 0, fbW, fbH);
            return;
        }

        // Clamp/clip each rect to screen and issue UPDATE.
        // If we get an excessive number of rects, fall back to fullscreen.
        if (dirtyCount > 128)
        {
            m_framebuffer->swap();
            svga.updateRect(0, 0, fbW, fbH);
            return;
        }

        QC::Rect clippedRects[128];
        QC::usize clippedCount = 0;
        bool qgfxUploadSucceeded = useQGfxUploads;

        for (QC::usize i = 0; i < dirtyCount; ++i)
        {
            const QC::Rect &r = dirtyRects[i];
            if (r.isEmpty())
                continue;

            QC::i32 x0 = r.x;
            QC::i32 y0 = r.y;
            QC::i32 x1 = r.right();
            QC::i32 y1 = r.bottom();

            if (x1 <= 0 || y1 <= 0)
                continue;
            if (x0 >= static_cast<QC::i32>(fbW) || y0 >= static_cast<QC::i32>(fbH))
                continue;

            if (x0 < 0)
                x0 = 0;
            if (y0 < 0)
                y0 = 0;
            if (x1 > static_cast<QC::i32>(fbW))
                x1 = static_cast<QC::i32>(fbW);
            if (y1 > static_cast<QC::i32>(fbH))
                y1 = static_cast<QC::i32>(fbH);

            const QC::u32 w = static_cast<QC::u32>(x1 - x0);
            const QC::u32 h = static_cast<QC::u32>(y1 - y0);
            if (w == 0 || h == 0)
                continue;

            const QC::Rect clipped{ x0, y0, w, h };
            if (qgfxUploadSucceeded)
            {
                const auto *srcPixels = reinterpret_cast<const QC::u32 *>(
                    static_cast<const QC::u8 *>(m_framebuffer->backBuffer()) +
                    static_cast<QC::usize>(y0) * m_framebuffer->pitch() +
                    static_cast<QC::usize>(x0) * sizeof(QC::u32));

                if (!m_qgfxContext.uploadSurfaceRegion(m_scanoutSurface,
                                                      clipped,
                                                      srcPixels,
                                                      m_framebuffer->pitch() / sizeof(QC::u32)))
                {
                    qgfxUploadSucceeded = false;
                    ++m_accelerationStats.qgfxScanoutUploadFallbacks;
                    m_framebuffer->swapRect(clipped);
                }
            }
            else
            {
                m_framebuffer->swapRect(clipped);
            }
            clippedRects[clippedCount++] = clipped;
        }

        if (clippedCount == 0)
            return;

        if (qgfxUploadSucceeded)
        {
            if (m_qgfxContext.present())
            {
                ++m_accelerationStats.qgfxPresentSuccesses;
                return;
            }

            ++m_accelerationStats.qgfxScanoutUploadFallbacks;
        }

        if (clippedCount == 1)
        {
            const QC::Rect &r = clippedRects[0];
            svga.updateRect(static_cast<QC::u32>(r.x), static_cast<QC::u32>(r.y), r.width, r.height);
            return;
        }

        svga.updateRects(clippedRects, clippedCount);
    }

    bool VmwareSVGAPresentBackend::supportsRectCopy() const
    {
        return m_qgfxReady && m_qgfxDriver.capabilities().supportsScreenRectCopy;
    }

    void VmwareSVGAPresentBackend::rectCopy(const QC::Rect &src, const QC::Rect &dst)
    {
        if (!supportsRectCopy())
            return;
        if (src.width == 0 || src.height == 0 || dst.width != src.width || dst.height != src.height)
            return;

        if (!m_pendingRectCopyBatch.target().isValid())
            m_pendingRectCopyBatch.setTarget(m_scanoutSurface.id);

        QGfx::DrawOp op;
        op.srcSurface = m_scanoutSurface.id;
        op.srcRect = src;
        op.dstRect = dst;
        m_pendingRectCopyBatch.addOp(op);

        ++m_accelerationStats.qgfxRectCopyBatches;
        ++m_accelerationStats.qgfxRectCopyOps;
    }

    bool VmwareSVGAPresentBackend::uploadScanoutRect(const QC::Rect &rect,
                                                     const QC::u32 *pixels,
                                                     QC::u32 stridePixels)
    {
        (void)pixels;
        (void)stridePixels;

        if (!m_framebuffer)
            return false;

        ++m_accelerationStats.qgfxScanoutUploadCalls;
        ++m_accelerationStats.qgfxScanoutUploadRects;
        m_framebuffer->swapRect(rect);
        return true;
    }

    bool VmwareSVGAPresentBackend::hasHardwareCursor() const
    {
        return QDrv::VmwareSVGA::instance().hasHardwareCursor();
    }

    void VmwareSVGAPresentBackend::setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                                                  QC::u16 hotspotX, QC::u16 hotspotY)
    {
        QDrv::VmwareSVGA::instance().setCursorImage(pixels, width, height, hotspotX, hotspotY);
    }

    void VmwareSVGAPresentBackend::setCursorVisible(bool visible)
    {
        QDrv::VmwareSVGA::instance().setCursorVisible(visible);
    }

    void VmwareSVGAPresentBackend::setCursorPosition(QC::u16 x, QC::u16 y)
    {
        QDrv::VmwareSVGA::instance().setCursorPosition(x, y);
    }
}
