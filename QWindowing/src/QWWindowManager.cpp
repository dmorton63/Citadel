// QWindowing Window Manager - Desktop window management implementation
// Namespace: QW

#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QWFramebuffer.h"
#include "QWCompositor.h"
#include "QWStyleSystem.h"
#include "QWMessageBus.h"
#include "QKEventManager.h"
#include "QKMemHeap.h"
#include "QCMemUtil.h"
#include "QCLinearAlgebra.h"
#include "QDrvTimer.h"

namespace QW
{

    static QC::u32 mouseButtonToMask(QK::Event::MouseButton button)
    {
        using namespace QK::Event;
        if (button == MouseButton::None)
            return 0;
        const QC::u8 b = static_cast<QC::u8>(button);
        if (b == 0)
            return 0;
        // MouseButton values are 1..N.
        return (1u << (b - 1));
    }
    WindowManager &WindowManager::instance()
    {
        static WindowManager s_instance;
        return s_instance;
    }

    WindowManager::WindowManager()
        : m_nextWindowId(1),
          m_focusedWindow(nullptr),
          m_hoveredWindow(nullptr),
          m_framebuffer(nullptr),
          m_compositor(nullptr),
          m_mousePos{0, 0},
            m_lastInputTimestamp(0),
            m_lastInputMs(0),
          m_listenerId(QK::Event::InvalidListenerId),
          m_dragWindow(nullptr),
          m_dragOffset{0, 0},
          m_dragStartBounds{0, 0, 0, 0}
    {
    }

    WindowManager::~WindowManager()
    {
        shutdown();
    }

    void WindowManager::initialize(Framebuffer *fb)
    {
        m_framebuffer = fb;

        MessageBus::instance().initialize();

        // Create compositor
        m_compositor = new Compositor(fb);
        m_compositor->initialize();

        auto &styleSystem = StyleSystem::instance();
        styleSystem.initialize();
        styleSystem.addListener(this);

        // Register as event listener for input events
        auto &eventMgr = QK::Event::EventManager::instance();
        QK::Event::EventListener listener;
        listener.categoryMask = QK::Event::Category::Input;
        listener.handler = [](const QK::Event::Event &event, void *userData) -> bool
        {
            auto *wm = static_cast<WindowManager *>(userData);
            return wm->onEvent(event);
        };
        listener.userData = this;

        m_listenerId = eventMgr.addListener(listener);
    }

    void WindowManager::shutdown()
    {
        StyleSystem::instance().removeListener(this);

        // Unregister from event manager
        if (m_listenerId != QK::Event::InvalidListenerId)
        {
            QK::Event::EventManager::instance().removeListener(m_listenerId);
            m_listenerId = QK::Event::InvalidListenerId;
        }

        // Destroy all windows
        for (QC::usize i = 0; i < m_windows.size(); ++i)
        {
            delete m_windows[i];
        }
        m_windows.clear();

        // Destroy compositor
        if (m_compositor)
        {
            delete m_compositor;
            m_compositor = nullptr;
        }

        m_focusedWindow = nullptr;
        m_hoveredWindow = nullptr;
    }

    bool WindowManager::onEvent(const QK::Event::Event &event)
    {
        using namespace QK::Event;

        if (!event.isInput())
        {
            return false;
        }

        switch (event.type())
        {
        case Type::MouseMove:
        case Type::MouseButtonDown:
        case Type::MouseButtonUp:
        case Type::MouseScroll:
            routeMouseEvent(event.asMouse());
            // Don't consume - let other listeners see the event
            return false;

        case Type::KeyDown:
        case Type::KeyUp:
        case Type::KeyPress:
            routeKeyEvent(event.asKey());
            // Don't consume - let other listeners see the event
            return false;

        default:
            break;
        }

        return false;
    }

    QK::Event::Category WindowManager::getEventMask() const
    {
        return QK::Event::Category::Input;
    }

