// QWindowing Compositor - Window compositing implementation
// Namespace: QW
#include "QWCompositor.h"
#include "QWWindow.h"
#include "QWFramebuffer.h"
#include "QWRenderer.h"
#include "QKMemHeap.h"
#include "QCMemUtil.h"
#include "QCLogger.h"
#include "QDrvTimer.h"

#include "QWPresentBackend.h"
#include "QWFramebufferPresentBackend.h"
#include "QWVmwareSVGAPresentBackend.h"
#include "QWDisplayBootstrapUtil.h"

#include "QG/CameraRH.h"


namespace QW
{

    namespace
    {
        // Off by default: set to 1 when you want to visually validate camera/projection math.
        static constexpr bool QAIOS_DEBUG_CAMERA_OVERLAY = false;
        static constexpr bool QAIOS_MOUSE_INFO_LOGS = false;

        Rect unionRect(const Rect &a, const Rect &b)
        {
            if (a.isEmpty())
                return b;
            if (b.isEmpty())
                return a;

            const QC::i32 left = (a.x < b.x) ? a.x : b.x;
            const QC::i32 top = (a.y < b.y) ? a.y : b.y;
            const QC::i32 right = (a.right() > b.right()) ? a.right() : b.right();
            const QC::i32 bottom = (a.bottom() > b.bottom()) ? a.bottom() : b.bottom();
            return Rect{left,
                        top,
                        static_cast<QC::u32>(right - left),
                        static_cast<QC::u32>(bottom - top)};
        }

        Rect intersectRect(const Rect &a, const Rect &b)
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

        Rect dirtyRectForWindow(const QC::Vector<DirtyRegion> &dirtyRegions,
                                const Rect &windowBounds,
                                QC::u32 bufferWidth,
                                QC::u32 bufferHeight)
        {
            if (bufferWidth == 0 || bufferHeight == 0)
                return Rect{0, 0, 0, 0};

            if (dirtyRegions.empty())
                return Rect{0, 0, bufferWidth, bufferHeight};

            Rect localDirty{0, 0, 0, 0};
            bool hasDirty = false;

            for (QC::usize i = 0; i < dirtyRegions.size(); ++i)
            {
                const Rect overlap = intersectRect(windowBounds, dirtyRegions[i].rect);
                if (overlap.isEmpty())
                    continue;

                Rect localRect{overlap.x - windowBounds.x,
                               overlap.y - windowBounds.y,
                               overlap.width,
                               overlap.height};

                if (!hasDirty)
                {
                    localDirty = localRect;
                    hasDirty = true;
                }
                else
                {
                    localDirty = unionRect(localDirty, localRect);
                }
            }

            return hasDirty ? localDirty : Rect{0, 0, 0, 0};
        }

        void blitWindowToBackBuffer(Renderer *renderer, Window *window)
        {
            if (!renderer || !window || !window->buffer())
                return;

            const Rect bounds = window->bounds();
            renderer->blit(bounds.x,
                           bounds.y,
                           window->buffer(),
                           window->bufferWidth(),
                           window->bufferHeight(),
                           window->bufferPitchBytes());
        }

    }

    Compositor::Compositor(Framebuffer *fb)
        : m_framebuffer(fb),
          m_renderer(nullptr),
          m_presentBackend(nullptr),
          m_effects(0),
          m_wallpaper(nullptr),
          m_wallpaperWidth(0),
          m_wallpaperHeight(0),
          m_cursorPixels(nullptr),
          m_cursorWidth(0),
          m_cursorHeight(0),
          m_cursorHotspotX(0),
          m_cursorHotspotY(0),
          m_cursorBackground(nullptr),
          m_cursorBackX(0),
          m_cursorBackY(0),
          m_lastComposeTime(0),
          m_frameCount(0)
    {
    }

    Compositor::~Compositor()
    {
        if (m_presentBackend)
        {
            delete m_presentBackend;
            m_presentBackend = nullptr;
        }
        if (m_renderer)
        {
            delete m_renderer;
            m_renderer = nullptr;
        }
        if (m_wallpaper)
        {
            QK::Memory::Heap::instance().free(m_wallpaper);
            m_wallpaper = nullptr;
        }
        if (m_cursorPixels)
        {
            QK::Memory::Heap::instance().free(m_cursorPixels);
            m_cursorPixels = nullptr;
        }
        if (m_cursorBackground)
        {
            QK::Memory::Heap::instance().free(m_cursorBackground);
            m_cursorBackground = nullptr;
        }
    }

