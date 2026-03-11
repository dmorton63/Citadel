#pragma once

// QCommon Color - Core color type and utilities
// Namespace: QC
//
// Color is a foundational type used throughout the system.
// Lives in QCommon so all modules can depend on it.

#include "QCTypes.h"

namespace QC
{

    /// ARGB Color (32-bit)
    /// Memory layout: [B, G, R, A] (little-endian compatible)
    struct Color
    {
        union
        {
            u32 value;
            struct
            {
                u8 b, g, r, a;
            };
        };

        Color() : value(0) {}

        Color(u8 red, u8 green, u8 blue, u8 alpha = 255)
        {
            r = red;
            g = green;
            b = blue;
            a = alpha;
        }

        explicit Color(u32 argb) : value(argb) {}

        bool operator==(const Color &other) const { return value == other.value; }
        bool operator!=(const Color &other) const { return value != other.value; }

        // ==================== Factory Methods ====================

        static Color fromRGB(u8 red, u8 green, u8 blue)
        {
            return Color(red, green, blue, 255);
        }

        static Color fromARGB(u8 alpha, u8 red, u8 green, u8 blue)
        {
            return Color(red, green, blue, alpha);
        }

        static Color fromValue(u32 argb)
        {
            return Color(argb);
        }

        // ==================== Color Operations ====================

        /// Create a darker shade (factor 0.0-1.0)
        Color darker(f32 factor = 0.7f) const
        {
            return Color(
                static_cast<u8>(r * factor),
                static_cast<u8>(g * factor),
                static_cast<u8>(b * factor),
                a);
        }

        /// Create a lighter shade (factor 0.0-1.0)
        Color lighter(f32 factor = 0.3f) const
        {
            return Color(
                static_cast<u8>(r + (255 - r) * factor),
                static_cast<u8>(g + (255 - g) * factor),
                static_cast<u8>(b + (255 - b) * factor),
                a);
        }

        /// Create with different alpha
        Color withAlpha(u8 alpha) const
        {
            return Color(r, g, b, alpha);
        }

        /// Blend with another color (this over other)
        Color blend(const Color &other) const
        {
            if (a == 255)
                return *this;
            if (a == 0)
                return other;

            // Fast divide-by-255 helper for values in [0, 65025].
            // This avoids integer division in the hot alpha-blend path.
            auto div255 = [](u32 x) -> u32 {
                x += 128;
                return (x + (x >> 8)) >> 8;
            };

            u32 invAlpha = 255 - a;
            return Color(
                static_cast<u8>(div255(r * a + other.r * invAlpha)),
                static_cast<u8>(div255(g * a + other.g * invAlpha)),
                static_cast<u8>(div255(b * a + other.b * invAlpha)),
                static_cast<u8>(a + div255(other.a * invAlpha)));
        }

        /// Linear interpolation between two colors
        static Color lerp(const Color &from, const Color &to, f32 t)
        {
            if (t <= 0.0f)
                return from;
            if (t >= 1.0f)
                return to;

            return Color(
                static_cast<u8>(from.r + (to.r - from.r) * t),
                static_cast<u8>(from.g + (to.g - from.g) * t),
                static_cast<u8>(from.b + (to.b - from.b) * t),
                static_cast<u8>(from.a + (to.a - from.a) * t));
        }

        // ==================== Predefined Colors ====================

        static Color transparent() { return Color(0, 0, 0, 0); }
        static Color black() { return Color(0, 0, 0); }
        static Color white() { return Color(255, 255, 255); }
        static Color red() { return Color(255, 0, 0); }
        static Color green() { return Color(0, 255, 0); }
        static Color blue() { return Color(0, 0, 255); }
        static Color yellow() { return Color(255, 255, 0); }
        static Color cyan() { return Color(0, 255, 255); }
        static Color magenta() { return Color(255, 0, 255); }
        static Color gray() { return Color(128, 128, 128); }
        static Color lightGray() { return Color(192, 192, 192); }
        static Color darkGray() { return Color(64, 64, 64); }

        // Windows-style system colors
        static Color windowBackground() { return Color(240, 240, 240); }
        static Color buttonFace() { return Color(225, 225, 225); }
        static Color buttonHighlight() { return Color(255, 255, 255); }
        static Color buttonShadow() { return Color(160, 160, 160); }
        static Color buttonDarkShadow() { return Color(105, 105, 105); }
        static Color windowFrame() { return Color(100, 100, 100); }
        static Color activeCaption() { return Color(0, 120, 215); }
        static Color inactiveCaption() { return Color(191, 205, 219); }
        static Color captionText() { return Color(255, 255, 255); }
        static Color controlText() { return Color(0, 0, 0); }
        static Color highlightBackground() { return Color(0, 120, 215); }
        static Color highlightText() { return Color(255, 255, 255); }
    };

} // namespace QC
