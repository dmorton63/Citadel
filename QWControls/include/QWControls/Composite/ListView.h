#pragma once

// QWControls ListView - List/table view control
// Namespace: QW::Controls

#include "QCTypes.h"
#include "QCVector.h"
#include "QWControls/Base/ControlBase.h"
#include "QWControls/Leaf/Label.h"
#include "QKEventTypes.h"

namespace QW
{
    class Window;

    namespace Controls
    {

        // Selection mode
        enum class SelectionMode : QC::u8
        {
            None,
            Single,
            Multiple
        };

        // List view item
        struct ListViewItem
        {
            char text[256];
            void *userData;
            bool selected;
        };

        // Column definition
        struct ListViewColumn
        {
            char header[64];
            QC::u32 width;
            TextAlign align;
        };

        // Selection change callback
        class ListView;
        using SelectionChangeHandler = void (*)(ListView *listView, void *userData);
        using ItemDoubleClickHandler = void (*)(ListView *listView, QC::usize index, void *userData);
        using ScrollOffsetChangeHandler = void (*)(ListView *listView, void *userData);

        class ListView : public ControlBase
        {
        public:
            ListView();
            ListView(Window *window, Rect bounds);
            virtual ~ListView();

            // Selection mode
            SelectionMode selectionMode() const { return m_selectionMode; }
            void setSelectionMode(SelectionMode mode) { m_selectionMode = mode; }

            // Columns
            void addColumn(const char *header, QC::u32 width, TextAlign align = TextAlign::Left);
            void removeColumn(QC::usize index);
            QC::usize columnCount() const { return m_columns.size(); }
            const ListViewColumn *column(QC::usize index) const;

            // Items
            QC::usize addItem(const char *text, void *userData = nullptr);
            void removeItem(QC::usize index);
            void clearItems();
            QC::usize itemCount() const { return m_items.size(); }
            const ListViewItem *item(QC::usize index) const;

            void setItemText(QC::usize index, const char *text);
            void setItemData(QC::usize index, void *userData);

            // Selection
            QC::isize selectedIndex() const;
            void setSelectedIndex(QC::isize index);
            void selectAll();
            void clearSelection();
            bool isSelected(QC::usize index) const;

            // Scrolling
            QC::usize scrollOffset() const { return m_scrollOffset; }
            void setScrollOffset(QC::usize offset);
            void ensureVisible(QC::usize index);

            // Called when scrollOffset changes (mouse wheel, ensureVisible, setScrollOffset).
            void setScrollOffsetChangeHandler(ScrollOffsetChangeHandler handler, void *userData);

            // Appearance
            Color textColor() const { return m_textColor; }
            void setTextColor(Color color) { m_textColor = color; }

            Color selectionColor() const { return m_selColor; }
            void setSelectionColor(Color color) { m_selColor = color; }

            Color headerColor() const { return m_headerColor; }
            void setHeaderColor(Color color) { m_headerColor = color; }

            Color backgroundColor() const { return m_bgColor; }
            void setBackgroundColor(Color color) { m_bgColor = color; }

            QC::u32 itemHeight() const { return m_itemHeight; }
            void setItemHeight(QC::u32 height) { m_itemHeight = height; }

            bool showHeader() const { return m_showHeader; }
            void setShowHeader(bool show) { m_showHeader = show; }

            // Events
            void setSelectionChangeHandler(SelectionChangeHandler handler, void *userData);
            void setItemDoubleClickHandler(ItemDoubleClickHandler handler, void *userData);

            // Rendering (override from ControlBase)
            void paint(const PaintContext &context) override;

            // Event handlers (override from ControlBase)
            bool onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY) override;
            bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseScroll(QC::i32 delta) override;
            bool onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods) override;

        private:
            QC::isize itemAtPoint(QC::i32 x, QC::i32 y);
            QC::usize visibleItemCount() const;

            QC::Vector<ListViewColumn> m_columns;
            QC::Vector<ListViewItem> m_items;

            SelectionMode m_selectionMode;
            QC::usize m_scrollOffset;
            QC::u32 m_itemHeight;
            bool m_showHeader;

            Color m_textColor;
            Color m_selColor;
            Color m_headerColor;
            Color m_bgColor;

            SelectionChangeHandler m_selChangeHandler;
            void *m_selChangeUserData;
            ItemDoubleClickHandler m_dblClickHandler;
            void *m_dblClickUserData;

            ScrollOffsetChangeHandler m_scrollChangeHandler;
            void *m_scrollChangeUserData;

            QC::i32 m_hoverIndex;
        };

    } // namespace Controls
} // namespace QW