    void Compositor::resetStats()
    {
        m_lastComposeTime = 0;
        m_frameCount = 0;
        m_dirtyCollapseCount = 0;
        m_stats = CompositorStats{};
        if (m_presentBackend)
            m_presentBackend->resetAccelerationStats();
    }

    const PresentAccelerationStats &Compositor::accelerationStats() const
    {
        if (m_presentBackend)
            return m_presentBackend->accelerationStats();

        static PresentAccelerationStats s_empty;
        return s_empty;
    }

    void Compositor::initialize()
    {
        // Select presentation backend.
        // Default is software framebuffer swap; accelerated present is surfaced through CVD.
        if (QDrv::Display::cvd_has_accelerated_present())
        {
            m_presentBackend = new VmwareSVGAPresentBackend();
        }
        else
        {
            m_presentBackend = new FramebufferPresentBackend();
        }
        if (m_presentBackend)
        {
            m_presentBackend->initialize(m_framebuffer);
        }

        m_renderer = new Renderer();
        if (m_framebuffer)
        {
            m_renderer->setTarget(
                static_cast<QC::u32 *>(m_framebuffer->backBuffer()),
                m_framebuffer->width(),
                m_framebuffer->height(),
                m_framebuffer->pitch());
        }

        // Create a simple default arrow cursor (12x16)
        static constexpr QC::u32 cursorWidth = 12;
        static constexpr QC::u32 cursorHeight = 16;
        static constexpr QC::u32 W = 0xFFFFFFFF; // White
        static constexpr QC::u32 B = 0xFF000000; // Black
        static constexpr QC::u32 T = 0x00000000; // Transparent

        static const QC::u32 defaultCursor[cursorHeight * cursorWidth] = {
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            W,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            W,
            W,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            W,
            W,
            W,
            W,
            W,
            B,
            T,
            T,
            T,
            B,
            W,
            W,
            W,
            W,
            W,
            W,
            W,
            W,
            B,
            T,
            T,
            B,
            W,
            W,
            W,
            W,
            W,
            B,
            B,
            B,
            B,
            T,
            T,
            B,
            W,
            W,
            B,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            B,
            W,
            B,
            T,
            B,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            B,
            B,
            T,
            T,
            B,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            B,
            T,
            T,
            T,
            T,
            B,
            W,
            W,
            B,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            T,
            B,
            B,
            B,
            T,
            T,
            T,
            T,
        };

        setCursor(defaultCursor, cursorWidth, cursorHeight, 0, 0);

        const bool hwCursor = (m_presentBackend && m_presentBackend->hasHardwareCursor());
        QC_LOG_INFO("QWCompositor", "Hardware cursor: %s", hwCursor ? "ON" : "OFF");
    }

