#pragma once

#include "QCColor.h"
#include "QCTypes.h"
#include "QWStyleTypes.h"

namespace QD
{
    struct ColorOverride
    {
        bool set = false;
        QC::Color value;
    };

    struct PaletteOverrides
    {
        ColorOverride accent;
        ColorOverride accentLight;
        ColorOverride accentDark;
        ColorOverride panel;
        ColorOverride panelBorder;
        ColorOverride text;
        ColorOverride textSecondary;
    };

    struct MetricsOverrides
    {
        bool cornerRadiusSet = false;
        QC::u32 cornerRadius = 0;
        bool buttonCornerRadiusSet = false;
        QC::u32 buttonCornerRadius = 0;
        bool borderWidthSet = false;
        QC::u32 borderWidth = 0;
    };

    struct ButtonStyleOverrides
    {
        ColorOverride fillNormal;
        ColorOverride fillHover;
        ColorOverride fillPressed;
        ColorOverride text;
        ColorOverride border;
        bool glassSet = false;
        bool glass = false;
        bool shineSet = false;
        float shineIntensity = 0.0f;
        bool materialSet = false;
        char material[48] = {};
        bool hasAny() const
        {
            return fillNormal.set || fillHover.set || fillPressed.set || text.set || border.set || glassSet || shineSet || materialSet;
        }
    };

    inline constexpr QC::u32 MAX_THEME_MATERIALS = 16;

    struct ButtonMaterialStyle
    {
        ColorOverride fillNormal;
        ColorOverride fillHover;
        ColorOverride fillPressed;
        ColorOverride text;
        ColorOverride border;
        bool glassSet = false;
        bool glass = false;
        bool shineSet = false;
        float shineIntensity = 0.0f;
        bool hasAny() const
        {
            return fillNormal.set || fillHover.set || fillPressed.set || text.set || border.set || glassSet || shineSet;
        }
    };

    struct ButtonMaterialLayerSet
    {
        ColorOverride glossTop;
        ColorOverride glossBottom;
        ColorOverride shadeTop;
        ColorOverride shadeBottom;
        bool hasAny() const
        {
            return glossTop.set || glossBottom.set || shadeTop.set || shadeBottom.set;
        }
    };

    struct ButtonMaterialLayers
    {
        ButtonMaterialLayerSet normal;
        ButtonMaterialLayerSet hover;
        ButtonMaterialLayerSet pressed;
        bool hasAny() const
        {
            return normal.hasAny() || hover.hasAny() || pressed.hasAny();
        }
    };

    struct ButtonMaterialDefinition
    {
        bool used = false;
        char name[48] = {};
        ButtonMaterialStyle style;
        ButtonMaterialLayers layers;
        bool hasAny() const
        {
            return style.hasAny() || layers.hasAny();
        }
    };

    struct ShadowOverrides
    {
        bool offsetXSet = false;
        QC::i32 offsetX = 0;
        bool offsetYSet = false;
        QC::i32 offsetY = 0;
        bool blurSet = false;
        QC::u32 blurRadius = 0;
        ColorOverride color;
    };

    struct GlowOverrides
    {
        bool radiusSet = false;
        QC::u32 radius = 0;
        bool intensitySet = false;
        QC::u32 intensity = 0;
        ColorOverride color;
    };

    struct TransparencyOverrides
    {
        bool windowOpacitySet = false;
        QC::u8 windowOpacity = 0xFF;
        bool panelOpacitySet = false;
        QC::u8 panelOpacity = 0xFF;
    };

    struct EffectsOverrides
    {
        ColorOverride borderColor;
        ShadowOverrides shadow;
        GlowOverrides glow;
    };

    struct FontOverrides
    {
        bool familySet = false;
        char family[48] = {};
        bool sizeSet = false;
        QC::u8 size = 12;
    };

    struct ThemeOverrides
    {
        PaletteOverrides palette;
        MetricsOverrides metrics;
        ButtonStyleOverrides button[static_cast<QC::u32>(QW::ButtonRole::Count)];
        ButtonMaterialDefinition materials[MAX_THEME_MATERIALS];
        QC::u32 materialCount = 0;
        EffectsOverrides effects;
        TransparencyOverrides transparency;
        FontOverrides font;
        bool active = false;
    };

} // namespace QD
