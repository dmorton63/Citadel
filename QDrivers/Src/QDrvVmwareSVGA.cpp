#include "QDrvVmwareSVGA.h"

#include "QArchPCI.h"
#include "QArchPort.h"
#include "QKMemTranslator.h"
#include "QCBuiltins.h"
#include "QCLogger.h"

namespace QDrv
{
    namespace
    {
        constexpr QC::u16 VMwareVendor = 0x15AD;
        constexpr QC::u16 SVGADevice = 0x0405;

        // SVGA I/O offsets (added to ioBase from PCI BAR0)
        constexpr QC::u16 SVGA_INDEX_PORT = 0x0;
        constexpr QC::u16 SVGA_VALUE_PORT = 0x1;

        // SVGA IDs
        constexpr QC::u32 SVGA_ID_0 = 0x90000000;
        constexpr QC::u32 SVGA_ID_1 = 0x90000001;
        constexpr QC::u32 SVGA_ID_2 = 0x90000002;

        // SVGA registers
        constexpr QC::u32 SVGA_REG_ID = 0;
        constexpr QC::u32 SVGA_REG_ENABLE = 1;

        // Mode registers
        constexpr QC::u32 SVGA_REG_WIDTH = 2;
        constexpr QC::u32 SVGA_REG_HEIGHT = 3;
        // 4 = MAX_WIDTH, 5 = MAX_HEIGHT
        constexpr QC::u32 SVGA_REG_BITS_PER_PIXEL = 7;
        constexpr QC::u32 SVGA_REG_BYTES_PER_LINE = 12;

        // Framebuffer / VRAM info (SVGA II)
        constexpr QC::u32 SVGA_REG_FB_START = 13;
        constexpr QC::u32 SVGA_REG_FB_OFFSET = 14;
        constexpr QC::u32 SVGA_REG_VRAM_SIZE = 15;
        constexpr QC::u32 SVGA_REG_FB_SIZE = 16;
        constexpr QC::u32 SVGA_REG_CAPABILITIES = 17;

        // FIFO/config registers
        constexpr QC::u32 SVGA_REG_FIFO_START = 18;
        constexpr QC::u32 SVGA_REG_FIFO_SIZE = 19;
        constexpr QC::u32 SVGA_REG_CONFIG_DONE = 20;
        constexpr QC::u32 SVGA_REG_SYNC = 21;
        constexpr QC::u32 SVGA_REG_BUSY = 22;

        // FIFO memory indices
        constexpr QC::u32 SVGA_FIFO_MIN = 0;
        constexpr QC::u32 SVGA_FIFO_MAX = 1;
        constexpr QC::u32 SVGA_FIFO_NEXT_CMD = 2;
        constexpr QC::u32 SVGA_FIFO_STOP = 3;

        // FIFO commands (legacy 2D)
        constexpr QC::u32 SVGA_CMD_UPDATE = 1;
        constexpr QC::u32 SVGA_CMD_RECT_COPY = 3;

        // Cursor FIFO commands
        constexpr QC::u32 SVGA_CMD_DEFINE_CURSOR = 19;

        // Cursor registers (VMware SVGA II)
        constexpr QC::u32 SVGA_REG_CURSOR_ID = 24;
        constexpr QC::u32 SVGA_REG_CURSOR_X = 25;
        constexpr QC::u32 SVGA_REG_CURSOR_Y = 26;
        constexpr QC::u32 SVGA_REG_CURSOR_ON = 27;
    }

    VmwareSVGA &VmwareSVGA::instance()
    {
        static VmwareSVGA inst;
        return inst;
    }

    VmwareSVGA::VmwareSVGA()
        : m_initialized(false),
          m_available(false),
          m_hwCursor(false),
          m_cursorVisible(false),
          m_2dInitialized(false),
          m_2dAvailable(false),
          m_ioBase(0),
          m_fifoVirt(0),
          m_fifo(nullptr),
          m_fifoSizeBytes(0)
    {
    }