    Window *WindowManager::createWindow(const char *title, Rect bounds)
    {
        Window *window = new Window(title, bounds);
        // Start windows hidden; callers typically set flags (including Visible)
        // after populating controls. This also avoids synchronous paints during
        // desktop initialization.
        window->setVisible(false);
        window->setWindowId(m_nextWindowId++);
        m_windows.push_back(window);

        applyStyleToWindow(window, StyleSystem::instance().currentStyle());

        // Post window create event
        postWindowEvent(QK::Event::Type::WindowCreate, window);

        return window;
    }

    void WindowManager::destroyWindow(Window *window)
    {
        if (!window)
            return;

        // Capture bounds before removal so we can repaint the region that was covered.
        const Rect oldBounds = window->bounds();

        // If called from within a window's event handler, defer deletion until after
        // dispatch returns to avoid deleting 'this' while executing.
        if (m_dispatchDepth > 0)
        {
            // Avoid double-queue.
            for (QC::usize i = 0; i < m_pendingDestroy.size(); ++i)
            {
                if (m_pendingDestroy[i].window == window)
                {
                    return;
                }
            }

            // Post window destroy event immediately.
            postWindowEvent(QK::Event::Type::WindowDestroy, window);

            // Clear focus/hover references.
            if (m_focusedWindow == window)
                m_focusedWindow = nullptr;
            if (m_hoveredWindow == window)
                m_hoveredWindow = nullptr;
            if (m_dragWindow == window)
                m_dragWindow = nullptr;
            if (m_captureWindow == window)
            {
                m_captureWindow = nullptr;
                m_captureButtonsMask = 0;
            }

            // Remove from z-order list so it won't receive more events.
            for (QC::usize i = 0; i < m_windows.size(); ++i)
            {
                if (m_windows[i] == window)
                {
                    for (QC::usize j = i; j < m_windows.size() - 1; ++j)
                        m_windows[j] = m_windows[j + 1];
                    m_windows.pop_back();
                    break;
                }
            }

            window->setVisible(false);
            m_pendingDestroy.push_back(PendingDestroy{window, oldBounds});
            invalidate(oldBounds);
            return;
        }

        // Post window destroy event
        postWindowEvent(QK::Event::Type::WindowDestroy, window);

        // Clear focus if this window was focused
        if (m_focusedWindow == window)
        {
            m_focusedWindow = nullptr;
        }
        if (m_hoveredWindow == window)
        {
            m_hoveredWindow = nullptr;
        }

        if (m_dragWindow == window)
            m_dragWindow = nullptr;
        if (m_captureWindow == window)
        {
            m_captureWindow = nullptr;
            m_captureButtonsMask = 0;
        }

        // Remove from list
        for (QC::usize i = 0; i < m_windows.size(); ++i)
        {
            if (m_windows[i] == window)
            {
                // Shift elements left to fill the gap
                for (QC::usize j = i; j < m_windows.size() - 1; ++j)
                {
                    m_windows[j] = m_windows[j + 1];
                }
                m_windows.pop_back();
                break;
            }
        }

        delete window;

        // Force the compositor to redraw what was underneath.
        invalidate(oldBounds);
    }

    void WindowManager::processPendingDestroy()
    {
        if (m_dispatchDepth != 0 || m_pendingDestroy.empty())
            return;

        for (QC::usize i = 0; i < m_pendingDestroy.size(); ++i)
        {
            delete m_pendingDestroy[i].window;
        }
        m_pendingDestroy.clear();
    }

    Window *WindowManager::windowById(QC::u32 id) const
    {
        for (QC::usize i = 0; i < m_windows.size(); ++i)
        {
            if (m_windows[i]->windowId() == id)
            {
                return m_windows[i];
            }
        }
        return nullptr;
    }

    void WindowManager::setFocus(Window *window)
    {
        if (window && (window->flags() & WindowFlags::NoFocus) != 0)
        {
            window = nullptr;
        }

        if (m_focusedWindow == window)
            return;

        Window *oldFocus = m_focusedWindow;
        m_focusedWindow = window;

        // Post blur event to old window
        if (oldFocus)
        {
            postWindowEvent(QK::Event::Type::WindowBlur, oldFocus);
        }

        // Post focus event to new window
        if (window)
        {
            postWindowEvent(QK::Event::Type::WindowFocus, window);
        }
    }

