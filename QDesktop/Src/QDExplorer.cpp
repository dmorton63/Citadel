// QDesktop Explorer - simple file browser window
// Namespace: QD

#include "QDExplorer.h"
#include "QDAppWindow.h"
#include "QDDesktop.h"
#include "QDTextFormat.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QFSVFS.h"
#include "QFSDirectory.h"
#include "QFSVolumeManager.h"

namespace QD
{
    namespace
    {
        constexpr QC::u32 kWindowW = 560;
        constexpr QC::u32 kWindowH = 400;
        constexpr QC::u32 kButtonW = 72;
        constexpr QC::u32 kButtonH = 24;
        constexpr QC::usize kMaxVolumes = 16;

        static bool endsWithIgnoreCase(const char *text, const char *suffix)
        {
            const QC::usize textLen = QC::String::strlen(text);
            const QC::usize suffixLen = QC::String::strlen(suffix);
            if (suffixLen > textLen)
                return false;
            const char *tail = text + (textLen - suffixLen);
            for (QC::usize i = 0; i < suffixLen; ++i)
            {
                char a = tail[i];
                char b = suffix[i];
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    return false;
            }
            return true;
        }

        static void joinPath(char *out, QC::usize cap, const char *dir, const char *name)
        {
            QC::usize len = Text::append(out, cap, 0, dir);
            if (len == 0 || out[len - 1] != '/')
                len = Text::append(out, cap, len, "/");
            Text::append(out, cap, len, name);
        }
    }

    Explorer::Explorer(Desktop *desktop)
        : m_desktop(desktop),
          m_window(nullptr),
          m_pathLabel(nullptr),
          m_list(nullptr),
          m_statusLabel(nullptr),
          m_path{},
          m_entries(new Entry[MAX_ENTRIES]),
          m_entryCount(0)
    {
        Text::append(m_path, sizeof(m_path), 0, "/");
    }

    Explorer::~Explorer()
    {
        if (m_window && m_desktop)
            m_desktop->removeTaskbarWindow(m_window->windowId());
        AppWindow::destroy(m_window);
        m_window = nullptr;
        delete[] m_entries;
        m_entries = nullptr;
    }

    bool Explorer::isOpen() const
    {
        return m_window && m_window->isVisible();
    }

    void Explorer::open()
    {
        if (!m_desktop)
            return;

        if (!m_window)
            createWindow();
        if (!m_window)
            return;

        refresh();
        m_desktop->addTaskbarWindow(m_window->windowId(), "Explorer");
        m_desktop->activateWindow(m_window->windowId());
    }

    void Explorer::close()
    {
        if (!m_window)
            return;
        if (m_desktop)
            m_desktop->removeTaskbarWindow(m_window->windowId());
        Desktop::hideWindow(m_window);
    }

    void Explorer::createWindow()
    {
        m_window = AppWindow::create("Explorer", kWindowW, kWindowH, m_desktop->workArea(), &Explorer::onCloseClick, this);
        if (!m_window)
            return;

        const QC::Rect bounds = m_window->bounds();
        const QC::i32 pad = AppWindow::PAD;
        const QC::i32 toolbarY = AppWindow::TITLE_BAR_HEIGHT + pad;
        const QC::u32 contentW = bounds.width - 2 * pad;

        QC::i32 x = pad;
        AppWindow::addButton(m_window, "Up", {x, toolbarY, kButtonW, kButtonH}, &Explorer::onUpClick, this);
        x += static_cast<QC::i32>(kButtonW) + 4;
        AppWindow::addButton(m_window, "Open", {x, toolbarY, kButtonW, kButtonH}, &Explorer::onOpenClick, this);
        x += static_cast<QC::i32>(kButtonW) + 4;
        AppWindow::addButton(m_window, "Refresh", {x, toolbarY, kButtonW, kButtonH}, &Explorer::onRefreshClick, this);
        x += static_cast<QC::i32>(kButtonW) + 8;

        m_pathLabel = AppWindow::addLabel(m_window, m_path,
                                          {x, toolbarY + 4, bounds.width - static_cast<QC::u32>(x) - pad, 16});

        const QC::i32 listY = toolbarY + static_cast<QC::i32>(kButtonH) + pad;
        const QC::i32 statusH = 18;
        const QC::i32 listH = static_cast<QC::i32>(bounds.height) - listY - statusH - 2 * pad;
        m_list = AppWindow::addList(m_window, {pad, listY, contentW, static_cast<QC::u32>(listH > 0 ? listH : 0)});

        m_statusLabel = AppWindow::addLabel(m_window, "",
                                            {pad, listY + listH + pad, contentW, static_cast<QC::u32>(statusH)},
                                            CitadelTheme::textDim());
    }

