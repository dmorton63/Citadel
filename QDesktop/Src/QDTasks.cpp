// QDesktop Tasks - system overview and running applications
// Namespace: QD

#include "QDTasks.h"
#include "QDAppWindow.h"
#include "QDDesktop.h"
#include "QDTextFormat.h"
#include "QCString.h"
#include "QDrvTimer.h"
#include "QKTaskManager.h"
#include "QKMemPMM.h"
#include "QKMemHeap.h"

// Kernel boot services (defined in kernel/Boot); declarations must match kernel/Boot/QKBoot.h.
namespace QKBoot
{
    static constexpr QC::usize kSmpStatusMaxEntries = 256;

    enum class SmpCpuState : QC::u32
    {
        Offline = 0,
        Launching = 1,
        Initializing = 2,
        Online = 3,
    };

    struct SmpCpuStatusEntry
    {
        QC::u32 cpuIndex = 0;
        QC::u32 lapicId = 0;
        bool online = false;
        SmpCpuState state = SmpCpuState::Offline;
        QC::u64 idleHeartbeats = 0;
        QC::u64 timerHookTicks = 0;
        QC::u64 schedulerHookKicks = 0;
        QC::u64 schedulerTaskTurns[3] = {};
        QC::u64 schedulerTaskWakeups = 0;
        QC::u64 schedulerTasksReaped = 0;
        QC::u64 workItemsCompleted = 0;
        QC::u64 workChecksum = 0;
    };

    struct SmpStatusSnapshot
    {
        QC::u64 totalCpus = 0;
        QC::u64 targetAps = 0;
        QC::u64 onlineAps = 0;
        QC::usize slotCount = 0;
        SmpCpuStatusEntry entries[kSmpStatusMaxEntries]{};
    };

    bool getSmpStatusSnapshot(SmpStatusSnapshot &out);
}

namespace QK::Boot::Tpm
{
    bool IsReady();
}

namespace QD
{
    namespace
    {
        constexpr QC::u32 kWindowW = 600;
        constexpr QC::u32 kWindowH = 460;
        constexpr QC::u64 kRefreshMs = 1000;
        constexpr QC::u32 kMaxCpuRows = 64;

        static const char *cpuStateName(QKBoot::SmpCpuState state)
        {
            switch (state)
            {
            case QKBoot::SmpCpuState::Online:
                return "online";
            case QKBoot::SmpCpuState::Initializing:
                return "initializing";
            case QKBoot::SmpCpuState::Launching:
                return "launching";
            default:
                return "offline";
            }
        }

        // Updates the list in place when the row count is unchanged so the selection survives refreshes.
        class ListUpdater
        {
        public:
            explicit ListUpdater(QW::Controls::ListView *list) : m_list(list), m_row(0) {}

            void add(const char *text)
            {
                if (m_row < m_list->itemCount())
                {
                    if (QC::String::strcmp(m_list->item(m_row)->text, text) != 0)
                        m_list->setItemText(m_row, text);
                }
                else
                {
                    m_list->addItem(text);
                }
                ++m_row;
            }

            void finish()
            {
                while (m_list->itemCount() > m_row)
                    m_list->removeItem(m_list->itemCount() - 1);
                m_list->invalidate();
            }

        private:
            QW::Controls::ListView *m_list;
            QC::usize m_row;
        };
    }

    Tasks::Tasks(Desktop *desktop)
        : m_desktop(desktop),
          m_window(nullptr),
          m_cpuLabel(nullptr),
          m_memoryLabel(nullptr),
          m_heapLabel(nullptr),
          m_securityLabel(nullptr),
          m_appList(nullptr),
          m_cpuList(nullptr),
          m_lastRefreshMs(0)
    {
    }

    Tasks::~Tasks()
    {
        if (m_window && m_desktop)
            m_desktop->removeTaskbarWindow(m_window->windowId());
        AppWindow::destroy(m_window);
        m_window = nullptr;
    }

    bool Tasks::isOpen() const
    {
        return m_window && m_window->isVisible();
    }

    void Tasks::open()
    {
        if (!m_desktop)
            return;

        if (!m_window)
            createWindow();
        if (!m_window)
            return;

        m_desktop->addTaskbarWindow(m_window->windowId(), "Tasks");
        refresh();
        m_desktop->activateWindow(m_window->windowId());
    }

