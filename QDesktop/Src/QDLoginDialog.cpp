// QDesktop Login Dialog - implementation
// Namespace: QD

#include "QDLoginDialog.h"

#include "QDDesktop.h"

#include "QCString.h"
#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/TextBox.h"
#include "QWControls/Leaf/Button.h"
#include "QKSecurityCenter.h"

namespace QD
{
    namespace
    {
        constexpr QC::i32 DIALOG_WIDTH = 440;
        constexpr QC::i32 DIALOG_HEIGHT = 240;

        static inline bool isEmpty(const char *s)
        {
            return !s || *s == '\0';
        }
    }

    LoginDialog::LoginDialog(Desktop *desktop)
        : m_desktop(desktop),
          m_window(nullptr),
          m_root(nullptr),
          m_title(nullptr),
          m_hint(nullptr),
          m_pinLabel(nullptr),
          m_pinBox(nullptr),
          m_status(nullptr),
          m_unlockButton(nullptr),
          m_cancelButton(nullptr)
    {
    }

    LoginDialog::~LoginDialog()
    {
        close();
    }

    void LoginDialog::open()
    {
        if (!m_window)
        {
            createWindow();
        }

        if (!m_window)
            return;

        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        m_window->setVisible(true);
        QW::WindowManager::instance().render();
    }

    void LoginDialog::close()
    {
        if (!m_window)
            return;

        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;

        m_root = nullptr;
        m_title = nullptr;
        m_hint = nullptr;
        m_pinLabel = nullptr;
        m_pinBox = nullptr;
        m_status = nullptr;
        m_unlockButton = nullptr;
        m_cancelButton = nullptr;
    }

    void LoginDialog::createWindow()
    {
        if (!m_desktop)
            return;

        const QC::Rect work = m_desktop->workArea();
        QC::i32 x = work.x + static_cast<QC::i32>((work.width - DIALOG_WIDTH) / 2);
        QC::i32 y = work.y + static_cast<QC::i32>((work.height - DIALOG_HEIGHT) / 2);
        QW::Rect bounds = {x, y, static_cast<QC::u32>(DIALOG_WIDTH), static_cast<QC::u32>(DIALOG_HEIGHT)};

        m_window = QW::WindowManager::instance().createWindow("Login", bounds);
        if (!m_window)
            return;

        m_window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_root = m_window->root();
        if (!m_root)
            return;

        m_root->setPadding(14);
        m_root->setBorderStyle(QW::Controls::BorderStyle::None);

        QW::Rect titleBounds = {18, 18, static_cast<QC::u32>(DIALOG_WIDTH - 36), 20};
        m_title = new QW::Controls::Label(m_window, "Unlock", titleBounds);
        m_root->addChild(m_title);

        QW::Rect hintBounds = {18, 44, static_cast<QC::u32>(DIALOG_WIDTH - 36), 40};
        m_hint = new QW::Controls::Label(m_window, "Enter your PIN to unlock secure features.", hintBounds);
        m_hint->setWordWrap(true);
        m_root->addChild(m_hint);

        QW::Rect pinLabelBounds = {24, 108, 120, 24};
        m_pinLabel = new QW::Controls::Label(m_window, "PIN:", pinLabelBounds);
        m_root->addChild(m_pinLabel);

        QW::Rect pinBoxBounds = {24 + 120, 108, static_cast<QC::u32>(DIALOG_WIDTH - 24 - 120 - 24), 24};
        m_pinBox = new QW::Controls::TextBox(m_window, pinBoxBounds);
        m_pinBox->setPlaceholder("PIN");
        m_pinBox->setPassword(true);
        m_pinBox->setMaxLength(16);
        m_root->addChild(m_pinBox);

        QW::Rect statusBounds = {18, 140, static_cast<QC::u32>(DIALOG_WIDTH - 36), 30};
        m_status = new QW::Controls::Label(m_window, "", statusBounds);
        m_root->addChild(m_status);

        const QC::i32 buttonWidth = 140;
        const QC::i32 buttonHeight = 32;
        const QC::i32 spacing = 14;
        const QC::i32 baseY = DIALOG_HEIGHT - buttonHeight - 22;
        const QC::i32 startX = (DIALOG_WIDTH - (buttonWidth * 2 + spacing)) / 2;

        QW::Rect unlockBounds = {startX, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_unlockButton = new QW::Controls::Button(m_window, "Unlock", unlockBounds);
        m_unlockButton->setContentMode(QW::ButtonContentMode::Text);
        m_unlockButton->setRole(QW::ButtonRole::Accent);
        m_unlockButton->setClickHandler(&LoginDialog::onUnlockClick, this);
        m_root->addChild(m_unlockButton);

        QW::Rect cancelBounds = {startX + buttonWidth + spacing, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_cancelButton = new QW::Controls::Button(m_window, "Cancel", cancelBounds);
        m_cancelButton->setContentMode(QW::ButtonContentMode::Text);
        m_cancelButton->setRole(QW::ButtonRole::Default);
        m_cancelButton->setClickHandler(&LoginDialog::onCancelClick, this);
        m_root->addChild(m_cancelButton);

        setStatus("Enter PIN and select 'Unlock'.");
    }

    void LoginDialog::setStatus(const char *text)
    {
        if (m_status)
            m_status->setText(text ? text : "");
    }

    void LoginDialog::onUnlockClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<LoginDialog *>(userData);
        if (!self)
            return;

        const bool bypass = QK::SecurityCenter::instance().bypassEnabled();

        // NOTE: For early UI testing we accept blank PIN.
        const char *pin = self->m_pinBox ? self->m_pinBox->text() : nullptr;
        if (!pin)
            pin = "";

        if (bypass)
        {
            self->setStatus("Unlocked (SC bypass). ");
            self->close();
            return;
        }

        // v1: username is fixed to Owner until multi-user exists.
        const QC::Status st = QK::SecurityCenter::instance().ownerUnlock("Owner", pin);
        if (st == QC::Status::Success)
        {
            self->setStatus("Unlocked.");
            self->close();
            return;
        }

        char msg[128];
        QC::String::memset(msg, 0, sizeof(msg));
        QC::String::strncpy(msg, "Denied.", sizeof(msg) - 1);
        const QC::u32 backoff = QK::SecurityCenter::instance().ownerUnlockBackoffMs();
        if (backoff)
        {
            const QC::usize used0 = QC::String::strlen(msg);
            QC::String::strncpy(msg + used0, " Backoff ", sizeof(msg) - 1 - used0);

            char num[16];
            QC::String::memset(num, 0, sizeof(num));
            QC::u32 v = backoff;
            char tmp[16];
            QC::String::memset(tmp, 0, sizeof(tmp));
            QC::usize n = 0;
            do
            {
                tmp[n++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            } while (v && n < sizeof(tmp));
            for (QC::usize i = 0; i < n && i < sizeof(num) - 1; ++i)
                num[i] = tmp[n - 1 - i];

            const QC::usize used1 = QC::String::strlen(msg);
            QC::String::strncpy(msg + used1, num, sizeof(msg) - 1 - used1);
            const QC::usize used2 = QC::String::strlen(msg);
            QC::String::strncpy(msg + used2, "ms.", sizeof(msg) - 1 - used2);
        }
        self->setStatus(msg);
    }

    void LoginDialog::onCancelClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<LoginDialog *>(userData);
        if (!self)
            return;

        self->close();
    }

} // namespace QD