    void Compositor::compose()
    {
        if (!m_framebuffer || !m_renderer)
            return;

        const QC::u64 composeStartMs = QDrv::Timer::instance().milliseconds();

        const bool hasHwCursor = (m_presentBackend && m_presentBackend->hasHardwareCursor());
        const bool useWindowSurfaceBatches = (m_presentBackend && m_presentBackend->supportsWindowSurfaceBatches());
        m_stats.hardwareCursorActive = hasHwCursor;
        m_stats.dirtyRegionCount = m_dirtyRegions.size();

        // Merge overlapping regions before deciding how tightly we can clip composition.
        if (!m_dirtyRegions.empty())
        {
            mergeDirtyRegions();
            m_stats.lastMergedDirtyRegionCount = m_dirtyRegions.size();

            static constexpr QC::usize kMaxDirtyRegionsBeforeFullscreen = 24;
            static constexpr QC::u32 kMaxDirtyCoveragePercent = 60;

            QC::u64 totalDirtyArea = 0;
            for (QC::usize i = 0; i < m_dirtyRegions.size(); ++i)
            {
                totalDirtyArea += static_cast<QC::u64>(m_dirtyRegions[i].rect.width) *
                                  static_cast<QC::u64>(m_dirtyRegions[i].rect.height);
            }

            const QC::u64 screenArea = static_cast<QC::u64>(m_framebuffer->width()) *
                                       static_cast<QC::u64>(m_framebuffer->height());
            m_stats.lastDirtyArea = totalDirtyArea;
            m_stats.lastDirtyCoveragePercent = (screenArea != 0)
                                                  ? static_cast<QC::u32>((totalDirtyArea * 100u) / screenArea)
                                                  : 0;
            const bool tooManyRegions = m_dirtyRegions.size() > kMaxDirtyRegionsBeforeFullscreen;
            const bool tooMuchCoverage = (screenArea != 0) &&
                                         (totalDirtyArea * 100u >= screenArea * kMaxDirtyCoveragePercent);

            if (tooManyRegions || tooMuchCoverage)
            {
                ++m_dirtyCollapseCount;
                m_stats.dirtyCollapseCount = m_dirtyCollapseCount;
                if ((m_dirtyCollapseCount % 60u) == 1u)
                {
                    QC_LOG_INFO("QWCompositor",
                                "Collapsing dirty work to fullscreen (regions=%lu area=%llu screen=%llu)",
                                static_cast<unsigned long>(m_dirtyRegions.size()),
                                static_cast<unsigned long long>(totalDirtyArea),
                                static_cast<unsigned long long>(screenArea));
                }

                m_dirtyRegions.clear();
                m_dirtyRegions.push_back(DirtyRegion{Rect{0, 0, m_framebuffer->width(), m_framebuffer->height()}, false});
                m_stats.lastMergedDirtyRegionCount = m_dirtyRegions.size();
                m_stats.lastDirtyArea = screenArea;
                m_stats.lastDirtyCoveragePercent = 100;
            }
        }

        // When using a hardware cursor, we can safely redraw only dirty regions.
        // (Software cursor needs full redraw unless we track/restore background.)

        // If nothing is dirty and we have a hardware cursor, skip recompositing/presenting.
        // Cursor movement is handled via cursor registers, so we don't need framebuffer updates.
        if (hasHwCursor && m_dirtyRegions.empty())
        {
            syncHardwareCursorPosition();
            return;
        }

        // Keep a simple compose duration metric for performance tracking.

        auto &wm = WindowManager::instance();
        const auto composeScene = [&]()
        {
            // Draw desktop background
            drawDesktop();
            if (QAIOS_DEBUG_CAMERA_OVERLAY)
            {
                // Minimal integration demo: use the UI ortho camera to transform a simple box.
                // This is intentionally simple and self-contained.
                QG::UICameraOrthoRH cam;
                cam.width = m_framebuffer->width();
                cam.height = m_framebuffer->height();

                // A 64x64 box in pixel space.
                const QC::Vec3f p0{24.0f, 24.0f, 0.0f};
                const QC::Vec3f p1{24.0f + 64.0f, 24.0f, 0.0f};
                const QC::Vec3f p2{24.0f + 64.0f, 24.0f + 64.0f, 0.0f};
                const QC::Vec3f p3{24.0f, 24.0f + 64.0f, 0.0f};

                (void)QC::transformPoint(cam.viewProj(), p0);
                (void)QC::transformPoint(cam.viewProj(), p1);
                (void)QC::transformPoint(cam.viewProj(), p2);
                (void)QC::transformPoint(cam.viewProj(), p3);
            }

            // Compose all windows from bottom to top
            for (QC::usize i = 0; i < wm.windowCount(); ++i)
            {
                Window *window = wm.windowAtIndex(i);
                if (window && window->isVisible())
                {
                    if (window->flags() & WindowFlags::HasBorder)
                    {
                        drawWindowDecorations(window);
                    }

                    if (!useWindowSurfaceBatches)
                    {
                        blitWindowToBackBuffer(m_renderer, window);
                    }
                }
            }
        };

        if (hasHwCursor && !m_dirtyRegions.empty())
        {
            for (QC::usize i = 0; i < m_dirtyRegions.size(); ++i)
            {
                const Rect &dirty = m_dirtyRegions[i].rect;
                if (dirty.isEmpty())
                    continue;
                m_renderer->setClipRect(dirty);
                composeScene();
            }
            m_renderer->clearClipRect();
        }
        else
        {
            composeScene();
        }

        // Draw cursor
        Point mousePos = wm.mousePosition();
        if (hasHwCursor)
        {
            syncHardwareCursorPosition();
        }
        else
        {
            drawCursor(mousePos.x, mousePos.y);
        }

        if (useWindowSurfaceBatches)
        {
            QGfx::Batch frameBatch;
            QC::Vector<WindowSurfaceBlit> frameBlits;

            Rect batchDirtyRegion{0, 0, 0, 0};
            bool hasBatchDirtyRegion = false;

            for (QC::usize i = 0; i < wm.windowCount(); ++i)
            {
                Window *window = wm.windowAtIndex(i);
                if (!window || !window->isVisible() || !window->buffer())
                    continue;

                const Rect bounds = window->bounds();
                const Rect localDirty = dirtyRectForWindow(m_dirtyRegions,
                                                           bounds,
                                                           window->bufferWidth(),
                                                           window->bufferHeight());
                if (localDirty.isEmpty())
                    continue;

                WindowSurfaceBlit blit;
                blit.surface = &window->graphicsSurface();
                blit.pixels = window->buffer();
                blit.stridePixels = window->bufferPitchBytes() / sizeof(QC::u32);
                blit.dirtyRect = localDirty;
                frameBlits.push_back(blit);

                QGfx::DrawOp op;
                op.srcSurface = window->graphicsSurface().id;
                op.srcRect = Rect{0, 0, window->bufferWidth(), window->bufferHeight()};
                op.dstRect = bounds;
                op.zOrder = static_cast<QC::i32>(i);
                frameBatch.addOp(op);

                const Rect screenDirty{bounds.x + localDirty.x,
                                       bounds.y + localDirty.y,
                                       localDirty.width,
                                       localDirty.height};
                if (!hasBatchDirtyRegion)
                {
                    batchDirtyRegion = screenDirty;
                    hasBatchDirtyRegion = true;
                }
                else
                {
                    batchDirtyRegion = unionRect(batchDirtyRegion, screenDirty);
                }
            }

            if (hasBatchDirtyRegion)
            {
                frameBatch.setDirtyRegion(batchDirtyRegion);
            }

            if (!frameBatch.ops().empty() &&
                !m_presentBackend->submitWindowSurfaceBatch(frameBatch, frameBlits.data(), frameBlits.size()))
            {
                for (QC::usize i = 0; i < wm.windowCount(); ++i)
                {
                    Window *window = wm.windowAtIndex(i);
                    if (window && window->isVisible())
                    {
                        blitWindowToBackBuffer(m_renderer, window);
                    }
                }
            }
        }

        // Present frame
        const QC::u64 presentStartMs = QDrv::Timer::instance().milliseconds();
        if (m_presentBackend)
        {
            // Present only dirty rectangles when supported.
            static constexpr QC::usize kMaxDirtyRects = 64;
            QC::Rect dirtyRects[kMaxDirtyRects];
            QC::usize dirtyCount = m_dirtyRegions.size();
            m_stats.lastPresentedDirtyRectCount = dirtyCount;
            m_stats.lastPresentWasFullFrame = false;
            m_stats.lastPresentUsedBatching = false;

            if (dirtyCount == 0)
            {
                // No changes recorded; keep legacy behavior for the software cursor path.
                // (With hardware cursor, we'd have returned above.)
                m_presentBackend->present();
                m_stats.lastPresentWasFullFrame = true;
            }
            else if (dirtyCount > kMaxDirtyRects)
            {
                // Too many rects; cheaper to do a full present.
                m_presentBackend->present();
                m_stats.lastPresentWasFullFrame = true;
            }
            else
            {
                for (QC::usize i = 0; i < dirtyCount; ++i)
                {
                    dirtyRects[i] = m_dirtyRegions[i].rect;
                }

                m_presentBackend->present(dirtyRects, dirtyCount);
                m_stats.lastPresentUsedBatching = (dirtyCount > 1);
            }
        }
        else
        {
            m_framebuffer->swap();
            m_stats.lastPresentedDirtyRectCount = 0;
            m_stats.lastPresentWasFullFrame = true;
            m_stats.lastPresentUsedBatching = false;
        }

        const QC::u64 composeEndMs = QDrv::Timer::instance().milliseconds();
        m_lastComposeTime = (composeEndMs >= composeStartMs) ? (composeEndMs - composeStartMs) : 0;
        m_stats.lastComposeTimeMs = m_lastComposeTime;
        m_stats.lastPresentTimeMs = (composeEndMs >= presentStartMs) ? (composeEndMs - presentStartMs) : 0;
        if (m_stats.lastPresentTimeMs > m_stats.maxPresentTimeMs)
            m_stats.maxPresentTimeMs = m_stats.lastPresentTimeMs;

        // Low-noise latency instrumentation: measure time from last input dispatch to this present.
        // This helps distinguish "event backlog" vs "render/present" lag.
        {
            static QC::u64 s_lastLoggedInputTs = 0;
            auto &wm = WindowManager::instance();
            const QC::u64 inputTs = wm.lastInputTimestamp();
            if (inputTs != 0 && inputTs != s_lastLoggedInputTs)
            {
                const QC::u64 inputMs = wm.lastInputMs();
                const QC::u64 sinceInputMs = (composeEndMs >= inputMs) ? (composeEndMs - inputMs) : 0;
                m_stats.lastInputToPresentMs = sinceInputMs;
                if (sinceInputMs > m_stats.maxInputToPresentMs)
                    m_stats.maxInputToPresentMs = sinceInputMs;

                // Only log when latency is noticeable.
                if (QAIOS_MOUSE_INFO_LOGS && sinceInputMs >= 100)
                {
                    const QC::u64 presentMs = (composeEndMs >= presentStartMs) ? (composeEndMs - presentStartMs) : 0;
                    QC_LOG_INFO("QWLatency",
                                "Input->Present dt=%lums (compose=%lums present=%lums dirty=%lu) ts=%llu",
                                static_cast<unsigned long>(sinceInputMs),
                                static_cast<unsigned long>(m_lastComposeTime),
                                static_cast<unsigned long>(presentMs),
                                static_cast<unsigned long>(m_dirtyRegions.size()),
                                static_cast<unsigned long long>(inputTs));
                }

                s_lastLoggedInputTs = inputTs;
            }
        }

        m_frameCount++;
        m_stats.frameCount = m_frameCount;
        clearDirtyRegions();
        m_stats.dirtyRegionCount = 0;
    }