    void Explorer::setStatus(const char *text)
    {
        if (m_statusLabel)
        {
            m_statusLabel->setText(text);
            m_statusLabel->invalidate();
        }
    }

    void Explorer::navigate(const char *path)
    {
        Text::append(m_path, sizeof(m_path), 0, path && path[0] ? path : "/");
        refresh();
    }

    void Explorer::refresh()
    {
        if (!m_list)
            return;

        m_list->clearItems();
        m_list->setScrollOffset(0);
        m_entryCount = 0;

        if (m_pathLabel)
            m_pathLabel->setText(m_path);

        if (QC::String::strcmp(m_path, "/") == 0)
            listVolumes();
        else
            listDirectory();

        if (m_window)
            m_window->invalidate();
    }

    void Explorer::listVolumes()
    {
        auto *volumes = new QFS::VolumeInfo[kMaxVolumes];
        const QC::usize count = QFS::VolumeManager::instance().copyVolumeInfo(volumes, kMaxVolumes);

        QC::u32 mounted = 0;
        for (QC::usize i = 0; i < count && i < kMaxVolumes && m_entryCount < MAX_ENTRIES; ++i)
        {
            const QFS::VolumeInfo &info = volumes[i];
            if (!info.mounted || !info.mountPath[0])
                continue;

            Entry &entry = m_entries[m_entryCount++];
            Text::append(entry.name, sizeof(entry.name), 0, info.name);
            Text::append(entry.target, sizeof(entry.target), 0, info.mountPath);
            entry.isDirectory = true;

            char line[256];
            QC::usize len = Text::append(line, sizeof(line), 0, "[VOL]  ");
            len = Text::append(line, sizeof(line), len, info.mountPath);
            len = Text::append(line, sizeof(line), len, "   ");
            len = Text::append(line, sizeof(line), len, info.name);
            Text::append(line, sizeof(line), len, info.persistent ? "  (persistent)" : "  (volatile)");
            m_list->addItem(line);
            ++mounted;
        }
        delete[] volumes;

        char status[64];
        QC::usize len = Text::appendUnsigned(status, sizeof(status), 0, mounted);
        Text::append(status, sizeof(status), len, mounted == 1 ? " volume mounted" : " volumes mounted");
        setStatus(status);
    }

