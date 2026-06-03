#pragma once

// QCMSDbBrowser - QCQL table/page browser panel
// Namespace: QCMS

#include "QCQLEngine.h"
#include "QWControls/Composite/ComboBox.h"
#include "QWControls/Composite/ListView.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"

namespace QW { class Window; }

namespace QCMS
{

    class DbBrowser
    {
    public:
        DbBrowser() = default;

        void build(QW::Window *window, QW::Controls::Panel *parent, QCQL::Database *database, const QW::Rect &bounds);
        void destroy();

        void setDatabase(QCQL::Database *database);
        void setVisible(bool visible);
        QW::Controls::Panel *panel() const { return m_panel; }

    private:
        void reloadTableList();
        void renderCurrentPage();

        static void onTableChanged(QW::Controls::ComboBox *comboBox, void *userData);
        static void onPrevClicked(QW::Controls::Button *button, void *userData);
        static void onNextClicked(QW::Controls::Button *button, void *userData);

        static void appendChar(char *dst, QC::usize &len, QC::usize cap, char c);
        static void appendText(char *dst, QC::usize &len, QC::usize cap, const char *src);
        static void appendU32(char *dst, QC::usize &len, QC::usize cap, QC::u32 value);
        static void appendCell(char *dst, QC::usize &len, QC::usize cap, const QCQL::Cell &cell);

        QCQL::Database           *m_database = nullptr;
        QW::Controls::Panel      *m_panel = nullptr;
        QW::Controls::Label      *m_title = nullptr;
        QW::Controls::ComboBox   *m_tableCombo = nullptr;
        QW::Controls::ListView   *m_rows = nullptr;
        QW::Controls::Button     *m_prev = nullptr;
        QW::Controls::Button     *m_next = nullptr;
        QW::Controls::Label      *m_status = nullptr;

        QC::usize                m_selectedTableIndex = 0;
        QC::usize                m_pageIndex = 0;
    };

} // namespace QCMS
