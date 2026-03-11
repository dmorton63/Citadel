// QWControls TextBox - Text input control implementation
// Namespace: QW::Controls

#include "QWControls/Leaf/TextBox.h"
#include "QKMemHeap.h"
#include "QCMemUtil.h"
#include "QCString.h"
#include "QWWindow.h"
#include "QGPainter.h"

namespace QW
{
    namespace Controls
    {

        TextBox::TextBox()
            : ControlBase(),
              m_text(nullptr),
              m_textLength(0),
              m_textCapacity(0),
              m_cursorPos(0),
              m_selStart(0),
              m_selEnd(0),
              m_maxLength(1024),
              m_readOnly(false),
              m_password(false),
              m_textColor(Color(0, 0, 0, 255)),
              m_borderColor(Color(128, 128, 128, 255)),
              m_selectionColor(Color(51, 153, 255, 255)),
              m_changeHandler(nullptr),
              m_changeUserData(nullptr),
              m_submitHandler(nullptr),
              m_submitUserData(nullptr),
              m_scrollOffset(0)
        {
            m_bgColor = Color(255, 255, 255, 255);
            m_placeholder[0] = '\0';

            // Initial allocation
            m_textCapacity = 256;
            m_text = static_cast<char *>(QK::Memory::Heap::instance().allocate(m_textCapacity));
            if (m_text)
            {
                m_text[0] = '\0';
            }
        }

        TextBox::TextBox(Window *window, Rect bounds)
            : ControlBase(window, bounds),
              m_text(nullptr),
              m_textLength(0),
              m_textCapacity(0),
              m_cursorPos(0),
              m_selStart(0),
              m_selEnd(0),
              m_maxLength(1024),
              m_readOnly(false),
              m_password(false),
              m_textColor(Color(0, 0, 0, 255)),
              m_borderColor(Color(128, 128, 128, 255)),
              m_selectionColor(Color(51, 153, 255, 255)),
              m_changeHandler(nullptr),
              m_changeUserData(nullptr),
              m_submitHandler(nullptr),
              m_submitUserData(nullptr),
              m_scrollOffset(0)
        {
            m_bgColor = Color(255, 255, 255, 255);
            m_placeholder[0] = '\0';

            // Initial allocation
            m_textCapacity = 256;
            m_text = static_cast<char *>(QK::Memory::Heap::instance().allocate(m_textCapacity));
            if (m_text)
            {
                m_text[0] = '\0';
            }
        }

        TextBox::~TextBox()
        {
            if (m_text)
            {
                QK::Memory::Heap::instance().free(m_text);
                m_text = nullptr;
            }
        }

        void TextBox::setText(const char *text)
        {
            if (!text)
            {
                if (m_text)
                {
                    m_text[0] = '\0';
                }
                m_textLength = 0;
                m_cursorPos = 0;
                clearSelection();
                invalidate();
                return;
            }

            QC::usize len = strlen(text);
            if (len > m_maxLength)
            {
                len = m_maxLength;
            }

            if (len + 1 > m_textCapacity)
            {
                QC::usize newCapacity = len + 1;
                char *newText = static_cast<char *>(QK::Memory::Heap::instance().allocate(newCapacity));
                if (newText)
                {
                    QK::Memory::Heap::instance().free(m_text);
                    m_text = newText;
                    m_textCapacity = newCapacity;
                }
                else
                {
                    return;
                }
            }

            strncpy(m_text, text, len);
            m_text[len] = '\0';
            m_textLength = len;
            m_cursorPos = len;
            clearSelection();
            invalidate();
        }

        void TextBox::setPlaceholder(const char *placeholder)
        {
            if (placeholder)
            {
                strncpy(m_placeholder, placeholder, sizeof(m_placeholder) - 1);
                m_placeholder[sizeof(m_placeholder) - 1] = '\0';
            }
            else
            {
                m_placeholder[0] = '\0';
            }
        }

        void TextBox::setCursorPosition(QC::usize pos)
        {
            if (pos > m_textLength)
            {
                pos = m_textLength;
            }
            m_cursorPos = pos;
        }

        void TextBox::setSelection(QC::usize start, QC::usize end)
        {
            if (start > m_textLength)
                start = m_textLength;
            if (end > m_textLength)
                end = m_textLength;

            m_selStart = start;
            m_selEnd = end;
            m_cursorPos = end;
        }

        void TextBox::selectAll()
        {
            m_selStart = 0;
            m_selEnd = m_textLength;
            m_cursorPos = m_textLength;
        }

        void TextBox::clearSelection()
        {
            m_selStart = 0;
            m_selEnd = 0;
        }

        void TextBox::setTextChangeHandler(TextChangeHandler handler, void *userData)
        {
            m_changeHandler = handler;
            m_changeUserData = userData;
        }

        void TextBox::setTextSubmitHandler(TextSubmitHandler handler, void *userData)
        {
            m_submitHandler = handler;
            m_submitUserData = userData;
        }

