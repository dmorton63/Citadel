#include "QWWindow.h"
#include "QWMessageBus.h"
#include "QWWindowManager.h"
#include "QWControls/Containers/Panel.h"
#include "QKEventTypes.h"
#include <cstring>

namespace QW
{

    namespace
    {
        QC::u32 nextGraphicsSurfaceId()
        {
            static QC::u32 s_nextSurfaceId = 1;
            return s_nextSurfaceId++;
        }
    }

    Window::Window(const char *title, Rect bounds)
        : m_windowId(0),
          m_bounds(bounds),
          m_flags(WindowFlags::Default),
          m_root(nullptr),
          m_surfaceBackend(),
          m_styleRenderer(),
          m_painter(),
          m_surfacePixels(),
          m_qgfxSurface(),
          m_bufferWidth(0),
          m_bufferHeight(0),
          m_bufferPitchBytes(0)
    {
        std::strncpy(m_title, title ? title : "", sizeof(m_title) - 1);
        m_title[sizeof(m_title) - 1] = '\0';

        m_qgfxSurface.id.value = nextGraphicsSurfaceId();
        m_qgfxSurface.width = bounds.width;
        m_qgfxSurface.height = bounds.height;
        m_qgfxSurface.format = QGfx::PixelFormat::ARGB8888;
        m_qgfxSurface.usage = QGfx::SurfaceUsage::Dynamic;

        // Create root panel
        m_root = new Controls::Panel();
        m_root->setWindow(this);
        m_root->setBounds(Rect{0, 0, bounds.width, bounds.height});

        // Connect backend to style renderer
        m_styleRenderer.setBackend(&m_surfaceBackend);

        ensureSurface(bounds.width, bounds.height);
    }

    Window::~Window()
    {
        delete m_root;
        m_root = nullptr;
    }

    uint32_t Window::windowId() const { return m_windowId; }
    void Window::setWindowId(uint32_t id) { m_windowId = id; }

    const char *Window::title() const { return m_title; }

    void Window::setTitle(const char *title)
    {
        std::strncpy(m_title, title ? title : "", sizeof(m_title) - 1);
        m_title[sizeof(m_title) - 1] = '\0';
    }

    Rect Window::bounds() const { return m_bounds; }

    void Window::setBounds(const Rect &bounds)
    {
        const Rect oldBounds = m_bounds;
        const bool moved = (bounds.x != oldBounds.x) || (bounds.y != oldBounds.y);
        const bool sizeChanged = (bounds.width != m_bounds.width) ||
                                 (bounds.height != m_bounds.height);
        m_bounds = bounds;

        if ((moved || sizeChanged) && isVisible())
        {
            // Include decoration/shadow fringe so old window footprints are fully cleaned.
            static constexpr QC::i32 kLeftExpand = 1;
            static constexpr QC::i32 kTopExpand = 1;
            static constexpr QC::i32 kRightExpand = 7;
            static constexpr QC::i32 kBottomExpand = 7;

            const Rect oldDecor = {
                oldBounds.x - kLeftExpand,
                oldBounds.y - kTopExpand,
                oldBounds.width + static_cast<QC::u32>(kLeftExpand + kRightExpand),
                oldBounds.height + static_cast<QC::u32>(kTopExpand + kBottomExpand)};

            const Rect newDecor = {
                m_bounds.x - kLeftExpand,
                m_bounds.y - kTopExpand,
                m_bounds.width + static_cast<QC::u32>(kLeftExpand + kRightExpand),
                m_bounds.height + static_cast<QC::u32>(kTopExpand + kBottomExpand)};

            WindowManager::instance().invalidate(oldDecor);
            WindowManager::instance().invalidate(newDecor);
        }

        if (sizeChanged)
            onResize(bounds.width, bounds.height);
        else if (moved)
            invalidate();
    }

    bool Window::isVisible() const
    {
        return (m_flags & WindowFlags::Visible) != 0;
    }

    void Window::setVisible(bool visible)
    {
        const bool wasVisible = isVisible();
        if (visible)
            m_flags |= WindowFlags::Visible;
        else
            m_flags &= ~WindowFlags::Visible;

        // If transitioning to visible, ensure we paint at least once.
        if (!wasVisible && visible)
        {
            invalidate();
        }
    }

    uint32_t Window::flags() const
    {
        return m_flags;
    }

    void Window::setFlags(uint32_t flags)
    {
        const bool wasVisible = isVisible();
        m_flags = flags;
        const bool nowVisible = isVisible();
        if (!wasVisible && nowVisible)
        {
            invalidate();
        }
    }

    Controls::Panel *Window::root() const { return m_root; }

    void Window::setStyleSnapshot(const StyleSnapshot *snapshot)
    {
        m_styleRenderer.setStyleSnapshot(snapshot);
        invalidate();
    }

