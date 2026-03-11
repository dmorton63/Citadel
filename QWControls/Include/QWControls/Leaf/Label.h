#pragma once

// QWControls Label - Static text label
// Namespace: QW::Controls

#include "QCTypes.h"
#include "QWControls/Base/ControlBase.h"
#include "QKEventTypes.h"

namespace QW
{
    class Window;

    namespace Controls
    {

        // Text alignment (use the QEvent-compatible definition)
        enum class TextAlign : QC::u8
        {
            Left,
            Center,
            Right
        };

        enum class VerticalAlign : QC::u8
        {
            Top,
            Middle,
            Bottom
        };

        class Label : public ControlBase
        {
        public:
            Label();
            Label(Window *window, const char *text, Rect bounds);
            virtual ~Label();

            // Properties
            const char *text() const { return m_text; }
            void setText(const char *text);

            // Alignment
            TextAlign textAlign() const { return m_textAlign; }
            void setTextAlign(TextAlign align) { m_textAlign = align; }

            VerticalAlign verticalAlign() const { return m_verticalAlign; }
            void setVerticalAlign(VerticalAlign align) { m_verticalAlign = align; }

            // Word wrap
            bool wordWrap() const { return m_wordWrap; }
            void setWordWrap(bool wrap) { m_wordWrap = wrap; }

            // Appearance
            Color textColor() const { return m_textColor; }
            void setTextColor(Color color) { m_textColor = color; }

            Color backgroundColor() const { return m_bgColor; }
            void setBackgroundColor(Color color) { m_bgColor = color; }

            bool transparent() const { return m_transparent; }
            void setTransparent(bool transparent) { m_transparent = transparent; }

            // Labels are non-interactive by default; they should not intercept mouse hit tests.
            bool hitTest(int, int) const override { return false; }

            // Rendering (override from ControlBase)
            void paint(const PaintContext &context) override;

        private:
            char *m_text;

            TextAlign m_textAlign;
            VerticalAlign m_verticalAlign;
            bool m_wordWrap;
            bool m_transparent;

            Color m_textColor;
            Color m_bgColor;
        };

    } // namespace Controls
} // namespace QW
