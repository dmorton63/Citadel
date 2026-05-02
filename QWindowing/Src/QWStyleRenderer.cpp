// QWindowing StyleRenderer implementation

#include "QWStyleRenderer.h"
#include "QGPainter.h"
#include "QCUIStyle.h"
#include "QG/Image.h"

namespace QW
{

    namespace
    {
        inline QC::u32 umin(QC::u32 a, QC::u32 b)
        {
            return a < b ? a : b;
        }

        inline QC::u32 umax(QC::u32 a, QC::u32 b)
        {
            return a > b ? a : b;
        }

        inline QC::u32 isqrt_u32(QC::u32 x)
        {
            QC::u32 op = x;
            QC::u32 res = 0;
            QC::u32 one = 1u << 30;
            while (one > op)
            {
                one >>= 2;
            }
            while (one != 0)
            {
                if (op >= res + one)
                {
                    op -= res + one;
                    res += one << 1;
                }
                res >>= 1;
                one >>= 2;
            }
            return res;
        }

        inline bool spanForRoundedRow(const QC::Rect &rect, QC::u32 radius, QC::i32 y, QC::i32 &x1, QC::i32 &x2)
        {
            if (rect.width == 0 || rect.height == 0)
                return false;

            const QC::i32 left = rect.x;
            const QC::i32 right = rect.x + static_cast<QC::i32>(rect.width) - 1;

            if (radius == 0)
            {
                x1 = left;
                x2 = right;
                return x1 <= x2;
            }

            const QC::u32 maxR = umin(rect.width / 2, rect.height / 2);
            if (radius > maxR)
                radius = maxR;
            if (radius == 0)
            {
                x1 = left;
                x2 = right;
                return x1 <= x2;
            }

            const QC::i32 top = rect.y;
            const QC::i32 bottom = rect.y + static_cast<QC::i32>(rect.height) - 1;
            const QC::i32 r = static_cast<QC::i32>(radius);

            QC::i32 inset = 0;
            const QC::i32 topBandEnd = top + r - 1;
            const QC::i32 bottomBandStart = bottom - r + 1;
            if (y <= topBandEnd || y >= bottomBandStart)
            {
                const QC::i32 centerY = (y <= topBandEnd)
                                          ? (top + r)
                                          : (bottom - r + 1);
                const QC::u32 diameter = radius * 2u;
                const QC::i32 dyScaled = ((y * 2) + 1) - (centerY * 2);
                const QC::u32 dyScaledAbs = static_cast<QC::u32>(dyScaled < 0 ? -dyScaled : dyScaled);
                const QC::u32 diameter2 = diameter * diameter;
                const QC::u32 dy2 = dyScaledAbs * dyScaledAbs;
                const QC::u32 inside = (dy2 >= diameter2) ? 0 : (diameter2 - dy2);
                const QC::u32 maxDxScaled = isqrt_u32(inside);
                inset = static_cast<QC::i32>((diameter - maxDxScaled) / 2u);
                if (inset < 0)
                    inset = 0;
                if (inset > r)
                    inset = r;
            }

            x1 = left + inset;
            x2 = right - inset;
            return x1 <= x2;
        }

        inline void drawRoundedGradientV(QG::GraphicsBackend *backend,
                                         const QC::Rect &shapeRect,
                                         QC::u32 radius,
                                         const QC::Rect &areaRect,
                                         QC::Color from,
                                         QC::Color to)
        {
            if (!backend)
                return;
            if (shapeRect.width == 0 || shapeRect.height == 0)
                return;
            if (areaRect.width == 0 || areaRect.height == 0)
                return;

            const QC::i32 yStart = areaRect.y;
            const QC::i32 yEnd = areaRect.y + static_cast<QC::i32>(areaRect.height);
            const QC::f32 denom = areaRect.height > 1 ? static_cast<QC::f32>(areaRect.height - 1) : 1.0f;
            const QC::i32 areaLeft = areaRect.x;
            const QC::i32 areaRight = areaRect.x + static_cast<QC::i32>(areaRect.width) - 1;

            for (QC::i32 y = yStart; y < yEnd; ++y)
            {
                const QC::i32 rel = y - areaRect.y;
                const QC::f32 t = denom == 0.0f ? 0.0f : static_cast<QC::f32>(rel) / denom;
                const QC::Color color = QC::Color::lerp(from, to, t);
                if (color.a == 0)
                    continue;

                QC::i32 x1 = 0, x2 = -1;
                if (!spanForRoundedRow(shapeRect, radius, y, x1, x2))
                    continue;

                if (x1 < areaLeft)
                    x1 = areaLeft;
                if (x2 > areaRight)
                    x2 = areaRight;
                if (x1 > x2)
                    continue;

                backend->drawRect(QC::Rect{x1, y, static_cast<QC::u32>(x2 - x1 + 1), 1},
                                  color,
                                  QC::Color::transparent(),
                                  0);
            }
        }

        inline QC::u32 roleIndex(ButtonRole role)
        {
            QC::u32 idx = static_cast<QC::u32>(role);
            if (idx >= static_cast<QC::u32>(ButtonRole::Count))
            {
                idx = 0;
            }
            return idx;
        }

        inline const StyleSnapshot::ButtonStyle &buttonStyle(const StyleSnapshot &snapshot, ButtonRole role)
        {
            return snapshot.buttonStyles[roleIndex(role)];
        }

