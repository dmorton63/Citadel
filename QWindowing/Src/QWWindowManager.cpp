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
#include "QCLogger.h"
#include "QDrvTimer.h"
#include "QKRuntimeRegistries.h"
#include "QFSVFS.h"
#include "QFSFile.h"

namespace QW
{

    static void syncRuntimeWindowRegistry(const QC::Vector<Window *> &windows, Window *focused)
    {
        QK::Runtime::WindowSnapshot snaps[QK::Runtime::Registries::MaxWindows] = {};

        QC::usize outCount = 0;
        QC::u32 z = 0;
        for (QC::usize i = 0; i < windows.size() && outCount < QK::Runtime::Registries::MaxWindows; ++i)
        {
            Window *w = windows[i];
            if (!w)
                continue;

            const Rect b = w->bounds();
            QK::Runtime::WindowSnapshot s{};
            s.windowId = w->windowId();
            s.x = b.x;
            s.y = b.y;
            s.width = b.width;
            s.height = b.height;
            s.flags = w->flags();
            s.zIndex = z++;
            s.focused = (focused && focused->windowId() == w->windowId());
            s.ownerPid = 0;

            snaps[outCount++] = s;
        }

        QK::Runtime::Registries::instance().syncWindows(snaps, outCount);
    }

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

    static Rect decoratedInvalidationRect(const Rect &bounds)
    {
        // Compositor decorations paint at the bounds edge, and shadow extends
        // beyond the right/bottom edge. Expand slightly on all sides so drag/
        // destroy invalidation fully clears any residual pixels.
        static constexpr QC::i32 kLeftExpand = 1;
        static constexpr QC::i32 kTopExpand = 1;
        static constexpr QC::i32 kRightExpand = 7;
        static constexpr QC::i32 kBottomExpand = 7;

        const QC::i32 expandedX = bounds.x - kLeftExpand;
        const QC::i32 expandedY = bounds.y - kTopExpand;
        const QC::u32 expandedWidth = bounds.width + static_cast<QC::u32>(kLeftExpand + kRightExpand);
        const QC::u32 expandedHeight = bounds.height + static_cast<QC::u32>(kTopExpand + kBottomExpand);
        return Rect{expandedX, expandedY, expandedWidth, expandedHeight};
    }

    static Rect intersectRect(const Rect &a, const Rect &b)
    {
        const QC::i32 left = (a.x > b.x) ? a.x : b.x;
        const QC::i32 top = (a.y > b.y) ? a.y : b.y;
        const QC::i32 right = (a.right() < b.right()) ? a.right() : b.right();
        const QC::i32 bottom = (a.bottom() < b.bottom()) ? a.bottom() : b.bottom();

        if (right <= left || bottom <= top)
            return Rect{0, 0, 0, 0};

        return Rect{left,
                    top,
                    static_cast<QC::u32>(right - left),
                    static_cast<QC::u32>(bottom - top)};
    }

    static void invalidateRectDifference(WindowManager *wm,
                                         const Rect &whole,
                                         const Rect &covered)
    {
        if (!wm || whole.isEmpty())
            return;

        const Rect overlap = intersectRect(whole, covered);
        if (overlap.isEmpty())
        {
            wm->invalidate(whole);
            return;
        }

        if (overlap.y > whole.y)
        {
            wm->invalidate(Rect{whole.x,
                                whole.y,
                                whole.width,
                                static_cast<QC::u32>(overlap.y - whole.y)});
        }

        if (overlap.bottom() < whole.bottom())
        {
            wm->invalidate(Rect{whole.x,
                                overlap.bottom(),
                                whole.width,
                                static_cast<QC::u32>(whole.bottom() - overlap.bottom())});
        }

        if (overlap.x > whole.x)
        {
            wm->invalidate(Rect{whole.x,
                                overlap.y,
                                static_cast<QC::u32>(overlap.x - whole.x),
                                overlap.height});
        }

        if (overlap.right() < whole.right())
        {
            wm->invalidate(Rect{overlap.right(),
                                overlap.y,
                                static_cast<QC::u32>(whole.right() - overlap.right()),
                                overlap.height});
        }
    }

