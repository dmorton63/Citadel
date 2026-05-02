#include "QCMSDbBrowser.h"

#include "QCString.h"
#include "QCTypes.h"
#include "QWWindow.h"

namespace QCMS
{
    namespace
    {
        constexpr QC::u32 kPad = 12;

        char nibbleToHex(QC::u8 value)
        {
            return static_cast<char>(value < 10 ? ('0' + value) : ('A' + (value - 10)));
        }

        void appendU64Dec(char *dst, QC::usize &len, QC::usize cap, QC::u64 value)
        {
            char tmp[32] = {};
            QC::usize digits = 0;
            if (value == 0)
            {
                if (len + 1 < cap)
                {
                    dst[len++] = '0';
                    dst[len] = '\0';
                }
                return;
            }

            while (value > 0 && digits < sizeof(tmp))
            {
                tmp[digits++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }

            while (digits > 0)
            {
                if (len + 1 >= cap)
                    break;
                dst[len++] = tmp[--digits];
            }
            dst[len] = '\0';
        }
    } // namespace

    void DbBrowser::build(QW::Window *window, QW::Controls::Panel *parent, QCQL::Database *database, const QW::Rect &bounds)
    {
        if (!window || !parent)
            return;

        m_database = database;
        m_panel = new QW::Controls::Panel(window, bounds);
        m_panel->setBorderStyle(QW::Controls::BorderStyle::None);
        m_panel->setPadding(12);
        m_panel->setVisible(false);
        parent->addChild(m_panel);

        m_title = new QW::Controls::Label(window, "QCQL Table Browser", {10, 8, bounds.width - 20, 24});
        m_panel->addChild(m_title);

        m_tableCombo = new QW::Controls::ComboBox(window, {10, 38, 320, 24});
        m_tableCombo->setSelectionChangeHandler(&DbBrowser::onTableChanged, this);
        m_panel->addChild(m_tableCombo);

        m_rows = new QW::Controls::ListView(window, {10, 70, bounds.width - 20, bounds.height - 120});
        m_rows->setSelectionMode(QW::Controls::SelectionMode::Single);
        m_rows->setShowHeader(true);
        m_rows->addColumn("Rows", bounds.width - 40, QW::Controls::TextAlign::Left);
        m_panel->addChild(m_rows);

        const QC::i32 footerY = static_cast<QC::i32>(bounds.height - 40);
        m_prev = new QW::Controls::Button(window, "Prev", {10, footerY, 72, 24});
        m_prev->setContentMode(QW::ButtonContentMode::Text);
        m_prev->setClickHandler(&DbBrowser::onPrevClicked, this);
        m_panel->addChild(m_prev);

        m_next = new QW::Controls::Button(window, "Next", {88, footerY, 72, 24});
        m_next->setContentMode(QW::ButtonContentMode::Text);
        m_next->setClickHandler(&DbBrowser::onNextClicked, this);
        m_panel->addChild(m_next);

        m_status = new QW::Controls::Label(window, "No table selected", {170, footerY + 3, bounds.width - 180, 20});
        m_panel->addChild(m_status);

        reloadTableList();
        renderCurrentPage();
    }

    void DbBrowser::destroy()
    {
        m_database = nullptr;
        m_panel = nullptr;
        m_title = nullptr;
        m_tableCombo = nullptr;
        m_rows = nullptr;
        m_prev = nullptr;
        m_next = nullptr;
        m_status = nullptr;
    }

    void DbBrowser::setVisible(bool visible)
    {
        if (m_panel)
            m_panel->setVisible(visible);
    }

    void DbBrowser::reloadTableList()
    {
        if (!m_tableCombo)
            return;

        m_tableCombo->clearItems();
        m_selectedTableIndex = 0;
        m_pageIndex = 0;

        if (!m_database)
            return;

        const QC::usize tableCount = m_database->tables.size();
        for (QC::usize i = 0; i < tableCount; ++i)
        {
            const void *itemData = reinterpret_cast<void *>(i + 1);
            m_tableCombo->addItem(m_database->tables[i].name, const_cast<void *>(itemData));
        }

        if (tableCount > 0)
            m_tableCombo->setSelectedIndex(0);
    }