        inline QC::Rect insetRect(const QC::Rect &rect, QC::u32 inset)
        {
            if (inset == 0)
            {
                return rect;
            }

            QC::Rect result = rect;
            const QC::u32 insetAmount = inset * 2;
            result.x += static_cast<QC::i32>(inset);
            result.y += static_cast<QC::i32>(inset);
            result.width = (result.width > insetAmount) ? (result.width - insetAmount) : 0;
            result.height = (result.height > insetAmount) ? (result.height - insetAmount) : 0;
            return result;
        }

        inline QC::Rect expandRect(const QC::Rect &rect, QC::i32 amount)
        {
            if (amount <= 0)
            {
                return rect;
            }

            QC::Rect result = rect;
            result.x -= amount;
            result.y -= amount;

            const QC::i64 width = static_cast<QC::i64>(result.width) + static_cast<QC::i64>(amount) * 2;
            const QC::i64 height = static_cast<QC::i64>(result.height) + static_cast<QC::i64>(amount) * 2;
            result.width = width > 0 ? static_cast<QC::u32>(width) : 0;
            result.height = height > 0 ? static_cast<QC::u32>(height) : 0;
            return result;
        }

        inline void drawFlatPanelBorder(QG::GraphicsBackend *backend,
                                        const QC::Rect &bounds,
                                        QC::Color color,
                                        QC::u32 width)
        {
            if (!backend || width == 0)
            {
                return;
            }

            backend->drawRect(bounds,
                              QC::Color::transparent(),
                              color,
                              width);
        }

        void drawPanelBorder(const PanelPaintArgs &args,
                             QG::GraphicsBackend *backend,
                             QG::IPainter *painter,
                             QC::Color borderColor,
                             QC::u32 borderWidth)
        {
            if (args.borderStyle == PanelBorderStyle::None || borderWidth == 0)
            {
                return;
            }

            if (args.borderStyle == PanelBorderStyle::Flat || !painter)
            {
                drawFlatPanelBorder(backend, args.bounds, borderColor, borderWidth);
                return;
            }

            const QC::Color light = borderColor.lighter(0.35f);
            const QC::Color dark = borderColor.darker(0.4f);

            switch (args.borderStyle)
            {
            case PanelBorderStyle::Raised:
                painter->drawRaisedBorder(args.bounds, light, dark, borderWidth);
                break;
            case PanelBorderStyle::Sunken:
                painter->drawSunkenBorder(args.bounds, light, dark, borderWidth);
                break;
            case PanelBorderStyle::Etched:
                painter->drawEtchedBorder(args.bounds, light, dark);
                break;
            default:
                drawFlatPanelBorder(backend, args.bounds, borderColor, borderWidth);
                break;
            }
        }
    } // namespace

    const StyleSnapshot &StyleSnapshot::fallback()
    {
        static StyleSnapshot snapshot;
        return snapshot;
    }

