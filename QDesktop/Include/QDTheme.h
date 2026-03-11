#pragma once

// QDesktop Theme definition and loading helpers
// Namespace: QD

#include "QCColor.h"
#include "QCJson.h"

namespace QD
{
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

} // namespace QD