    void Compositor::syncHardwareCursorPosition()
    {
        if (!m_presentBackend || !m_presentBackend->hasHardwareCursor() || !m_framebuffer)
            return;

        auto &wm = WindowManager::instance();
        const Point mousePos = wm.mousePosition();

        m_presentBackend->setCursorVisible(true);

        QC::i32 cx = mousePos.x - m_cursorHotspotX;
        QC::i32 cy = mousePos.y - m_cursorHotspotY;
        if (cx < 0)
            cx = 0;
        if (cy < 0)
            cy = 0;
        const QC::i32 maxX = static_cast<QC::i32>(m_framebuffer->width() > 0 ? (m_framebuffer->width() - 1) : 0);
        const QC::i32 maxY = static_cast<QC::i32>(m_framebuffer->height() > 0 ? (m_framebuffer->height() - 1) : 0);
        if (cx > maxX)
            cx = maxX;
        if (cy > maxY)
            cy = maxY;

        m_presentBackend->setCursorPosition(
            static_cast<QC::u16>(cx),
            static_cast<QC::u16>(cy));
    }

    bool Compositor::supportsRectCopy() const
    {
        return m_framebuffer && m_presentBackend && m_presentBackend->supportsRectCopy();
    }

    bool Compositor::copyRectInBackBuffer(const Rect &src, const Rect &dst)
    {
        if (!m_framebuffer)
            return false;
        if (src.width == 0 || src.height == 0 || dst.width != src.width || dst.height != src.height)
            return false;

        return m_framebuffer->copyBackBufferRect(src, dst);
    }