    StyleSnapshot StyleSnapshot::makeVista(const VistaThemeConfig &config)
    {
        StyleSnapshot snapshot;

        snapshot.palette.windowBackground = config.windowBackground;
        snapshot.palette.windowBorderActive = config.windowBorder;
        snapshot.palette.windowBorderInactive = config.windowBorder.darker();
        snapshot.palette.panelBackground = config.sidebarBackground;
        snapshot.palette.buttonFace = config.sidebarBackground;
        snapshot.palette.buttonHover = config.sidebarHover;
        snapshot.palette.buttonPressed = config.sidebarBackground.darker(0.6f);
        snapshot.palette.buttonBorder = config.topBarDivider;
        snapshot.palette.controlText = config.sidebarText;
        snapshot.palette.accent = config.accent;
        snapshot.palette.desktopBackgroundTop = config.desktopBackgroundTop;
        snapshot.palette.desktopBackgroundBottom = config.desktopBackgroundBottom;

        snapshot.metrics.windowCornerRadius = (QC::currentUIStyle() == QC::UIStyle::Vista) ? 6 : 4;
        snapshot.metrics.buttonCornerRadius = snapshot.metrics.windowCornerRadius;
        snapshot.metrics.borderWidth = 1;
        snapshot.metrics.shadowSize = config.windowShadow.a > 0 ? 8 : 0;
        snapshot.metrics.buttonHoverLift = 1;
        snapshot.metrics.buttonPressDepth = 1;
        snapshot.metrics.buttonTextHoverOffset = 0;
        snapshot.metrics.buttonTextPressedOffset = 1;
        snapshot.metrics.buttonShadowOffsetX = 0;
        snapshot.metrics.buttonShadowOffsetY = 2;
        snapshot.metrics.buttonShadowSoftness = 10;
        snapshot.metrics.focusRingWidth = 2;
        snapshot.metrics.textScale = 1.0f;

        const auto configureButton = [&](ButtonRole role, auto &&fn)
        {
            QC::u32 idx = static_cast<QC::u32>(role);
            if (idx >= static_cast<QC::u32>(ButtonRole::Count))
                return;
            auto &spec = snapshot.buttonStyles[idx];
            spec = ButtonStyle{};
            fn(spec);

            if (spec.borderWidth == 0)
                spec.borderWidth = snapshot.metrics.borderWidth;
            spec.cornerRadius = snapshot.metrics.buttonCornerRadius;

            if (spec.fillDisabled.a == 0)
                spec.fillDisabled = spec.fillNormal.darker(0.25f);
            if (spec.textDisabled.a == 0)
                spec.textDisabled = spec.text.withAlpha(180);
            if (spec.borderDisabled.a == 0)
                spec.borderDisabled = spec.border;
        };

        const QC::Color textOnDark(255, 255, 255, 255);
        const QC::Color destructiveBase(200, 64, 64, 255);

        configureButton(ButtonRole::Default, [&](ButtonStyle &spec)
                        {
            spec.fillNormal = config.windowBackground;
            spec.fillHover = config.windowBackground.lighter(0.05f);
            spec.fillPressed = config.windowBackground.darker(0.15f);
            spec.text = config.sidebarText;
            spec.border = config.topBarDivider;
            spec.glow = QC::Color::transparent();
            spec.glass = false;
            spec.overlayHover = config.windowBackground.lighter(0.2f).withAlpha(45);
            spec.overlayPressed = config.windowBackground.darker(0.25f).withAlpha(70);
            spec.outline = config.topBarDivider.withAlpha(140);
            spec.outlineHover = config.topBarDivider.withAlpha(200);
            spec.outlinePressed = config.topBarDivider.darker(0.2f).withAlpha(220);
            spec.focusOutline = config.accent.withAlpha(200);
            spec.castsShadow = false; });

        configureButton(ButtonRole::Accent, [&](ButtonStyle &spec)
                        {
            QC::Color a = config.accent;
            spec.fillNormal = a;
            spec.fillHover = a.lighter(0.08f);
            spec.fillPressed = a.darker(0.2f);
            spec.text = textOnDark;
            spec.border = a.darker(0.25f);
            spec.glow = config.accent.withAlpha(90);
            spec.glass = true;
            spec.overlayHover = textOnDark.withAlpha(90);
            spec.overlayPressed = a.darker(0.35f).withAlpha(110);
            spec.outline = a.darker(0.35f);
            spec.outlineHover = a.lighter(0.06f);
            spec.outlinePressed = a.darker(0.45f);
            spec.focusOutline = config.windowBorder;
            spec.castsShadow = true; });

        configureButton(ButtonRole::Sidebar, [&](ButtonStyle &spec)
                        {
            spec.fillNormal = config.sidebarBackground;
            spec.fillHover = config.sidebarHover;
            spec.fillPressed = config.sidebarHover.darker(0.15f);
            spec.text = config.sidebarText;
            spec.border = config.topBarDivider;
            spec.glow = QC::Color::transparent();
            spec.glass = false;
            spec.overlayHover = textOnDark.withAlpha(35);
            spec.overlayPressed = config.sidebarHover.darker(0.2f).withAlpha(80);
            spec.outline = config.topBarDivider.withAlpha(90);
            spec.outlineHover = config.topBarDivider.withAlpha(140);
            spec.outlinePressed = config.topBarDivider.darker(0.25f).withAlpha(170);
            spec.focusOutline = config.accent.withAlpha(140);
            spec.castsShadow = false; });

        configureButton(ButtonRole::SidebarSelected, [&](ButtonStyle &spec)
                        {
            QC::Color base = config.sidebarSelected;
            spec.fillNormal = base;
            spec.fillHover = base.lighter(0.1f);
            spec.fillPressed = base.darker(0.2f);
            spec.text = textOnDark;
            spec.border = base.darker(0.3f);
            spec.glow = config.accent.withAlpha(70);
            spec.glass = true;
            spec.overlayHover = textOnDark.withAlpha(60);
            spec.overlayPressed = base.darker(0.35f).withAlpha(110);
            spec.outline = base.darker(0.35f);
            spec.outlineHover = base.lighter(0.05f);
            spec.outlinePressed = base.darker(0.45f);
            spec.focusOutline = base.withAlpha(200);
            spec.castsShadow = true; });

        configureButton(ButtonRole::Taskbar, [&](ButtonStyle &spec)
                        {
            spec.fillNormal = config.taskbarBackground;
            spec.fillHover = config.taskbarHover;
            spec.fillPressed = config.taskbarHover.darker(0.15f);
            spec.text = config.taskbarText;
            spec.border = config.topBarDivider;
            spec.glow = QC::Color::transparent();
            spec.glass = false;
            spec.overlayHover = textOnDark.withAlpha(30);
            spec.overlayPressed = config.taskbarHover.darker(0.25f).withAlpha(70);
            spec.outline = config.topBarDivider.withAlpha(120);
            spec.outlineHover = config.topBarDivider.withAlpha(170);
            spec.outlinePressed = config.topBarDivider.darker(0.2f).withAlpha(200);
            spec.focusOutline = config.accent.withAlpha(180);
            spec.castsShadow = false; });

        configureButton(ButtonRole::TaskbarActive, [&](ButtonStyle &spec)
                        {
            QC::Color base = config.taskbarActiveWindow;
            spec.fillNormal = base;
            spec.fillHover = base.lighter(0.1f);
            spec.fillPressed = base.darker(0.2f);
            spec.text = config.taskbarText;
            spec.border = base.darker(0.3f);
            spec.glow = base.withAlpha(90);
            spec.glass = true;
            spec.overlayHover = textOnDark.withAlpha(70);
            spec.overlayPressed = base.darker(0.35f).withAlpha(110);
            spec.outline = base.darker(0.35f);
            spec.outlineHover = base.lighter(0.06f);
            spec.outlinePressed = base.darker(0.45f);
            spec.focusOutline = config.windowBorder;
            spec.castsShadow = true; });

        configureButton(ButtonRole::Destructive, [&](ButtonStyle &spec)
                        {
            spec.fillNormal = destructiveBase;
            spec.fillHover = destructiveBase.lighter(0.08f);
            spec.fillPressed = destructiveBase.darker(0.25f);
            spec.text = textOnDark;
            spec.border = destructiveBase.darker(0.25f);
            spec.glow = destructiveBase.withAlpha(80);
            spec.glass = true;
            spec.overlayHover = textOnDark.withAlpha(85);
            spec.overlayPressed = destructiveBase.darker(0.35f).withAlpha(120);
            spec.outline = destructiveBase.darker(0.35f);
            spec.outlineHover = destructiveBase.lighter(0.05f);
            spec.outlinePressed = destructiveBase.darker(0.45f);
            spec.focusOutline = destructiveBase.withAlpha(210);
            spec.castsShadow = true; });

        return snapshot;
    }

