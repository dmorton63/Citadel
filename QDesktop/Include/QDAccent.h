#pragma once

// QDesktop Accent - Accent color definitions and theme colors
// Namespace: QD
//
// Provides accent colors and style-specific color palettes for the desktop

#include "QCTypes.h"
#include "QCColor.h"
#include "QCUIStyle.h"

namespace QD
{
    using QC::Color;

    // ==================== Accent Colors ====================
    // System accent colors - pick one as the primary accent

    enum class AccentColor : QC::u8
    {
        ElectricBlue, // #3A8DFF - default
        Teal,         // #2CC7A2
        Orange,       // #FF8A3A
        Purple        // #A06BFF
    };

    /// Get the Color value for an accent
    inline Color accentToColor(AccentColor accent)
    {
        switch (accent)
        {
        case AccentColor::ElectricBlue:
            return Color(0x3A, 0x8D, 0xFF, 0xFF);
        case AccentColor::Teal:
            return Color(0x2C, 0xC7, 0xA2, 0xFF);
        case AccentColor::Orange:
            return Color(0xFF, 0x8A, 0x3A, 0xFF);
        case AccentColor::Purple:
            return Color(0xA0, 0x6B, 0xFF, 0xFF);
        default:
            return Color(0x3A, 0x8D, 0xFF, 0xFF);
        }
    }

    // ==================== Global Accent Management ====================

    /// Get current system accent color
    AccentColor currentAccent();

    /// Set system accent color
    void setAccent(AccentColor accent);

    /// Get current accent as Color
    inline Color accent() { return accentToColor(currentAccent()); }

    /// Get accent with modified alpha
    inline Color accentWithAlpha(QC::u8 alpha)
    {
        Color c = accent();
        c.a = alpha;
        return c;
    }

    // ==================== Desktop Colors ====================
    // Style-specific color palette for desktop elements

    struct DesktopColors
    {
        // Background
        Color bgTop;    // Top of background gradient
        Color bgBottom; // Bottom of background gradient

        // Top bar
        Color topBarBg;
        Color topBarText;
        Color topBarDivider;

        // Sidebar
        Color sidebarBg;
        Color sidebarText;
        Color sidebarHover;
        Color sidebarSelected;

        // Taskbar
        Color taskbarBg;
        Color taskbarText;
        Color taskbarActiveWindow;
        Color taskbarHover;

        // Window chrome
        Color windowBg;
        Color windowBorder;
        Color windowTitleBg;
        Color windowTitleText;
        Color windowShadow;
    };

    /// Get desktop colors for QWStyle
    inline DesktopColors colorsQWStyle()
    {
        DesktopColors c;

        // Deep slate blue → charcoal gradient
        c.bgTop = Color(0x2D, 0x3A, 0x4A, 0xFF);
        c.bgBottom = Color(0x1A, 0x1E, 0x24, 0xFF);

        // Top bar: semi-transparent dark
        c.topBarBg = Color(0x1A, 0x1E, 0x24, 0xE0);
        c.topBarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.topBarDivider = Color(0x40, 0x44, 0x4A, 0xFF);

        // Sidebar: slightly lighter than bg
        c.sidebarBg = Color(0x28, 0x2C, 0x34, 0xFF);
        c.sidebarText = Color(0xCC, 0xCC, 0xCC, 0xFF);
        c.sidebarHover = Color(0x38, 0x3C, 0x44, 0xFF);
        c.sidebarSelected = Color(0x3A, 0x8D, 0xFF, 0xFF); // Accent

        // Taskbar: slightly darker than top bar
        c.taskbarBg = Color(0x16, 0x1A, 0x20, 0xE8);
        c.taskbarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.taskbarActiveWindow = Color(0x3A, 0x8D, 0xFF, 0x80);
        c.taskbarHover = Color(0x40, 0x44, 0x50, 0xFF);

        // Window
        c.windowBg = Color(0x2A, 0x2E, 0x36, 0xFF);
        c.windowBorder = Color(0x3A, 0x8D, 0xFF, 0xFF); // Accent
        c.windowTitleBg = Color(0x32, 0x36, 0x3E, 0xFF);
        c.windowTitleText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.windowShadow = Color(0x00, 0x00, 0x00, 0x60);

        return c;
    }

