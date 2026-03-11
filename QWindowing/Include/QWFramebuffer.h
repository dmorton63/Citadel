#pragma once

// QWindowing Framebuffer - Display framebuffer management
// Namespace: QW

#include "QCTypes.h"
#include "QWWindowManager.h"

namespace QW
{

    // Pixel format
    enum class PixelFormat : QC::u8
    {
        RGB888,
        BGR888,
        ARGB8888,
        ABGR8888,
        RGB565,
        BGR565
    };

    class Framebuffer
    {
    public:
        Framebuffer();
        ~Framebuffer();

        // Initialization
        QC::Status initialize(QC::uptr physicalAddress,
                              QC::u32 width, QC::u32 height,
                              QC::u32 pitch, PixelFormat format);

        // Properties
        QC::u32 width() const { return m_width; }
        QC::u32 height() const { return m_height; }
        QC::u32 pitch() const { return m_pitch; }
        QC::u32 bpp() const { return m_bpp; }
        PixelFormat format() const { return m_format; }

        // Buffer access
        void *buffer() const { return m_buffer; }
        QC::usize bufferSize() const { return m_pitch * m_height; }

        // Double buffering
        void *backBuffer() const { return m_backBuffer; }
        void swap();
        void setVSync(bool enabled) { m_vsync = enabled; }

        // Pixel operations
        void setPixel(QC::u32 x, QC::u32 y, QC::u32 color);
        QC::u32 getPixel(QC::u32 x, QC::u32 y) const;

        void clear(QC::u32 color);
        void fillRect(QC::u32 x, QC::u32 y, QC::u32 w, QC::u32 h, QC::u32 color);

        // Blitting
        void blit(QC::u32 x, QC::u32 y, const void *src,
                  QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch);

        // Mode switching
        QC::Status setMode(QC::u32 width, QC::u32 height, QC::u32 bpp);
        void getAvailableModes(QC::u32 *widths, QC::u32 *heights,
                               QC::u32 *count, QC::u32 maxCount);

    private:
        void *m_buffer;
        void *m_backBuffer;
        QC::uptr m_physicalAddress;

        bool m_frontbufferIsMMIO;

        QC::u32 m_width;
        QC::u32 m_height;
        QC::u32 m_pitch;
        QC::u32 m_bpp;
        PixelFormat m_format;

        bool m_doubleBuffered;
        bool m_vsync;
    };

} // namespace QW
