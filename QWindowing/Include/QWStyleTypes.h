#pragma once

// QWindowing Style types
// Namespace: QW

#include "QCColor.h"
#include "QCGeometry.h"
#include "QCTypes.h"
#include "QGPainter.h"

namespace QG
{
    struct ImageSurface;
}

namespace QW
{

    enum class ButtonContentMode : QC::u8
    {
        Auto = 0,
        Text,
        Icon,
        TextAndIcon
    };

    enum class ButtonVariant : QC::u8
    {
        Standard = 0,
        Borderless,
        Icon,
        Pill,
        Toolbar,
        Ghost,
        Compact
    };

    enum class ButtonRole : QC::u8
    {
        Default = 0,
        Accent,
        Sidebar,
        SidebarSelected,
        Taskbar,
        TaskbarActive,
        Destructive,
        Count
    };

    struct StyleSnapshot
    {
        struct VistaThemeConfig
        {
            QC::Color windowBackground = QC::Color::windowBackground();
            QC::Color windowBorder = QC::Color::buttonShadow();
            QC::Color sidebarBackground = QC::Color::buttonFace();
            QC::Color sidebarHover = QC::Color::buttonFace().lighter(0.2f);
            QC::Color sidebarSelected = QC::Color::activeCaption();
            QC::Color sidebarText = QC::Color::controlText();
            QC::Color topBarDivider = QC::Color::buttonShadow();
            QC::Color taskbarBackground = QC::Color::buttonFace();
            QC::Color taskbarHover = QC::Color::buttonFace().lighter(0.15f);
            QC::Color taskbarText = QC::Color::controlText();
            QC::Color taskbarActiveWindow = QC::Color::activeCaption();
            QC::Color desktopBackgroundTop = QC::Color::windowBackground();
            QC::Color desktopBackgroundBottom = QC::Color::windowBackground();
            QC::Color windowShadow = QC::Color::transparent();
            QC::Color accent = QC::Color::activeCaption();
        };

        struct Palette
        {
            QC::Color windowBackground = QC::Color::windowBackground();
            QC::Color windowBorderActive = QC::Color::activeCaption();
            QC::Color windowBorderInactive = QC::Color::inactiveCaption();
            QC::Color panelBackground = QC::Color::windowBackground();
            QC::Color buttonFace = QC::Color::buttonFace();
            QC::Color buttonHover = QC::Color::buttonFace().lighter(0.2f);
            QC::Color buttonPressed = QC::Color::buttonFace().darker(0.2f);
            QC::Color buttonBorder = QC::Color::buttonShadow();
            QC::Color controlText = QC::Color::controlText();
            QC::Color accent = QC::Color::activeCaption();
            QC::Color desktopBackgroundTop = QC::Color::windowBackground();
            QC::Color desktopBackgroundBottom = QC::Color::windowBackground();
        } palette;

        struct Metrics
        {
            QC::u32 windowCornerRadius = 4;
            QC::u32 buttonCornerRadius = 4;
            QC::u32 borderWidth = 1;
            QC::u32 shadowSize = 6;
            QC::i32 buttonHoverLift = 0;
            QC::i32 buttonPressDepth = 1;
            QC::i32 buttonTextHoverOffset = 0;
            QC::i32 buttonTextPressedOffset = 1;
            QC::i32 buttonShadowOffsetX = 0;
            QC::i32 buttonShadowOffsetY = 2;
            QC::u32 buttonShadowSoftness = 8;
            QC::u32 focusRingWidth = 2;
            float textScale = 1.0f;
        } metrics;

        struct ButtonStyle
        {
            struct MaterialLayerSet
            {
                QC::Color glossTop = QC::Color::transparent();
                QC::Color glossBottom = QC::Color::transparent();
                QC::Color shadeTop = QC::Color::transparent();
                QC::Color shadeBottom = QC::Color::transparent();
            };

            struct MaterialLayers
            {
                bool enabled = false;
                MaterialLayerSet normal;
                MaterialLayerSet hovered;
                MaterialLayerSet pressed;
            } materialLayers;

            QC::Color fillNormal = QC::Color::buttonFace();
            QC::Color fillHover = QC::Color::buttonFace().lighter(0.15f);
            QC::Color fillPressed = QC::Color::buttonFace().darker(0.2f);
            QC::Color fillDisabled = QC::Color::buttonFace().darker(0.35f);
            QC::Color text = QC::Color::controlText();
            QC::Color textDisabled = QC::Color::controlText().darker(0.4f);
            QC::Color border = QC::Color::buttonShadow();
            QC::Color borderDisabled = QC::Color::buttonShadow();
            QC::Color glow = QC::Color::transparent();
            QC::Color overlayHover = QC::Color::transparent();
            QC::Color overlayPressed = QC::Color::transparent();
            QC::Color outline = QC::Color::transparent();
            QC::Color outlineHover = QC::Color::transparent();
            QC::Color outlinePressed = QC::Color::transparent();
            QC::Color focusOutline = QC::Color::transparent();
            QC::u32 borderWidth = 1;
            QC::u32 cornerRadius = 4;
            bool glass = false;
            bool castsShadow = true;
        };

        ButtonStyle buttonStyles[static_cast<QC::u32>(ButtonRole::Count)];

        static StyleSnapshot makeVista(const VistaThemeConfig &config);
        static const StyleSnapshot &fallback();
    };

    struct FrameContext
    {
        QC::Rect surfaceBounds;
        QG::IPainter *painter = nullptr;
    };

    struct WindowPaintArgs
    {
        enum class Surface : QC::u8
        {
            Window,
            Desktop
        } surface = Surface::Window;

        QC::Rect bounds;
        const char *title = nullptr;
        bool active = true;
        bool focused = false;
    };

    enum class PanelBorderStyle : QC::u8
    {
        None,
        Flat,
        Raised,
        Sunken,
        Etched
    };

    struct PanelPaintArgs
    {
        QC::Rect bounds;
        bool sunken = false;
        bool hasBackgroundOverride = false;
        QC::Color backgroundColor = QC::Color::transparent();
        PanelBorderStyle borderStyle = PanelBorderStyle::Flat;
        QC::u32 borderWidth = 1;
        bool hasBorderColorOverride = false;
        QC::Color borderColor = QC::Color::transparent();
    };

    struct ButtonPaintArgs
    {
        enum class State : QC::u8
        {
            Normal,
            Hovered,
            Pressed,
            Disabled
        } state = State::Normal;

        QC::Rect bounds;
        const char *text = nullptr;
        const QG::ImageSurface *icon = nullptr;
        ButtonContentMode contentMode = ButtonContentMode::Auto;
        ButtonVariant variant = ButtonVariant::Standard;
        bool defaultButton = false;
        bool borderless = false;
        ButtonRole role = ButtonRole::Default;
        QC::u32 cornerRadius = 0; // 0 = use style/role default
    };

    const char *buttonRoleToString(ButtonRole role);
    bool buttonRoleFromString(const char *text, ButtonRole *outRole);

} // namespace QW
