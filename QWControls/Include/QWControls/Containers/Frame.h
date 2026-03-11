#pragma once

// Frame - visual decoration helper for controls
// Namespace: QW::Controls

#include "QCGeometry.h"
#include "QCColor.h"
#include "QGPainter.h"

namespace QW
{
    using Rect = QC::Rect;
    using Color = QC::Color;

    namespace Controls
    {
        namespace FrameStyle
        {
            constexpr QC::u32 None = 0x0000;
            constexpr QC::u32 BorderFlat = 0x0001;
            constexpr QC::u32 BorderRaised = 0x0002;
            constexpr QC::u32 BorderSunken = 0x0004;
            constexpr QC::u32 BorderEtched = 0x0008;
            constexpr QC::u32 BorderDouble = 0x0010;
            constexpr QC::u32 BorderGroove = 0x0020;
            constexpr QC::u32 BorderMask = 0x003F;

            constexpr QC::u32 DropShadow = 0x0100;
            constexpr QC::u32 DropShadowSoft = 0x0200;
            constexpr QC::u32 InnerShadow = 0x0400;
            constexpr QC::u32 GlowEffect = 0x0800;

            constexpr QC::u32 FillSolid = 0x1000;
            constexpr QC::u32 FillGradientV = 0x2000;
            constexpr QC::u32 FillGradientH = 0x4000;
            constexpr QC::u32 FillTransparent = 0x8000;
            constexpr QC::u32 FillMask = 0xF000;

            constexpr QC::u32 Button3D = BorderRaised | FillSolid;
            constexpr QC::u32 ButtonPressed = BorderSunken | FillSolid;
            constexpr QC::u32 TextBox = BorderSunken | FillSolid;
            constexpr QC::u32 Panel3D = BorderEtched | FillSolid;
            constexpr QC::u32 WindowFrame = BorderRaised | DropShadow | FillSolid;
            constexpr QC::u32 FlatButton = BorderFlat | FillSolid;
            constexpr QC::u32 MenuPopup = BorderFlat | DropShadow | FillSolid;
        }

        struct FrameColors
        {
            Color background;
            Color backgroundEnd;
            Color borderLight;
            Color borderDark;
            Color borderMid;
            Color shadow;
            Color glow;

            FrameColors();

            static FrameColors defaultColors();
            static FrameColors vistaColors();
            static FrameColors darkColors();
            static FrameColors lightColors();
        };

        struct FrameMetrics
        {
            QC::u32 borderWidth;
            QC::u32 shadowOffset;
            QC::u32 shadowSize;
            QC::u32 cornerRadius;
            QC::u32 paddingLeft;
            QC::u32 paddingTop;
            QC::u32 paddingRight;
            QC::u32 paddingBottom;

            FrameMetrics();

            void setPadding(QC::u32 all);
            void setPadding(QC::u32 horizontal, QC::u32 vertical);
            void setPadding(QC::u32 left, QC::u32 top, QC::u32 right, QC::u32 bottom);
        };

        class Frame
        {
        public:
            Frame();
            Frame(QC::u32 style);
            Frame(QC::u32 style, const FrameColors &colors);
            ~Frame() = default;

            QC::u32 style() const { return m_style; }
            void setStyle(QC::u32 style) { m_style = style; }
            void addStyle(QC::u32 flags) { m_style |= flags; }
            void removeStyle(QC::u32 flags) { m_style &= ~flags; }
            bool hasStyle(QC::u32 flag) const { return (m_style & flag) != 0; }
            QC::u32 borderStyle() const { return m_style & FrameStyle::BorderMask; }
            void setBorderStyle(QC::u32 borderFlag);
            QC::u32 fillStyle() const { return m_style & FrameStyle::FillMask; }

            Rect bounds() const { return m_bounds; }
            void setBounds(const Rect &bounds) { m_bounds = bounds; }
            void setPosition(QC::i32 x, QC::i32 y);
            void setSize(QC::u32 width, QC::u32 height);
            Rect contentRect() const;

            const FrameColors &colors() const { return m_colors; }
            FrameColors &colors() { return m_colors; }
            void setColors(const FrameColors &colors) { m_colors = colors; }
            Color backgroundColor() const { return m_colors.background; }
            void setBackgroundColor(Color color) { m_colors.background = color; }
            Color borderColor() const { return m_colors.borderMid; }
            void setBorderColor(Color color) { m_colors.borderMid = color; }
            Color shadowColor() const { return m_colors.shadow; }
            void setShadowColor(Color color) { m_colors.shadow = color; }

            const FrameMetrics &metrics() const { return m_metrics; }
            FrameMetrics &metrics() { return m_metrics; }
            void setMetrics(const FrameMetrics &metrics) { m_metrics = metrics; }
            QC::u32 borderWidth() const { return m_metrics.borderWidth; }
            void setBorderWidth(QC::u32 width) { m_metrics.borderWidth = width; }
            QC::u32 shadowOffset() const { return m_metrics.shadowOffset; }
            void setShadowOffset(QC::u32 offset) { m_metrics.shadowOffset = offset; }

            void paint(QG::IPainter *painter) const;
            void paintShadow(QG::IPainter *painter) const;
            void paintBackground(QG::IPainter *painter) const;
            void paintBorder(QG::IPainter *painter) const;

        private:
            void paintBorderFlat(QG::IPainter *painter) const;
            void paintBorderRaised(QG::IPainter *painter) const;
            void paintBorderSunken(QG::IPainter *painter) const;
            void paintBorderEtched(QG::IPainter *painter) const;
            void paintBorderDouble(QG::IPainter *painter) const;
            void paintBorderGroove(QG::IPainter *painter) const;

            void paintFillSolid(QG::IPainter *painter) const;
            void paintFillGradientV(QG::IPainter *painter) const;
            void paintFillGradientH(QG::IPainter *painter) const;

            void paintDropShadow(QG::IPainter *painter) const;
            void paintInnerShadow(QG::IPainter *painter) const;

            QC::u32 m_style;
            Rect m_bounds;
            FrameColors m_colors;
            FrameMetrics m_metrics;
        };
    }
} // namespace QW