    void WindowManager::bringToFront(Window *window)
    {
        if (!window)
            return;

        // Desktop/background surfaces are pinned to the bottom.
        if ((window->flags() & WindowFlags::AlwaysBottom) != 0)
            return;

        for (QC::usize i = 0; i < m_windows.size(); ++i)
        {
            if (m_windows[i] == window)
            {
                // Move to end (top of z-order)
                // Shift elements left to fill the gap
                for (QC::usize j = i; j < m_windows.size() - 1; ++j)
                {
                    m_windows[j] = m_windows[j + 1];
                }
                m_windows[m_windows.size() - 1] = window;
                break;
            }
        }

        setFocus(window);
        invalidate(window->bounds());
    }

    void WindowManager::sendToBack(Window *window)
    {
        if (!window)
            return;

        // Find current index.
        QC::usize fromIndex = m_windows.size();
        for (QC::usize i = 0; i < m_windows.size(); ++i)
        {
            if (m_windows[i] == window)
            {
                fromIndex = i;
                break;
            }
        }
        if (fromIndex == m_windows.size())
            return;

        // Compute target index.
        QC::usize toIndex = 0;
        if ((window->flags() & WindowFlags::AlwaysBottom) == 0)
        {
            // Non-bottom windows may not move below any AlwaysBottom windows.
            // Place them just above the AlwaysBottom segment.
            while (toIndex < m_windows.size())
            {
                Window *w = m_windows[toIndex];
                if (!w || (w->flags() & WindowFlags::AlwaysBottom) == 0)
                    break;
                ++toIndex;
            }
        }
        // else: AlwaysBottom windows go to true bottom (index 0).

        if (fromIndex == toIndex)
            return;

        // Remove window from current position.
        for (QC::usize j = fromIndex; j + 1 < m_windows.size(); ++j)
            m_windows[j] = m_windows[j + 1];
        m_windows.pop_back();

        // If we removed an element before the insertion point, adjust.
        if (fromIndex < toIndex)
            --toIndex;

        // Insert window at target index.
        m_windows.push_back(window);
        for (QC::usize j = m_windows.size() - 1; j > toIndex; --j)
            m_windows[j] = m_windows[j - 1];
        m_windows[toIndex] = window;

        invalidate(window->bounds());
    }

    void WindowManager::invalidate(const Rect &rect)
    {
        if (m_compositor)
        {
            m_compositor->invalidate(rect);
            m_needsRender = true;
        }
    }

    void WindowManager::render()
    {
        if (m_compositor)
        {
            // Paint dirty windows once per frame (coalesced).
            for (QC::usize i = 0; i < m_windows.size(); ++i)
            {
                Window *window = m_windows[i];
                if (window && window->isVisible() && window->needsPaint())
                {
                    window->paintIfNeeded();
                }
            }

            m_compositor->compose();
            m_needsRender = false;
        }
    }

    Size WindowManager::screenSize() const
    {
        if (m_framebuffer)
        {
            return Size{m_framebuffer->width(), m_framebuffer->height()};
        }
        return Size{0, 0};
    }

    Window *WindowManager::windowAt(Point p)
    {
        // Search from top to bottom (end to beginning)
        for (QC::isize i = static_cast<QC::isize>(m_windows.size()) - 1; i >= 0; --i)
        {
            Window *window = m_windows[static_cast<QC::usize>(i)];
            if (window->isVisible() && window->bounds().contains(p))
            {
                return window;
            }
        }
        return nullptr;
    }

