// QDesktop Shutdown Dialog - implementation
// Namespace: QD

#include "QDShutdownDialog.h"

#include "QDDesktop.h"
#include "QCString.h"
#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/Button.h"

namespace QD
{
    namespace
    {
        constexpr QC::i32 DIALOG_WIDTH = 520;
        constexpr QC::i32 DIALOG_HEIGHT = 220;
    }

    ShutdownDialog::ShutdownDialog(Desktop *desktop)
        : m_desktop(desktop),
          m_reason(QK::Shutdown::Reason::UserRequest),
          m_window(nullptr),
          m_root(nullptr),
          m_message(nullptr),
          m_confirmButton(nullptr),
          m_cancelButton(nullptr)
    {
    }

    ShutdownDialog::~ShutdownDialog()
    {
        close();
    }

    void ShutdownDialog::open(QK::Shutdown::Reason reason)
    {
        m_reason = reason;
        if (!m_window)
        {
            createWindow();
        }

        updateMessage();
        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        m_window->setVisible(true);
        QW::WindowManager::instance().render();
    }

    void ShutdownDialog::close()
    {
        if (!m_window)
            return;

        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;
        m_root = nullptr;
        m_message = nullptr;
        m_confirmButton = nullptr;
        m_cancelButton = nullptr;
    }

    void ShutdownDialog::createWindow()
    {
        if (!m_desktop)
            return;

        const QC::Rect work = m_desktop->workArea();
        QC::i32 x = work.x + static_cast<QC::i32>((work.width - DIALOG_WIDTH) / 2);
        QC::i32 y = work.y + static_cast<QC::i32>((work.height - DIALOG_HEIGHT) / 2);
        QW::Rect bounds = {x, y, static_cast<QC::u32>(DIALOG_WIDTH), static_cast<QC::u32>(DIALOG_HEIGHT)};

        m_window = QW::WindowManager::instance().createWindow("Shut Down", bounds);
        if (!m_window)
            return;

        m_window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_root = m_window->root();
        if (!m_root)
            return;

        m_root->setPadding(12);
        m_root->setBorderStyle(QW::Controls::BorderStyle::None);

        QW::Rect labelBounds = {20, 24, static_cast<QC::u32>(DIALOG_WIDTH - 40), 120};
        m_message = new QW::Controls::Label(m_window, "Preparing shutdown...", labelBounds);
        m_message->setWordWrap(true);
        m_root->addChild(m_message);

        const QC::i32 buttonWidth = 150;
        const QC::i32 buttonHeight = 32;
        const QC::i32 spacing = 24;
        const QC::i32 baseY = DIALOG_HEIGHT - buttonHeight - 32;
        QC::i32 totalWidth = buttonWidth * 2 + spacing;
        QC::i32 startX = (DIALOG_WIDTH - totalWidth) / 2;

        QW::Rect confirmBounds = {startX, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_confirmButton = new QW::Controls::Button(m_window, "Shut Down", confirmBounds);
        m_confirmButton->setClickHandler(onConfirm, this);
        m_confirmButton->setRole(QW::ButtonRole::Destructive);
        m_root->addChild(m_confirmButton);

        QW::Rect cancelBounds = {startX + buttonWidth + spacing, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_cancelButton = new QW::Controls::Button(m_window, "Cancel", cancelBounds);
        m_cancelButton->setClickHandler(onCancel, this);
        m_cancelButton->setRole(QW::ButtonRole::Default);
        m_root->addChild(m_cancelButton);
    }

    void ShutdownDialog::updateMessage()
    {
        if (!m_message)
            return;

        const char *reasonText = "User requested shutdown.";
        switch (m_reason)
        {
        case QK::Shutdown::Reason::ShellCommand:
            reasonText = "Shutdown requested from shell.";
            break;
        case QK::Shutdown::Reason::KeyboardShortcut:
            reasonText = "Ctrl+Q shortcut requested shutdown.";
            break;
        case QK::Shutdown::Reason::SidebarPowerButton:
            reasonText = "Power sidebar button pressed.";
            break;
        case QK::Shutdown::Reason::SystemPolicy:
            reasonText = "System requested shutdown.";
            break;
        default:
            break;
        }

        static char buffer[256];
        buffer[0] = '\0';

        const char *part1 = "Shutdown requested.\nReason: ";
        const char *part2 = "\nAll registered subsystems have been asked to finish their work.";
        const char *part3 = "\nSelect 'Shut Down' to power off now or 'Cancel' to stay in the system.";

        QC::usize offset = 0;
        auto append = [&](const char *text)
        {
            if (!text)
                return;
            QC::usize len = QC::String::strlen(text);
            if (offset + len >= sizeof(buffer))
                len = sizeof(buffer) - offset - 1;
            for (QC::usize i = 0; i < len; ++i)
            {
                buffer[offset++] = text[i];
            }
            buffer[offset] = '\0';
        };

        append(part1);
        append(reasonText);
        append(part2);
        append(part3);

        m_message->setText(buffer);
    }

    void ShutdownDialog::onConfirm(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *dialog = static_cast<ShutdownDialog *>(userData);
        if (!dialog)
            return;

        QK::Shutdown::Controller::instance().confirm(QK::Shutdown::UserChoice::Proceed);
        dialog->close();
    }

    void ShutdownDialog::onCancel(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *dialog = static_cast<ShutdownDialog *>(userData);
        if (!dialog)
            return;

        QK::Shutdown::Controller::instance().confirm(QK::Shutdown::UserChoice::Cancel);
        dialog->close();
    }

} // namespace QD
