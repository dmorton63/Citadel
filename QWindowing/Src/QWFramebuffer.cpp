// QWindowing Framebuffer - Display framebuffer management implementation
// Namespace: QW

#include "QWFramebuffer.h"
#include "QKMemHeap.h"
#include "QKMemTranslator.h"
#include "QCMemUtil.h"
#include "QCBuiltins.h"
#include "QCLogger.h"

// Provided by the kernel boot code (QKMain.cpp)
extern QC::u64 getHHDMOffset();

namespace QW
{

    namespace
    {
        inline void memcpy_stream64(void *dst, const void *src, QC::usize bytes)
        {
            auto *d = static_cast<QC::u8 *>(dst);
            auto *s = static_cast<const QC::u8 *>(src);

            QC::usize i = 0;
            for (; i + sizeof(QC::u64) <= bytes; i += sizeof(QC::u64))
            {
                const QC::u64 v = *reinterpret_cast<const QC::u64 *>(s + i);
                asm volatile("movnti %1, %0" : "=m"(*reinterpret_cast<volatile QC::u64 *>(d + i)) : "r"(v));
            }

            // Tail bytes (rare for our current 32bpp modes)
            for (; i < bytes; ++i)
            {
                d[i] = s[i];
            }

            // Ensure streaming stores are globally visible.
            QC::write_barrier();
        }
    }

    Framebuffer::Framebuffer()
        : m_buffer(nullptr),
          m_backBuffer(nullptr),
          m_physicalAddress(0),
          m_frontbufferIsMMIO(false),
          m_width(0),
          m_height(0),
          m_pitch(0),
          m_bpp(0),
          m_format(PixelFormat::ARGB8888),
          m_doubleBuffered(false),
          m_vsync(false)
    {
    }

    Framebuffer::~Framebuffer()
    {
        if (m_backBuffer)
        {
            QK::Memory::Heap::instance().free(m_backBuffer);
            m_backBuffer = nullptr;
        }
        // Note: m_buffer is typically memory-mapped, not freed
    }

    QC::Status Framebuffer::initialize(QC::uptr physicalAddress,
                                       QC::u32 width, QC::u32 height,
                                       QC::u32 pitch, PixelFormat format)
    {
        // NOTE: Despite the parameter name, Limine may provide a framebuffer address that is
        // already mapped into the higher-half direct map (HHDM). In that case, treating it as
        // a physical address and MMIO-mapping it will create a bogus mapping and can #PF.
        const QC::u64 hhdm = getHHDMOffset();
        const bool looksLikeHhdmVirt = (hhdm != 0) && (physicalAddress >= static_cast<QC::uptr>(hhdm));

        // If Limine passed an HHDM-mapped virtual address, keep it as a fallback.
        void *hhdmVirtPtr = nullptr;

        if (looksLikeHhdmVirt)
        {
            hhdmVirtPtr = reinterpret_cast<void *>(physicalAddress);
            m_physicalAddress = static_cast<QC::uptr>(physicalAddress - static_cast<QC::uptr>(hhdm));
        }
        else
        {
            m_physicalAddress = physicalAddress;
        }

        m_width = width;
        m_height = height;
        m_pitch = pitch;
        m_format = format;

        // Determine BPP from format
        switch (format)
        {
        case PixelFormat::RGB888:
        case PixelFormat::BGR888:
            m_bpp = 24;
            break;
        case PixelFormat::ARGB8888:
        case PixelFormat::ABGR8888:
            m_bpp = 32;
            break;
        case PixelFormat::RGB565:
        case PixelFormat::BGR565:
            m_bpp = 16;
            break;
        }

        // Map the physical framebuffer.
        // NOTE: Even if Limine provides a higher-half direct-map (HHDM-looking) pointer, that
        // mapping may be cacheable. For device VRAM this can lead to writes not reaching scanout
        // (blank screen) depending on paging attributes/emulation. Prefer an explicit MMIO mapping
        // when possible and keep Limine's pointer as a fallback.
        const QC::usize bufferSize = m_pitch * m_height;
        const QC::VirtAddr fbVirt = QK::Memory::Translator::instance().mapMMIO(
            static_cast<QC::PhysAddr>(m_physicalAddress),
            bufferSize);
        if (fbVirt)
        {
            m_buffer = reinterpret_cast<void *>(fbVirt);
            m_frontbufferIsMMIO = true;
        }
        else if (hhdmVirtPtr)
        {
            QC_LOG_WARN("QWFramebuffer", "Framebuffer MMIO map failed; falling back to Limine pointer (virt=0x%llX)",
                        static_cast<unsigned long long>(reinterpret_cast<QC::uptr>(hhdmVirtPtr)));
            m_buffer = hhdmVirtPtr;
            m_frontbufferIsMMIO = false;
        }
        else
        {
            QC_LOG_WARN("QWFramebuffer", "Failed to map framebuffer MMIO (phys=0x%llX size=0x%llX)",
                        static_cast<unsigned long long>(m_physicalAddress),
                        static_cast<unsigned long long>(bufferSize));

            // Last resort: legacy physical-as-pointer.
            m_buffer = reinterpret_cast<void *>(m_physicalAddress);
            m_frontbufferIsMMIO = false;
        }

        QC_LOG_INFO("QWFramebuffer", "Framebuffer mapped (phys=0x%llX virt=0x%llX size=0x%llX)",
                    static_cast<unsigned long long>(m_physicalAddress),
                    static_cast<unsigned long long>(reinterpret_cast<QC::uptr>(m_buffer)),
                    static_cast<unsigned long long>(bufferSize));

        // Allocate back buffer for double buffering
        m_backBuffer = QK::Memory::Heap::instance().allocate(bufferSize);
        if (m_backBuffer)
        {
            m_doubleBuffered = true;
            memset(m_backBuffer, 0, bufferSize);
        }

        return QC::Status::Success;
    }

