#pragma once

// QGraphics Brush - Fill style for drawing shapes
// Namespace: QG

#include "QCTypes.h"
#include "QCColor.h"

namespace QG
{

    /// Brush styles for filling shapes
    enum class BrushStyle : QC::u8
    {
        Solid,           // Solid color fill
        None,            // Transparent (no fill)
        LinearGradientH, // Horizontal gradient
        LinearGradientV, // Vertical gradient
        LinearGradientD, // Diagonal gradient (top-left to bottom-right)
        Pattern          // Future: patterns/textures
    };

    /// Brush - defines how shapes are filled
    class Brush
    {
    public:
        Brush()
            : m_color(QC::Color::white()),
              m_colorEnd(QC::Color::lightGray()),
              m_style(BrushStyle::Solid)
        {
        }

        explicit Brush(QC::Color color)
            : m_color(color),
              m_colorEnd(color),
              m_style(BrushStyle::Solid)
        {
        }

        Brush(QC::Color start, QC::Color end, BrushStyle style)
            : m_color(start),
              m_colorEnd(end),
              m_style(style)
        {
        }

        // Accessors
        QC::Color color() const { return m_color; }
        void setColor(QC::Color color) { m_color = color; }

        QC::Color colorEnd() const { return m_colorEnd; }
        void setColorEnd(QC::Color color) { m_colorEnd = color; }

        BrushStyle style() const { return m_style; }
        void setStyle(BrushStyle style) { m_style = style; }

        bool isNull() const { return m_style == BrushStyle::None; }
        bool isGradient() const
        {
            return m_style == BrushStyle::LinearGradientH ||
                   m_style == BrushStyle::LinearGradientV ||
                   m_style == BrushStyle::LinearGradientD;
        }

        // Factory methods
        static Brush null() { return Brush(QC::Color::transparent()); }

        static Brush solid(QC::Color color)
        {
            return Brush(color);
        }

        static Brush gradientH(QC::Color start, QC::Color end)
        {
            return Brush(start, end, BrushStyle::LinearGradientH);
        }

        static Brush gradientV(QC::Color start, QC::Color end)
        {
            return Brush(start, end, BrushStyle::LinearGradientV);
        }

    private:
        QC::Color m_color;    // Primary color / gradient start
        QC::Color m_colorEnd; // Gradient end color
        BrushStyle m_style;
    };

} // namespace QG
