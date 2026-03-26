// QWControls Label - Static text label implementation
// Namespace: QW::Controls

#include "QWControls/Leaf/Label.h"
#include "QKMemHeap.h"
#include "QCMemUtil.h"
#include "QCString.h"
#include "QWWindow.h"
#include "QGPainter.h"

namespace QW
{
    namespace Controls
    {

        Label::Label()
            : ControlBase(),
              m_text(nullptr),
              m_textAlign(TextAlign::Left),
              m_verticalAlign(VerticalAlign::Top),
              m_wordWrap(false),
              m_transparent(true),
                            m_underline(false),
                            m_textScaleOverride(0.0f),
              m_textColor(Color(0, 0, 0, 255))
        {
            m_bgColor = Color(255, 255, 255, 255);
                        m_clickHandler = nullptr;
                        m_clickUserData = nullptr;
        }

        Label::Label(Window *window, const char *text, Rect bounds)
            : ControlBase(window, bounds),
              m_text(nullptr),
              m_textAlign(TextAlign::Left),
              m_verticalAlign(VerticalAlign::Top),
              m_wordWrap(false),
              m_transparent(false),
                            m_underline(false),
                            m_textScaleOverride(0.0f),
              m_textColor(Color(0, 0, 0, 255))
        {
            m_bgColor = Color(255, 255, 255, 255);
            setText(text);
                        m_clickHandler = nullptr;
                        m_clickUserData = nullptr;
        }

        Label::~Label()
        {
            if (m_text)
            {
                QK::Memory::Heap::instance().free(m_text);
                m_text = nullptr;
            }
        }

        void Label::setText(const char *text)
        {
            if (m_text)
            {
                QK::Memory::Heap::instance().free(m_text);
                m_text = nullptr;
            }

            if (text)
            {
                QC::usize len = strlen(text);
                m_text = static_cast<char *>(QK::Memory::Heap::instance().allocate(len + 1));
                if (m_text)
                {
                    strcpy(m_text, text);
                }
            }

            invalidate();
        }

        void Label::setClickHandler(LabelClickHandler handler, void *userData)
        {
            m_clickHandler = handler;
            m_clickUserData = userData;
        }

        bool Label::hitTest(int x, int y) const
        {
            if (!m_clickHandler)
                return false;
            return ControlBase::hitTest(x, y);
        }

        bool Label::onMouseDown(int x, int y, QK::Event::MouseButton button)
        {
            if (!m_enabled || !m_visible || !m_clickHandler)
                return false;
            if (button != QK::Event::MouseButton::Left)
                return false;
            return hitTest(x, y);
        }

        bool Label::onMouseUp(int x, int y, QK::Event::MouseButton button)
        {
            if (!m_enabled || !m_visible || !m_clickHandler)
                return false;
            if (button != QK::Event::MouseButton::Left)
                return false;
            if (!hitTest(x, y))
                return false;
            m_clickHandler(this, m_clickUserData);
            return true;
        }

        void Label::paint(const PaintContext &context)
        {
            if (!m_visible || !context.painter)
                return;

            Rect abs = absoluteBounds();
            auto *painter = context.painter;

            const float oldScale = painter->textScale();
            if (m_textScaleOverride > 0.0f)
            {
                painter->setTextScale(m_textScaleOverride);
            }

            if (!m_transparent)
            {
                painter->fillRect(abs, m_bgColor);
            }

            if (!m_text)
            {
                if (m_textScaleOverride > 0.0f)
                    painter->setTextScale(oldScale);
                return;
            }

            const QC::Size textSize = painter->measureText(m_text);

            QC::i32 textX = abs.x;
            QC::i32 textY = abs.y;

            switch (m_textAlign)
            {
            case TextAlign::Center:
                textX = abs.x + (static_cast<QC::i32>(abs.width) - textSize.width) / 2;
                break;
            case TextAlign::Right:
                textX = abs.x + static_cast<QC::i32>(abs.width) - textSize.width;
                break;
            default:
                break;
            }

            switch (m_verticalAlign)
            {
            case VerticalAlign::Middle:
                textY = abs.y + (static_cast<QC::i32>(abs.height) - textSize.height) / 2;
                break;
            case VerticalAlign::Bottom:
                textY = abs.y + static_cast<QC::i32>(abs.height) - textSize.height;
                break;
            default:
                break;
            }

            const QC::Rect painterBounds = painter->bounds();
            const QC::Rect oldClip = painter->clipRect();
            const bool oldWasFullClip = (oldClip == painterBounds);

            // Painter clipping is in pixel coordinates (after origin translation is applied).
            // Our `absoluteBounds()` is in window coordinates, so we must offset by the current origin.
            const QC::Point origin = painter->origin();
            const QC::Rect absInPixels = abs.offset(origin.x, origin.y);
            const QC::Rect clip = oldClip.intersection(absInPixels);
            painter->setClipRect(clip);

            painter->drawText(textX, textY, m_text, m_textColor);

            if (oldWasFullClip)
                painter->clearClipRect();
            else
                painter->setClipRect(oldClip);

            if (m_underline)
            {
                // Underline only the first line (sufficient for MVP).
                const QC::i32 ulY = textY + static_cast<QC::i32>(textSize.height) - 1;
                if (textSize.width > 0)
                    painter->drawHLine(textX, ulY, textSize.width, m_textColor);
            }

            if (m_textScaleOverride > 0.0f)
                painter->setTextScale(oldScale);
        }

    } // namespace Controls
} // namespace QW