    void Explorer::listDirectory()
    {
        auto &vfs = QFS::VFS::instance();
        QFS::Directory *dir = vfs.openDir(m_path);
        if (!dir)
        {
            setStatus("Cannot open this folder.");
            return;
        }

        QC::u32 folders = 0;
        QC::u32 files = 0;
        bool truncated = false;
        QFS::DirEntry dirEntry{};
        // Two passes keep folders listed before files.
        for (int pass = 0; pass < 2; ++pass)
        {
            if (pass == 1)
            {
                vfs.closeDir(dir);
                dir = vfs.openDir(m_path);
                if (!dir)
                    break;
            }

            while (dir->read(&dirEntry))
            {
                if (dirEntry.name[0] == '\0' || QC::String::strcmp(dirEntry.name, ".") == 0 ||
                    QC::String::strcmp(dirEntry.name, "..") == 0)
                    continue;

                const bool isDirectory = dirEntry.type == QFS::FileType::Directory;
                if (isDirectory != (pass == 0))
                    continue;

                if (m_entryCount >= MAX_ENTRIES)
                {
                    truncated = true;
                    continue;
                }

                Entry &entry = m_entries[m_entryCount++];
                Text::append(entry.name, sizeof(entry.name), 0, dirEntry.name);
                joinPath(entry.target, sizeof(entry.target), m_path, dirEntry.name);
                entry.isDirectory = isDirectory;

                char line[256];
                QC::usize len = Text::append(line, sizeof(line), 0, isDirectory ? "[DIR]  " : "       ");
                len = Text::append(line, sizeof(line), len, dirEntry.name);
                if (!isDirectory)
                {
                    len = Text::append(line, sizeof(line), len, "   ");
                    Text::appendBytes(line, sizeof(line), len, dirEntry.size);
                    ++files;
                }
                else
                {
                    ++folders;
                }
                m_list->addItem(line);
            }
        }
        if (dir)
            vfs.closeDir(dir);

        char status[96];
        QC::usize len = Text::appendUnsigned(status, sizeof(status), 0, folders);
        len = Text::append(status, sizeof(status), len, folders == 1 ? " folder, " : " folders, ");
        len = Text::appendUnsigned(status, sizeof(status), len, files);
        len = Text::append(status, sizeof(status), len, files == 1 ? " file" : " files");
        if (truncated)
            Text::append(status, sizeof(status), len, " (list truncated)");
        setStatus(status);
    }

    void Explorer::goUp()
    {
        if (QC::String::strcmp(m_path, "/") == 0)
            return;

        // Volume roots go back to the volume list.
        QC::usize len = QC::String::strlen(m_path);
        while (len > 1 && m_path[len - 1] == '/')
            m_path[--len] = '\0';

        QC::usize cut = len;
        while (cut > 0 && m_path[cut - 1] != '/')
            --cut;
        if (cut <= 1)
        {
            navigate("/");
            return;
        }

        m_path[cut - 1] = '\0';

        // If the parent is not itself listable (e.g. "/system" parent "/"), fall back to the volume list.
        QFS::Directory *dir = QFS::VFS::instance().openDir(m_path);
        if (!dir)
        {
            navigate("/");
            return;
        }
        QFS::VFS::instance().closeDir(dir);
        refresh();
    }

    void Explorer::openSelected()
    {
        if (!m_list)
            return;

        const QC::isize index = m_list->selectedIndex();
        if (index < 0 || static_cast<QC::u32>(index) >= m_entryCount)
        {
            setStatus("Select an item first.");
            return;
        }

        const Entry &entry = m_entries[index];
        if (entry.isDirectory)
        {
            char target[PATH_MAX];
            Text::append(target, sizeof(target), 0, entry.target);
            navigate(target);
            return;
        }

        openFile(entry.target);
    }

    void Explorer::openFile(const char *path)
    {
        if (endsWithIgnoreCase(path, ".htm") || endsWithIgnoreCase(path, ".html"))
        {
            m_desktop->openBrowserFile(path);
            setStatus("Opened in HTML Viewer.");
        }
        else if (endsWithIgnoreCase(path, ".cuiml") || endsWithIgnoreCase(path, ".cml"))
        {
            m_desktop->openCuiMLFile(path);
            setStatus("Opened in CuiML Viewer.");
        }
        else
        {
            setStatus("No viewer for this file type. Use the Terminal to inspect it.");
        }
    }

    void Explorer::onCloseClick(QW::Controls::Button *, void *userData)
    {
        if (auto *self = static_cast<Explorer *>(userData))
            self->close();
    }

    void Explorer::onUpClick(QW::Controls::Button *, void *userData)
    {
        if (auto *self = static_cast<Explorer *>(userData))
            self->goUp();
    }

    void Explorer::onOpenClick(QW::Controls::Button *, void *userData)
    {
        if (auto *self = static_cast<Explorer *>(userData))
            self->openSelected();
    }

    void Explorer::onRefreshClick(QW::Controls::Button *, void *userData)
    {
        if (auto *self = static_cast<Explorer *>(userData))
            self->refresh();
    }

} // namespace QD