        void TextBox::paint(const PaintContext &context)
        {
            if (!m_visible || !context.painter)
                return;

            Rect abs = absoluteBounds();
            auto *painter = context.painter;

            // Background + border
            painter->fillRect(abs, m_bgColor);
            painter->drawRect(abs, m_borderColor);

            // Text drawing baseline (simple vertical centering)
            QC::i32 textX = abs.x + 4; // small left padding
            QC::i32 textY = abs.y + static_cast<QC::i32>(abs.height / 2);

            if (m_textLength > 0)
            {
                if (m_password)
                {
                    char masked[256];
                    QC::usize len = m_textLength < 255 ? m_textLength : 255;
                    for (QC::usize i = 0; i < len; ++i)
                        masked[i] = '*';
                    masked[len] = '\0';
                    painter->drawText(textX, textY, masked, m_textColor);
                }
                else
                {
                    painter->drawText(textX, textY, m_text, m_textColor);
                }
            }
            else if (m_placeholder[0] != '\0')
            {
                Color placeholderColor(160, 160, 160, 255);
                painter->drawText(textX, textY, m_placeholder, placeholderColor);
            }

            // Selection highlight
            if (m_selStart != m_selEnd)
            {
                QC::usize start = m_selStart < m_selEnd ? m_selStart : m_selEnd;
                QC::usize end = m_selStart > m_selEnd ? m_selStart : m_selEnd;
                QC::i32 selX = abs.x + 4 + static_cast<QC::i32>(start * 8);
                QC::u32 selWidth = static_cast<QC::u32>((end - start) * 8);
                Rect selRect = {selX, abs.y + 2, selWidth, abs.height - 4};
                painter->fillRect(selRect, m_selectionColor);
            }

            // Caret last so it stays visible atop selection
            if (m_focused)
            {
                QC::i32 cursorX = abs.x + 4 + static_cast<QC::i32>(m_cursorPos * 8);
                Rect cursorRect = {cursorX, abs.y + 2, 1, abs.height - 4};
                painter->fillRect(cursorRect, m_textColor);
            }
        }

        bool TextBox::onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods)
        {
            if (!m_enabled)
                return false;

            (void)keycode;

            // Handle printable characters
            if (character >= 32 && character < 127 && !m_readOnly)
            {
                // Delete selection first if any
                if (m_selStart != m_selEnd)
                {
                    QC::usize start = m_selStart < m_selEnd ? m_selStart : m_selEnd;
                    QC::usize end = m_selStart > m_selEnd ? m_selStart : m_selEnd;

                    QC::usize remaining = m_textLength - end;
                    for (QC::usize i = 0; i <= remaining; ++i)
                    {
                        m_text[start + i] = m_text[end + i];
                    }
                    m_textLength -= (end - start);
                    m_cursorPos = start;
                    clearSelection();
                }
                insertChar(character);
                invalidate();
                return true;
            }

            // Handle special keys
            (void)keycode;
            (void)mods;

            // Enter submits the current text
            if (character == '\n')
            {
                if (m_submitHandler)
                {
                    m_submitHandler(this, m_submitUserData);
                }
                return true;
            }

            // Backspace deletes one character
            if (character == '\b' && !m_readOnly)
            {
                deleteChar(false);
                invalidate();
                return true;
            }

            // TODO: Map scancodes to actions (arrows, home, end, delete)
            (void)scancode;
            return false;
        }

        bool TextBox::onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY)
        {
            (void)deltaX;
            (void)deltaY;
            (void)x;
            (void)y;
            // TODO: Handle selection dragging
            return hitTest(x, y);
        }

        bool TextBox::onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            bool inside = hitTest(x, y);

            if (inside)
            {
                setFocused(true);
                // TODO: Calculate cursor position from x coordinate
                clearSelection();
                invalidate();
                return true;
            }

            return false;
        }

        bool TextBox::onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            (void)x;
            (void)y;
            (void)button;
            return false;
        }

        void TextBox::onFocus()
        {
            invalidate();
        }

        void TextBox::onBlur()
        {
            clearSelection();
            invalidate();
        }

        void TextBox::insertChar(char c)
        {
            if (m_textLength >= m_maxLength)
                return;

            // Grow buffer if needed
            if (m_textLength + 2 > m_textCapacity)
            {
                QC::usize newCapacity = m_textCapacity * 2;
                char *newText = static_cast<char *>(QK::Memory::Heap::instance().allocate(newCapacity));
                if (newText)
                {
                    strcpy(newText, m_text);
                    QK::Memory::Heap::instance().free(m_text);
                    m_text = newText;
                    m_textCapacity = newCapacity;
                }
                else
                {
                    return;
                }
            }

            // Shift characters after cursor
            for (QC::usize i = m_textLength; i > m_cursorPos; --i)
            {
                m_text[i] = m_text[i - 1];
            }

            m_text[m_cursorPos] = c;
            m_cursorPos++;
            m_textLength++;
            m_text[m_textLength] = '\0';

            if (m_changeHandler)
            {
                m_changeHandler(this, m_changeUserData);
            }
        }

        void TextBox::deleteChar(bool forward)
        {
            if (forward)
            {
                if (m_cursorPos >= m_textLength)
                    return;

                for (QC::usize i = m_cursorPos; i < m_textLength; ++i)
                {
                    m_text[i] = m_text[i + 1];
                }
                m_textLength--;
            }
            else
            {
                if (m_cursorPos == 0)
                    return;

                m_cursorPos--;
                for (QC::usize i = m_cursorPos; i < m_textLength; ++i)
                {
                    m_text[i] = m_text[i + 1];
                }
                m_textLength--;
            }

            if (m_changeHandler)
            {
                m_changeHandler(this, m_changeUserData);
            }
        }

        void TextBox::moveCursor(QC::isize delta, bool extend)
        {
            QC::isize newPos = static_cast<QC::isize>(m_cursorPos) + delta;
            if (newPos < 0)
                newPos = 0;
            if (static_cast<QC::usize>(newPos) > m_textLength)
                newPos = static_cast<QC::isize>(m_textLength);

            if (extend)
            {
                if (m_selStart == m_selEnd)
                {
                    m_selStart = m_cursorPos;
                }
                m_selEnd = static_cast<QC::usize>(newPos);
            }
            else
            {
                clearSelection();
            }

            m_cursorPos = static_cast<QC::usize>(newPos);
        }

    } // namespace Controls
} // namespace QW
