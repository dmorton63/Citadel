#pragma once

// QWControls ComboBox - Dropdown selection control
// Namespace: QW::Controls

#include "QCTypes.h"
#include "QCVector.h"
#include "QWControls/Containers/Panel.h"
#include "QKEventTypes.h"

namespace QW
{
    class Window;

    namespace Controls
    {

        /// Forward declarations
        class Button;
        class TextBox;
        class ListView;

        /// ComboBox item
        struct ComboBoxItem
        {
            char text[256];
            void *userData;
        };

        /// Selection change callback
        class ComboBox;
        using ComboBoxChangeHandler = void (*)(ComboBox *comboBox, void *userData);

        /// ComboBox style
        enum class ComboBoxStyle : QC::u8
        {
            DropDown, // Editable text + dropdown
            DropList  // Read-only text + dropdown
        };

        /// ComboBox - Dropdown selection control
        /// Composite control: Panel containing TextBox + Button, with popup ListView
        class ComboBox : public Panel
        {
        public:
            ComboBox();
            ComboBox(Window *window, Rect bounds);
            virtual ~ComboBox();

            // ==================== Style ====================

            ComboBoxStyle style() const { return m_style; }
            void setStyle(ComboBoxStyle style);

            // ==================== Items ====================

            /// Add an item to the dropdown
            QC::usize addItem(const char *text, void *userData = nullptr);

            /// Remove item at index
            void removeItem(QC::usize index);

            /// Clear all items
            void clearItems();

            /// Get item count
            QC::usize itemCount() const { return m_items.size(); }

            /// Get item text at index
            const char *itemText(QC::usize index) const;

            /// Get item data at index
            void *itemData(QC::usize index) const;

            /// Set item text
            void setItemText(QC::usize index, const char *text);

            /// Set item data
            void setItemData(QC::usize index, void *userData);

            // ==================== Selection ====================

            /// Get selected index (-1 if none)
            QC::isize selectedIndex() const { return m_selectedIndex; }

            /// Set selected index
            void setSelectedIndex(QC::isize index);

            /// Get selected text
            const char *selectedText() const;

            // ==================== Text ====================

            /// Get current text (editable mode)
            const char *text() const;

            /// Set current text (editable mode)
            void setText(const char *text);

            // ==================== Appearance ====================

            Color textColor() const { return m_textColor; }
            void setTextColor(Color color);

            Color dropdownBackgroundColor() const { return m_dropdownBgColor; }
            void setDropdownBackgroundColor(Color color);

            /// Maximum visible items in dropdown
            QC::u32 maxDropdownItems() const { return m_maxDropdownItems; }
            void setMaxDropdownItems(QC::u32 count) { m_maxDropdownItems = count; }

            // ==================== Dropdown State ====================

            bool isDroppedDown() const { return m_droppedDown; }
            void dropDown();
            void closeDropDown();
            void toggleDropDown();

            // ==================== Events ====================

            void setSelectionChangeHandler(ComboBoxChangeHandler handler, void *userData);

            // ==================== Rendering ====================

            void paint(const PaintContext &context) override;

            // ==================== Event Handlers ====================

            bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods) override;

        private:
            void createChildControls();
            void updateDropdownPosition();
            void onDropdownButtonClick();
            void onDropdownItemSelected(QC::isize index);

            ComboBoxStyle m_style;
            QC::Vector<ComboBoxItem> m_items;
            QC::isize m_selectedIndex;
            bool m_droppedDown;

            Color m_textColor;
            Color m_dropdownBgColor;
            QC::u32 m_maxDropdownItems;

            // Child controls (we own these)
            TextBox *m_textBox;
            Button *m_dropButton;
            ListView *m_dropdownList;
            Panel *m_dropdownPanel;

            ComboBoxChangeHandler m_changeHandler;
            void *m_changeUserData;
        };

    } // namespace Controls
} // namespace QW