    bool Compositor::rectCopy(const Rect &src, const Rect &dst)
    {
        if (!supportsRectCopy())
            return false;
        if (src.width == 0 || src.height == 0 || dst.width != src.width || dst.height != src.height)
            return false;

        if (!copyRectInBackBuffer(src, dst))
            return false;

        m_presentBackend->rectCopy(src, dst);
        return true;
    }

    void Compositor::composeWindow(Window *window)
    {
        if (!window || !m_renderer)
            return;

        // Draw window decorations
        if (window->flags() & WindowFlags::HasBorder)
        {
            drawWindowDecorations(window);
        }

        // Blit window content
        Rect bounds = window->bounds();
        if (window->buffer())
        {
            m_renderer->blit(
                bounds.x, bounds.y,
                window->buffer(),
                window->bufferWidth(),
                window->bufferHeight(),
                window->bufferPitchBytes());
        }
    }

    void Compositor::invalidate(const Rect &rect)
    {
        DirtyRegion region;
        region.rect = rect;
        region.merged = false;
        m_dirtyRegions.push_back(region);
    }

    void Compositor::invalidateAll()
    {
        if (m_framebuffer)
        {
            invalidate(Rect{0, 0, m_framebuffer->width(), m_framebuffer->height()});
        }
    }

    void Compositor::clearDirtyRegions()
    {
        m_dirtyRegions.clear();
    }

