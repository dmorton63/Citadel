#pragma once

// Panel - decorated container control
// Namespace: QW::Controls

#include "QWControls/Containers/Container.h"
#include "QCColor.h"

namespace QW
{
    namespace Controls
    {
        enum class BorderStyle : QC::u8
        {
            None,
            Flat,
            Raised,
            Sunken,
            Etched
        };

        class Panel : public Container
        {
        public:
            Panel();
            Panel(Window *window, Rect bounds);
            virtual ~Panel() = default;

            Panel *asPanel() override { return this; }
            const Panel *asPanel() const override { return this; }

            bool isFrameVisible() const { return m_frameVisible; }
            void setFrameVisible(bool visible)
            {
                if (m_frameVisible == visible)
                    return;
                m_frameVisible = visible;
                invalidate();
            }

            BorderStyle borderStyle() const { return m_borderStyle; }
            void setBorderStyle(BorderStyle style);

            Color borderColor() const { return m_borderColor; }
            void setBorderColor(Color color);
            bool hasBorderColorOverride() const { return m_hasBorderColorOverride; }

            QC::u32 borderWidth() const { return m_borderWidth; }
            void setBorderWidth(QC::u32 width);

            bool hasBackgroundOverride() const { return m_hasBackgroundOverride; }
            Color backgroundColor() const { return m_backgroundColor; }
            void setBackgroundColor(Color color);
            void clearBackgroundColor();

            void setPadding(QC::u32 left, QC::u32 top, QC::u32 right, QC::u32 bottom);
            void setPadding(QC::u32 all) { setPadding(all, all, all, all); }

            QC::u32 paddingLeft() const { return m_paddingLeft; }
            QC::u32 paddingTop() const { return m_paddingTop; }
            QC::u32 paddingRight() const { return m_paddingRight; }
            QC::u32 paddingBottom() const { return m_paddingBottom; }

            Rect clientRect() const;

            void paint(const PaintContext &context) override;

        protected:
            bool m_frameVisible;
            BorderStyle m_borderStyle;
            QC::u32 m_paddingLeft;
            QC::u32 m_paddingTop;
            QC::u32 m_paddingRight;
            QC::u32 m_paddingBottom;
            QC::u32 m_borderWidth;
            bool m_hasBorderColorOverride;
            Color m_borderColor;
            bool m_hasBackgroundOverride;
            Color m_backgroundColor;
        };
    }
} // namespace QW
