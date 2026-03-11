#pragma once

#include "QCTypes.h"

namespace QDrv
{
    class VmwareSVGA
    {
    public:
        static VmwareSVGA &instance();

        // Detects VMware SVGA II device and enables hardware cursor if supported.
        // Safe to call multiple times.
        bool initialize();

        bool isAvailable() const { return m_available; }
        bool hasHardwareCursor() const { return m_hwCursor; }
        bool has2D() const { return m_2dAvailable; }

        // Query common mode/framebuffer properties (valid after initialize())
        QC::u32 bytesPerLine() const;
        QC::u32 framebufferStart() const;
        QC::u32 framebufferSizeBytes() const;
        QC::u32 width() const;
        QC::u32 height() const;
        QC::u32 bitsPerPixel() const;

        // Defines the hardware cursor image and hotspot (best-effort).
        // If unsupported/unavailable, this is a no-op.
        void setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                            QC::u16 hotspotX, QC::u16 hotspotY);
        void setCursorVisible(bool visible);
        void setCursorPosition(QC::u16 x, QC::u16 y);

        // SVGA2D FIFO (best-effort). Safe to call multiple times.
        bool initialize2D();

        // SVGA2D commands (no-op if 2D FIFO is unavailable)
        void updateRect(QC::u32 x, QC::u32 y, QC::u32 w, QC::u32 h);
        void rectCopy(QC::u32 srcX, QC::u32 srcY, QC::u32 dstX, QC::u32 dstY, QC::u32 w, QC::u32 h);

    private:
        VmwareSVGA();
        VmwareSVGA(const VmwareSVGA &) = delete;
        VmwareSVGA &operator=(const VmwareSVGA &) = delete;

        QC::u32 readReg(QC::u32 reg) const;
        void writeReg(QC::u32 reg, QC::u32 value) const;

        bool m_initialized;
        bool m_available;
        bool m_hwCursor;
        bool m_cursorVisible;
        bool m_2dInitialized;
        bool m_2dAvailable;
        QC::u16 m_ioBase;

        QC::VirtAddr m_fifoVirt;
        volatile QC::u32 *m_fifo;
        QC::u32 m_fifoSizeBytes;

        bool m_cursorDefined = false;
    };
}