    void Compositor::setEffect(CompositionEffect effect, bool enabled)
    {
        QC::u32 bit = 1u << static_cast<QC::u32>(effect);
        if (enabled)
        {
            m_effects |= bit;
        }
        else
        {
            m_effects &= ~bit;
        }
    }

    bool Compositor::hasEffect(CompositionEffect effect) const
    {
        QC::u32 bit = 1u << static_cast<QC::u32>(effect);
        return (m_effects & bit) != 0;
    }

    void Compositor::drawWindowDecorations(Window *window)
    {
        if (!window || !m_renderer)
            return;

        Rect bounds = window->bounds();

        // Draw border
        if (window->flags() & WindowFlags::HasBorder)
        {
            drawBorder(window);
        }

        // Draw title bar
        if (window->flags() & WindowFlags::HasTitle)
        {
            drawTitleBar(window);
        }

        // Draw shadow
        if (hasEffect(CompositionEffect::Shadow))
        {
            drawShadow(window);
        }
    }

    void Compositor::drawTitleBar(Window *window)
    {
            if (!window || !m_renderer)
                return;

            Rect bounds = window->bounds();
            constexpr QC::u32 kTitleBarHeight = 24;
        
            // Title bar area
            Rect titleBar{bounds.x, bounds.y, bounds.width, kTitleBarHeight};
        
            // Get focus state
            auto &wm = WindowManager::instance();
            const bool isFocused = (window == wm.focusedWindow());
        
            // Draw title bar background (darker if not focused)
            Color titleBgColor = isFocused ? 
                Color::fromRGB(70, 130, 180) :
                Color::fromRGB(100, 100, 100);
            m_renderer->fillRect(titleBar, titleBgColor);
        
            // Draw title bar border
            Color titleBorderColor = isFocused ?
                Color::fromRGB(100, 160, 210) :
                Color::fromRGB(130, 130, 130);
            m_renderer->drawRect(titleBar, titleBorderColor);
        
            // Draw title text (simple for now: just window title)
            const char *title = window->title();
            if (title && *title)
            {
                m_renderer->drawString(bounds.x + 4, bounds.y + 6, title,
                                     Color::fromRGB(255, 255, 255));
            }
    }

    void Compositor::drawBorder(Window *window)
    {
        if (!window || !m_renderer)
            return;

        Rect bounds = window->bounds();
        
            // Get focus state for border color
            auto &wm = WindowManager::instance();
            const bool isFocused = (window == wm.focusedWindow());
        
            Color borderColor = isFocused ?
                Color::fromRGB(100, 160, 210) :
                Color::fromRGB(100, 100, 100);
        
            // Draw outline only (not filled)
            m_renderer->drawRect(bounds, borderColor);
    }

    void Compositor::drawShadow(Window *window)
    {
            if (!window || !m_renderer)
                return;

            Rect bounds = window->bounds();
            constexpr QC::i32 kShadowOffset = 3;
            constexpr QC::i32 kShadowWidth = 4;
        
            Color shadowColor = Color::fromRGB(0, 0, 0);
        
            // Shadow on right edge
            Rect rightShadow{
                bounds.x + static_cast<QC::i32>(bounds.width),
                bounds.y + kShadowOffset,
                static_cast<QC::u32>(kShadowWidth),
                bounds.height
            };
            m_renderer->fillRect(rightShadow, shadowColor);
        
            // Shadow on bottom edge
            Rect bottomShadow{
                bounds.x + kShadowOffset,
                bounds.y + static_cast<QC::i32>(bounds.height),
                bounds.width,
                static_cast<QC::u32>(kShadowWidth)
            };
            m_renderer->fillRect(bottomShadow, shadowColor);
    }