    namespace
    {
        inline QC::u32 fifoHeaderBytes()
        {
            // The legacy FIFO header includes at least 4 dwords (MIN/MAX/NEXT/STOP)
            return 4u * sizeof(QC::u32);
        }

        inline bool fifoHasSpace(QC::u32 min, QC::u32 next, QC::u32 stop, QC::u32 max, QC::u32 neededBytes)
        {
            // next/stop/max are byte offsets.
            if (next >= max || stop >= max)
                return false;

            if (min >= max)
                return false;

            if (next >= stop)
            {
                // Free space is [next..max) + [min..stop)
                const QC::u32 spaceAtEnd = max - next;
                if (spaceAtEnd >= neededBytes)
                    return true;
                if (stop <= min)
                    return false;
                const QC::u32 spaceAtStart = stop - min;
                return spaceAtStart >= neededBytes;
            }
            else
            {
                // Free space is [next..stop)
                return (stop - next) >= neededBytes;
            }
        }

    }

    QC::u32 VmwareSVGA::readReg(QC::u32 reg) const
    {
        if (!m_ioBase)
            return 0;

        QArch::outl(static_cast<QC::u16>(m_ioBase + SVGA_INDEX_PORT), reg);
        return QArch::inl(static_cast<QC::u16>(m_ioBase + SVGA_VALUE_PORT));
    }

    void VmwareSVGA::writeReg(QC::u32 reg, QC::u32 value) const
    {
        if (!m_ioBase)
            return;

        QArch::outl(static_cast<QC::u16>(m_ioBase + SVGA_INDEX_PORT), reg);
        QArch::outl(static_cast<QC::u16>(m_ioBase + SVGA_VALUE_PORT), value);
    }

    QC::u32 VmwareSVGA::bytesPerLine() const
    {
        return readReg(SVGA_REG_BYTES_PER_LINE);
    }

    QC::u32 VmwareSVGA::framebufferStart() const
    {
        return readReg(SVGA_REG_FB_START);
    }

    QC::u32 VmwareSVGA::framebufferSizeBytes() const
    {
        return readReg(SVGA_REG_FB_SIZE);
    }

    QC::u32 VmwareSVGA::width() const
    {
        return readReg(SVGA_REG_WIDTH);
    }

    QC::u32 VmwareSVGA::height() const
    {
        return readReg(SVGA_REG_HEIGHT);
    }

    QC::u32 VmwareSVGA::bitsPerPixel() const
    {
        return readReg(SVGA_REG_BITS_PER_PIXEL);
    }

    bool VmwareSVGA::initialize()
    {
        if (m_initialized)
            return m_available;

        m_initialized = true;

        auto *dev = QArch::PCI::instance().findDevice(VMwareVendor, SVGADevice);
        if (!dev)
        {
            QC_LOG_INFO("QDrvSVGA", "VMware SVGA II device not found");
            return false;
        }

        // Enable I/O space (ports) and memory space (FIFO/VRAM aperture).
        // Some VMs/bootloaders leave Memory Space disabled, which breaks FIFO.
        QArch::PCI::instance().enableIOSpace(dev->address);
        QArch::PCI::instance().enableMemorySpace(dev->address);
        QArch::PCI::instance().enableBusMastering(dev->address);

        // BAR0 is expected to be an I/O BAR.
        m_ioBase = static_cast<QC::u16>(dev->bar[0] & 0xFFFF);
        if (!m_ioBase)
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA BAR0 I/O base is 0");
            return false;
        }

