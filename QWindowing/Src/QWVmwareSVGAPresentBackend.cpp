// QWindowing VMware SVGA Present Backend
// Namespace: QW

#include "QWVmwareSVGAPresentBackend.h"

#include "QWDisplayBootstrapUtil.h"
#include "QCLogger.h"
#include "QCBuiltins.h"

#include <cstring>

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

        const WindowSurfaceBlit *findWindowSurfaceBlit(const WindowSurfaceBlit *blits,
                                                       QC::usize blitCount,
                                                       QGfx::SurfaceId surfaceId)
        {
            if (!blits || !surfaceId.isValid())
                return nullptr;

            for (QC::usize i = 0; i < blitCount; ++i)
            {
                if (blits[i].surface && blits[i].surface->id.value == surfaceId.value)
                    return &blits[i];
            }

            return nullptr;
        }
    }

    void VmwareSVGAPresentBackend::initialize(Framebuffer *fb)
    {
        m_framebuffer = fb;

        if (m_framebuffer)
        {
            const QDrv::Display::cvd_boot_framebuffer_desc_t desc = makeBootFramebufferDesc(m_framebuffer);
            QDrv::Display::cvd_sync_value_t readyValue = 0;
            (void)QDrv::Display::cvd_open_boot_swapchain(&desc,
                                                         &m_device,
                                                         &m_output,
                                                         &m_swapchain,
                                                         &m_presentSurface,
                                                         &readyValue);
        }

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
        if (m_qgfxReady && m_device && m_swapchain && m_presentSurface.isValid())
        {
            QGfx::DisplaySurfaceBinding binding{};
            binding.device = m_device;
            binding.swapchain = m_swapchain;
            binding.surface_id = m_presentSurface;
            binding.ready_value = 0;
            QGfx::bindDisplaySurface(m_scanoutSurface, binding);
        }
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

        if (!m_device || !m_swapchain || !m_presentSurface.isValid())
            return;

        // We still swap the whole backbuffer today (rendering path is still full-frame).
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

            (void)QDrv::Display::cvd_present_regions(m_device,
                                                     m_swapchain,
                                                     m_presentSurface,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     QDrv::Display::CVD_PRESENT_VSYNC);
            return;
        }

        // Clamp/clip each rect to screen and issue UPDATE.
        // If we get an excessive number of rects, fall back to fullscreen.
        if (dirtyCount > 128)
        {
            (void)QDrv::Display::cvd_present_regions(m_device,
                                                     m_swapchain,
                                                     m_presentSurface,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     QDrv::Display::CVD_PRESENT_VSYNC);
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
                }
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

        (void)QDrv::Display::cvd_present_regions(m_device,
                                                 m_swapchain,
                                                 m_presentSurface,
                                                 0,
                                                 clippedRects,
                                                 clippedCount,
                                                 QDrv::Display::CVD_PRESENT_VSYNC);
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

    bool VmwareSVGAPresentBackend::submitWindowSurfaceBatch(const QGfx::Batch &batch,
                                                            const WindowSurfaceBlit *blits,
                                                            QC::usize blitCount)
    {
        if (!m_framebuffer || !blits)
            return false;

        const QC::u32 fbW = m_framebuffer->width();
        const QC::u32 fbH = m_framebuffer->height();
        bool submittedAny = false;

        for (QC::usize i = 0; i < batch.ops().size(); ++i)
        {
            const QGfx::DrawOp &op = batch.ops()[i];
            const WindowSurfaceBlit *blit = findWindowSurfaceBlit(blits, blitCount, op.srcSurface);
            if (!blit || !blit->surface || !blit->pixels || blit->stridePixels == 0)
                return false;

            QC::Rect localRect = blit->dirtyRect;
            if (localRect.isEmpty())
                continue;

            if (localRect.x < 0)
            {
                const QC::i32 trim = -localRect.x;
                if (localRect.width <= static_cast<QC::u32>(trim))
                    continue;
                localRect.x = 0;
                localRect.width -= static_cast<QC::u32>(trim);
            }
            if (localRect.y < 0)
            {
                const QC::i32 trim = -localRect.y;
                if (localRect.height <= static_cast<QC::u32>(trim))
                    continue;
                localRect.y = 0;
                localRect.height -= static_cast<QC::u32>(trim);
            }

            if (localRect.right() > static_cast<QC::i32>(blit->surface->width))
            {
                const QC::i32 trimmedWidth = static_cast<QC::i32>(blit->surface->width) - localRect.x;
                if (trimmedWidth <= 0)
                    continue;
                localRect.width = static_cast<QC::u32>(trimmedWidth);
            }
            if (localRect.bottom() > static_cast<QC::i32>(blit->surface->height))
            {
                const QC::i32 trimmedHeight = static_cast<QC::i32>(blit->surface->height) - localRect.y;
                if (trimmedHeight <= 0)
                    continue;
                localRect.height = static_cast<QC::u32>(trimmedHeight);
            }

            QC::i32 dstX = op.dstRect.x + localRect.x;
            QC::i32 dstY = op.dstRect.y + localRect.y;

            if (dstX < 0)
            {
                const QC::i32 trim = -dstX;
                if (localRect.width <= static_cast<QC::u32>(trim))
                    continue;
                localRect.x += trim;
                localRect.width -= static_cast<QC::u32>(trim);
                dstX = 0;
            }
            if (dstY < 0)
            {
                const QC::i32 trim = -dstY;
                if (localRect.height <= static_cast<QC::u32>(trim))
                    continue;
                localRect.y += trim;
                localRect.height -= static_cast<QC::u32>(trim);
                dstY = 0;
            }

            if (dstX >= static_cast<QC::i32>(fbW) || dstY >= static_cast<QC::i32>(fbH))
                continue;

            if (dstX + static_cast<QC::i32>(localRect.width) > static_cast<QC::i32>(fbW))
            {
                localRect.width = static_cast<QC::u32>(static_cast<QC::i32>(fbW) - dstX);
            }
            if (dstY + static_cast<QC::i32>(localRect.height) > static_cast<QC::i32>(fbH))
            {
                localRect.height = static_cast<QC::u32>(static_cast<QC::i32>(fbH) - dstY);
            }

            if (localRect.isEmpty())
                continue;

            const QC::u32 *srcPixels = blit->pixels +
                                       static_cast<QC::usize>(localRect.y) * blit->stridePixels +
                                       static_cast<QC::usize>(localRect.x);

            m_framebuffer->blit(static_cast<QC::u32>(dstX),
                                static_cast<QC::u32>(dstY),
                                srcPixels,
                                localRect.width,
                                localRect.height,
                                blit->stridePixels * sizeof(QC::u32));
            submittedAny = true;
        }

        return submittedAny;
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
        (void)rect;
        (void)pixels;
        (void)stridePixels;
        return true;
    }

    bool VmwareSVGAPresentBackend::hasHardwareCursor() const
    {
        if (!m_output)
            return false;

        QDrv::Display::cvd_output_caps_t caps{};
        return QDrv::Display::cvd_get_output_caps(m_output, &caps) == QDrv::Display::CVD_OK &&
               (caps.flags & QDrv::Display::CVD_OUTPUT_CAP_HW_CURSOR) != 0;
    }

    void VmwareSVGAPresentBackend::setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                                                  QC::u16 hotspotX, QC::u16 hotspotY)
    {
        if (!pixels || !m_device || !m_output || width == 0 || height == 0)
            return;

        if (!m_cursorSurface.isValid())
        {
            QDrv::Display::cvd_surface_desc_t desc{};
            desc.size.width = width;
            desc.size.height = height;
            desc.format = QDrv::Display::CVD_FORMAT_ARGB8888;
            desc.flags = QDrv::Display::CVD_SURFACE_CPU_VISIBLE | QDrv::Display::CVD_SURFACE_CURSOR_CAPABLE | QDrv::Display::CVD_SURFACE_LINEAR;
            if (QDrv::Display::cvd_surface_create(m_device, &desc, &m_cursorSurface) != QDrv::Display::CVD_OK)
                return;
        }

        QDrv::Display::cvd_surface_map_t map{};
        if (QDrv::Display::cvd_surface_map(m_device, m_cursorSurface, QDrv::Display::CVD_MAP_WRITE_DISCARD, &map) != QDrv::Display::CVD_OK)
            return;

        for (QC::u32 row = 0; row < height; ++row)
        {
            QC::u8 *dst = static_cast<QC::u8 *>(map.ptr) + static_cast<QC::usize>(row) * map.pitch;
            const QC::u8 *src = reinterpret_cast<const QC::u8 *>(pixels + static_cast<QC::usize>(row) * width);
            memcpy(dst, src, static_cast<QC::usize>(width) * sizeof(QC::u32));
        }

        (void)QDrv::Display::cvd_surface_unmap(m_device, m_cursorSurface);
        (void)QDrv::Display::cvd_cursor_set_image(m_output, m_cursorSurface, hotspotX, hotspotY);
    }

    void VmwareSVGAPresentBackend::setCursorVisible(bool visible)
    {
        if (m_output)
            (void)QDrv::Display::cvd_cursor_show(m_output, visible);
    }

    void VmwareSVGAPresentBackend::setCursorPosition(QC::u16 x, QC::u16 y)
    {
        if (m_output)
            (void)QDrv::Display::cvd_cursor_set_position(m_output, x, y);
    }
}