    void Compositor::setWallpaper(const QC::u32 *pixels, QC::u32 width, QC::u32 height)
    {
        if (m_wallpaper)
        {
            QK::Memory::Heap::instance().free(m_wallpaper);
            m_wallpaper = nullptr;
        }

        if (pixels && width > 0 && height > 0)
        {
            QC::usize size = width * height * sizeof(QC::u32);
            m_wallpaper = static_cast<QC::u32 *>(QK::Memory::Heap::instance().allocate(size));
            if (m_wallpaper)
            {
                memcpy(m_wallpaper, pixels, size);
                m_wallpaperWidth = width;
                m_wallpaperHeight = height;
            }
        }
    }

    void Compositor::drawDesktop()
    {
        if (!m_renderer)
            return;

        if (m_wallpaper)
        {
                // If wallpaper doesn't match screen size, tile it;
                // otherwise blit as-is (wallpaper should be pre-scaled by caller).
                if (m_framebuffer)
                {
                    const QC::u32 screenWidth = m_framebuffer->width();
                    const QC::u32 screenHeight = m_framebuffer->height();
                
                    if (m_wallpaperWidth == screenWidth && m_wallpaperHeight == screenHeight)
                    {
                        // Exact match: blit directly
                        m_renderer->blit(0, 0, m_wallpaper, m_wallpaperWidth, m_wallpaperHeight,
                                       m_wallpaperWidth * sizeof(QC::u32));
                    }
                    else
                    {
                        // Tile the wallpaper to fill the screen
                        for (QC::u32 y = 0; y < screenHeight; y += m_wallpaperHeight)
                        {
                            for (QC::u32 x = 0; x < screenWidth; x += m_wallpaperWidth)
                            {
                                m_renderer->blit(static_cast<QC::i32>(x), static_cast<QC::i32>(y),
                                               m_wallpaper, m_wallpaperWidth, m_wallpaperHeight,
                                               m_wallpaperWidth * sizeof(QC::u32));
                            }
                        }
                    }
                }
        }
        else
        {
            // Default desktop background
            m_renderer->clear(Color::fromRGB(0, 128, 128));
        }
    }

    void Compositor::setCursor(const QC::u32 *pixels, QC::u32 width, QC::u32 height,
                               QC::i32 hotspotX, QC::i32 hotspotY)
    {
        if (m_cursorPixels)
        {
            QK::Memory::Heap::instance().free(m_cursorPixels);
            m_cursorPixels = nullptr;
        }
        if (m_cursorBackground)
        {
            QK::Memory::Heap::instance().free(m_cursorBackground);
            m_cursorBackground = nullptr;
        }

        if (pixels && width > 0 && height > 0)
        {
            QC::usize size = width * height * sizeof(QC::u32);
            m_cursorPixels = static_cast<QC::u32 *>(QK::Memory::Heap::instance().allocate(size));
            m_cursorBackground = static_cast<QC::u32 *>(QK::Memory::Heap::instance().allocate(size));
            if (m_cursorPixels)
            {
                memcpy(m_cursorPixels, pixels, size);
                m_cursorWidth = width;
                m_cursorHeight = height;
                m_cursorHotspotX = hotspotX;
                m_cursorHotspotY = hotspotY;

                if (m_presentBackend && m_presentBackend->hasHardwareCursor())
                {
                    QC::i32 safeHotspotX = hotspotX;
                    QC::i32 safeHotspotY = hotspotY;
                    if (safeHotspotX < 0)
                        safeHotspotX = 0;
                    if (safeHotspotY < 0)
                        safeHotspotY = 0;
                    if (safeHotspotX >= static_cast<QC::i32>(width))
                        safeHotspotX = static_cast<QC::i32>(width) - 1;
                    if (safeHotspotY >= static_cast<QC::i32>(height))
                        safeHotspotY = static_cast<QC::i32>(height) - 1;

                    m_presentBackend->setCursorImage(
                        m_cursorPixels,
                        static_cast<QC::u16>(width),
                        static_cast<QC::u16>(height),
                        static_cast<QC::u16>(safeHotspotX),
                        static_cast<QC::u16>(safeHotspotY));
                }
            }
        }
    }

