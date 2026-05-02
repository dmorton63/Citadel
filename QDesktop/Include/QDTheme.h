#pragma once

// QDesktop Theme definition and loading helpers
// Namespace: QD

#include "QCColor.h"
#include "QCJson.h"
#include "QWStyleTypes.h"

namespace QD
{
    enum class ThemeID : QC::u16
    {
        Default = 0,
        Standard,
        Winter,
        Spring,
        Summer,
        Autumn,
        Midnight,
        HighContrast,
        Custom,
        Count
    };

    struct CitadelThemeMetadata
    {
        char name[64] = {};
        char author[64] = {};
        char version[32] = {};
        char description[128] = {};
        bool darkTheme = false;
        bool highContrast = false;
        bool supportsReducedMotion = false;
    };

    struct CitadelStyle
    {
        struct Colors
        {
            QC::Color surfaceDesktop = QC::Color::windowBackground();
            QC::Color surfaceWindow = QC::Color::windowBackground();
            QC::Color surfacePanel = QC::Color::windowBackground();
            QC::Color surfaceSidebar = QC::Color::buttonFace();
            QC::Color surfaceTaskbar = QC::Color::buttonFace();
            QC::Color borderSubtle = QC::Color::buttonShadow();
            QC::Color borderStrong = QC::Color::activeCaption();
            QC::Color shadow = QC::Color::transparent();
            QC::Color textPrimary = QC::Color::controlText();
            QC::Color textSecondary = QC::Color::controlText().darker(0.25f);
            QC::Color textDisabled = QC::Color::controlText().darker(0.4f);
            QC::Color textOnAccent = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
            QC::Color accentPrimary = QC::Color::activeCaption();
            QC::Color accentHover = QC::Color::activeCaption().lighter(0.15f);
            QC::Color accentPressed = QC::Color::activeCaption().darker(0.15f);
            QC::Color success = QC::Color(0x3A, 0x9C, 0x5D, 0xFF);
            QC::Color warning = QC::Color(0xD2, 0x91, 0x2B, 0xFF);
            QC::Color error = QC::Color(0xC8, 0x40, 0x40, 0xFF);
            QC::Color focusRing = QC::Color::activeCaption();
        } colors;

        struct Metrics
        {
            QC::u32 windowCornerRadius = 4;
            QC::u32 panelCornerRadius = 4;
            QC::u32 buttonCornerRadius = 4;
            QC::u32 borderWidth = 1;
            QC::u32 focusRingWidth = 2;
            QC::u32 shadowBlur = 6;
            QC::i32 shadowOffsetX = 0;
            QC::i32 shadowOffsetY = 2;
            float textScale = 1.0f;
        } metrics;

        struct Typography
        {
            char family[48] = {};
            QC::u32 displaySize = 22;
            QC::u32 headingSize = 16;
            QC::u32 bodySize = 12;
            QC::u32 captionSize = 10;
        } typography;

        struct Motion
        {
            QC::u32 hoverDurationMs = 150;
            QC::u32 pressDurationMs = 50;
            QC::u32 enterDurationMs = 200;
            bool reducedMotion = false;
        } motion;

        struct ButtonRoleStyle
        {
            QC::Color fillNormal = QC::Color::buttonFace();
            QC::Color fillHover = QC::Color::buttonFace().lighter(0.15f);
            QC::Color fillPressed = QC::Color::buttonFace().darker(0.2f);
            QC::Color fillDisabled = QC::Color::buttonFace().darker(0.35f);
            QC::Color text = QC::Color::controlText();
            QC::Color border = QC::Color::buttonShadow();
            QC::Color glow = QC::Color::transparent();
            bool glass = false;
            bool castsShadow = true;
        } buttonRoles[static_cast<QC::u32>(QW::ButtonRole::Count)];
    };

    struct CitadelThemeAssets
    {
        struct Icons
        {
            const char *settings = nullptr;
            const char *terminal = nullptr;
            const char *folder = nullptr;
            const char *start = nullptr;
            const char *shutdown = nullptr;
        } icons;

        struct Backgrounds
        {
            const char *desktopPrimary = nullptr;
            const char *desktopSecondary = nullptr;
            const char *lockScreen = nullptr;
        } backgrounds;

        struct Textures
        {
            const char *glassNoise = nullptr;
            const char *panelOverlay = nullptr;
        } textures;

        struct Illustrations
        {
            const char *boot = nullptr;
            const char *setup = nullptr;
            const char *recovery = nullptr;
        } illustrations;
    };

    struct CitadelThemePackage
    {
        ThemeID id = ThemeID::Default;
        CitadelStyle style;
        CitadelThemeAssets assets;
        CitadelThemeMetadata metadata;
    };

    struct ThemeColorPalette
    {
        QC::Color windowBackground;
        QC::Color titleBarGradientStart;
        QC::Color titleBarGradientEnd;
        QC::Color buttonNormal;
        QC::Color buttonHover;
        QC::Color buttonPressed;
        QC::Color buttonGlow;
        QC::Color textPrimary;
        QC::Color textSecondary;
        QC::Color border;
        QC::Color shadow;
        QC::Color accentPrimary;
        QC::Color accentSecondary;
    };

    struct ThemeBorderStyle
    {
        QC::u32 width;
        QC::u32 radius;
        QC::Color color;
    };

    struct ThemeShadowStyle
    {
        QC::i32 offsetX;
        QC::i32 offsetY;
        QC::u32 blurRadius;
        QC::Color color;
    };

    struct ThemeGlowStyle
    {
        QC::Color color;
        QC::u32 radius;
        QC::u32 intensity;
    };

    struct ThemeTransparency
    {
        QC::u8 windowOpacity;
        QC::u8 panelOpacity;
    };

    struct ThemeFont
    {
        char family[48];
        QC::u8 size;
    };

    struct ThemeEffects
    {
        QC::i32 glassBlurRadius;
        ThemeBorderStyle border;
        ThemeShadowStyle shadow;
        ThemeGlowStyle glow;
        ThemeTransparency transparency;
    };

    struct ThemeAnimations
    {
        QC::u32 hoverDurationMs;
        QC::u32 pressDurationMs;
        QC::u32 windowOpenDurationMs;
    };

    class Theme
    {
    public:
        Theme();

        void reset();
        void setName(const char *name);
        const char *name() const { return m_name; }

        ThemeColorPalette &colors() { return m_colors; }
        const ThemeColorPalette &colors() const { return m_colors; }

        ThemeFont &font() { return m_font; }
        const ThemeFont &font() const { return m_font; }

        ThemeEffects &effects() { return m_effects; }
        const ThemeEffects &effects() const { return m_effects; }

        ThemeAnimations &animations() { return m_animations; }
        const ThemeAnimations &animations() const { return m_animations; }

        bool loadFromJson(const QC::JSON::Value &root);
        bool loadFromFile(const char *path);

    private:
        void applyVistaDefaults();

        char m_name[64];
        ThemeColorPalette m_colors;
        ThemeFont m_font;
        ThemeEffects m_effects;
        ThemeAnimations m_animations;
    };

    bool loadThemeFromBuffer(const char *buffer, QC::usize length, Theme &outTheme);
    bool loadThemeFromJsonString(const char *text, Theme &outTheme);
    const char *themeIdToString(ThemeID id);
    bool themeIdFromString(const char *text, ThemeID *outId);

} // namespace QD