    void Tasks::close()
    {
        if (!m_window)
            return;
        if (m_desktop)
            m_desktop->removeTaskbarWindow(m_window->windowId());
        Desktop::hideWindow(m_window);
    }

    void Tasks::tick()
    {
        if (!isOpen())
            return;

        const QC::u64 now = QDrv::Timer::instance().milliseconds();
        if ((now - m_lastRefreshMs) < kRefreshMs)
            return;
        refresh();
    }

    void Tasks::createWindow()
    {
        m_window = AppWindow::create("Tasks", kWindowW, kWindowH, m_desktop->workArea(), &Tasks::onCloseClick, this);
        if (!m_window)
            return;

        const QC::Rect bounds = m_window->bounds();
        const QC::i32 pad = AppWindow::PAD;
        const QC::u32 contentW = bounds.width - 2 * pad;
        const QC::i32 lineH = 18;

        QC::i32 y = AppWindow::TITLE_BAR_HEIGHT + pad;
        m_cpuLabel = AppWindow::addLabel(m_window, "", {pad, y, contentW, lineH});
        y += lineH;
        m_memoryLabel = AppWindow::addLabel(m_window, "", {pad, y, contentW, lineH});
        y += lineH;
        m_heapLabel = AppWindow::addLabel(m_window, "", {pad, y, contentW, lineH});
        y += lineH;
        m_securityLabel = AppWindow::addLabel(m_window, "", {pad, y, contentW, lineH});
        y += lineH + pad;

        AppWindow::addLabel(m_window, "Applications", {pad, y + 4, 200, 16}, CitadelTheme::textDim());
        AppWindow::addButton(m_window, "Switch To", {static_cast<QC::i32>(bounds.width) - pad - 96, y, 96, 24},
                             &Tasks::onSwitchClick, this);
        y += 24 + 4;

        const QC::u32 appListH = 96;
        m_appList = AppWindow::addList(m_window, {pad, y, contentW, appListH});
        y += static_cast<QC::i32>(appListH) + pad;

        AppWindow::addLabel(m_window, "Processors", {pad, y, 200, 16}, CitadelTheme::textDim());
        y += 16 + 4;

        const QC::i32 cpuListH = static_cast<QC::i32>(bounds.height) - y - pad;
        m_cpuList = AppWindow::addList(m_window, {pad, y, contentW, static_cast<QC::u32>(cpuListH > 0 ? cpuListH : 0)});
    }

    void Tasks::refresh()
    {
        if (!m_window)
            return;

        m_lastRefreshMs = QDrv::Timer::instance().milliseconds();
        refreshSummary();
        refreshApplications();
        refreshCpus();
        m_window->invalidate();
    }

    void Tasks::refreshSummary()
    {
        char line[160];

        auto &pmm = QK::Memory::PMM::instance();
        QC::usize len = Text::append(line, sizeof(line), 0, "Memory: ");
        len = Text::appendBytes(line, sizeof(line), len, pmm.usedMemory());
        len = Text::append(line, sizeof(line), len, " used of ");
        len = Text::appendBytes(line, sizeof(line), len, pmm.totalMemory());
        len = Text::append(line, sizeof(line), len, " (");
        len = Text::appendBytes(line, sizeof(line), len, pmm.freeMemory());
        Text::append(line, sizeof(line), len, " free)");
        m_memoryLabel->setText(line);

        auto &heap = QK::Memory::Heap::instance();
        len = Text::append(line, sizeof(line), 0, "Kernel heap: ");
        len = Text::appendBytes(line, sizeof(line), len, heap.usedSize());
        len = Text::append(line, sizeof(line), len, " used of ");
        Text::appendBytes(line, sizeof(line), len, heap.totalSize());
        m_heapLabel->setText(line);

        len = Text::append(line, sizeof(line), 0, "TPM: ");
        len = Text::append(line, sizeof(line), len, QK::Boot::Tpm::IsReady() ? "present" : "not available");
        Text::append(line, sizeof(line), len, "    Secure Boot: not verified yet");
        m_securityLabel->setText(line);
    }

    void Tasks::refreshApplications()
    {
        ListUpdater rows(m_appList);
        const QC::u32 count = m_desktop->runningWindowCount();
        for (QC::u32 i = 0; i < count; ++i)
        {
            QC::u32 windowId = 0;
            const char *title = nullptr;
            if (!m_desktop->runningWindowAt(i, windowId, title))
                break;

            const QW::Window *window = QW::WindowManager::instance().windowById(windowId);
            char line[96];
            QC::usize len = Text::append(line, sizeof(line), 0, title);
            Text::append(line, sizeof(line), len, (window && window->isVisible()) ? "" : "   (minimized)");
            rows.add(line);
        }
        rows.finish();
    }

