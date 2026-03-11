#pragma once

// QCommon UIStyle - Global UI appearance style selector
// Namespace: QC
//
// Controls the visual appearance of all UI elements:
// - Vista: Aero glass effects, gradients, glow, transparency (Windows Vista/7)
// - Metro: Flat design, solid colors, sharp edges (Windows 8/10)
// - QWStyle: Modern hybrid with soft shadows and rounded corners (our unique style)

#include "QCTypes.h"

namespace QC
{

    /// Global UI style enumeration
    /// Determines rendering behavior for all controls and windows
    enum class UIStyle : u8
    {
        Vista,  ///< Aero glass, gradients, glow effects, transparency
        Metro,  ///< Flat design, solid colors, sharp edges
        QWStyle ///< Modern hybrid (Windows 11/macOS inspired, unique)
    };

    // ==================== Style Characteristics ====================
    //
    // Vista:
    //   - Borders: 3D raised/sunken effects
    //   - Backgrounds: Gradients with glass transparency
    //   - Shadows: Drop shadows on windows and controls
    //   - Corners: Slight rounding (2-4px)
    //   - Hover: Glowing effect
    //   - Focus: Glowing border
    //   - Animations: Smooth fades (150-200ms)
    //
    // Metro:
    //   - Borders: Flat, 1px solid lines
    //   - Backgrounds: Solid flat colors
    //   - Shadows: None or minimal
    //   - Corners: Square (0px)
    //   - Hover: Color shift (lighter/darker)
    //   - Focus: Accent color underline
    //   - Animations: Quick snaps (50-100ms)
    //
    // QWStyle:
    //   - Borders: Subtle rounded edges
    //   - Backgrounds: Soft gradients, mica-like effects
    //   - Shadows: Soft ambient shadows
    //   - Corners: Rounded (6-8px)
    //   - Hover: Subtle elevation lift
    //   - Focus: Ring with soft shadow
    //   - Animations: Fluid spring physics (100-150ms)

    // ==================== Global Style Management ====================

    /// Get the current global UI style
    /// Default: QWStyle
    UIStyle currentUIStyle();

    /// Set the global UI style
    /// Affects all subsequent rendering operations
    /// Note: Does not automatically repaint - caller should invalidate windows
    void setUIStyle(UIStyle style);

    // ==================== Style Queries ====================

    /// Check if current style uses 3D border effects
    inline bool styleUses3DBorders()
    {
        return currentUIStyle() == UIStyle::Vista;
    }

    /// Check if current style uses flat design
    inline bool styleIsFlat()
    {
        return currentUIStyle() == UIStyle::Metro;
    }

    /// Check if current style uses rounded corners
    inline bool styleUsesRoundedCorners()
    {
        UIStyle s = currentUIStyle();
        return s == UIStyle::Vista || s == UIStyle::QWStyle;
    }

    /// Check if current style uses shadows
    inline bool styleUsesShadows()
    {
        UIStyle s = currentUIStyle();
        return s == UIStyle::Vista || s == UIStyle::QWStyle;
    }

    /// Check if current style uses glow effects
    inline bool styleUsesGlow()
    {
        return currentUIStyle() == UIStyle::Vista;
    }

    /// Check if current style uses gradient backgrounds
    inline bool styleUsesGradients()
    {
        UIStyle s = currentUIStyle();
        return s == UIStyle::Vista || s == UIStyle::QWStyle;
    }

    // ==================== Style Metrics ====================

    /// Get recommended corner radius for current style
    inline u32 styleCornerRadius()
    {
        switch (currentUIStyle())
        {
        case UIStyle::Vista:
            return 3;
        case UIStyle::Metro:
            return 0;
        case UIStyle::QWStyle:
            return 8;
        default:
            return 0;
        }
    }

    /// Get recommended border width for current style
    inline u32 styleBorderWidth()
    {
        switch (currentUIStyle())
        {
        case UIStyle::Vista:
            return 1;
        case UIStyle::Metro:
            return 1;
        case UIStyle::QWStyle:
            return 1;
        default:
            return 1;
        }
    }

    /// Get recommended shadow offset for current style
    inline u32 styleShadowOffset()
    {
        switch (currentUIStyle())
        {
        case UIStyle::Vista:
            return 2;
        case UIStyle::Metro:
            return 0;
        case UIStyle::QWStyle:
            return 4;
        default:
            return 0;
        }
    }

    /// Get recommended animation duration (milliseconds) for current style
    inline u32 styleAnimationDuration()
    {
        switch (currentUIStyle())
        {
        case UIStyle::Vista:
            return 150;
        case UIStyle::Metro:
            return 75;
        case UIStyle::QWStyle:
            return 120;
        default:
            return 100;
        }
    }

} // namespace QC