    void DbBrowser::renderCurrentPage()
    {
        if (!m_rows || !m_status)
            return;

        m_rows->clearItems();

        if (!m_database)
        {
            m_status->setText("Database unavailable");
            return;
        }

        const QC::usize tableCount = m_database->tables.size();
        if (tableCount == 0)
        {
            m_status->setText("No tables in database");
            return;
        }

        if (m_selectedTableIndex >= tableCount)
            m_selectedTableIndex = 0;

        QCQL::Table &table = m_database->tables[m_selectedTableIndex];
        if (table.pages.size() == 0)
        {
            m_status->setText("Table has no pages");
            return;
        }

        if (m_pageIndex >= table.pages.size())
            m_pageIndex = table.pages.size() - 1;

        const QC::u32 pageId = table.pages[m_pageIndex];
        QCQL::Page page{};
        const QCQL::Status loadSt = QCQL::Engine::instance().loadPage(*m_database, pageId, page);
        if (loadSt != QCQL::Status::Success)
        {
            m_status->setText("Failed to load page");
            return;
        }

        const QC::usize rowCount = page.rowOffsets.size();
        for (QC::usize i = 0; i < rowCount; ++i)
        {
            QCQL::Row row{};
            const QCQL::Status readSt = QCQL::Engine::instance().readRow(*m_database, pageId, page.rowOffsets[i], row);
            if (readSt != QCQL::Status::Success || row.tombstone)
                continue;

            char line[256] = {};
            QC::usize len = 0;

            appendText(line, len, sizeof(line), "[");
            appendChar(line, len, sizeof(line), '#');
            appendText(line, len, sizeof(line), " ");

            // show row offset for quick diagnostics
            const QC::u16 off = page.rowOffsets[i];
            appendChar(line, len, sizeof(line), nibbleToHex(static_cast<QC::u8>((off >> 12) & 0xF)));
            appendChar(line, len, sizeof(line), nibbleToHex(static_cast<QC::u8>((off >> 8) & 0xF)));
            appendChar(line, len, sizeof(line), nibbleToHex(static_cast<QC::u8>((off >> 4) & 0xF)));
            appendChar(line, len, sizeof(line), nibbleToHex(static_cast<QC::u8>(off & 0xF)));
            appendText(line, len, sizeof(line), "] ");

            for (QC::usize c = 0; c < row.cells.size(); ++c)
            {
                if (c > 0)
                    appendText(line, len, sizeof(line), " | ");
                appendCell(line, len, sizeof(line), row.cells[c]);
            }

            m_rows->addItem(line, nullptr);
        }

        char status[128] = {};
        QC::usize sLen = 0;
        appendText(status, sLen, sizeof(status), "Table: ");
        appendText(status, sLen, sizeof(status), table.name);
        appendText(status, sLen, sizeof(status), "   Page ");

        const QC::u32 pageNum = static_cast<QC::u32>(m_pageIndex + 1);
        const QC::u32 pageTotal = static_cast<QC::u32>(table.pages.size());
        appendU32(status, sLen, sizeof(status), pageNum);
        appendText(status, sLen, sizeof(status), "/");
        appendU32(status, sLen, sizeof(status), pageTotal);

        m_status->setText(status);
    }

    void DbBrowser::onTableChanged(QW::Controls::ComboBox *comboBox, void *userData)
    {
        auto *self = static_cast<DbBrowser *>(userData);
        if (!self || !comboBox)
            return;

        const QC::isize idx = comboBox->selectedIndex();
        if (idx < 0)
            return;

        self->m_selectedTableIndex = static_cast<QC::usize>(idx);
        self->m_pageIndex = 0;
        self->renderCurrentPage();
    }

    void DbBrowser::onPrevClicked(QW::Controls::Button *, void *userData)
    {
        auto *self = static_cast<DbBrowser *>(userData);
        if (!self)
            return;

        if (self->m_pageIndex > 0)
            --self->m_pageIndex;
        self->renderCurrentPage();
    }

