#pragma once

// QCMSQueryStudio - Service-oriented SQL workspace for CSSMS MVP
// Namespace: QCMS

#include "QCQLEngine.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/TextBox.h"
#include "QWControls/Composite/ListView.h"

namespace QW { class Window; }

namespace QCMS
{

    class QueryStudio
    {
    public:
        QueryStudio() = default;

        void build(QW::Window *window, QW::Controls::Panel *parent, QCQL::Database *database, const QW::Rect &bounds);
        void destroy();
        void setDatabase(QCQL::Database *database);
        void setVisible(bool visible);
        QW::Controls::Panel *panel() const { return m_panel; }

    private:
        void executeCurrentQuery();
        void renderIntro();
        void refreshStatus();

        static void onExecuteClicked(QW::Controls::Button *button, void *userData);
        static void onClearClicked(QW::Controls::Button *button, void *userData);
        static void onQuerySubmitted(QW::Controls::TextBox *textBox, void *userData);

        QCQL::Database          *m_database = nullptr;
        QW::Controls::Panel     *m_panel = nullptr;
        QW::Controls::Label     *m_title = nullptr;
        QW::Controls::Label     *m_hint = nullptr;
        QW::Controls::TextBox   *m_queryBox = nullptr;
        QW::Controls::Button    *m_executeButton = nullptr;
        QW::Controls::Button    *m_clearButton = nullptr;
        QW::Controls::ListView  *m_results = nullptr;
        QW::Controls::Label     *m_status = nullptr;
    };

} // namespace QCMS