    void Tasks::refreshCpus()
    {
        auto *snapshot = new QKBoot::SmpStatusSnapshot();
        const QC::u32 taskCapacity = QK::kTaskManagerMaxTasks;
        auto *tasks = new QK::TaskInfo[taskCapacity];

        const bool haveSmp = QKBoot::getSmpStatusSnapshot(*snapshot);
        const QC::u32 taskCount = QK::TaskManager::instance().copyTaskInfo(tasks, taskCapacity);

        char line[256];
        QC::u32 onlineCpus = 0;
        QC::u32 totalCpus = 1;
        ListUpdater rows(m_cpuList);

        if (haveSmp && snapshot->slotCount > 0)
        {
            totalCpus = static_cast<QC::u32>(snapshot->totalCpus);
            for (QC::usize slot = 0; slot < snapshot->slotCount && slot < QKBoot::kSmpStatusMaxEntries && slot < kMaxCpuRows; ++slot)
            {
                const QKBoot::SmpCpuStatusEntry &cpu = snapshot->entries[slot];
                const bool online = cpu.online || cpu.state == QKBoot::SmpCpuState::Online;
                if (online)
                    ++onlineCpus;

                QC::u32 active = 0;
                for (QC::u32 t = 0; t < taskCount; ++t)
                {
                    if (tasks[t].cpuIndex == cpu.cpuIndex && tasks[t].state != QK::TaskState::Terminated)
                        ++active;
                }

                QC::usize len = Text::append(line, sizeof(line), 0, "CPU");
                len = Text::appendUnsigned(line, sizeof(line), len, cpu.cpuIndex);
                len = Text::append(line, sizeof(line), len, "   ");
                len = Text::append(line, sizeof(line), len, online ? "online" : cpuStateName(cpu.state));
                len = Text::append(line, sizeof(line), len, "   tasks: ");
                len = Text::appendUnsigned(line, sizeof(line), len, active);

                const char *separator = "   [";
                bool any = false;
                for (QC::u32 t = 0; t < taskCount; ++t)
                {
                    if (tasks[t].cpuIndex != cpu.cpuIndex || tasks[t].state == QK::TaskState::Terminated)
                        continue;
                    len = Text::append(line, sizeof(line), len, separator);
                    len = Text::append(line, sizeof(line), len, tasks[t].name[0] ? tasks[t].name : "task");
                    separator = ", ";
                    any = true;
                }
                if (any)
                    Text::append(line, sizeof(line), len, "]");

                rows.add(line);
            }
        }
        else
        {
            onlineCpus = 1;
            QC::u32 active = 0;
            for (QC::u32 t = 0; t < taskCount; ++t)
            {
                if (tasks[t].state != QK::TaskState::Terminated)
                    ++active;
            }
            QC::usize len = Text::append(line, sizeof(line), 0, "CPU0   online   tasks: ");
            Text::appendUnsigned(line, sizeof(line), len, active);
            rows.add(line);
        }
        rows.finish();

        QC::usize len = Text::append(line, sizeof(line), 0, "Processors: ");
        len = Text::appendUnsigned(line, sizeof(line), len, onlineCpus);
        len = Text::append(line, sizeof(line), len, " of ");
        len = Text::appendUnsigned(line, sizeof(line), len, totalCpus);
        Text::append(line, sizeof(line), len, " online");
        m_cpuLabel->setText(line);

        delete[] tasks;
        delete snapshot;
    }

    void Tasks::switchToSelected()
    {
        if (!m_appList)
            return;

        const QC::isize index = m_appList->selectedIndex();
        QC::u32 windowId = 0;
        const char *title = nullptr;
        if (index < 0 || !m_desktop->runningWindowAt(static_cast<QC::u32>(index), windowId, title))
            return;

        m_desktop->activateWindow(windowId);
    }

    void Tasks::onCloseClick(QW::Controls::Button *, void *userData)
    {
        if (auto *self = static_cast<Tasks *>(userData))
            self->close();
    }

    void Tasks::onSwitchClick(QW::Controls::Button *, void *userData)
    {
        if (auto *self = static_cast<Tasks *>(userData))
            self->switchToSelected();
    }

} // namespace QD
