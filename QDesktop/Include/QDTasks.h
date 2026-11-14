#pragma once

// QDesktop Tasks - system overview and running applications
// Namespace: QD

#include "QCTypes.h"
#include "QWWindow.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Composite/ListView.h"

namespace QD
{
    class Desktop;

    /// Tasks - shows CPU/memory/security status, running applications and per-CPU scheduler tasks.
    /// The window is persistent: closing hides it so it can be reopened cheaply.
    class Tasks
    {
    public:
        explicit Tasks(Desktop *desktop);
        ~Tasks();

        void open();
        void close();
        bool isOpen() const;

        /// Periodic refresh while visible (driven by Desktop::tickAnimations)
        void tick();

    private:
        void createWindow();
        void refresh();
        void refreshSummary();
        void refreshApplications();
        void refreshCpus();
        void switchToSelected();

        static void onCloseClick(QW::Controls::Button *button, void *userData);
        static void onSwitchClick(QW::Controls::Button *button, void *userData);

        Desktop *m_desktop;
        QW::Window *m_window;
        QW::Controls::Label *m_cpuLabel;
        QW::Controls::Label *m_memoryLabel;
        QW::Controls::Label *m_heapLabel;
        QW::Controls::Label *m_securityLabel;
        QW::Controls::ListView *m_appList;
        QW::Controls::ListView *m_cpuList;
        QC::u64 m_lastRefreshMs;
    };

} // namespace QD
