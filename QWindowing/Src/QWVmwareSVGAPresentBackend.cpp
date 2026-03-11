// QWindowing VMware SVGA Present Backend
// Namespace: QW

#include "QWVmwareSVGAPresentBackend.h"

#include "QWFramebuffer.h"
#include "QDrvVmwareSVGA.h"
#include "QCLogger.h"
#include "QCBuiltins.h"

namespace QW
{
    void VmwareSVGAPresentBackend::initialize(Framebuffer *fb)
    {
        m_framebuffer = fb;
        (void)QDrv::VmwareSVGA::instance().initialize();
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
        m_framebuffer->swap();

        auto &svga = QDrv::VmwareSVGA::instance();
        if (!(svga.has2D() || svga.initialize2D()))
            return;

        const QC::u32 fbW = m_framebuffer->width();
        const QC::u32 fbH = m_framebuffer->height();

        // If no dirty rects are provided, we don't know what's changed;
        // update the whole frame so QEMU refreshes its surface.
        if (!dirtyRects || dirtyCount == 0)
        {
            svga.updateRect(0, 0, fbW, fbH);
            return;
        }

        // Clamp/clip each rect to screen and issue UPDATE.
        // If we get an excessive number of rects, fall back to fullscreen.
        if (dirtyCount > 128)
        {
            svga.updateRect(0, 0, fbW, fbH);
            return;
        }

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

            svga.updateRect(static_cast<QC::u32>(x0), static_cast<QC::u32>(y0), w, h);
        }
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
