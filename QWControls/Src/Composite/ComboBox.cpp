// QWControls ComboBox - Dropdown selection control implementation
// Namespace: QW::Controls

#include "QWControls/Composite/ComboBox.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/TextBox.h"
#include "QWControls/Composite/ListView.h"
#include "QWWindow.h"
#include "QCMemUtil.h"
#include "QCString.h"

namespace QW
{
    namespace Controls
    {

        ComboBox::ComboBox()
            : Panel(),
              m_style(ComboBoxStyle::DropList),
              m_selectedIndex(-1),
              m_droppedDown(false),
              m_textColor(Color(0, 0, 0, 255)),
              m_dropdownBgColor(Color(255, 255, 255, 255)),
              m_maxDropdownItems(8),
              m_textBox(nullptr),
              m_dropButton(nullptr),
              m_dropdownList(nullptr),
              m_dropdownPanel(nullptr),
              m_changeHandler(nullptr),
              m_changeUserData(nullptr)
        {
            setBorderStyle(BorderStyle::Sunken);
        }

        ComboBox::ComboBox(Window *window, Rect bounds)
            : Panel(window, bounds),
              m_style(ComboBoxStyle::DropList),
              m_selectedIndex(-1),
              m_droppedDown(false),
              m_textColor(Color(0, 0, 0, 255)),
              m_dropdownBgColor(Color(255, 255, 255, 255)),
              m_maxDropdownItems(8),
              m_textBox(nullptr),
              m_dropButton(nullptr),
              m_dropdownList(nullptr),
              m_dropdownPanel(nullptr),
              m_changeHandler(nullptr),
              m_changeUserData(nullptr)
        {
            setBorderStyle(BorderStyle::Sunken);
            createChildControls();
        }

        ComboBox::~ComboBox()
        {
            // Clean up child controls we created
            if (m_textBox)
            {
                removeChild(m_textBox);
                delete m_textBox;
            }
            if (m_dropButton)
            {
                removeChild(m_dropButton);
                delete m_dropButton;
            }
            if (m_dropdownPanel)
            {
                // m_dropdownList is inside m_dropdownPanel
                delete m_dropdownPanel;
            }
        }

        void ComboBox::createChildControls()
        {
            // Calculate button width (same as height for square button)
            QC::u32 buttonWidth = m_bounds.height;
            QC::u32 textWidth = m_bounds.width > buttonWidth ? m_bounds.width - buttonWidth : 0;

            // Create TextBox
            Rect textRect = {0, 0, textWidth, m_bounds.height};
            m_textBox = new TextBox(m_window, textRect);
            if (m_style == ComboBoxStyle::DropList)
            {
                m_textBox->setReadOnly(true);
            }
            addChild(m_textBox);

            // Create dropdown button
            Rect buttonRect = {static_cast<QC::i32>(textWidth), 0, buttonWidth, m_bounds.height};
            m_dropButton = new Button(m_window, "v", buttonRect);
            addChild(m_dropButton);

            // Create dropdown panel (initially hidden)
            QC::u32 dropdownHeight = m_maxDropdownItems * 20; // 20px per item
            Rect dropdownRect = {0, static_cast<QC::i32>(m_bounds.height), m_bounds.width, dropdownHeight};
            m_dropdownPanel = new Panel(m_window, dropdownRect);
            m_dropdownPanel->setVisible(false);
            m_dropdownPanel->setBorderStyle(BorderStyle::Flat);
            m_dropdownPanel->setBackgroundColor(m_dropdownBgColor);

            // Create ListView inside dropdown panel
            Rect listRect = {0, 0, m_bounds.width, dropdownHeight};
            m_dropdownList = new ListView(m_window, listRect);
            m_dropdownList->setShowHeader(false);
            m_dropdownList->setSelectionMode(SelectionMode::Single);
            m_dropdownList->setBackgroundColor(m_dropdownBgColor);
            m_dropdownPanel->addChild(m_dropdownList);
        }

        void ComboBox::setStyle(ComboBoxStyle style)
        {
            m_style = style;
            if (m_textBox)
            {
                m_textBox->setReadOnly(style == ComboBoxStyle::DropList);
            }
        }

        QC::usize ComboBox::addItem(const char *text, void *userData)
        {
            ComboBoxItem item;
            if (text)
            {
                strncpy(item.text, text, sizeof(item.text) - 1);
                item.text[sizeof(item.text) - 1] = '\0';
            }
            else
            {
                item.text[0] = '\0';
            }
            item.userData = userData;
            m_items.push_back(item);

            // Also add to the dropdown list
            if (m_dropdownList)
            {
                m_dropdownList->addItem(text, userData);
            }

            return m_items.size() - 1;
        }

        void ComboBox::removeItem(QC::usize index)
        {
            if (index >= m_items.size())
                return;

            // Shift elements
            for (QC::usize i = index; i < m_items.size() - 1; ++i)
            {
                m_items[i] = m_items[i + 1];
            }
            m_items.pop_back();

            if (m_dropdownList)
            {
                m_dropdownList->removeItem(index);
            }

            // Update selection if needed
            if (m_selectedIndex >= static_cast<QC::isize>(m_items.size()))
            {
                m_selectedIndex = static_cast<QC::isize>(m_items.size()) - 1;
            }
        }

        void ComboBox::clearItems()
        {
            m_items.clear();
            m_selectedIndex = -1;

            if (m_dropdownList)
            {
                m_dropdownList->clearItems();
            }
            if (m_textBox)
            {
                m_textBox->setText("");
            }
        }

        const char *ComboBox::itemText(QC::usize index) const
        {
            if (index < m_items.size())
            {
                return m_items[index].text;
            }
            return nullptr;
        }