    StyleRenderer *Window::styleRenderer() { return &m_styleRenderer; }

    const QC::u32 *Window::buffer() const
    {
        return m_surfacePixels.empty() ? nullptr : m_surfacePixels.data();
    }

    QC::u32 Window::bufferWidth() const
    {
        return m_bufferWidth;
    }

    QC::u32 Window::bufferHeight() const
    {
        return m_bufferHeight;
    }

    QC::u32 Window::bufferPitchBytes() const
    {
        return m_bufferPitchBytes;
    }

    void Window::invalidate()
    {
        invalidateRect(Rect{0, 0, m_bounds.width, m_bounds.height});
    }

    void Window::invalidateRect(const Rect &rect)
    {
        // Hidden windows should not trigger compositor work or paints.
        if (!isVisible())
            return;

        // Controls may draw slightly outside their nominal bounds (hover lift,
        // focus rings, soft shadows). Keep margin modest to avoid over-invalidation
        // spikes when rapidly traversing between distant UI regions.
        static constexpr QC::i32 kInvalidateMargin = 10;

        const QC::i32 maxW = static_cast<QC::i32>(m_bounds.width);
        const QC::i32 maxH = static_cast<QC::i32>(m_bounds.height);

        QC::i32 x1 = rect.x - kInvalidateMargin;
        QC::i32 y1 = rect.y - kInvalidateMargin;
        QC::i32 x2 = rect.x + static_cast<QC::i32>(rect.width) + kInvalidateMargin;
        QC::i32 y2 = rect.y + static_cast<QC::i32>(rect.height) + kInvalidateMargin;

        if (x1 < 0)
            x1 = 0;
        if (y1 < 0)
            y1 = 0;
        if (x2 > maxW)
            x2 = maxW;
        if (y2 > maxH)
            y2 = maxH;

        if (x2 <= x1 || y2 <= y1)
            return;

        // Title-bar controls are painted into the window surface on top of the
        // chrome background. If a partial repaint touches the title strip, the
        // chrome redraw can wipe part of a control without repainting the rest
        // of that control. Expand such updates to the full title bar so close
        // buttons/title labels stay visually coherent.
        static constexpr QC::i32 kTitleBarHeight = 24;
        if ((m_flags & WindowFlags::HasTitle) != 0 && y1 < kTitleBarHeight)
        {
            x1 = 0;
            if (y1 > 0)
                y1 = 0;
            x2 = maxW;
            if (y2 < kTitleBarHeight)
                y2 = kTitleBarHeight;
        }

        const Rect clipped{ x1, y1,
                            static_cast<QC::u32>(x2 - x1),
                            static_cast<QC::u32>(y2 - y1) };

        // Notify compositor of the affected screen region.
        // `rect` is window-local; convert to screen coordinates.
        const Rect screenRect{m_bounds.x + clipped.x,
                              m_bounds.y + clipped.y,
                              clipped.width,
                              clipped.height};
        WindowManager::instance().invalidate(screenRect);

        // Track union dirty rect for clip-based partial painting.
        if (!m_hasDirtyRect)
        {
            m_dirtyRect = clipped;
            m_hasDirtyRect = true;
        }
        else
        {
            const QC::i32 ux1 = (clipped.x < m_dirtyRect.x) ? clipped.x : m_dirtyRect.x;
            const QC::i32 uy1 = (clipped.y < m_dirtyRect.y) ? clipped.y : m_dirtyRect.y;
            const QC::i32 ux2 = (static_cast<QC::i32>(clipped.right()) > static_cast<QC::i32>(m_dirtyRect.right()))
                                    ? static_cast<QC::i32>(clipped.right())
                                    : static_cast<QC::i32>(m_dirtyRect.right());
            const QC::i32 uy2 = (static_cast<QC::i32>(clipped.bottom()) > static_cast<QC::i32>(m_dirtyRect.bottom()))
                                    ? static_cast<QC::i32>(clipped.bottom())
                                    : static_cast<QC::i32>(m_dirtyRect.bottom());

            m_dirtyRect.x = ux1;
            m_dirtyRect.y = uy1;
            m_dirtyRect.width = static_cast<QC::u32>(ux2 - ux1);
            m_dirtyRect.height = static_cast<QC::u32>(uy2 - uy1);
        }

        // Coalesce paints: do not repaint synchronously inside invalidation.
        m_needsPaint = true;
    }

    void Window::paintIfNeeded()
    {
        if (!m_needsPaint)
            return;

        // Clip repaint to what was invalidated since the last paint.
        if (m_hasDirtyRect)
        {
            m_painter.setClipRect(m_dirtyRect);
        }
        else
        {
            m_painter.clearClipRect();
        }

        m_needsPaint = false;
        m_hasDirtyRect = false;
        paint();

        // Restore no-clip for subsequent operations.
        m_painter.clearClipRect();
    }