        // Negotiate an ID we understand.
        QC::u32 originalId = readReg(SVGA_REG_ID);
        writeReg(SVGA_REG_ID, SVGA_ID_2);
        QC::u32 id = readReg(SVGA_REG_ID);
        if (id != SVGA_ID_2)
        {
            writeReg(SVGA_REG_ID, SVGA_ID_1);
            id = readReg(SVGA_REG_ID);
        }
        if (id != SVGA_ID_2 && id != SVGA_ID_1 && id != SVGA_ID_0)
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA ID negotiation failed (orig=0x%08X, now=0x%08X)", originalId, id);
            return false;
        }

        // Ensure device enabled (may already be on in BIOS/bootloader mode).
        writeReg(SVGA_REG_ENABLE, 1);

        const QC::u32 caps = readReg(SVGA_REG_CAPABILITIES);
        const QC::u32 modeW = readReg(SVGA_REG_WIDTH);
        const QC::u32 modeH = readReg(SVGA_REG_HEIGHT);
        const QC::u32 bpp = readReg(SVGA_REG_BITS_PER_PIXEL);
        const QC::u32 bpl = readReg(SVGA_REG_BYTES_PER_LINE);
        const QC::u32 fbStart = readReg(SVGA_REG_FB_START);
        const QC::u32 fbOffset = readReg(SVGA_REG_FB_OFFSET);
        const QC::u32 vramSize = readReg(SVGA_REG_VRAM_SIZE);
        const QC::u32 fbSize = readReg(SVGA_REG_FB_SIZE);
        QC_LOG_INFO(
            "QDrvSVGA",
            "VMware SVGA II present (io=0x%04X id=0x%08X caps=0x%08X mode=%ux%u bpp=%u bpl=%u fb_start=0x%08X fb_off=0x%08X vram=0x%08X fb_size=0x%08X)",
            m_ioBase,
            id,
            caps,
            modeW,
            modeH,
            bpp,
            bpl,
            fbStart,
            fbOffset,
            vramSize,
            fbSize);

        // Best-effort cursor enablement.
        // We treat the cursor as supported if the ON register behaves like a latch.
        writeReg(SVGA_REG_CURSOR_ID, 0);
        writeReg(SVGA_REG_CURSOR_ON, 0);
        const QC::u32 offRead = readReg(SVGA_REG_CURSOR_ON);
        writeReg(SVGA_REG_CURSOR_ON, 1);
        const QC::u32 onRead = readReg(SVGA_REG_CURSOR_ON);

        if ((offRead & 1u) == 0u && (onRead & 1u) == 1u)
        {
            m_hwCursor = true;
            m_cursorVisible = true;
            // Start at (0,0) and visible.
            writeReg(SVGA_REG_CURSOR_X, 0);
            writeReg(SVGA_REG_CURSOR_Y, 0);
        }
        else
        {
            m_hwCursor = false;
            m_cursorVisible = false;
            // Restore OFF if it doesn't behave as expected.
            writeReg(SVGA_REG_CURSOR_ON, 0);
            QC_LOG_INFO("QDrvSVGA", "Hardware cursor not enabled (cursor_on readback off=%u on=%u)", offRead, onRead);
        }

        m_available = true;
        return true;
    }

    bool VmwareSVGA::initialize2D()
    {
        if (!m_available)
            return false;

        if (m_2dInitialized)
            return m_2dAvailable;

        m_2dInitialized = true;

        const QC::u32 fifoStart = readReg(SVGA_REG_FIFO_START);
        const QC::u32 fifoSize = readReg(SVGA_REG_FIFO_SIZE);
        if (fifoStart == 0 || fifoSize < (fifoHeaderBytes() + 64))
        {
            QC_LOG_INFO("QDrvSVGA", "SVGA2D FIFO not available (start=0x%08X size=0x%08X)", fifoStart, fifoSize);
            m_2dAvailable = false;
            return false;
        }

        // Map FIFO memory. This assumes physical FIFO lives in MMIO/VRAM space.
        // Translator is expected to be initialized by the time the desktop stack starts.
        m_fifoVirt = QK::Memory::Translator::instance().mapMMIO(static_cast<QC::PhysAddr>(fifoStart), fifoSize);
        if (!m_fifoVirt)
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA2D FIFO map failed (phys=0x%08X size=0x%08X)", fifoStart, fifoSize);
            m_2dAvailable = false;
            return false;
        }

        m_fifo = reinterpret_cast<volatile QC::u32 *>(m_fifoVirt);
        m_fifoSizeBytes = fifoSize;

        // FIFO header handling:
        // - Some implementations leave FIFO memory zeroed until the guest initializes it.
        // - Others pre-fill MIN/MAX.
        // We support both while staying conservative.
        QC::u32 min = m_fifo[SVGA_FIFO_MIN];
        QC::u32 max = m_fifo[SVGA_FIFO_MAX];

        if (min == 0 && max == 0)
        {
            // Guest-initialize FIFO header.
            // Use a conservative MIN offset to avoid overlapping any extended FIFO registers.
            // Some implementations expect a larger header region than just MIN/MAX/NEXT/STOP.
            min = 0x1000;
            if (min < fifoHeaderBytes())
                min = fifoHeaderBytes();
            max = fifoSize;
            m_fifo[SVGA_FIFO_MIN] = min;
            m_fifo[SVGA_FIFO_MAX] = max;
            m_fifo[SVGA_FIFO_NEXT_CMD] = min;
            m_fifo[SVGA_FIFO_STOP] = min;
            QC::write_barrier();
        }
        else
        {
            // Validate device-provided header.
            if (min < fifoHeaderBytes() || max > fifoSize || min >= max)
            {
                QC_LOG_WARN("QDrvSVGA", "SVGA2D FIFO header invalid (min=0x%08X max=0x%08X size=0x%08X)", min, max, fifoSize);
                m_2dAvailable = false;
                return false;
            }

            // Reset producer pointer to a known-good empty state.
            // STOP is device-owned; only touch it if it's clearly invalid.
            const QC::u32 stop = m_fifo[SVGA_FIFO_STOP];
            if (stop < min || stop >= max)
            {
                m_fifo[SVGA_FIFO_STOP] = min;
            }
            m_fifo[SVGA_FIFO_NEXT_CMD] = min;
            QC::write_barrier();
        }

        // QEMU's vmware-svga expects the guest to program a mode (new_width/new_height/new_depth)
        // when switching into SVGA (enable+config) operation. If we skip this, QEMU may create a
        // 0x0 surface and the screen stays blank even though VRAM writes + UPDATEs work.
        const QC::u32 modeW = readReg(SVGA_REG_WIDTH);
        const QC::u32 modeH = readReg(SVGA_REG_HEIGHT);
        const QC::u32 modeBpp = readReg(SVGA_REG_BITS_PER_PIXEL);
        writeReg(SVGA_REG_BITS_PER_PIXEL, (modeBpp == 0) ? 32u : modeBpp);
        writeReg(SVGA_REG_WIDTH, modeW);
        writeReg(SVGA_REG_HEIGHT, modeH);

        // Tell device FIFO config is complete.
        writeReg(SVGA_REG_CONFIG_DONE, 1);

        m_2dAvailable = true;
        QC_LOG_INFO("QDrvSVGA", "SVGA2D FIFO enabled (phys=0x%08X size=0x%08X min=0x%08X max=0x%08X)", fifoStart, fifoSize, min, max);
        return true;
    }

    void VmwareSVGA::updateRect(QC::u32 x, QC::u32 y, QC::u32 w, QC::u32 h)
    {
        if (!m_2dAvailable || !m_fifo)
            return;

        static QC::u32 s_updateCount = 0;
        ++s_updateCount;

        // Command: [CMD, x, y, w, h]
        constexpr QC::u32 dwords = 5;
        const QC::u32 bytes = dwords * sizeof(QC::u32);

        const QC::u32 min = m_fifo[SVGA_FIFO_MIN];
        const QC::u32 max = m_fifo[SVGA_FIFO_MAX];
        QC::u32 next = m_fifo[SVGA_FIFO_NEXT_CMD];
        const QC::u32 stop = m_fifo[SVGA_FIFO_STOP];

        if ((s_updateCount % 120u) == 1u)
        {
            // Defensive: if something disabled the device mid-run, re-enable it.
            const QC::u32 enabled = readReg(SVGA_REG_ENABLE);
            if (enabled == 0)
            {
                QC_LOG_WARN("QDrvSVGA", "SVGA_REG_ENABLE was 0; re-enabling display");
                writeReg(SVGA_REG_ENABLE, 1);
            }
            QC_LOG_INFO("QDrvSVGA", "SVGA_CMD_UPDATE #%u rect=%u,%u %ux%u next=0x%X stop=0x%X (min=0x%X max=0x%X)",
                        s_updateCount, x, y, w, h, next, stop, min, max);
        }

        if (!fifoHasSpace(min, next, stop, max, bytes))
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA2D FIFO full; drop UPDATE");
            return;
        }

        if (next + bytes > max)
        {
            // Wrap: only if there's space at start.
            if (!fifoHasSpace(min, min, stop, max, bytes))
            {
                QC_LOG_WARN("QDrvSVGA", "SVGA2D FIFO wrap blocked; drop UPDATE");
                return;
            }
            next = min;
        }

        volatile QC::u32 *cmd = reinterpret_cast<volatile QC::u32 *>(reinterpret_cast<volatile QC::u8 *>(m_fifo) + next);
        cmd[0] = SVGA_CMD_UPDATE;
        cmd[1] = x;
        cmd[2] = y;
        cmd[3] = w;
        cmd[4] = h;

        QC::write_barrier();

        next += bytes;
        if (next >= max)
            next = min;
        m_fifo[SVGA_FIFO_NEXT_CMD] = next;

        // Ensure NEXT_CMD is visible before kicking the device.
        QC::write_barrier();

        // Best-effort: nudge the device to process FIFO.
        // Avoid long busy-waits in the compositor path.
        writeReg(SVGA_REG_SYNC, 1);
        QC::u32 busy = 1;
        for (QC::u32 i = 0; i < 256; ++i)
        {
            busy = readReg(SVGA_REG_BUSY);
            if (busy == 0)
                break;
        }

        // Read back STOP to confirm command consumption.
        // If STOP doesn't catch up to NEXT_CMD, the host isn't processing FIFO.
        const QC::u32 stopAfter = m_fifo[SVGA_FIFO_STOP];
        if (stopAfter != next && (s_updateCount % 120u) == 1u)
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA FIFO not fully consumed after SYNC (stop=0x%X expected=0x%X)", stopAfter, next);
        }
        if (busy != 0 && (s_updateCount % 120u) == 1u)
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA busy still set after UPDATE kick (busy=%u)", busy);
        }
    }

    void VmwareSVGA::rectCopy(QC::u32 srcX, QC::u32 srcY, QC::u32 dstX, QC::u32 dstY, QC::u32 w, QC::u32 h)
    {
        if (!m_2dAvailable || !m_fifo)
            return;

        // Command: [CMD, srcX, srcY, dstX, dstY, w, h]
        constexpr QC::u32 dwords = 7;
        const QC::u32 bytes = dwords * sizeof(QC::u32);

        const QC::u32 min = m_fifo[SVGA_FIFO_MIN];
        const QC::u32 max = m_fifo[SVGA_FIFO_MAX];
        QC::u32 next = m_fifo[SVGA_FIFO_NEXT_CMD];
        const QC::u32 stop = m_fifo[SVGA_FIFO_STOP];

        if (!fifoHasSpace(min, next, stop, max, bytes))
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA2D FIFO full; drop RECT_COPY");
            return;
        }

        if (next + bytes > max)
        {
            if (!fifoHasSpace(min, min, stop, max, bytes))
            {
                QC_LOG_WARN("QDrvSVGA", "SVGA2D FIFO wrap blocked; drop RECT_COPY");
                return;
            }
            next = min;
        }

        volatile QC::u32 *cmd = reinterpret_cast<volatile QC::u32 *>(reinterpret_cast<volatile QC::u8 *>(m_fifo) + next);
        cmd[0] = SVGA_CMD_RECT_COPY;
        cmd[1] = srcX;
        cmd[2] = srcY;
        cmd[3] = dstX;
        cmd[4] = dstY;
        cmd[5] = w;
        cmd[6] = h;

        QC::write_barrier();

        next += bytes;
        if (next >= max)
            next = min;
        m_fifo[SVGA_FIFO_NEXT_CMD] = next;

        writeReg(SVGA_REG_SYNC, 1);
        for (QC::u32 i = 0; i < 10'000; ++i)
        {
            if (readReg(SVGA_REG_BUSY) == 0)
                break;
        }
    }

    void VmwareSVGA::setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                                    QC::u16 hotspotX, QC::u16 hotspotY)
    {
        if (!m_hwCursor || !pixels || width == 0 || height == 0)
            return;

        // Cursor definition is pushed via the same FIFO used for SVGA2D.
        if (!(m_2dAvailable || initialize2D()) || !m_fifo)
        {
            if (!m_cursorDefined)
            {
                QC_LOG_WARN("QDrvSVGA", "Cursor define skipped (FIFO unavailable)");
            }
            return;
        }

        if (width > 256 || height > 256)
        {
            QC_LOG_WARN("QDrvSVGA", "Cursor define rejected (size=%ux%u)", width, height);
            return;
        }

        static QC::u32 s_cursorDefineCount = 0;
        ++s_cursorDefineCount;
        if (s_cursorDefineCount <= 4 || (s_cursorDefineCount % 120u) == 1u)
        {
            QC_LOG_INFO("QDrvSVGA", "setCursorImage %ux%u hotspot=(%u,%u)", width, height, hotspotX, hotspotY);
        }

        // SVGA_CMD_DEFINE_CURSOR payload format (legacy SVGA FIFO, as implemented by QEMU):
        // [CMD, id, hot_x, hot_y, w, h, (ignored), bpp, mask..., image...]
        // Mask is a 1bpp bitmap sized in dwords as SVGA_BITMAP_SIZE(w,h) (rows padded to 32 bits).
        // QEMU later interprets it as a tightly-packed mono bitmap (bpl = ceil(w/8)), so we pack
        // the first (bpl * h) bytes tightly and leave any remaining per-row padding bytes as 0.
        constexpr QC::u32 cursorId = 0;
        constexpr QC::u32 ignored = 0;
        constexpr QC::u32 bpp = 32;

        const QC::u32 dwordsPerMaskRow = (static_cast<QC::u32>(width) + 31u) >> 5u;
        const QC::u32 maskDwords = dwordsPerMaskRow * static_cast<QC::u32>(height);
        const QC::u32 imageDwords = static_cast<QC::u32>(width) * static_cast<QC::u32>(height);

        constexpr QC::u32 headerDwords = 8; // cmd + 7 args
        const QC::u32 totalDwords = headerDwords + maskDwords + imageDwords;
        const QC::u32 totalBytes = totalDwords * sizeof(QC::u32);

        const QC::u32 min = m_fifo[SVGA_FIFO_MIN];
        const QC::u32 max = m_fifo[SVGA_FIFO_MAX];
        QC::u32 next = m_fifo[SVGA_FIFO_NEXT_CMD];
        const QC::u32 stop = m_fifo[SVGA_FIFO_STOP];

        if (!fifoHasSpace(min, next, stop, max, totalBytes))
        {
            QC_LOG_WARN("QDrvSVGA", "SVGA FIFO full; drop DEFINE_CURSOR (bytes=%u)", totalBytes);
            return;
        }

        if (next + totalBytes > max)
        {
            if (!fifoHasSpace(min, min, stop, max, totalBytes))
            {
                QC_LOG_WARN("QDrvSVGA", "SVGA FIFO wrap blocked; drop DEFINE_CURSOR (bytes=%u)", totalBytes);
                return;
            }
            next = min;
        }

        volatile QC::u32 *cmd = reinterpret_cast<volatile QC::u32 *>(reinterpret_cast<volatile QC::u8 *>(m_fifo) + next);
        cmd[0] = SVGA_CMD_DEFINE_CURSOR;
        cmd[1] = cursorId;
        cmd[2] = hotspotX;
        cmd[3] = hotspotY;
        cmd[4] = width;
        cmd[5] = height;
        cmd[6] = ignored;
        cmd[7] = bpp;

        // Build the 1bpp AND mask.
        // QEMU treats mask-bit=1 as transparent when bpp=32 (see cursor_set_mono with transparent=1).
        // Therefore: set bit=1 for transparent pixels, bit=0 for opaque pixels.
        // Bits are MSB-first in each byte.

        // Zero-fill full FIFO mask area (includes any per-row padding).
        volatile QC::u8 *maskBytes = reinterpret_cast<volatile QC::u8 *>(&cmd[headerDwords]);
        const QC::u32 maskBytesTotal = maskDwords * sizeof(QC::u32);
        for (QC::u32 i = 0; i < maskBytesTotal; ++i)
        {
            maskBytes[i] = 0;
        }

        const QC::u32 maskBpl = (static_cast<QC::u32>(width) + 7u) >> 3u;
        for (QC::u32 y = 0; y < height; ++y)
        {
            QC::u8 rowMask[32] = {};
            for (QC::u32 x = 0; x < width; ++x)
            {
                const QC::u32 pixel = pixels[y * static_cast<QC::u32>(width) + x];
                const QC::u32 alpha = (pixel >> 24) & 0xFFu;
                if (alpha < 0x80u)
                {
                    const QC::u32 byteIndex = x >> 3;
                    const QC::u8 bit = static_cast<QC::u8>(0x80u >> (x & 7u));
                    rowMask[byteIndex] = static_cast<QC::u8>(rowMask[byteIndex] | bit);
                }
            }

            // Tightly pack scanlines at the start of the mask payload.
            const QC::u32 dst = y * maskBpl;
            for (QC::u32 b = 0; b < maskBpl; ++b)
            {
                maskBytes[dst + b] = rowMask[b];
            }
        }

        // Fill the 32bpp pixmap payload (RGB in low 24 bits; alpha comes from mask).
        const QC::u32 imageBase = headerDwords + maskDwords;
        for (QC::u32 i = 0; i < imageDwords; ++i)
        {
            cmd[imageBase + i] = pixels[i] & 0x00FFFFFFu;
        }

        QC::write_barrier();

        next += totalBytes;
        if (next >= max)
            next = min;
        m_fifo[SVGA_FIFO_NEXT_CMD] = next;

        // Ensure the cursor ID is selected and kick FIFO.
        writeReg(SVGA_REG_CURSOR_ID, cursorId);
        writeReg(SVGA_REG_SYNC, 1);
        for (QC::u32 i = 0; i < 10'000; ++i)
        {
            if (readReg(SVGA_REG_BUSY) == 0)
                break;
        }

        // Keep the cursor visible state latched after a redefine.
        writeReg(SVGA_REG_CURSOR_ON, m_cursorVisible ? 1u : 0u);
        m_cursorDefined = true;
    }

    void VmwareSVGA::setCursorVisible(bool visible)
    {
        if (!m_hwCursor)
            return;
        m_cursorVisible = visible;
        writeReg(SVGA_REG_CURSOR_ON, visible ? 1u : 0u);
    }

    void VmwareSVGA::setCursorPosition(QC::u16 x, QC::u16 y)
    {
        if (!m_hwCursor)
            return;

        static QC::u32 s_cursorPosWrites = 0;
        ++s_cursorPosWrites;
        if ((s_cursorPosWrites % 240u) == 1u)
        {
            QC_LOG_INFO("QDrvSVGA", "Cursor pos write #%u: %u,%u (visible=%u)",
                        s_cursorPosWrites, x, y, m_cursorVisible ? 1u : 0u);
        }
        writeReg(SVGA_REG_CURSOR_X, x);
        writeReg(SVGA_REG_CURSOR_Y, y);
    }
}