        void *ComboBox::itemData(QC::usize index) const
        {
            if (index < m_items.size())
            {
                return m_items[index].userData;
            }
            return nullptr;
        }

        void ComboBox::setItemText(QC::usize index, const char *text)
        {
            if (index < m_items.size() && text)
            {
                strncpy(m_items[index].text, text, sizeof(m_items[index].text) - 1);
                m_items[index].text[sizeof(m_items[index].text) - 1] = '\0';

                if (m_dropdownList)
                {
                    m_dropdownList->setItemText(index, text);
                }
            }
        }

        void ComboBox::setItemData(QC::usize index, void *userData)
        {
            if (index < m_items.size())
            {
                m_items[index].userData = userData;

                if (m_dropdownList)
                {
                    m_dropdownList->setItemData(index, userData);
                }
            }
        }

        void ComboBox::setSelectedIndex(QC::isize index)
        {
            if (index < -1 || index >= static_cast<QC::isize>(m_items.size()))
                return;

            if (m_selectedIndex != index)
            {
                m_selectedIndex = index;

                if (m_textBox)
                {
                    if (index >= 0)
                    {
                        m_textBox->setText(m_items[static_cast<QC::usize>(index)].text);
                    }
                    else
                    {
                        m_textBox->setText("");
                    }
                }

                if (m_dropdownList)
                {
                    m_dropdownList->setSelectedIndex(index);
                }

                if (m_changeHandler)
                {
                    m_changeHandler(this, m_changeUserData);
                }

                invalidate();
            }
        }

        const char *ComboBox::selectedText() const
        {
            if (m_selectedIndex >= 0 && static_cast<QC::usize>(m_selectedIndex) < m_items.size())
            {
                return m_items[static_cast<QC::usize>(m_selectedIndex)].text;
            }
            return nullptr;
        }

        const char *ComboBox::text() const
        {
            if (m_textBox)
            {
                return m_textBox->text();
            }
            return "";
        }

        void ComboBox::setText(const char *text)
        {
            if (m_textBox)
            {
                m_textBox->setText(text);
            }
        }

        void ComboBox::setTextColor(Color color)
        {
            m_textColor = color;
            if (m_textBox)
            {
                m_textBox->setTextColor(color);
            }
        }

        void ComboBox::setDropdownBackgroundColor(Color color)
        {
            m_dropdownBgColor = color;
            if (m_dropdownPanel)
            {
                m_dropdownPanel->setBackgroundColor(color);
            }
            if (m_dropdownList)
            {
                m_dropdownList->setBackgroundColor(color);
            }
        }

        void ComboBox::dropDown()
        {
            if (!m_droppedDown)
            {
                m_droppedDown = true;
                updateDropdownPosition();
                if (m_dropdownPanel)
                {
                    m_dropdownPanel->setVisible(true);
                }
                invalidate();
            }
        }

        void ComboBox::closeDropDown()
        {
            if (m_droppedDown)
            {
                m_droppedDown = false;
                if (m_dropdownPanel)
                {
                    m_dropdownPanel->setVisible(false);
                }
                invalidate();
            }
        }

        void ComboBox::toggleDropDown()
        {
            if (m_droppedDown)
            {
                closeDropDown();
            }
            else
            {
                dropDown();
            }
        }

        void ComboBox::setSelectionChangeHandler(ComboBoxChangeHandler handler, void *userData)
        {
            m_changeHandler = handler;
            m_changeUserData = userData;
        }

        void ComboBox::paint(const PaintContext &context)
        {
            // Paint the main panel
            Panel::paint(context);

            // Paint dropdown if open
            if (m_droppedDown && m_dropdownPanel)
            {
                m_dropdownPanel->paint(context);
            }
        }

        bool ComboBox::onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            // Check if clicking on dropdown button
            if (m_dropButton && m_dropButton->hitTest(x, y))
            {
                toggleDropDown();
                return true;
            }

            // Check if clicking on dropdown list
            if (m_droppedDown && m_dropdownPanel && m_dropdownList)
            {
                if (m_dropdownList->hitTest(x, y))
                {
                    // Let the list handle it and get the selection
                    m_dropdownList->onMouseDown(x, y, button);
                    QC::isize sel = m_dropdownList->selectedIndex();
                    if (sel >= 0)
                    {
                        onDropdownItemSelected(sel);
                    }
                    return true;
                }
                else
                {
                    // Click outside dropdown, close it
                    closeDropDown();
                }
            }

            // Forward to panel for other children
            return Panel::onMouseDown(x, y, button);
        }

        bool ComboBox::onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            return Panel::onMouseUp(x, y, button);
        }

        bool ComboBox::onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods)
        {
            // TODO: Handle arrow keys for navigation
            // TODO: Handle Enter to select
            // TODO: Handle Escape to close dropdown

            return Panel::onKeyDown(scancode, keycode, character, mods);
        }

        void ComboBox::updateDropdownPosition()
        {
            if (m_dropdownPanel)
            {
                Rect abs = absoluteBounds();
                QC::u32 dropdownHeight = m_maxDropdownItems * 20;
                Rect dropdownRect = {abs.x, abs.y + static_cast<QC::i32>(m_bounds.height),
                                     m_bounds.width, dropdownHeight};
                m_dropdownPanel->setBounds(dropdownRect);

                if (m_dropdownList)
                {
                    Rect listRect = {0, 0, m_bounds.width, dropdownHeight};
                    m_dropdownList->setBounds(listRect);
                }
            }
        }

        void ComboBox::onDropdownButtonClick()
        {
            toggleDropDown();
        }

        void ComboBox::onDropdownItemSelected(QC::isize index)
        {
            setSelectedIndex(index);
            closeDropDown();
        }

    } // namespace Controls
} // namespace QW