    void DbBrowser::onNextClicked(QW::Controls::Button *, void *userData)
    {
        auto *self = static_cast<DbBrowser *>(userData);
        if (!self || !self->m_database)
            return;

        const QC::usize count = self->m_database->tables.size();
        if (count == 0 || self->m_selectedTableIndex >= count)
            return;

        const QC::usize pageCount = self->m_database->tables[self->m_selectedTableIndex].pages.size();
        if (self->m_pageIndex + 1 < pageCount)
            ++self->m_pageIndex;
        self->renderCurrentPage();
    }

    void DbBrowser::appendChar(char *dst, QC::usize &len, QC::usize cap, char c)
    {
        if (!dst || len + 1 >= cap)
            return;
        dst[len++] = c;
        dst[len] = '\0';
    }

    void DbBrowser::appendText(char *dst, QC::usize &len, QC::usize cap, const char *src)
    {
        if (!dst || !src)
            return;
        for (QC::usize i = 0; src[i] != '\0'; ++i)
        {
            if (len + 1 >= cap)
                break;
            dst[len++] = src[i];
        }
        dst[len] = '\0';
    }

    void DbBrowser::appendU32(char *dst, QC::usize &len, QC::usize cap, QC::u32 value)
    {
        char tmp[16] = {};
        QC::usize digits = 0;
        if (value == 0)
        {
            appendChar(dst, len, cap, '0');
            return;
        }

        while (value > 0 && digits < sizeof(tmp))
        {
            tmp[digits++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }

        while (digits > 0)
            appendChar(dst, len, cap, tmp[--digits]);
    }

    void DbBrowser::appendCell(char *dst, QC::usize &len, QC::usize cap, const QCQL::Cell &cell)
    {
        if (cell.type == QCQL::ColumnType::Text)
        {
            for (QC::usize i = 0; i < cell.bytes.size(); ++i)
            {
                const char ch = static_cast<char>(cell.bytes[i]);
                if (ch == '\0')
                    continue;

                // Keep display deterministic and readable in constrained fonts.
                if (ch >= 32 && ch <= 126)
                {
                    appendChar(dst, len, cap, ch);
                }
                else if (ch == '\n' || ch == '\r' || ch == '\t')
                {
                    appendChar(dst, len, cap, ' ');
                }
                else
                {
                    appendText(dst, len, cap, "\\x");
                    appendChar(dst, len, cap, nibbleToHex(static_cast<QC::u8>((cell.bytes[i] >> 4) & 0xF)));
                    appendChar(dst, len, cap, nibbleToHex(static_cast<QC::u8>(cell.bytes[i] & 0xF)));
                }
            }
            return;
        }

        if (cell.type == QCQL::ColumnType::Bool)
        {
            if (!cell.bytes.empty() && cell.bytes[0] != 0)
                appendText(dst, len, cap, "true");
            else
                appendText(dst, len, cap, "false");
            return;
        }

        if (cell.type == QCQL::ColumnType::Int || cell.type == QCQL::ColumnType::DateTime)
        {
            // Decode little-endian integers up to 8 bytes.
            QC::u64 value = 0;
            const QC::usize maxBytes = cell.bytes.size() < 8 ? cell.bytes.size() : static_cast<QC::usize>(8);
            for (QC::usize i = 0; i < maxBytes; ++i)
            {
                value |= (static_cast<QC::u64>(cell.bytes[i]) << (8 * i));
            }

            if (cell.type == QCQL::ColumnType::DateTime)
                appendText(dst, len, cap, "ts=");
            appendU64Dec(dst, len, cap, value);
            return;
        }

        // Non-text cells: hex dump prefixed with 0x
        appendText(dst, len, cap, "0x");
        for (QC::usize i = 0; i < cell.bytes.size(); ++i)
        {
            const QC::u8 b = cell.bytes[i];
            appendChar(dst, len, cap, nibbleToHex(static_cast<QC::u8>((b >> 4) & 0xF)));
            appendChar(dst, len, cap, nibbleToHex(static_cast<QC::u8>(b & 0xF)));
        }
    }

} // namespace QCMS