    void Window::paint()
    {
        if (!isVisible())
            return;

        if (!ensureSurface(m_bounds.width, m_bounds.height))
            return;

        float textScale = 1.0f;
        if (const StyleSnapshot *snapshot = m_styleRenderer.styleSnapshot())
        {
            textScale = snapshot->metrics.textScale;
        }
        else
        {
            textScale = StyleSnapshot::fallback().metrics.textScale;
        }
        if (textScale <= 0.0f)
        {
            textScale = 1.0f;
        }
        m_painter.setTextScale(textScale);

        FrameContext frameCtx{};
        frameCtx.surfaceBounds = Rect{0, 0, m_bounds.width, m_bounds.height};
        frameCtx.painter = &m_painter;

        if (!m_styleRenderer.beginFrame(frameCtx))
            return;

        WindowPaintArgs chromeArgs{};
        // Treat borderless/titleless windows as a desktop surface so the style system
        // can render the correct background without window chrome.
        if ((m_flags & WindowFlags::HasBorder) == 0 && (m_flags & WindowFlags::HasTitle) == 0)
        {
            chromeArgs.surface = WindowPaintArgs::Surface::Desktop;
        }
        chromeArgs.bounds = frameCtx.surfaceBounds;
        chromeArgs.title = m_title;
        chromeArgs.active = true; // TODO: hook into WindowManager focus state
        chromeArgs.focused = true;

        m_styleRenderer.drawWindowChrome(chromeArgs);

        Controls::PaintContext controlCtx{};
        controlCtx.window = this;
        controlCtx.styleRenderer = &m_styleRenderer;
        controlCtx.painter = &m_painter;

        if (m_root)
            m_root->paint(controlCtx);

        m_styleRenderer.endFrame();

        onPaint();
    }

    void Window::onPaint()
    {
        // Platform compositor will blit m_surfaceBackend output
    }

    void Window::onResize(uint32_t w, uint32_t h)
    {
        ensureSurface(w, h);
        if (m_root)
            m_root->setBounds(Rect{0, 0, w, h});
        invalidate();
    }

    bool Window::onEvent(const QK::Event::Event &e)
    {
        if (!m_root)
            return false;

        return m_root->onEvent(e);
    }

    void Window::setMessageHandler(MessageHandler handler, void *userData)
    {
        m_msgHandler = handler;
        m_msgUserData = userData;
    }

    bool Window::handleMessage(const Message &msg)
    {
        if (!m_msgHandler)
        {
            return false;
        }
        return m_msgHandler(this, msg, m_msgUserData);
    }

    bool Window::ensureSurface(QC::u32 width, QC::u32 height)
    {
        if (width == 0 || height == 0)
        {
            m_surfacePixels.resize(0);
            m_qgfxSurface.width = 0;
            m_qgfxSurface.height = 0;
            m_qgfxSurface.clearDirtyRegion();
            m_bufferWidth = 0;
            m_bufferHeight = 0;
            m_bufferPitchBytes = 0;
            m_painter.setSurface(nullptr, 0, 0, 0);
            m_surfaceBackend.setSurface(nullptr, nullptr, 0, 0);
            return false;
        }

        const bool sizeChanged = (width != m_bufferWidth) || (height != m_bufferHeight) || m_surfacePixels.empty();
        if (sizeChanged)
        {
            const QC::usize pixelCount = static_cast<QC::usize>(width) * static_cast<QC::usize>(height);
            m_surfacePixels.resize(pixelCount);
            m_bufferWidth = width;
            m_bufferHeight = height;
            m_bufferPitchBytes = width * sizeof(QC::u32);
        }

        if (m_surfacePixels.empty())
            return false;

        m_qgfxSurface.width = m_bufferWidth;
        m_qgfxSurface.height = m_bufferHeight;
        m_qgfxSurface.format = QGfx::PixelFormat::ARGB8888;
        m_qgfxSurface.usage = QGfx::SurfaceUsage::Dynamic;

        // IMPORTANT: do not rebind the painter/backend every frame.
        // PainterSurface::setSurface() resets the clip rect, which breaks dirty-rect repaint.
        if (sizeChanged)
        {
            QC::u32 *pixelData = m_surfacePixels.data();
            m_painter.setSurface(pixelData, m_bufferWidth, m_bufferHeight, m_bufferWidth);
            m_surfaceBackend.setSurface(&m_painter,
                                        pixelData,
                                        m_bufferWidth,
                                        m_bufferHeight,
                                        m_bufferPitchBytes);
        }
        return true;
    }

} // namespace QW