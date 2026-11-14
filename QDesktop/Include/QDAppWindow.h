#pragma once

// QDesktop application window helpers for the built-in Citadel apps
// Namespace: QD::AppWindow

#include "QCTypes.h"
#include "QDCitadelTheme.h"
#include "QWWindow.h"
#include "QWWindowManager.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Composite/ListView.h"

namespace QD::AppWindow
{
    constexpr QC::i32 TITLE_BAR_HEIGHT = 24;
    constexpr QC::i32 PAD = 8;

    /// Creates a movable window centered in the work area with a Citadel title bar and close button.
    inline QW::Window *create(const char *title, QC::u32 width, QC::u32 height, const QC::Rect &workArea,
                              QW::Controls::ButtonClickHandler onClose, void *userData)
    {
        if (width > workArea.width)
            width = workArea.width;
        if (height > workArea.height)
            height = workArea.height;
        const QC::i32 x = workArea.x + static_cast<QC::i32>((workArea.width - width) / 2);
        const QC::i32 y = workArea.y + static_cast<QC::i32>((workArea.height - height) / 2);

        QW::Window *window = QW::WindowManager::instance().createWindow(title, {x, y, width, height});
        if (!window)
            return nullptr;

        window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle |
                         QW::WindowFlags::HasBorder);

        auto *root = window->root();
        root->setBorderStyle(QW::Controls::BorderStyle::None);
        root->setPadding(0);
        root->setBackgroundColor(CitadelTheme::surface());

        auto *titleBar = new QW::Controls::Panel(window, {0, 0, width, static_cast<QC::u32>(TITLE_BAR_HEIGHT)});
        titleBar->setBorderStyle(QW::Controls::BorderStyle::None);
        titleBar->setPadding(0);
        titleBar->setBackgroundColor(CitadelTheme::taskbar());
        root->addChild(titleBar);

        auto *titleLabel = new QW::Controls::Label(window, title, {PAD, 4, width - 64, 16});
        titleLabel->setTextColor(CitadelTheme::text());
        titleLabel->setBackgroundColor(QC::Color(0, 0, 0, 0));
        titleBar->addChild(titleLabel);

        auto *closeButton = new QW::Controls::Button(window, "X", {static_cast<QC::i32>(width) - PAD - 20, 2, 20, 20});
        closeButton->setContentMode(QW::ButtonContentMode::Text);
        closeButton->setVariant(QW::ButtonVariant::Compact);
        closeButton->setRole(QW::ButtonRole::Destructive);
        closeButton->setClickHandler(onClose, userData);
        titleBar->addChild(closeButton);

        return window;
    }

    /// Deletes every control created under the window (recursively) and destroys the window.
    inline void destroy(QW::Window *window)
    {
        if (!window)
            return;

        struct Local
        {
            static void deleteChildren(QW::Controls::IControl *control)
            {
                auto *container = control ? control->asPanel() : nullptr;
                if (!container)
                    return;
                while (container->childCount() > 0)
                {
                    QW::Controls::IControl *child = container->childAt(0);
                    container->removeChildAt(0);
                    deleteChildren(child);
                    delete child;
                }
            }
        };

        Local::deleteChildren(window->root());
        QW::WindowManager::instance().destroyWindow(window);
    }

    inline QW::Controls::Label *addLabel(QW::Window *window, const char *text, const QC::Rect &bounds,
                                         QC::Color color = CitadelTheme::text())
    {
        auto *label = new QW::Controls::Label(window, text, bounds);
        label->setTextColor(color);
        label->setBackgroundColor(QC::Color(0, 0, 0, 0));
        window->root()->addChild(label);
        return label;
    }

    inline QW::Controls::Button *addButton(QW::Window *window, const char *text, const QC::Rect &bounds,
                                           QW::Controls::ButtonClickHandler handler, void *userData)
    {
        auto *button = new QW::Controls::Button(window, text, bounds);
        button->setContentMode(QW::ButtonContentMode::Text);
        button->setRole(QW::ButtonRole::Default);
        button->setClickHandler(handler, userData);
        window->root()->addChild(button);
        return button;
    }

    inline QW::Controls::ListView *addList(QW::Window *window, const QC::Rect &bounds)
    {
        auto *list = new QW::Controls::ListView(window, bounds);
        list->setShowHeader(false);
        list->setBackgroundColor(CitadelTheme::surfaceAlt());
        list->setTextColor(CitadelTheme::text());
        list->setSelectionColor(CitadelTheme::accent());
        window->root()->addChild(list);
        return list;
    }
}