    static bool writeAll(QFS::File *file, const void *data, QC::usize size)
    {
        if (!file)
            return false;

        const QC::u8 *bytes = static_cast<const QC::u8 *>(data);
        QC::usize written = 0;
        while (written < size)
        {
            const QC::isize rc = file->write(bytes + written, size - written);
            if (rc <= 0)
                return false;
            written += static_cast<QC::usize>(rc);
        }

        return true;
    }

    static bool appendChar(char *dst, QC::usize cap, QC::usize &pos, char c)
    {
        if (!dst || pos + 1 >= cap)
            return false;
        dst[pos++] = c;
        dst[pos] = '\0';
        return true;
    }

    static bool appendText(char *dst, QC::usize cap, QC::usize &pos, const char *text)
    {
        if (!dst || !text)
            return false;
        for (QC::usize i = 0; text[i] != '\0'; ++i)
        {
            if (!appendChar(dst, cap, pos, text[i]))
                return false;
        }
        return true;
    }

    static bool appendUnsigned(char *dst, QC::usize cap, QC::usize &pos, QC::u32 value)
    {
        char digits[16];
        QC::usize count = 0;
        do
        {
            digits[count++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value != 0 && count < sizeof(digits));

        while (count > 0)
        {
            if (!appendChar(dst, cap, pos, digits[--count]))
                return false;
        }
        return true;
    }

    static bool appendSigned(char *dst, QC::usize cap, QC::usize &pos, QC::i32 value)
    {
        if (value < 0)
        {
            if (!appendChar(dst, cap, pos, '-'))
                return false;
            const QC::u32 magnitude = static_cast<QC::u32>(-(value + 1)) + 1u;
            return appendUnsigned(dst, cap, pos, magnitude);
        }

        return appendUnsigned(dst, cap, pos, static_cast<QC::u32>(value));
    }

    static bool buildDragDumpPath(char *dst, QC::usize cap, QC::u32 sequence, const char *phase, const char *suffix)
    {
        QC::usize pos = 0;
        if (!appendText(dst, cap, pos, "/shared/drag_"))
            return false;
        if (sequence < 100u && !appendChar(dst, cap, pos, '0'))
            return false;
        if (sequence < 10u && !appendChar(dst, cap, pos, '0'))
            return false;
        if (!appendUnsigned(dst, cap, pos, sequence))
            return false;
        if (!appendChar(dst, cap, pos, '_'))
            return false;
        if (!appendText(dst, cap, pos, phase))
            return false;
        if (!appendChar(dst, cap, pos, '_'))
            return false;
        if (!appendText(dst, cap, pos, suffix))
            return false;
        return true;
    }

    static QC::u32 readRgbPixel(const QC::u8 *row, QC::u32 x, QC::u32 bpp, PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::ARGB8888:
        {
            const QC::u32 pixel = reinterpret_cast<const QC::u32 *>(row)[x];
            return ((pixel >> 16) & 0xFFu) | (pixel & 0x00FF00u) | ((pixel & 0xFFu) << 16);
        }
        case PixelFormat::ABGR8888:
        {
            const QC::u32 pixel = reinterpret_cast<const QC::u32 *>(row)[x];
            return pixel & 0x00FFFFFFu;
        }
        case PixelFormat::RGB888:
        {
            const QC::u8 *pixel = row + static_cast<QC::usize>(x) * 3u;
            return (static_cast<QC::u32>(pixel[0]) << 16) |
                   (static_cast<QC::u32>(pixel[1]) << 8) |
                   static_cast<QC::u32>(pixel[2]);
        }
        case PixelFormat::BGR888:
        {
            const QC::u8 *pixel = row + static_cast<QC::usize>(x) * 3u;
            return (static_cast<QC::u32>(pixel[2]) << 16) |
                   (static_cast<QC::u32>(pixel[1]) << 8) |
                   static_cast<QC::u32>(pixel[0]);
        }
        case PixelFormat::RGB565:
        case PixelFormat::BGR565:
        {
            const QC::u16 pixel = reinterpret_cast<const QC::u16 *>(row)[x];
            QC::u32 r = ((pixel >> 11) & 0x1Fu) * 255u / 31u;
            QC::u32 g = ((pixel >> 5) & 0x3Fu) * 255u / 63u;
            QC::u32 b = (pixel & 0x1Fu) * 255u / 31u;
            if (format == PixelFormat::BGR565)
            {
                const QC::u32 tmp = r;
                r = b;
                b = tmp;
            }
            return (r << 16) | (g << 8) | b;
        }
        }

        (void)bpp;
        return 0;
    }