    void Framebuffer::swap()
    {
        if (!m_doubleBuffered || !m_buffer || !m_backBuffer)
            return;

        const QC::usize bytes = m_pitch * m_height;

        // Copy back buffer to front buffer.
        // If the frontbuffer is a cacheable mapping (HHDM), use non-temporal stores so VRAM
        // sees the update without requiring an expensive full-cache flush.
        if (m_frontbufferIsMMIO)
            memcpy(m_buffer, m_backBuffer, bytes);
        else
        {
            memcpy_stream64(m_buffer, m_backBuffer, bytes);

            // Debug/reliability: some emulated SVGA framebuffers behave poorly when mapped as
            // cacheable RAM via HHDM. Even with non-temporal stores + sfence, scanout can appear
            // stale/black. A full cache writeback is expensive, but it makes the write visibility
            // question unambiguous.
            QC::wbinvd();
        }
    }

    void Framebuffer::setPixel(QC::u32 x, QC::u32 y, QC::u32 color)
    {
        if (x >= m_width || y >= m_height)
            return;

        void *target = m_doubleBuffered ? m_backBuffer : m_buffer;
        if (!target)
            return;

        QC::u8 *row = static_cast<QC::u8 *>(target) + y * m_pitch;

        switch (m_format)
        {
        case PixelFormat::ARGB8888:
        case PixelFormat::ABGR8888:
            reinterpret_cast<QC::u32 *>(row)[x] = color;
            break;
        case PixelFormat::RGB888:
        case PixelFormat::BGR888:
        {
            QC::u8 *pixel = row + x * 3;
            pixel[0] = color & 0xFF;
            pixel[1] = (color >> 8) & 0xFF;
            pixel[2] = (color >> 16) & 0xFF;
            break;
        }
        case PixelFormat::RGB565:
        case PixelFormat::BGR565:
            reinterpret_cast<QC::u16 *>(row)[x] = static_cast<QC::u16>(color);
            break;
        }
    }

    QC::u32 Framebuffer::getPixel(QC::u32 x, QC::u32 y) const
    {
        if (x >= m_width || y >= m_height)
            return 0;

        void *target = m_doubleBuffered ? m_backBuffer : m_buffer;
        if (!target)
            return 0;

        const QC::u8 *row = static_cast<const QC::u8 *>(target) + y * m_pitch;

        switch (m_format)
        {
        case PixelFormat::ARGB8888:
        case PixelFormat::ABGR8888:
            return reinterpret_cast<const QC::u32 *>(row)[x];
        case PixelFormat::RGB888:
        case PixelFormat::BGR888:
        {
            const QC::u8 *pixel = row + x * 3;
            return pixel[0] | (pixel[1] << 8) | (pixel[2] << 16) | 0xFF000000;
        }
        case PixelFormat::RGB565:
        case PixelFormat::BGR565:
            return reinterpret_cast<const QC::u16 *>(row)[x];
        }

        return 0;
    }

    void Framebuffer::clear(QC::u32 color)
    {
        fillRect(0, 0, m_width, m_height, color);
    }

    void Framebuffer::fillRect(QC::u32 x, QC::u32 y, QC::u32 w, QC::u32 h, QC::u32 color)
    {
        if (x >= m_width || y >= m_height)
            return;

        if (x + w > m_width)
            w = m_width - x;
        if (y + h > m_height)
            h = m_height - y;

        void *target = m_doubleBuffered ? m_backBuffer : m_buffer;
        if (!target)
            return;

        for (QC::u32 row = y; row < y + h; ++row)
        {
            for (QC::u32 col = x; col < x + w; ++col)
            {
                setPixel(col, row, color);
            }
        }
    }

    void Framebuffer::blit(QC::u32 x, QC::u32 y, const void *src,
                           QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch)
    {
        if (!src)
            return;

        void *target = m_doubleBuffered ? m_backBuffer : m_buffer;
        if (!target)
            return;

        // Clip to screen bounds
        QC::u32 drawWidth = (x + srcWidth > m_width) ? (m_width - x) : srcWidth;
        QC::u32 drawHeight = (y + srcHeight > m_height) ? (m_height - y) : srcHeight;

        const QC::u8 *srcRow = static_cast<const QC::u8 *>(src);
        QC::u8 *dstRow = static_cast<QC::u8 *>(target) + y * m_pitch + x * (m_bpp / 8);

        for (QC::u32 row = 0; row < drawHeight; ++row)
        {
            memcpy(dstRow, srcRow, drawWidth * (m_bpp / 8));
            srcRow += srcPitch;
            dstRow += m_pitch;
        }
    }

    QC::Status Framebuffer::setMode(QC::u32 width, QC::u32 height, QC::u32 bpp)
    {
        (void)width;
        (void)height;
        (void)bpp;
        // TODO: Implement mode switching via hardware/firmware
        return QC::Status::NotSupported;
    }

    void Framebuffer::getAvailableModes(QC::u32 *widths, QC::u32 *heights,
                                        QC::u32 *count, QC::u32 maxCount)
    {
        (void)widths;
        (void)heights;
        (void)maxCount;
        // TODO: Query available modes from hardware
        *count = 0;
    }

} // namespace QW