    StyleRenderer::StyleRenderer()
        : StyleRenderer(nullptr)
    {
    }

    StyleRenderer::StyleRenderer(QG::GraphicsBackend *backend)
        : m_backend(backend),
          m_snapshot(nullptr),
          m_context{},
          m_frameActive(false)
    {
    }

    void StyleRenderer::setBackend(QG::GraphicsBackend *backend)
    {
        if (m_frameActive)
        {
            endFrame();
        }
        m_backend = backend;
    }

    void StyleRenderer::setStyleSnapshot(const StyleSnapshot *snapshot)
    {
        m_snapshot = snapshot;
    }

    bool StyleRenderer::beginFrame(const FrameContext &context)
    {
        m_context = context;
        m_frameActive = m_backend && m_backend->beginFrame();
        return m_frameActive;
    }

    void StyleRenderer::endFrame()
    {
        if (!m_backend || !m_frameActive)
            return;

        m_backend->endFrame();
        m_frameActive = false;
    }

    void StyleRenderer::drawWindowChrome(const WindowPaintArgs &args)
    {
        const StyleSnapshot &styleData = style();
        drawWindowBackground(args, styleData);
        if (args.surface != WindowPaintArgs::Surface::Desktop)
        {
            drawWindowBorder(args, styleData);
        }
    }

    void StyleRenderer::drawPanel(const PanelPaintArgs &args)
    {
        if (!m_backend)
            return;

        const StyleSnapshot &styleData = style();
        QC::Color fill = args.hasBackgroundOverride
                             ? args.backgroundColor
                             : (args.sunken ? styleData.palette.panelBackground.darker(0.1f)
                                            : styleData.palette.panelBackground);
        m_backend->drawRect(args.bounds,
                            fill,
                            QC::Color::transparent(),
                            0);

        const QC::u32 width = (args.borderWidth > 0) ? args.borderWidth : styleData.metrics.borderWidth;
        const QC::Color borderColor = args.hasBorderColorOverride
                                          ? args.borderColor
                                          : styleData.palette.windowBorderInactive;
        drawPanelBorder(args,
                        m_backend,
                        m_context.painter,
                        borderColor,
                        width);
    }

