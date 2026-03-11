#pragma once

// QDrivers BGA - Bochs Graphics Adapter driver with hardware cursor
// Namespace: QDrv

#include "QCTypes.h"

namespace QDrv
{

    // BGA (Bochs Graphics Adapter) I/O ports
    constexpr QC::u16 BGA_INDEX_PORT = 0x01CE;
    constexpr QC::u16 BGA_DATA_PORT = 0x01CF;

    // BGA register indices
    enum BGAIndex : QC::u16
    {
        BGA_INDEX_ID = 0,
        BGA_INDEX_XRES = 1,
        BGA_INDEX_YRES = 2,
        BGA_INDEX_BPP = 3,
        BGA_INDEX_ENABLE = 4,
        BGA_INDEX_BANK = 5,
        BGA_INDEX_VIRT_WIDTH = 6,
        BGA_INDEX_VIRT_HEIGHT = 7,
        BGA_INDEX_X_OFFSET = 8,
        BGA_INDEX_Y_OFFSET = 9,
        BGA_INDEX_VIDEO_MEMORY_64K = 10
    };

    // BGA ID values for version detection
    constexpr QC::u16 BGA_ID0 = 0xB0C0;
    constexpr QC::u16 BGA_ID1 = 0xB0C1;
    constexpr QC::u16 BGA_ID2 = 0xB0C2;
    constexpr QC::u16 BGA_ID3 = 0xB0C3;
    constexpr QC::u16 BGA_ID4 = 0xB0C4;
    constexpr QC::u16 BGA_ID5 = 0xB0C5;

    // BGA enable flags
    constexpr QC::u16 BGA_DISABLED = 0;
    constexpr QC::u16 BGA_ENABLED = 1;
    constexpr QC::u16 BGA_LFB_ENABLED = 0x40;
    constexpr QC::u16 BGA_NOCLEARMEM = 0x80;

    class BGA
    {
    public:
        static BGA &instance();

        bool initialize();
        bool isAvailable() const { return m_available; }
        QC::u16 version() const { return m_version; }

        // Mode setting
        void setMode(QC::u16 width, QC::u16 height, QC::u16 bpp);
        QC::u16 width() const { return m_width; }
        QC::u16 height() const { return m_height; }
        QC::u16 bpp() const { return m_bpp; }

        // Hardware cursor (if supported)
        bool hasHardwareCursor() const { return m_hasHwCursor; }
        void setCursorPosition(QC::u16 x, QC::u16 y);
        void setCursorVisible(bool visible);
        void setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                            QC::u16 hotspotX, QC::u16 hotspotY);

    private:
        BGA();
        ~BGA() = default;
        BGA(const BGA &) = delete;
        BGA &operator=(const BGA &) = delete;

        void writeRegister(QC::u16 index, QC::u16 value);
        QC::u16 readRegister(QC::u16 index);

        bool m_available;
        QC::u16 m_version;
        QC::u16 m_width;
        QC::u16 m_height;
        QC::u16 m_bpp;
        bool m_hasHwCursor;
        QC::u16 m_cursorX;
        QC::u16 m_cursorY;
        bool m_cursorVisible;
    };

} // namespace QDrv
