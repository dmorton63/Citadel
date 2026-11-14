#pragma once

// QDesktop Explorer - simple file browser window
// Namespace: QD

#include "QCTypes.h"
#include "QWWindow.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Composite/ListView.h"

namespace QD
{
    class Desktop;

    /// Explorer - lists mounted volumes at "/" and directory contents below them.
    /// The window is persistent: closing hides it so it can be reopened cheaply.
    class Explorer
    {
    public:
        explicit Explorer(Desktop *desktop);
        ~Explorer();

        void open();
        void close();
        bool isOpen() const;

    private:
        static constexpr QC::u32 MAX_ENTRIES = 256;
        static constexpr QC::u32 PATH_MAX = 256;

        struct Entry
        {
            char name[128];
            char target[PATH_MAX];
            bool isDirectory;
        };

        void createWindow();
        void navigate(const char *path);
        void refresh();
        void listVolumes();
        void listDirectory();
        void goUp();
        void openSelected();
        void openFile(const char *path);
        void setStatus(const char *text);

        static void onCloseClick(QW::Controls::Button *button, void *userData);
        static void onUpClick(QW::Controls::Button *button, void *userData);
        static void onOpenClick(QW::Controls::Button *button, void *userData);
        static void onRefreshClick(QW::Controls::Button *button, void *userData);

        Desktop *m_desktop;
        QW::Window *m_window;
        QW::Controls::Label *m_pathLabel;
        QW::Controls::ListView *m_list;
        QW::Controls::Label *m_statusLabel;

        char m_path[PATH_MAX];
        Entry *m_entries;
        QC::u32 m_entryCount;
    };

} // namespace QD