    static bool dumpBufferCropToPpm(const char *path,
                                    const void *buffer,
                                    QC::u32 surfaceWidth,
                                    QC::u32 surfaceHeight,
                                    QC::u32 pitchBytes,
                                    QC::u32 bpp,
                                    PixelFormat format,
                                    const Rect &requestedRect)
    {
        if (!path || !buffer || surfaceWidth == 0 || surfaceHeight == 0)
            return false;

        QC::i32 x0 = requestedRect.x;
        QC::i32 y0 = requestedRect.y;
        QC::i32 x1 = requestedRect.right();
        QC::i32 y1 = requestedRect.bottom();
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        if (x1 > static_cast<QC::i32>(surfaceWidth))
            x1 = static_cast<QC::i32>(surfaceWidth);
        if (y1 > static_cast<QC::i32>(surfaceHeight))
            y1 = static_cast<QC::i32>(surfaceHeight);
        if (x1 <= x0 || y1 <= y0)
            return false;

        const QC::u32 width = static_cast<QC::u32>(x1 - x0);
        const QC::u32 height = static_cast<QC::u32>(y1 - y0);

        auto &vfs = QFS::VFS::instance();
        QFS::File *file = vfs.open(path,
                                   QFS::OpenMode::Write |
                                   QFS::OpenMode::Create |
                                   QFS::OpenMode::Truncate |
                                   QFS::OpenMode::Binary);
        if (!file)
            return false;

        char header[64] = {};
        QC::usize headerPos = 0;
        bool ok = appendText(header, sizeof(header), headerPos, "P6\n") &&
              appendUnsigned(header, sizeof(header), headerPos, width) &&
              appendChar(header, sizeof(header), headerPos, ' ') &&
              appendUnsigned(header, sizeof(header), headerPos, height) &&
              appendText(header, sizeof(header), headerPos, "\n255\n") &&
              writeAll(file, header, headerPos);

        QC::Vector<QC::u8> row;
        if (ok)
            row.resize(static_cast<QC::usize>(width) * 3u);

        const QC::u8 *base = static_cast<const QC::u8 *>(buffer);
        for (QC::u32 y = 0; ok && y < height; ++y)
        {
            const QC::u8 *srcRow = base + static_cast<QC::usize>(y0 + static_cast<QC::i32>(y)) * pitchBytes;
            for (QC::u32 x = 0; x < width; ++x)
            {
                const QC::u32 rgb = readRgbPixel(srcRow, static_cast<QC::u32>(x0) + x, bpp, format);
                row[static_cast<QC::usize>(x) * 3u + 0u] = static_cast<QC::u8>((rgb >> 16) & 0xFFu);
                row[static_cast<QC::usize>(x) * 3u + 1u] = static_cast<QC::u8>((rgb >> 8) & 0xFFu);
                row[static_cast<QC::usize>(x) * 3u + 2u] = static_cast<QC::u8>(rgb & 0xFFu);
            }

            ok = writeAll(file, row.data(), row.size());
        }

        vfs.close(file);
        return ok;
    }

    static bool dumpWindowSurfaceToPpm(const char *path, Window *window)
    {
        if (!path || !window || !window->buffer())
            return false;

        return dumpBufferCropToPpm(path,
                                   window->buffer(),
                                   window->bufferWidth(),
                                   window->bufferHeight(),
                                   window->bufferPitchBytes(),
                                   32,
                                   PixelFormat::ARGB8888,
                                   Rect{0, 0, window->bufferWidth(), window->bufferHeight()});
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

        syncRuntimeWindowRegistry(m_windows, m_focusedWindow);

        return window;
    }

