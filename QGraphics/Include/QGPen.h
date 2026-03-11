#pragma once

// QGraphics Pen - Stroke style for drawing outlines
// Namespace: QG

#include "QCTypes.h"
#include "QCColor.h"

namespace QG
{

    /// Pen styles for line drawing
    enum class PenStyle : QC::u8
    {
        Solid,   // ___________
        Dash,    // _ _ _ _ _ _
        Dot,     // ...........
        DashDot, // _._._._._._
        None     // No stroke
    };

    /// Line cap styles
    enum class LineCap : QC::u8
    {
        Flat,   // Ends at endpoint
        Square, // Extends half line width past endpoint
        Round   // Rounded end
    };

    /// Line join styles
    enum class LineJoin : QC::u8
    {
        Miter, // Sharp corner
        Bevel, // Cut corner
        Round  // Rounded corner
    };

    /// Pen - defines how strokes are rendered
    class Pen
    {
    public:
        Pen()
            : m_color(QC::Color::black()),
              m_width(1),
              m_style(PenStyle::Solid),
              m_cap(LineCap::Flat),
              m_join(LineJoin::Miter)
        {
        }

        explicit Pen(QC::Color color, QC::u32 width = 1)
            : m_color(color),
              m_width(width),
              m_style(PenStyle::Solid),
              m_cap(LineCap::Flat),
              m_join(LineJoin::Miter)
        {
        }

        Pen(QC::Color color, QC::u32 width, PenStyle style)
            : m_color(color),
              m_width(width),
              m_style(style),
              m_cap(LineCap::Flat),
              m_join(LineJoin::Miter)
        {
        }

        // Accessors
        QC::Color color() const { return m_color; }
        void setColor(QC::Color color) { m_color = color; }

        QC::u32 width() const { return m_width; }
        void setWidth(QC::u32 width) { m_width = width; }

        PenStyle style() const { return m_style; }
        void setStyle(PenStyle style) { m_style = style; }

        LineCap cap() const { return m_cap; }
        void setCap(LineCap cap) { m_cap = cap; }

        LineJoin join() const { return m_join; }
        void setJoin(LineJoin join) { m_join = join; }

        bool isNull() const { return m_style == PenStyle::None || m_width == 0; }

        // Factory methods
        static Pen null() { return Pen(QC::Color::transparent(), 0, PenStyle::None); }

    private:
        QC::Color m_color;
        QC::u32 m_width;
        PenStyle m_style;
        LineCap m_cap;
        LineJoin m_join;
    };

} // namespace QG