    void WindowManager::routeMouseEvent(const QK::Event::MouseEventData &mouse)
    {
        using namespace QK::Event;

        // Capture timing for UI instrumentation/debugging.
        m_lastInputTimestamp = mouse.timestamp;
        m_lastInputMs = QDrv::Timer::instance().milliseconds();

        // The kernel input layer posts mouse events with a meaningful absolute cursor position
        // (clamped to screen bounds by the active mouse driver). Always trust x/y here to keep
        // cursor rendering and control hit-testing perfectly aligned.
        const Size scr = screenSize();
        const QC::i32 maxX = (scr.width > 0) ? (static_cast<QC::i32>(scr.width) - 1) : 0;
        const QC::i32 maxY = (scr.height > 0) ? (static_cast<QC::i32>(scr.height) - 1) : 0;

        QC::i32 x = mouse.x;
        QC::i32 y = mouse.y;
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x > maxX)
            x = maxX;
        if (y > maxY)
            y = maxY;

        m_mousePos = Point{x, y};

        // Keep hardware cursor position tightly synced to input events.
        // Do this without forcing a full compose/present.
        auto syncCursorNow = [&]()
        {
            if (m_compositor)
            {
                m_compositor->syncHardwareCursorPosition();
            }
        };

        // If a title-bar drag is active, keep moving the captured window even if
        // the cursor leaves its bounds.
        if (m_dragWindow)
        {
            if (mouse.type == Type::MouseMove)
            {
                Rect oldBounds = m_dragWindow->bounds();
                QC::i32 newX = m_mousePos.x - m_dragOffset.x;
                QC::i32 newY = m_mousePos.y - m_dragOffset.y;

                // Clamp to screen bounds (minimal UX: keep window on-screen).
                const Size scr = screenSize();
                const QC::i32 maxX = (scr.width > oldBounds.width) ? (static_cast<QC::i32>(scr.width - oldBounds.width)) : 0;
                const QC::i32 maxY = (scr.height > oldBounds.height) ? (static_cast<QC::i32>(scr.height - oldBounds.height)) : 0;
                if (newX < 0)
                    newX = 0;
                if (newY < 0)
                    newY = 0;
                if (newX > maxX)
                    newX = maxX;
                if (newY > maxY)
                    newY = maxY;

                if (newX != oldBounds.x || newY != oldBounds.y)
                {
                    Rect newBounds = oldBounds;
                    newBounds.x = newX;
                    newBounds.y = newY;

                    invalidate(oldBounds);
                    m_dragWindow->setBounds(newBounds);
                    invalidate(newBounds);
                    postWindowEvent(QK::Event::Type::WindowMove, m_dragWindow);
                }

                // UI movement invalidates; main loop will render. Sync cursor now.
                syncCursorNow();
                return;
            }

            if (mouse.type == Type::MouseButtonUp && mouse.button == MouseButton::Left)
            {
                m_dragWindow = nullptr;
                syncCursorNow();
                return;
            }
        }

        if (m_captureButtonsMask == 0)
            m_captureWindow = nullptr;

        Window *targetWindow = nullptr;
        if (m_captureWindow && m_captureButtonsMask != 0)
        {
            targetWindow = m_captureWindow;
        }
        else
        {
            targetWindow = windowAt(m_mousePos);
        }

        // Handle enter/leave
        if (targetWindow != m_hoveredWindow)
        {
            // TODO: Post MouseEnter/MouseLeave custom events if needed
            m_hoveredWindow = targetWindow;
        }

