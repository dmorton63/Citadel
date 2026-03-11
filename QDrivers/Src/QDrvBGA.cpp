// QDrivers BGA - Bochs Graphics Adapter driver implementation
// Namespace: QDrv

#include "QDrvBGA.h"
#include "QArchPort.h"
#include "QCLogger.h"

namespace QDrv
{

    BGA &BGA::instance()
    {
        static BGA instance;
        return instance;
    }

    BGA::BGA()
        : m_available(false),
          m_version(0),
          m_width(0),
          m_height(0),
          m_bpp(0),
          m_hasHwCursor(false),
          m_cursorX(0),
          m_cursorY(0),
          m_cursorVisible(false)
    {
    }

    void BGA::writeRegister(QC::u16 index, QC::u16 value)
    {
        QArch::outw(BGA_INDEX_PORT, index);
        QArch::outw(BGA_DATA_PORT, value);
    }

    QC::u16 BGA::readRegister(QC::u16 index)
    {
        QArch::outw(BGA_INDEX_PORT, index);
        return QArch::inw(BGA_DATA_PORT);
    }

    bool BGA::initialize()
    {
        QC_LOG_INFO("QDrvBGA", "Detecting Bochs Graphics Adapter...");

        // Check for BGA presence by reading ID register
        m_version = readRegister(BGA_INDEX_ID);

        // Valid BGA versions are 0xB0C0 through 0xB0C5
        if (m_version >= BGA_ID0 && m_version <= BGA_ID5)
        {
            m_available = true;
            QC_LOG_INFO("QDrvBGA", "BGA detected, version 0x%04X", m_version);

            // Get current mode info
            m_width = readRegister(BGA_INDEX_XRES);
            m_height = readRegister(BGA_INDEX_YRES);
            m_bpp = readRegister(BGA_INDEX_BPP);

            QC_LOG_INFO("QDrvBGA", "Current mode: %ux%u @ %u bpp",
                        m_width, m_height, m_bpp);

            // BGA versions 0xB0C4+ have additional features
            // but no true hardware cursor - that requires QXL or virtio-gpu
            m_hasHwCursor = false;

            return true;
        }

        QC_LOG_INFO("QDrvBGA", "BGA not detected (ID: 0x%04X)", m_version);
        m_available = false;
        return false;
    }

    void BGA::setMode(QC::u16 width, QC::u16 height, QC::u16 bpp)
    {
        if (!m_available)
            return;

        QC_LOG_INFO("QDrvBGA", "Setting mode %ux%u @ %u bpp", width, height, bpp);

        // Disable VBE extensions
        writeRegister(BGA_INDEX_ENABLE, BGA_DISABLED);

        // Set resolution
        writeRegister(BGA_INDEX_XRES, width);
        writeRegister(BGA_INDEX_YRES, height);
        writeRegister(BGA_INDEX_BPP, bpp);

        // Enable VBE with linear framebuffer, don't clear memory
        writeRegister(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED | BGA_NOCLEARMEM);

        m_width = width;
        m_height = height;
        m_bpp = bpp;
    }

    void BGA::setCursorPosition(QC::u16 x, QC::u16 y)
    {
        m_cursorX = x;
        m_cursorY = y;

        // BGA doesn't have hardware cursor registers
        // This is here for API compatibility with future QXL/virtio-gpu support
        if (m_hasHwCursor)
        {
            // Would write to hardware cursor position registers here
        }
    }

    void BGA::setCursorVisible(bool visible)
    {
        m_cursorVisible = visible;

        if (m_hasHwCursor)
        {
            // Would enable/disable hardware cursor here
        }
    }

    void BGA::setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                             QC::u16 hotspotX, QC::u16 hotspotY)
    {
        (void)pixels;
        (void)width;
        (void)height;
        (void)hotspotX;
        (void)hotspotY;

        if (m_hasHwCursor)
        {
            // Would upload cursor image to hardware here
        }
    }

} // namespace QDrv