    bool Compositor::hasHardwareCursor() const
    {
        return m_presentBackend && m_presentBackend->hasHardwareCursor();
    }

    Rect Compositor::cursorBoundsAt(QC::i32 x, QC::i32 y) const
    {
        if (!m_cursorPixels || m_cursorWidth == 0 || m_cursorHeight == 0)
            return Rect{0, 0, 0, 0};

        return Rect{x - m_cursorHotspotX,
                    y - m_cursorHotspotY,
                    m_cursorWidth,
                    m_cursorHeight};
    }

    void Compositor::drawCursor(QC::i32 x, QC::i32 y)
    {
        if (!m_cursorPixels || !m_renderer)
            return;

        QC::i32 drawX = x - m_cursorHotspotX;
        QC::i32 drawY = y - m_cursorHotspotY;

        m_renderer->blitAlpha(drawX, drawY, m_cursorPixels,
                              m_cursorWidth, m_cursorHeight,
                              m_cursorWidth * sizeof(QC::u32));
    }

    void Compositor::saveCursorBackground(QC::i32 x, QC::i32 y)
    {
        (void)x;
        (void)y;
        // TODO: Save background under cursor for restoration
    }

    void Compositor::restoreCursorBackground()
    {
        // TODO: Restore saved background
    }

    void Compositor::mergeDirtyRegions()
    {
        if (m_dirtyRegions.size() < 2)
            return;

        auto intersectsOrAdjacent = [](const Rect &a, const Rect &b) -> bool
        {
            const QC::i32 ax1 = a.x;
            const QC::i32 ay1 = a.y;
            const QC::i32 ax2 = a.x + static_cast<QC::i32>(a.width);
            const QC::i32 ay2 = a.y + static_cast<QC::i32>(a.height);

            const QC::i32 bx1 = b.x;
            const QC::i32 by1 = b.y;
            const QC::i32 bx2 = b.x + static_cast<QC::i32>(b.width);
            const QC::i32 by2 = b.y + static_cast<QC::i32>(b.height);

            // Merge only when rects overlap or touch edges.
            return !(ax2 < bx1 || bx2 < ax1 || ay2 < by1 || by2 < ay1);
        };

        QC::Vector<DirtyRegion> merged;
        for (QC::usize i = 0; i < m_dirtyRegions.size(); ++i)
        {
            if (m_dirtyRegions[i].merged)
                continue;

            Rect current = m_dirtyRegions[i].rect;
            bool changed = true;

            while (changed)
            {
                changed = false;
                for (QC::usize j = 0; j < m_dirtyRegions.size(); ++j)
                {
                    if (i == j || m_dirtyRegions[j].merged)
                        continue;

                    const Rect &other = m_dirtyRegions[j].rect;
                    if (!intersectsOrAdjacent(current, other))
                        continue;

                    const QC::i32 x1 = (current.x < other.x) ? current.x : other.x;
                    const QC::i32 y1 = (current.y < other.y) ? current.y : other.y;
                    const QC::i32 x2 = (current.x + static_cast<QC::i32>(current.width) >
                                        other.x + static_cast<QC::i32>(other.width))
                                           ? (current.x + static_cast<QC::i32>(current.width))
                                           : (other.x + static_cast<QC::i32>(other.width));
                    const QC::i32 y2 = (current.y + static_cast<QC::i32>(current.height) >
                                        other.y + static_cast<QC::i32>(other.height))
                                           ? (current.y + static_cast<QC::i32>(current.height))
                                           : (other.y + static_cast<QC::i32>(other.height));

                    current = Rect{x1, y1,
                                   static_cast<QC::u32>(x2 - x1),
                                   static_cast<QC::u32>(y2 - y1)};
                    m_dirtyRegions[j].merged = true;
                    changed = true;
                }
            }

            merged.push_back(DirtyRegion{current, false});
        }

        m_dirtyRegions = static_cast<QC::Vector<DirtyRegion> &&>(merged);
    }

} // namespace QW