        // Route to target window
        if (targetWindow)
        {
            // For mouse down, focus the window
            if (mouse.type == Type::MouseButtonDown)
            {
                if ((targetWindow->flags() & WindowFlags::AlwaysBottom) != 0)
                {
                    // Clicking the desktop/background should not raise it.
                    // Desktop itself is not focusable, and we also avoid
                    // changing focus away from the active window.
                }
                else
                {
                    bringToFront(targetWindow);
                }

                // Title-bar drag for movable windows.
                // NOTE: The compositor chrome is currently minimal, so we treat the
                // top N pixels of the window as the title bar hit region.
                static constexpr QC::i32 kTitleBarHeight = 24;
                const Rect windowBounds = targetWindow->bounds();
                const bool isMovable = (targetWindow->flags() & WindowFlags::Movable) != 0;
                const bool hasTitle = (targetWindow->flags() & WindowFlags::HasTitle) != 0;
                const bool leftDown = (mouse.button == MouseButton::Left);
                const QC::i32 localY = m_mousePos.y - windowBounds.y;
                if (leftDown && isMovable && hasTitle && localY >= 0 && localY < kTitleBarHeight)
                {
                    // Give controls in the title area (e.g. terminal close button)
                    // first chance to handle the click.
                    QK::Event::MouseEventData localMouse = mouse;
                    localMouse.x = m_mousePos.x;
                    localMouse.y = m_mousePos.y;
                    localMouse.x -= windowBounds.x;
                    localMouse.y -= windowBounds.y;

                    QK::Event::Event downEvent;
                    downEvent.data.mouse = localMouse;
                    const bool handled = targetWindow->onEvent(downEvent);
                    if (handled)
                    {
                        // Ensure mouse-up is delivered to the same window even
                        // if the cursor leaves while the button is held.
                        m_captureWindow = targetWindow;
                        m_captureButtonsMask |= mouseButtonToMask(mouse.button);
                        syncCursorNow();
                        return;
                    }

                    // Not handled by controls; treat as a window move drag.
                    m_captureWindow = nullptr;
                    m_captureButtonsMask = 0;
                    m_dragWindow = targetWindow;
                    m_dragOffset = Point{m_mousePos.x - windowBounds.x, m_mousePos.y - windowBounds.y};
                    m_dragStartBounds = windowBounds;
                    syncCursorNow();
                    return;
                }

                // Begin general capture for control interactions inside this window.
                // This ensures mouse-up always reaches the same window even if the
                // cursor leaves its bounds (e.g. scrollbar thumb drag).
                m_captureWindow = targetWindow;
                m_captureButtonsMask |= mouseButtonToMask(mouse.button);
            }

            // Translate coordinates into the window's local space so controls
            // can rely on window-relative positions for hit testing.
            QK::Event::MouseEventData localMouse = mouse;
            const Rect windowBounds = targetWindow->bounds();
            localMouse.x = m_mousePos.x;
            localMouse.y = m_mousePos.y;
            localMouse.x -= windowBounds.x;
            localMouse.y -= windowBounds.y;

            // Dispatch to window's onEvent
            QK::Event::Event event;
            event.data.mouse = localMouse;
            ++m_dispatchDepth;
            targetWindow->onEvent(event);
            --m_dispatchDepth;
            processPendingDestroy();

            // End capture when all pressed buttons are released.
            if (mouse.type == Type::MouseButtonUp && m_captureWindow == targetWindow)
            {
                m_captureButtonsMask &= ~mouseButtonToMask(mouse.button);
                if (m_captureButtonsMask == 0)
                    m_captureWindow = nullptr;
            }
        }

        // Ensure cursor position updates even if no repaint is needed.
        syncCursorNow();
    }

    void WindowManager::routeKeyEvent(const QK::Event::KeyEventData &key)
    {
        // Route keyboard events to focused window
        if (m_focusedWindow)
        {
            QK::Event::Event event;
            event.data.key = key;
            ++m_dispatchDepth;
            m_focusedWindow->onEvent(event);
            --m_dispatchDepth;
            processPendingDestroy();
        }
    }

    void WindowManager::postWindowEvent(QK::Event::Type type, Window *window)
    {
        auto &eventMgr = QK::Event::EventManager::instance();
        eventMgr.postWindowEvent(
            type,
            window->windowId(),
            window->bounds().x,
            window->bounds().y,
            window->bounds().width,
            window->bounds().height);
    }

    void WindowManager::applyStyleToWindow(Window *window, const StyleSnapshot &snapshot)
    {
        if (!window)
            return;
        window->setStyleSnapshot(&snapshot);
    }

    void WindowManager::onStyleChanged(const StyleSnapshot &snapshot)
    {
        for (QC::usize i = 0; i < m_windows.size(); ++i)
        {
            Window *window = m_windows[i];
            applyStyleToWindow(window, snapshot);
            if (window)
            {
                window->invalidate();
            }
        }
    }

} // namespace QW