    void StyleRenderer::drawButton(const ButtonPaintArgs &args)
    {
        if (!m_backend)
            return;

        const auto resolveContentMode = [&]() -> ButtonContentMode
        {
            if (args.contentMode != ButtonContentMode::Auto)
                return args.contentMode;

            const bool hasText = (args.text && args.text[0]);
            const bool hasIcon = (args.icon && args.icon->isValid());
            if (hasText && hasIcon)
                return ButtonContentMode::TextAndIcon;
            if (hasIcon)
                return ButtonContentMode::Icon;
            return ButtonContentMode::Text;
        };

        const ButtonContentMode contentMode = resolveContentMode();

        const auto resolveVariant = [&]() -> ButtonVariant
        {
            if (args.variant != ButtonVariant::Standard)
                return args.variant;

            if (args.borderless)
            {
                return contentMode == ButtonContentMode::Icon
                           ? ButtonVariant::Icon
                           : ButtonVariant::Borderless;
            }

            return ButtonVariant::Standard;
        };

        const ButtonVariant variant = resolveVariant();

        const StyleSnapshot &styleData = style();
        const auto &spec = buttonStyle(styleData, args.role);
        const auto &caps = m_backend->capabilities();

        const QC::u32 baseRadius = (args.cornerRadius > 0) ? args.cornerRadius : spec.cornerRadius;

        const bool borderless = args.borderless ||
                    variant == ButtonVariant::Borderless ||
                    variant == ButtonVariant::Icon ||
                    variant == ButtonVariant::Ghost;

        const bool disabled = args.state == ButtonPaintArgs::State::Disabled;
        const bool hovered = args.state == ButtonPaintArgs::State::Hovered;
        const bool pressed = args.state == ButtonPaintArgs::State::Pressed;

        QC::Rect buttonRect = args.bounds;
        if (hovered && styleData.metrics.buttonHoverLift != 0)
        {
            buttonRect.y -= styleData.metrics.buttonHoverLift;
        }
        if (pressed && styleData.metrics.buttonPressDepth != 0)
        {
            buttonRect.y += styleData.metrics.buttonPressDepth;
        }

        QC::u32 effectiveRadius = baseRadius;
        if (variant == ButtonVariant::Pill)
        {
            const QC::u32 maxAxis = umax(buttonRect.width, buttonRect.height);
            effectiveRadius = maxAxis > 0 ? maxAxis : baseRadius;
        }

        if (!borderless && args.defaultButton && spec.focusOutline.a > 0 && styleData.metrics.focusRingWidth > 0)
        {
            const QC::i32 inflate = static_cast<QC::i32>(styleData.metrics.focusRingWidth);
            QC::Rect focusRect = expandRect(buttonRect, inflate);
            const bool focusRounded = effectiveRadius > 0 && caps.supportsRoundedRect;
            if (focusRounded)
            {
                const QC::u32 focusRadius = effectiveRadius + styleData.metrics.focusRingWidth;
                m_backend->drawRoundedRect(focusRect,
                                           focusRadius,
                                           QC::Color::transparent(),
                                           spec.focusOutline,
                                           styleData.metrics.focusRingWidth);
            }
            else
            {
                m_backend->drawRect(focusRect,
                                    QC::Color::transparent(),
                                    spec.focusOutline,
                                    styleData.metrics.focusRingWidth);
            }
        }

        QC::Color fill = spec.fillNormal;
        switch (args.state)
        {
        case ButtonPaintArgs::State::Hovered:
            fill = spec.fillHover;
            break;
        case ButtonPaintArgs::State::Pressed:
            fill = spec.fillPressed;
            break;
        case ButtonPaintArgs::State::Disabled:
            fill = spec.fillDisabled;
            break;
        default:
            break;
        }

        QC::Color borderColor = disabled ? spec.borderDisabled : spec.border;
        QC::Color textColor = disabled ? spec.textDisabled : spec.text;

        const QC::u32 borderWidth = (spec.borderWidth > 0) ? spec.borderWidth : styleData.metrics.borderWidth;
        const bool canRound = effectiveRadius > 0 && caps.supportsRoundedRect;

        // For material-driven glass, avoid a uniform solid border/outline.
        // The "edge" should be conveyed by reflections (inner strokes) and transparency.
        const bool materialGlass = !borderless && spec.materialLayers.enabled && caps.supportsAlpha && !disabled;

        const bool hasShadow = !borderless && !disabled && spec.castsShadow && caps.supportsShadows && spec.glow.a > 0 && styleData.metrics.buttonShadowSoftness > 0;
        if (hasShadow)
        {
            m_backend->drawShadow(buttonRect,
                                  {styleData.metrics.buttonShadowOffsetX, styleData.metrics.buttonShadowOffsetY},
                                  static_cast<QC::i32>(styleData.metrics.buttonShadowSoftness),
                                  spec.glow,
                                  spec.glow.a);
        }

        auto drawShape = [&](const QC::Rect &rect, QC::Color shapeFill, QC::Color shapeBorder, QC::u32 shapeBorderWidth)
        {
            if (canRound)
            {
                m_backend->drawRoundedRect(rect,
                                           effectiveRadius,
                                           shapeFill,
                                           shapeBorder,
                                           shapeBorderWidth);
            }
            else
            {
                m_backend->drawRect(rect,
                                    shapeFill,
                                    shapeBorder,
                                    shapeBorderWidth);
            }
        };

        const auto overlayColorForState = [&]() -> QC::Color
        {
            if (disabled)
                return QC::Color::transparent();
            if (pressed)
                return spec.overlayPressed;
            if (hovered)
                return spec.overlayHover;
            return QC::Color::transparent();
        };

        const auto iconExtentForRect = [&](const QC::Rect &rect) -> QC::i32
        {
            QC::i32 maxIcon = static_cast<QC::i32>(rect.height) - 12;
            if (variant == ButtonVariant::Compact)
                maxIcon -= 2;
            else if (variant == ButtonVariant::Toolbar)
                maxIcon -= 1;

            if (maxIcon < 12)
                maxIcon = static_cast<QC::i32>(rect.height);

            QC::i32 cap = 24;
            if (variant == ButtonVariant::Compact)
                cap = 16;
            else if (variant == ButtonVariant::Toolbar)
                cap = 20;

            if (maxIcon > cap)
                maxIcon = cap;
            return maxIcon;
        };

        const auto contentGap = [&]() -> QC::i32
        {
            if (contentMode != ButtonContentMode::TextAndIcon)
                return 0;

            switch (variant)
            {
            case ButtonVariant::Compact:
                return 4;
            case ButtonVariant::Toolbar:
                return 5;
            default:
                return 6;
            }
        };

        const auto borderlessHighlightPadding = [&](bool hasText, bool hasIcon) -> QC::Point
        {
            QC::i32 padX = (hasText && hasIcon) ? 10 : 8;
            QC::i32 padY = 6;

            if (variant == ButtonVariant::Compact)
            {
                padX = (hasText && hasIcon) ? 8 : 6;
                padY = 4;
            }
            else if (variant == ButtonVariant::Toolbar)
            {
                padX = (hasText && hasIcon) ? 9 : 7;
                padY = 5;
            }

            return QC::Point{padX, padY};
        };

        if (borderless)
        {
            QC::Color overlay = overlayColorForState();
            if (overlay.a > 0)
            {
                const bool hasText = ((contentMode == ButtonContentMode::Text ||
                                       contentMode == ButtonContentMode::TextAndIcon) &&
                                      args.text && args.text[0]);
                const bool hasIcon = ((contentMode == ButtonContentMode::Icon ||
                                       contentMode == ButtonContentMode::TextAndIcon) &&
                                      args.icon && args.icon->isValid());

                QC::Size textSize{0, 0};
                if (hasText && m_context.painter)
                {
                    textSize = m_context.painter->measureText(args.text);
                }

                QC::i32 iconW = 0;
                QC::i32 iconH = 0;
                if (hasIcon)
                {
                    iconW = iconExtentForRect(buttonRect);
                    iconH = iconW;
                }

                const QC::i32 gap = contentGap();
                const QC::i32 contentW = (hasIcon ? iconW : 0) + gap + (hasText ? static_cast<QC::i32>(textSize.width) : 0);
                const QC::i32 contentH = (iconH > static_cast<QC::i32>(textSize.height)) ? iconH : static_cast<QC::i32>(textSize.height);
                QC::i32 contentX = buttonRect.x + (static_cast<QC::i32>(buttonRect.width) - contentW) / 2;
                QC::i32 contentY = buttonRect.y + (static_cast<QC::i32>(buttonRect.height) - contentH) / 2;

                QC::i32 contentYOffset = 0;
                if (hovered && styleData.metrics.buttonTextHoverOffset != 0)
                    contentYOffset -= styleData.metrics.buttonTextHoverOffset;
                if (pressed && styleData.metrics.buttonTextPressedOffset != 0)
                    contentYOffset += styleData.metrics.buttonTextPressedOffset;
                contentY += contentYOffset;

                const QC::Point highlightPadding = borderlessHighlightPadding(hasText, hasIcon);
                QC::Rect highlightRect{
                    contentX - highlightPadding.x,
                    contentY - highlightPadding.y,
                    static_cast<QC::u32>(contentW + (highlightPadding.x * 2)),
                    static_cast<QC::u32>(contentH + (highlightPadding.y * 2))};

                auto clampToButton = [&](const QC::Rect &rect) -> QC::Rect
                {
                    QC::i32 l = rect.x;
                    QC::i32 t = rect.y;
                    QC::i32 r = rect.x + static_cast<QC::i32>(rect.width);
                    QC::i32 b = rect.y + static_cast<QC::i32>(rect.height);

                    const QC::i32 bl = buttonRect.x;
                    const QC::i32 bt = buttonRect.y;
                    const QC::i32 br = buttonRect.x + static_cast<QC::i32>(buttonRect.width);
                    const QC::i32 bb = buttonRect.y + static_cast<QC::i32>(buttonRect.height);

                    if (l < bl)
                        l = bl;
                    if (t < bt)
                        t = bt;
                    if (r > br)
                        r = br;
                    if (b > bb)
                        b = bb;

                    if (r <= l || b <= t)
                        return QC::Rect{0, 0, 0, 0};
                    return QC::Rect{l, t, static_cast<QC::u32>(r - l), static_cast<QC::u32>(b - t)};
                };

                highlightRect = clampToButton(highlightRect);
                if (highlightRect.width > 0 && highlightRect.height > 0)
                {
                    drawShape(highlightRect, overlay, QC::Color::transparent(), 0);
                }
            }
        }
        else
        {
            drawShape(buttonRect,
                      fill,
                      materialGlass ? QC::Color::transparent() : borderColor,
                      materialGlass ? 0 : borderWidth);

            QC::Color overlay = overlayColorForState();
            if (overlay.a > 0)
            {
                drawShape(buttonRect, overlay, QC::Color::transparent(), 0);
            }
        }

        const auto outlineColorForState = [&]() -> QC::Color
        {
            if (pressed && spec.outlinePressed.a > 0)
                return spec.outlinePressed;
            if (hovered && spec.outlineHover.a > 0)
                return spec.outlineHover;
            return spec.outline;
        };

        if (!borderless)
        {
            QC::Color outlineColor = (!disabled && !materialGlass) ? outlineColorForState() : QC::Color::transparent();
            if (outlineColor.a > 0)
            {
                drawShape(buttonRect, QC::Color::transparent(), outlineColor, 1);
            }
        }

        const bool supportsAlpha = !borderless && caps.supportsAlpha && !disabled;
        if (supportsAlpha)
        {
            const QC::u32 inset = borderWidth > 0 ? borderWidth : 1;
            QC::Rect inner = insetRect(buttonRect, inset);
            if (inner.width > 0 && inner.height > 1)
            {
                QC::u32 innerRadius = 0;
                if (canRound && effectiveRadius > inset)
                {
                    innerRadius = effectiveRadius - inset;
                }

                QC::u32 glossHeight = inner.height / 2;
                if (spec.materialLayers.enabled && inner.height >= 6)
                {
                    // A smaller highlight band reads more like a reflection.
                    glossHeight = inner.height / 3;
                }
                if (glossHeight < 2)
                {
                    glossHeight = inner.height;
                }

                QC::Rect glossRect = inner;
                glossRect.height = glossHeight;

                if (spec.materialLayers.enabled)
                {
                    const auto &layers = pressed ? spec.materialLayers.pressed
                                                 : (hovered ? spec.materialLayers.hovered
                                                            : spec.materialLayers.normal);

                    // Edge reflections: subtle top/left highlight + bottom/right shade.
                    // This is what usually sells the "glass" impression.
                    auto pickMoreOpaque = [](QC::Color a, QC::Color b) -> QC::Color
                    {
                        return (a.a >= b.a) ? a : b;
                    };

                    const QC::Color light = pickMoreOpaque(layers.glossTop, layers.glossBottom);
                    const QC::Color dark = pickMoreOpaque(layers.shadeBottom, layers.shadeTop);

                    if (inner.width >= 2 && inner.height >= 2)
                    {
                        const QC::u32 innerInset = inset;
                        QC::u32 innerRadius = 0;
                        if (canRound && effectiveRadius > innerInset)
                        {
                            innerRadius = effectiveRadius - innerInset;
                        }

                        if (light.a > 0)
                        {
                            const QC::i32 xStart = inner.x + static_cast<QC::i32>(innerRadius);
                            const QC::i32 xEnd = inner.x + static_cast<QC::i32>(inner.width) - 1 - static_cast<QC::i32>(innerRadius);
                            const QC::i32 yStart = inner.y + static_cast<QC::i32>(innerRadius);
                            const QC::i32 yEnd = inner.y + static_cast<QC::i32>(inner.height) - 1 - static_cast<QC::i32>(innerRadius);

                            if (xStart <= xEnd)
                            {
                                m_backend->drawRect(QC::Rect{xStart, inner.y, static_cast<QC::u32>(xEnd - xStart + 1), 1}, light, QC::Color::transparent(), 0);
                            }
                            if (yStart <= yEnd)
                            {
                                m_backend->drawRect(QC::Rect{inner.x, yStart, 1, static_cast<QC::u32>(yEnd - yStart + 1)}, light, QC::Color::transparent(), 0);
                            }
                        }
                        if (dark.a > 0)
                        {
                            const QC::i32 xStart = inner.x + static_cast<QC::i32>(innerRadius);
                            const QC::i32 xEnd = inner.x + static_cast<QC::i32>(inner.width) - 1 - static_cast<QC::i32>(innerRadius);
                            const QC::i32 yStart = inner.y + static_cast<QC::i32>(innerRadius);
                            const QC::i32 yEnd = inner.y + static_cast<QC::i32>(inner.height) - 1 - static_cast<QC::i32>(innerRadius);

                            if (xStart <= xEnd)
                            {
                                m_backend->drawRect(QC::Rect{xStart,
                                                            inner.y + static_cast<QC::i32>(inner.height - 1),
                                                            static_cast<QC::u32>(xEnd - xStart + 1),
                                                            1},
                                                  dark,
                                                  QC::Color::transparent(),
                                                  0);
                            }
                            if (yStart <= yEnd)
                            {
                                m_backend->drawRect(QC::Rect{inner.x + static_cast<QC::i32>(inner.width - 1),
                                                            yStart,
                                                            1,
                                                            static_cast<QC::u32>(yEnd - yStart + 1)},
                                                  dark,
                                                  QC::Color::transparent(),
                                                  0);
                            }
                        }
                    }

                    if (layers.glossTop.a > 0 || layers.glossBottom.a > 0)
                    {
                        if (innerRadius > 0)
                        {
                            drawRoundedGradientV(m_backend,
                                                 inner,
                                                 innerRadius,
                                                 glossRect,
                                                 layers.glossTop,
                                                 layers.glossBottom);
                        }
                        else
                        {
                            m_backend->drawGradient(glossRect,
                                                    layers.glossTop,
                                                    layers.glossBottom,
                                                    QG::GradientDirection::Vertical);
                        }
                    }

                    if (inner.height > glossHeight)
                    {
                        QC::Rect shadeRect = inner;
                        QC::u32 shadeHeight = inner.height - glossHeight;
                        shadeRect.y = inner.y + static_cast<QC::i32>(inner.height - shadeHeight);
                        shadeRect.height = shadeHeight;

                        if (layers.shadeTop.a > 0 || layers.shadeBottom.a > 0)
                        {
                            if (innerRadius > 0)
                            {
                                drawRoundedGradientV(m_backend,
                                                     inner,
                                                     innerRadius,
                                                     shadeRect,
                                                     layers.shadeTop,
                                                     layers.shadeBottom);
                            }
                            else
                            {
                                m_backend->drawGradient(shadeRect,
                                                        layers.shadeTop,
                                                        layers.shadeBottom,
                                                        QG::GradientDirection::Vertical);
                            }
                        }
                    }
                }
                else
                {
                    const float glossBoost = spec.glass ? (pressed ? 0.25f : 0.4f) : (pressed ? 0.08f : 0.18f);
                    const QC::u8 glossAlpha = spec.glass ? (pressed ? 140 : 190) : (pressed ? 70 : 110);

                    QC::Color glossTop = fill.lighter(glossBoost).withAlpha(glossAlpha);
                    QC::Color glossBottom = fill.lighter(0.02f).withAlpha(0);
                    if (innerRadius > 0)
                    {
                        drawRoundedGradientV(m_backend,
                                             inner,
                                             innerRadius,
                                             glossRect,
                                             glossTop,
                                             glossBottom);
                    }
                    else
                    {
                        m_backend->drawGradient(glossRect,
                                                glossTop,
                                                glossBottom,
                                                QG::GradientDirection::Vertical);
                    }

                    if (inner.height > glossHeight)
                    {
                        QC::Rect shadeRect = inner;
                        QC::u32 shadeHeight = inner.height - glossHeight;
                        shadeRect.y = inner.y + static_cast<QC::i32>(inner.height - shadeHeight);
                        shadeRect.height = shadeHeight;

                        const float shadeBoost = spec.glass ? 0.35f : 0.18f;
                        QC::u8 shadeAlpha = spec.glass ? 150 : 110;
                        if (pressed)
                        {
                            shadeAlpha = static_cast<QC::u8>(shadeAlpha * 0.8f);
                        }

                        QC::Color shadeTop = fill.withAlpha(0);
                        QC::Color shadeBottom = fill.darker(shadeBoost).withAlpha(shadeAlpha);
                        if (innerRadius > 0)
                        {
                            drawRoundedGradientV(m_backend,
                                                 inner,
                                                 innerRadius,
                                                 shadeRect,
                                                 shadeTop,
                                                 shadeBottom);
                        }
                        else
                        {
                            m_backend->drawGradient(shadeRect,
                                                    shadeTop,
                                                    shadeBottom,
                                                    QG::GradientDirection::Vertical);
                        }
                    }
                }
            }
        }

        if (m_context.painter)
        {
            const bool hasText = ((contentMode == ButtonContentMode::Text ||
                                   contentMode == ButtonContentMode::TextAndIcon) &&
                                  args.text && args.text[0]);
            const bool hasIcon = ((contentMode == ButtonContentMode::Icon ||
                                   contentMode == ButtonContentMode::TextAndIcon) &&
                                  args.icon && args.icon->isValid());
            if (hasText || hasIcon)
            {
                QC::Vector<QC::u32> iconScratchRow;

                QC::Size textSize{0, 0};
                if (hasText)
                {
                    textSize = m_context.painter->measureText(args.text);
                }

                QC::i32 iconW = 0;
                QC::i32 iconH = 0;
                if (hasIcon)
                {
                    iconW = iconExtentForRect(buttonRect);
                    iconH = iconW;
                }

                const QC::i32 gap = contentGap();
                const QC::i32 contentW = (hasIcon ? iconW : 0) + gap + (hasText ? static_cast<QC::i32>(textSize.width) : 0);
                QC::i32 contentX = buttonRect.x + (static_cast<QC::i32>(buttonRect.width) - contentW) / 2;

                QC::i32 contentYOffset = 0;
                if (hovered && styleData.metrics.buttonTextHoverOffset != 0)
                    contentYOffset -= styleData.metrics.buttonTextHoverOffset;
                if (pressed && styleData.metrics.buttonTextPressedOffset != 0)
                    contentYOffset += styleData.metrics.buttonTextPressedOffset;

                if (hasIcon)
                {
                    const QC::i32 iconX = contentX;
                    const QC::i32 iconY = buttonRect.y + (static_cast<QC::i32>(buttonRect.height) - iconH) / 2 + contentYOffset;
                    QG::blitImage(m_context.painter,
                                 *args.icon,
                                 QC::Rect{iconX, iconY, static_cast<QC::u32>(iconW), static_cast<QC::u32>(iconH)},
                                 QG::ImageScaleMode::Fit,
                                 iconScratchRow);
                    contentX += iconW + gap;
                }

                if (hasText)
                {
                    const QC::i32 textX = contentX;
                    const QC::i32 textY = buttonRect.y + (static_cast<QC::i32>(buttonRect.height) - static_cast<QC::i32>(textSize.height)) / 2 + contentYOffset;
                    m_context.painter->drawText(textX, textY, args.text, textColor);
                }
            }
        }
    }