    void WindowManager::destroyWindow(Window *window)
    {
        if (!window)
            return;

        // Capture bounds before removal so we can repaint the region that was covered.
        const Rect oldBounds = decoratedInvalidationRect(window->bounds());

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

            syncRuntimeWindowRegistry(m_windows, m_focusedWindow);
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

        syncRuntimeWindowRegistry(m_windows, m_focusedWindow);

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
                oldFocus->invalidate();
        }

        // Post focus event to new window
        if (window)
        {
            postWindowEvent(QK::Event::Type::WindowFocus, window);
                window->invalidate();
        }

        syncRuntimeWindowRegistry(m_windows, m_focusedWindow);
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
        invalidate(decoratedInvalidationRect(window->bounds()));

        syncRuntimeWindowRegistry(m_windows, m_focusedWindow);
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

        invalidate(decoratedInvalidationRect(window->bounds()));

        syncRuntimeWindowRegistry(m_windows, m_focusedWindow);
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
        if (!m_needsRender)
            return;

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

    void WindowManager::resetInputLatencyStats()
    {
        m_lastInputTimestamp = 0;
        m_lastInputMs = 0;
        m_lastMouseEventPostedMs = 0;
        m_lastMouseQueueDelayMs = 0;
        m_maxMouseQueueDelayMs = 0;
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
        const Point oldMousePos = m_mousePos;

        // Capture timing for UI instrumentation/debugging.
        m_lastInputTimestamp = mouse.timestamp;
        m_lastInputMs = QDrv::Timer::instance().milliseconds();
        if (m_lastMouseEventPostedMs != 0 && m_lastInputMs >= m_lastMouseEventPostedMs)
        {
            m_lastMouseQueueDelayMs = m_lastInputMs - m_lastMouseEventPostedMs;
            if (m_lastMouseQueueDelayMs > m_maxMouseQueueDelayMs)
                m_maxMouseQueueDelayMs = m_lastMouseQueueDelayMs;
        }

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

        if (m_compositor && !m_compositor->hasHardwareCursor() &&
            (oldMousePos.x != m_mousePos.x || oldMousePos.y != m_mousePos.y))
        {
            const Rect oldCursorRect = m_compositor->cursorBoundsAt(oldMousePos.x, oldMousePos.y);
            const Rect newCursorRect = m_compositor->cursorBoundsAt(m_mousePos.x, m_mousePos.y);
            if (!oldCursorRect.isEmpty() && !newCursorRect.isEmpty())
            {
                const QC::i32 x1 = (oldCursorRect.x < newCursorRect.x) ? oldCursorRect.x : newCursorRect.x;
                const QC::i32 y1 = (oldCursorRect.y < newCursorRect.y) ? oldCursorRect.y : newCursorRect.y;
                const QC::i32 x2 = (oldCursorRect.right() > newCursorRect.right()) ? oldCursorRect.right() : newCursorRect.right();
                const QC::i32 y2 = (oldCursorRect.bottom() > newCursorRect.bottom()) ? oldCursorRect.bottom() : newCursorRect.bottom();
                invalidate(Rect{x1,
                                y1,
                                static_cast<QC::u32>(x2 - x1),
                                static_cast<QC::u32>(y2 - y1)});
            }
            else if (!oldCursorRect.isEmpty())
            {
                invalidate(oldCursorRect);
            }
            else if (!newCursorRect.isEmpty())
            {
                invalidate(newCursorRect);
            }
        }

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

                    const Rect oldDecoratedBounds = decoratedInvalidationRect(oldBounds);
                    const Rect newDecoratedBounds = decoratedInvalidationRect(newBounds);
                    const bool usedRectCopy = m_compositor &&
                                              m_compositor->rectCopy(oldDecoratedBounds, newDecoratedBounds);

                    m_dragWindow->setBounds(newBounds);

                    if (usedRectCopy)
                    {
                        // Copy the moved window image immediately, then repaint the
                        // destination and any newly exposed source strips for correctness.
                        invalidateRectDifference(this, oldDecoratedBounds, newDecoratedBounds);
                    }
                    else
                    {
                        invalidate(oldDecoratedBounds);
                    }

                    // Even after a successful rect-copy, the destination window still
                    // needs a real repaint. Without this, edge-local controls near the
                    // title bar/right edge can retain stale pixels until a later hover
                    // or expose invalidates them.
                    invalidate(newDecoratedBounds);
                    postWindowEvent(QK::Event::Type::WindowMove, m_dragWindow);

                    syncRuntimeWindowRegistry(m_windows, m_focusedWindow);
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

    void WindowManager::dumpDragReleaseBuffers(const char *phase,
                                               QC::u32 sequence,
                                               Window *window,
                                               const Rect &windowBounds,
                                               const Rect &decoratedBounds)
    {
        if (!phase || !m_framebuffer)
            return;

        auto &vfs = QFS::VFS::instance();
        (void)vfs.createDir("/shared/dragdump");

        char path[160] = {};
        char meta[256] = {};
        QC::usize metaPos = 0;
        const bool metaOk = appendText(meta, sizeof(meta), metaPos, "seq=") &&
                            appendUnsigned(meta, sizeof(meta), metaPos, sequence) &&
                            appendText(meta, sizeof(meta), metaPos, " phase=") &&
                            appendText(meta, sizeof(meta), metaPos, phase) &&
                            appendText(meta, sizeof(meta), metaPos, " window=") &&
                            appendSigned(meta, sizeof(meta), metaPos, windowBounds.x) &&
                            appendChar(meta, sizeof(meta), metaPos, ',') &&
                            appendSigned(meta, sizeof(meta), metaPos, windowBounds.y) &&
                            appendChar(meta, sizeof(meta), metaPos, ' ') &&
                            appendUnsigned(meta, sizeof(meta), metaPos, windowBounds.width) &&
                            appendChar(meta, sizeof(meta), metaPos, 'x') &&
                            appendUnsigned(meta, sizeof(meta), metaPos, windowBounds.height) &&
                            appendText(meta, sizeof(meta), metaPos, " decorated=") &&
                            appendSigned(meta, sizeof(meta), metaPos, decoratedBounds.x) &&
                            appendChar(meta, sizeof(meta), metaPos, ',') &&
                            appendSigned(meta, sizeof(meta), metaPos, decoratedBounds.y) &&
                            appendChar(meta, sizeof(meta), metaPos, ' ') &&
                            appendUnsigned(meta, sizeof(meta), metaPos, decoratedBounds.width) &&
                            appendChar(meta, sizeof(meta), metaPos, 'x') &&
                            appendUnsigned(meta, sizeof(meta), metaPos, decoratedBounds.height) &&
                            appendChar(meta, sizeof(meta), metaPos, '\n');
        if (metaOk && buildDragDumpPath(path, sizeof(path), sequence, phase, "meta.txt"))
        {
            QFS::File *metaFile = vfs.open(path,
                                           QFS::OpenMode::Write |
                                           QFS::OpenMode::Create |
                                           QFS::OpenMode::Truncate);
            if (metaFile)
            {
                (void)writeAll(metaFile, meta, metaPos);
                vfs.close(metaFile);
            }
        }

        if (window)
        {
            if (buildDragDumpPath(path, sizeof(path), sequence, phase, "window.ppm"))
                (void)dumpWindowSurfaceToPpm(path, window);
        }

        if (buildDragDumpPath(path, sizeof(path), sequence, phase, "back.ppm"))
        {
            (void)dumpBufferCropToPpm(path,
                                      m_framebuffer->backBuffer(),
                                      m_framebuffer->width(),
                                      m_framebuffer->height(),
                                      m_framebuffer->pitch(),
                                      m_framebuffer->bpp(),
                                      m_framebuffer->format(),
                                      decoratedBounds);
        }

        if (buildDragDumpPath(path, sizeof(path), sequence, phase, "front.ppm"))
        {
            (void)dumpBufferCropToPpm(path,
                                      m_framebuffer->buffer(),
                                      m_framebuffer->width(),
                                      m_framebuffer->height(),
                                      m_framebuffer->pitch(),
                                      m_framebuffer->bpp(),
                                      m_framebuffer->format(),
                                      decoratedBounds);
        }

        QC_LOG_INFO("QWDragDump", "Captured drag dump seq=%u phase=%s to /shared", sequence, phase);
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
