#pragma once

// QWControls TextBox - Text input control
// Namespace: QW::Controls

#include "QCTypes.h"
#include "QWControls/Base/ControlBase.h"
#include "QKEventTypes.h"

namespace QW
{
    class Window;

    namespace Controls
    {

        // Text change callback
        class TextBox;
        using TextChangeHandler = void (*)(TextBox *textBox, void *userData);
        using TextSubmitHandler = void (*)(TextBox *textBox, void *userData);

        class TextBox : public ControlBase
        {
        public:
            TextBox();
            TextBox(Window *window, Rect bounds);
            virtual ~TextBox();

            // Properties
            const char *text() const { return m_text; }
            void setText(const char *text);

            const char *placeholder() const { return m_placeholder; }
            void setPlaceholder(const char *placeholder);

            bool isReadOnly() const { return m_readOnly; }
            void setReadOnly(bool readOnly) { m_readOnly = readOnly; }

            bool isPassword() const { return m_password; }
            void setPassword(bool password) { m_password = password; }

            QC::usize maxLength() const { return m_maxLength; }
            void setMaxLength(QC::usize length) { m_maxLength = length; }

            // Selection
            QC::usize cursorPosition() const { return m_cursorPos; }
            void setCursorPosition(QC::usize pos);

            QC::usize selectionStart() const { return m_selStart; }
            QC::usize selectionEnd() const { return m_selEnd; }
            void setSelection(QC::usize start, QC::usize end);
            void selectAll();
            void clearSelection();

            // Appearance
            Color textColor() const { return m_textColor; }
            void setTextColor(Color color) { m_textColor = color; }

            Color borderColor() const { return m_borderColor; }
            void setBorderColor(Color color) { m_borderColor = color; }

            Color selectionColor() const { return m_selectionColor; }
            void setSelectionColor(Color color) { m_selectionColor = color; }

            Color backgroundColor() const { return m_bgColor; }
            void setBackgroundColor(Color color) { m_bgColor = color; }

            // Events
            void setTextChangeHandler(TextChangeHandler handler, void *userData);
            void setTextSubmitHandler(TextSubmitHandler handler, void *userData);

            // Rendering (override from ControlBase)
            void paint(const PaintContext &context) override;

            // Event handlers (override from ControlBase)
            bool onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY) override;
            bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods) override;
            void onFocus() override;
            void onBlur() override;

        private:
            void insertChar(char c);
            void deleteChar(bool forward);
            void moveCursor(QC::isize delta, bool extend);

            char *m_text;
            QC::usize m_textLength;
            QC::usize m_textCapacity;
            char m_placeholder[256];

            QC::usize m_cursorPos;
            QC::usize m_selStart;
            QC::usize m_selEnd;
            QC::usize m_maxLength;

            bool m_readOnly;
            bool m_password;

            Color m_textColor;
            Color m_borderColor;
            Color m_selectionColor;
            Color m_bgColor;

            TextChangeHandler m_changeHandler;
            void *m_changeUserData;

            TextSubmitHandler m_submitHandler;
            void *m_submitUserData;

            // Scroll position for long text
            QC::usize m_scrollOffset;
        };

    } // namespace Controls
} // namespace QW
