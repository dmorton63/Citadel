// QDesktop Setup Wizard - implementation
// Namespace: QD

#include "QDSetupWizard.h"

#include "QDDesktop.h"

#include "QCString.h"
#include "QCLogger.h"
#include "QFSVFS.h"
#include "QFSFile.h"

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
        constexpr const char *LOG_MODULE = "QDSetupWizard";

        constexpr QC::i32 WIZARD_WIDTH = 640;
        constexpr QC::i32 WIZARD_HEIGHT = 420;

        constexpr const char *OWNER_MARKER_PATH = "/system/owner.enrolled";
        constexpr const char *OWNER_INFO_PATH = "/system/owner.info";

        static inline bool isEmpty(const char *s)
        {
            return !s || *s == '\0';
        }
    }

    SetupWizard::SetupWizard(Desktop *desktop)
        : m_desktop(desktop),
          m_window(nullptr),
          m_root(nullptr),
          m_title(nullptr),
          m_hint(nullptr),
          m_userLabel(nullptr),
          m_userBox(nullptr),
          m_pinLabel(nullptr),
          m_pinBox(nullptr),
          m_questionLabel(nullptr),
          m_questionBox(nullptr),
          m_answerLabel(nullptr),
          m_answerBox(nullptr),
          m_status(nullptr),
          m_createButton(nullptr),
          m_cancelButton(nullptr)
    {
    }

    SetupWizard::~SetupWizard()
    {
        close();
    }

    void SetupWizard::open()
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

    void SetupWizard::close()
    {
        if (!m_window)
            return;

        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;

        m_root = nullptr;
        m_title = nullptr;
        m_hint = nullptr;
        m_userLabel = nullptr;
        m_userBox = nullptr;
        m_pinLabel = nullptr;
        m_pinBox = nullptr;
        m_questionLabel = nullptr;
        m_questionBox = nullptr;
        m_answerLabel = nullptr;
        m_answerBox = nullptr;
        m_status = nullptr;
        m_createButton = nullptr;
        m_cancelButton = nullptr;
    }

    void SetupWizard::createWindow()
    {
        if (!m_desktop)
            return;

        const QC::Rect work = m_desktop->workArea();
        QC::i32 x = work.x + static_cast<QC::i32>((work.width - WIZARD_WIDTH) / 2);
        QC::i32 y = work.y + static_cast<QC::i32>((work.height - WIZARD_HEIGHT) / 2);
        QW::Rect bounds = {x, y, static_cast<QC::u32>(WIZARD_WIDTH), static_cast<QC::u32>(WIZARD_HEIGHT)};

        m_window = QW::WindowManager::instance().createWindow("Setup", bounds);
        if (!m_window)
            return;

        m_window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_root = m_window->root();
        if (!m_root)
            return;

        m_root->setPadding(14);
        m_root->setBorderStyle(QW::Controls::BorderStyle::None);

        // Title
        QW::Rect titleBounds = {18, 18, static_cast<QC::u32>(WIZARD_WIDTH - 36), 20};
        m_title = new QW::Controls::Label(m_window, "Welcome", titleBounds);
        m_root->addChild(m_title);

        // Hint
        QW::Rect hintBounds = {18, 44, static_cast<QC::u32>(WIZARD_WIDTH - 36), 44};
        m_hint = new QW::Controls::Label(m_window, "Create the Owner profile. This protects secure features and your vault.", hintBounds);
        m_hint->setWordWrap(true);
        m_root->addChild(m_hint);

        const QC::i32 leftX = 24;
        const QC::i32 labelW = 170;
        const QC::i32 boxW = WIZARD_WIDTH - leftX - labelW - 42;
        const QC::i32 rowH = 26;
        const QC::i32 gapY = 12;
        QC::i32 yRow = 110;

        auto placeRow = [&](const char *labelText, QW::Controls::Label **outLabel, QW::Controls::TextBox **outBox, const char *placeholder, bool password, QC::usize maxLen)
        {
            QW::Rect l = {leftX, yRow, static_cast<QC::u32>(labelW), static_cast<QC::u32>(rowH)};
            *outLabel = new QW::Controls::Label(m_window, labelText, l);
            m_root->addChild(*outLabel);

            QW::Rect b = {leftX + labelW, yRow, static_cast<QC::u32>(boxW), static_cast<QC::u32>(rowH)};
            *outBox = new QW::Controls::TextBox(m_window, b);
            (*outBox)->setPlaceholder(placeholder);
            (*outBox)->setPassword(password);
            if (maxLen)
                (*outBox)->setMaxLength(maxLen);
            m_root->addChild(*outBox);

            yRow += rowH + gapY;
        };

        placeRow("User name:", &m_userLabel, &m_userBox, "Owner", false, 48);
        placeRow("PIN:", &m_pinLabel, &m_pinBox, "4+ digits", true, 16);
        placeRow("Secret question:", &m_questionLabel, &m_questionBox, "Recovery question", false, 96);
        placeRow("Secret answer:", &m_answerLabel, &m_answerBox, "Recovery answer", true, 96);

        // Status
        QW::Rect statusBounds = {18, static_cast<QC::i32>(WIZARD_HEIGHT - 120), static_cast<QC::u32>(WIZARD_WIDTH - 36), 44};
        m_status = new QW::Controls::Label(m_window, "", statusBounds);
        m_status->setWordWrap(true);
        m_root->addChild(m_status);

        // Buttons
        const QC::i32 buttonWidth = 160;
        const QC::i32 buttonHeight = 32;
        const QC::i32 spacing = 18;
        const QC::i32 baseY = WIZARD_HEIGHT - buttonHeight - 24;
        const QC::i32 startX = (WIZARD_WIDTH - (buttonWidth * 2 + spacing)) / 2;

        QW::Rect createBounds = {startX, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_createButton = new QW::Controls::Button(m_window, "Create Owner", createBounds);
        m_createButton->setRole(QW::ButtonRole::Accent);
        m_createButton->setClickHandler(&SetupWizard::onCreateClick, this);
        m_root->addChild(m_createButton);

        QW::Rect cancelBounds = {startX + buttonWidth + spacing, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_cancelButton = new QW::Controls::Button(m_window, "Cancel", cancelBounds);
        m_cancelButton->setRole(QW::ButtonRole::Default);
        m_cancelButton->setClickHandler(&SetupWizard::onCancelClick, this);
        m_root->addChild(m_cancelButton);

        setStatus("Enter details and select 'Create Owner'.");
    }

    void SetupWizard::setStatus(const char *text)
    {
        if (m_status)
        {
            m_status->setText(text ? text : "");
        }
    }

    bool SetupWizard::tryWriteOwnerMarker(const char *username)
    {
        // v1 behavior: create a marker file only.
        // Real key derivation / vault enrollment will live in SC.

        QFS::VFS &vfs = QFS::VFS::instance();
        if (!vfs.exists("/system"))
        {
            (void)vfs.createDir("/system");
        }

        {
            QFS::File *f = vfs.open(OWNER_MARKER_PATH, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f)
                return false;
            const char *marker = "owner_enrolled=1\n";
            (void)f->write(marker, QC::String::strlen(marker));
            (void)vfs.close(f);
        }

        {
            QFS::File *f = vfs.open(OWNER_INFO_PATH, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f)
                return false;

            const char *prefix = "username=";
            (void)f->write(prefix, QC::String::strlen(prefix));
            if (username)
                (void)f->write(username, QC::String::strlen(username));
            const char *newline = "\n";
            (void)f->write(newline, 1);

            (void)vfs.close(f);
        }

        return true;
    }

    void SetupWizard::onCreateClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<SetupWizard *>(userData);
        if (!self)
            return;

        const char *username = self->m_userBox ? self->m_userBox->text() : nullptr;
        // NOTE: For early UI testing we accept blank values.
        // Real enrollment verification + secret handling will be done by SC.
        if (isEmpty(username))
            username = "Owner";

        const bool bypass = QK::SecurityCenter::instance().bypassEnabled();
        if (!bypass)
        {
            self->setStatus("Enrollment blocked (SC enforce not wired yet). ");
            return;
        }

        if (!self->tryWriteOwnerMarker(username))
        {
            QC_LOG_ERROR(LOG_MODULE, "Failed to write owner marker files\n");
            self->setStatus("Failed to save setup data. Check /system mount.");
            return;
        }

        self->setStatus("Owner created (SC bypass). ");
        self->close();
    }

    void SetupWizard::onCancelClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<SetupWizard *>(userData);
        if (!self)
            return;

        self->close();
    }

} // namespace QD