    /// Get desktop colors for Metro style
    inline DesktopColors colorsMetro()
    {
        DesktopColors c;

        // Flat dark gray
        c.bgTop = Color(0x20, 0x20, 0x20, 0xFF);
        c.bgBottom = Color(0x20, 0x20, 0x20, 0xFF);

        // Top bar: solid dark
        c.topBarBg = Color(0x1A, 0x1A, 0x1A, 0xFF);
        c.topBarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.topBarDivider = Color(0x40, 0x40, 0x40, 0xFF);

        // Sidebar: slightly lighter
        c.sidebarBg = Color(0x2D, 0x2D, 0x2D, 0xFF);
        c.sidebarText = Color(0xCC, 0xCC, 0xCC, 0xFF);
        c.sidebarHover = Color(0x3A, 0x3A, 0x3A, 0xFF);
        c.sidebarSelected = Color(0x3A, 0x8D, 0xFF, 0xFF);

        // Taskbar
        c.taskbarBg = Color(0x14, 0x14, 0x14, 0xFF);
        c.taskbarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.taskbarActiveWindow = Color(0x3A, 0x8D, 0xFF, 0xFF);
        c.taskbarHover = Color(0x40, 0x40, 0x40, 0xFF);

        // Window
        c.windowBg = Color(0x2D, 0x2D, 0x2D, 0xFF);
        c.windowBorder = Color(0x3A, 0x8D, 0xFF, 0xFF);
        c.windowTitleBg = Color(0x2D, 0x2D, 0x2D, 0xFF);
        c.windowTitleText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.windowShadow = Color(0x00, 0x00, 0x00, 0x00); // No shadow

        return c;
    }

    /// Get desktop colors for Vista style
    inline DesktopColors colorsVista()
    {
        DesktopColors c;

        // Soft blue with faint radial highlight
        c.bgTop = Color(0x4A, 0x6F, 0x9C, 0xFF);
        c.bgBottom = Color(0x2D, 0x4A, 0x6E, 0xFF);

        // Top bar: semi-transparent black with glass
        c.topBarBg = Color(0x00, 0x00, 0x00, 0x80);
        c.topBarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.topBarDivider = Color(0xFF, 0xFF, 0xFF, 0x40);

        // Sidebar: glass-like
        c.sidebarBg = Color(0x00, 0x00, 0x00, 0x60);
        c.sidebarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.sidebarHover = Color(0xFF, 0xFF, 0xFF, 0x20);
        c.sidebarSelected = Color(0x52, 0xB4, 0xE5, 0xFF);

        // Taskbar
        c.taskbarBg = Color(0x00, 0x00, 0x00, 0xA0);
        c.taskbarText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.taskbarActiveWindow = Color(0x52, 0xB4, 0xE5, 0x80);
        c.taskbarHover = Color(0xFF, 0xFF, 0xFF, 0x20);

        // Window
        c.windowBg = Color(0xF0, 0xF0, 0xF0, 0xFF);
        c.windowBorder = Color(0x52, 0xB4, 0xE5, 0xFF);
        c.windowTitleBg = Color(0x3A, 0x6E, 0xA5, 0xCC);
        c.windowTitleText = Color(0xFF, 0xFF, 0xFF, 0xFF);
        c.windowShadow = Color(0x00, 0x00, 0x00, 0x40);

        return c;
    }

    /// Get desktop colors for current UIStyle
    inline DesktopColors currentColors()
    {
        switch (QC::currentUIStyle())
        {
        case QC::UIStyle::Vista:
            return colorsVista();
        case QC::UIStyle::Metro:
            return colorsMetro();
        case QC::UIStyle::QWStyle:
        default:
            return colorsQWStyle();
        }
    }

    /// Update accent colors in DesktopColors with current accent
    inline void applyAccent(DesktopColors &colors)
    {
        Color a = accent();
        colors.sidebarSelected = a;
        colors.taskbarActiveWindow = accentWithAlpha(0x80);
        colors.windowBorder = a;
    }

} // namespace QD
