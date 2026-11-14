#pragma once

// QDesktop Citadel Theme - the single, fixed desktop color scheme
// Namespace: QD::CitadelTheme
//
// The desktop is intentionally not themeable (see CitadelDesktopRedesign.md).

#include "QCColor.h"

namespace QD::CitadelTheme
{
    inline QC::Color background() { return QC::Color(0x1E, 0x26, 0x30, 0xFF); }  // dark slate
    inline QC::Color surface() { return QC::Color(0x27, 0x30, 0x3C, 0xFF); }     // window body
    inline QC::Color surfaceAlt() { return QC::Color(0x1A, 0x20, 0x28, 0xFF); }  // lists/inputs
    inline QC::Color border() { return QC::Color(0x3A, 0x45, 0x52, 0xFF); }
    inline QC::Color taskbar() { return QC::Color(0x2B, 0x2B, 0x2F, 0xFF); }     // charcoal
    inline QC::Color taskbarHover() { return QC::Color(0x3A, 0x3A, 0x40, 0xFF); }
    inline QC::Color text() { return QC::Color(0xD6, 0xDA, 0xDF, 0xFF); }        // light gray
    inline QC::Color textDim() { return QC::Color(0x8C, 0x95, 0xA0, 0xFF); }
    inline QC::Color accent() { return QC::Color(0x2F, 0x7C, 0xD6, 0xFF); }      // Citadel blue
    inline QC::Color warning() { return QC::Color(0xF0, 0xA8, 0x30, 0xFF); }     // amber
    inline QC::Color error() { return QC::Color(0xD9, 0x45, 0x45, 0xFF); }       // red
}