    const StyleSnapshot &StyleRenderer::style() const
    {
        return m_snapshot ? *m_snapshot : StyleSnapshot::fallback();
    }

    void StyleRenderer::drawWindowBackground(const WindowPaintArgs &args,
                                             const StyleSnapshot &styleData)
    {
        if (!m_backend)
            return;

        QC::Color top = styleData.palette.windowBackground;
        QC::Color bottom = top;

        if (args.surface == WindowPaintArgs::Surface::Desktop)
        {
            top = styleData.palette.desktopBackgroundTop;
            bottom = styleData.palette.desktopBackgroundBottom;
        }

        if (top != bottom)
        {
            m_backend->drawGradient(args.bounds,
                                    top,
                                    bottom,
                                    QG::GradientDirection::Vertical);
        }
        else
        {
            m_backend->drawRect(args.bounds,
                                top,
                                QC::Color::transparent(),
                                0);
        }
    }

    void StyleRenderer::drawWindowBorder(const WindowPaintArgs &args,
                                         const StyleSnapshot &styleData)
    {
        if (!m_backend)
            return;

        QC::Color borderColor = args.active ? styleData.palette.windowBorderActive
                                            : styleData.palette.windowBorderInactive;
        m_backend->drawRect(args.bounds,
                            QC::Color::transparent(),
                            borderColor,
                            styleData.metrics.borderWidth);
    }

} // namespace QW
