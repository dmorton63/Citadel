#include "QDThemeService.h"

#include "QCString.h"
#include "QWStyleTypes.h"

namespace QD
{
    namespace
    {
        constexpr float BASE_THEME_FONT_SIZE = 12.0f;

        inline const char *stringOrNull(const QC::JSON::Value *value)
        {
            return (value && value->isString()) ? value->asString(nullptr) : nullptr;
        }

        bool equalsIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;

            while (*a && *b)
            {
                char ca = *a;
                char cb = *b;
                if (ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca - 'A' + 'a');
                if (cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb - 'A' + 'a');
                if (ca != cb)
                    return false;
                ++a;
                ++b;
            }

            return *a == '\0' && *b == '\0';
        }

        bool containsIgnoreCase(const char *text, const char *needle)
        {
            if (!text || !needle || !*needle)
                return false;

            for (const char *cursor = text; *cursor; ++cursor)
            {
                const char *lhs = cursor;
                const char *rhs = needle;
                while (*lhs && *rhs)
                {
                    char ca = *lhs;
                    char cb = *rhs;
                    if (ca >= 'A' && ca <= 'Z')
                        ca = static_cast<char>(ca - 'A' + 'a');
                    if (cb >= 'A' && cb <= 'Z')
                        cb = static_cast<char>(cb - 'A' + 'a');
                    if (ca != cb)
                        break;
                    ++lhs;
                    ++rhs;
                }
                if (*rhs == '\0')
                    return true;
            }

            return false;
        }

        float clamp01(float value)
        {
            if (value < 0.0f)
                return 0.0f;
            if (value > 1.0f)
                return 1.0f;
            return value;
        }

        QC::u8 clampToByte(QC::u32 value)
        {
            return value > 255 ? 255 : static_cast<QC::u8>(value);
        }

        void assignAssetIfString(const QC::JSON::Value *object,
                                 const char *key,
                                 const char *&target)
        {
            if (!object || !object->isObject() || !key)
                return;
            const QC::JSON::Value *value = object->find(key);
            const char *resolved = stringOrNull(value);
            if (resolved && *resolved)
                target = resolved;
        }

        void applyAssetOverridesFromObject(const QC::JSON::Value *themeObject,
                                           CitadelThemeAssets &assets)
        {
            if (!themeObject || !themeObject->isObject())
                return;

            const QC::JSON::Value *assetsNode = themeObject->find("assets");
            if (!assetsNode || !assetsNode->isObject())
                return;

            const QC::JSON::Value *icons = assetsNode->find("icons");
            assignAssetIfString(icons, "settings", assets.icons.settings);
            assignAssetIfString(icons, "terminal", assets.icons.terminal);
            assignAssetIfString(icons, "folder", assets.icons.folder);
            assignAssetIfString(icons, "start", assets.icons.start);
            assignAssetIfString(icons, "shutdown", assets.icons.shutdown);

            const QC::JSON::Value *backgrounds = assetsNode->find("backgrounds");
            assignAssetIfString(backgrounds, "desktopPrimary", assets.backgrounds.desktopPrimary);
            assignAssetIfString(backgrounds, "desktopSecondary", assets.backgrounds.desktopSecondary);
            assignAssetIfString(backgrounds, "lockScreen", assets.backgrounds.lockScreen);

            const QC::JSON::Value *textures = assetsNode->find("textures");
            assignAssetIfString(textures, "glassNoise", assets.textures.glassNoise);
            assignAssetIfString(textures, "panelOverlay", assets.textures.panelOverlay);

            const QC::JSON::Value *illustrations = assetsNode->find("illustrations");
            assignAssetIfString(illustrations, "boot", assets.illustrations.boot);
            assignAssetIfString(illustrations, "setup", assets.illustrations.setup);
            assignAssetIfString(illustrations, "recovery", assets.illustrations.recovery);
        }

        void applyAssetOverrides(const QC::JSON::Value *themeValue,
                                 CitadelThemeAssets &assets)
        {
            if (!themeValue || !themeValue->isObject())
                return;

            applyAssetOverridesFromObject(themeValue, assets);

            const QC::JSON::Value *definition = themeValue->find("definition");
            if (definition && definition->isObject())
                applyAssetOverridesFromObject(definition, assets);
        }
    } // namespace

    bool ThemeService::loadTheme(const QC::JSON::Value *themeValue, ThemeLoadResult &outResult) const
    {
        resetResult(outResult);
        if (!themeValue)
            return false;

        outResult.package.id = resolveThemeId(themeValue);

        if (themeValue->isString())
        {
            outResult.loaded = tryLoadPath(themeValue->asString(nullptr), outResult);
        }
        else if (themeValue->isObject())
        {
            if (tryLoadPath(stringOrNull(themeValue->find("file")), outResult) ||
                tryLoadPath(stringOrNull(themeValue->find("path")), outResult))
            {
                outResult.loaded = true;
            }
            else if (const QC::JSON::Value *definition = themeValue->find("definition"); definition && definition->isObject())
            {
                outResult.loaded = outResult.theme.loadFromJson(*definition);
            }
            else if (themeValue->find("colors") || themeValue->find("effects") || themeValue->find("animations") || themeValue->find("base"))
            {
                outResult.loaded = outResult.theme.loadFromJson(*themeValue);
            }
        }

        if (!outResult.loaded)
        {
            if (outResult.package.id != ThemeID::Default && outResult.package.id != ThemeID::Custom)
            {
                if (!populateBuiltinTheme(outResult.package.id, outResult))
                    return false;
            }
            else
            {
                return false;
            }
        }

        populateMetadata(outResult.package.id, outResult.theme, outResult.package.metadata);
        populateStyle(outResult.theme, outResult.package.style);
        populateAssets(outResult.package.id, outResult.package.assets);
        applyAssetOverrides(themeValue, outResult.package.assets);
        return true;
    }

    bool ThemeService::populateBuiltinTheme(ThemeID id, ThemeLoadResult &outResult)
    {
        outResult.theme.reset(); // start from Vista base (effects, animations, font)
        ThemeColorPalette &p = outResult.theme.colors();

        switch (id)
        {
        case ThemeID::Winter:
            outResult.theme.setName("Citadel Winter");
            p.windowBackground      = QC::Color(0x1C, 0x24, 0x2C, 0xFF);
            p.border                = QC::Color(0x2E, 0x3A, 0x45, 0xFF);
            p.accentPrimary         = QC::Color(0x7A, 0xA0, 0xC8, 0xFF);
            p.accentSecondary       = QC::Color(0xBF, 0xD6, 0xF0, 0xFF);
            p.titleBarGradientStart = QC::Color(0x4A, 0x6A, 0x8A, 0xFF);
            p.titleBarGradientEnd   = QC::Color(0x2E, 0x3A, 0x45, 0xFF);
            p.textPrimary           = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
            p.textSecondary         = QC::Color(0xE6, 0xF0, 0xFA, 0xFF);
            p.buttonGlow            = QC::Color(0x80, 0x7A, 0xA0, 0xC8);
            break;

        case ThemeID::Spring:
            outResult.theme.setName("Citadel Spring");
            p.windowBackground      = QC::Color(0x1F, 0x2A, 0x1F, 0xFF);
            p.border                = QC::Color(0x3A, 0x4A, 0x3A, 0xFF);
            p.accentPrimary         = QC::Color(0x6C, 0xBF, 0x6C, 0xFF);
            p.accentSecondary       = QC::Color(0xA8, 0xE6, 0xA8, 0xFF);
            p.titleBarGradientStart = QC::Color(0x4A, 0x8F, 0x4A, 0xFF);
            p.titleBarGradientEnd   = QC::Color(0x3A, 0x4A, 0x3A, 0xFF);
            p.textPrimary           = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
            p.textSecondary         = QC::Color(0xE8, 0xF5, 0xE9, 0xFF);
            p.buttonGlow            = QC::Color(0x80, 0x6C, 0xBF, 0x6C);
            break;

        case ThemeID::Summer:
            outResult.theme.setName("Citadel Summer");
            p.windowBackground      = QC::Color(0x1E, 0x2A, 0x33, 0xFF);
            p.border                = QC::Color(0x2F, 0x3E, 0x4A, 0xFF);
            p.accentPrimary         = QC::Color(0x4F, 0xA7, 0xE3, 0xFF);
            p.accentSecondary       = QC::Color(0x8F, 0xD1, 0xFF, 0xFF);
            p.titleBarGradientStart = QC::Color(0x2F, 0x6F, 0xA3, 0xFF);
            p.titleBarGradientEnd   = QC::Color(0x2F, 0x3E, 0x4A, 0xFF);
            p.textPrimary           = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
            p.textSecondary         = QC::Color(0xE3, 0xF4, 0xFF, 0xFF);
            p.buttonGlow            = QC::Color(0x80, 0x4F, 0xA7, 0xE3);
            break;

        case ThemeID::Autumn:
            outResult.theme.setName("Citadel Autumn");
            p.windowBackground      = QC::Color(0x2A, 0x1E, 0x14, 0xFF);
            p.border                = QC::Color(0x3C, 0x2A, 0x1D, 0xFF);
            p.accentPrimary         = QC::Color(0xD9, 0x82, 0x2B, 0xFF);
            p.accentSecondary       = QC::Color(0xFF, 0xBE, 0x78, 0xFF);
            p.titleBarGradientStart = QC::Color(0x8A, 0x4F, 0x12, 0xFF);
            p.titleBarGradientEnd   = QC::Color(0x3C, 0x2A, 0x1D, 0xFF);
            p.textPrimary           = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
            p.textSecondary         = QC::Color(0xFF, 0xE8, 0xCC, 0xFF);
            p.buttonGlow            = QC::Color(0x80, 0xD9, 0x82, 0x2B);
            break;

        case ThemeID::Standard:
            outResult.theme.setName("Citadel Standard");
            p.windowBackground      = QC::Color(0x20, 0x20, 0x20, 0xFF);
            p.border                = QC::Color(0x30, 0x30, 0x30, 0xFF);
            p.accentPrimary         = QC::Color(0x4A, 0x90, 0xE2, 0xFF);
            p.accentSecondary       = QC::Color(0x6B, 0xB6, 0xFF, 0xFF);
            p.titleBarGradientStart = QC::Color(0x2F, 0x5A, 0x8A, 0xFF);
            p.titleBarGradientEnd   = QC::Color(0x30, 0x30, 0x30, 0xFF);
            p.textPrimary           = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
            p.textSecondary         = QC::Color(0xCC, 0xCC, 0xCC, 0xFF);
            p.buttonGlow            = QC::Color(0x80, 0x4A, 0x90, 0xE2);
            break;

        default:
            return false;
        }

        // buttonNormal/Hover/Pressed use translucent white (glass style) from Vista base.
        // Update glow effect color to match the seasonal accent.
        outResult.theme.effects().glow.color = p.buttonGlow;
        outResult.loaded = true;
        return true;
    }

    void ThemeService::resetResult(ThemeLoadResult &result)
    {
        result = ThemeLoadResult{};
    }

    ThemeID ThemeService::resolveThemeId(const QC::JSON::Value *themeValue)
    {
        if (!themeValue)
            return ThemeID::Default;

        const char *candidate = nullptr;
        if (themeValue->isString())
        {
            candidate = themeValue->asString(nullptr);
        }
        else if (themeValue->isObject())
        {
            if (themeIdFromString(stringOrNull(themeValue->find("id")), nullptr))
            {
                ThemeID id;
                if (themeIdFromString(stringOrNull(themeValue->find("id")), &id))
                    return id;
            }

            candidate = stringOrNull(themeValue->find("base"));
            if (!candidate)
                candidate = stringOrNull(themeValue->find("file"));
            if (!candidate)
                candidate = stringOrNull(themeValue->find("path"));
        }

        ThemeID parsedId;
        if (themeIdFromString(candidate, &parsedId))
            return parsedId;

        if (containsIgnoreCase(candidate, "winter"))
            return ThemeID::Winter;
        if (containsIgnoreCase(candidate, "spring"))
            return ThemeID::Spring;
        if (containsIgnoreCase(candidate, "summer"))
            return ThemeID::Summer;
        if (containsIgnoreCase(candidate, "autumn") || containsIgnoreCase(candidate, "fall"))
            return ThemeID::Autumn;
        if (containsIgnoreCase(candidate, "midnight"))
            return ThemeID::Midnight;
        if (containsIgnoreCase(candidate, "contrast"))
            return ThemeID::HighContrast;
        if (containsIgnoreCase(candidate, "standard"))
            return ThemeID::Standard;

        return ThemeID::Custom;
    }

    bool ThemeService::tryLoadPath(const char *path, ThemeLoadResult &outResult)
    {
        if (!path || !*path)
            return false;
        return outResult.theme.loadFromFile(path);
    }

    void ThemeService::populateMetadata(ThemeID themeId, const Theme &theme, CitadelThemeMetadata &outMetadata)
    {
        QC::String::strncpy(outMetadata.name, theme.name(), sizeof(outMetadata.name) - 1);
        outMetadata.name[sizeof(outMetadata.name) - 1] = '\0';
        QC::String::strncpy(outMetadata.version, "1", sizeof(outMetadata.version) - 1);
        outMetadata.version[sizeof(outMetadata.version) - 1] = '\0';
        outMetadata.darkTheme = (themeId == ThemeID::Midnight);
        outMetadata.highContrast = (themeId == ThemeID::HighContrast);
        outMetadata.supportsReducedMotion = true;
    }

    void ThemeService::populateStyle(const Theme &theme, CitadelStyle &outStyle)
    {
        const ThemeColorPalette &palette = theme.colors();
        const ThemeEffects &effects = theme.effects();
        const ThemeAnimations &animations = theme.animations();
        const ThemeFont &font = theme.font();

        outStyle.colors.surfaceWindow = palette.windowBackground;
        outStyle.colors.surfacePanel = palette.windowBackground;
        outStyle.colors.surfaceDesktop = palette.windowBackground;
        outStyle.colors.surfaceSidebar = palette.windowBackground;
        outStyle.colors.surfaceTaskbar = palette.windowBackground;
        outStyle.colors.borderSubtle = palette.border;
        outStyle.colors.borderStrong = palette.accentPrimary;
        outStyle.colors.shadow = palette.shadow;
        outStyle.colors.textPrimary = palette.textPrimary;
        outStyle.colors.textSecondary = palette.textSecondary;
        outStyle.colors.textOnAccent = palette.textPrimary;
        outStyle.colors.accentPrimary = palette.accentPrimary;
        outStyle.colors.accentHover = palette.accentSecondary;
        outStyle.colors.accentPressed = palette.accentPrimary.darker(0.2f);
        outStyle.colors.focusRing = palette.accentPrimary;

        outStyle.metrics.windowCornerRadius = effects.border.radius;
        outStyle.metrics.panelCornerRadius = effects.border.radius;
        outStyle.metrics.buttonCornerRadius = effects.border.radius;
        outStyle.metrics.borderWidth = effects.border.width;
        outStyle.metrics.shadowBlur = effects.shadow.blurRadius;
        outStyle.metrics.shadowOffsetX = effects.shadow.offsetX;
        outStyle.metrics.shadowOffsetY = effects.shadow.offsetY;
        outStyle.metrics.textScale = (font.size > 0)
                                         ? (static_cast<float>(font.size) / 12.0f)
                                         : 1.0f;

        QC::String::strncpy(outStyle.typography.family, font.family, sizeof(outStyle.typography.family) - 1);
        outStyle.typography.family[sizeof(outStyle.typography.family) - 1] = '\0';
        outStyle.typography.bodySize = font.size > 0 ? font.size : 12;
        outStyle.typography.headingSize = outStyle.typography.bodySize + 4;
        outStyle.typography.displaySize = outStyle.typography.bodySize + 10;
        outStyle.typography.captionSize = outStyle.typography.bodySize > 2 ? outStyle.typography.bodySize - 2 : outStyle.typography.bodySize;

        outStyle.motion.hoverDurationMs = animations.hoverDurationMs;
        outStyle.motion.pressDurationMs = animations.pressDurationMs;
        outStyle.motion.enterDurationMs = animations.windowOpenDurationMs;

        for (QC::u32 i = 0; i < static_cast<QC::u32>(QW::ButtonRole::Count); ++i)
        {
            auto &button = outStyle.buttonRoles[i];
            button.fillNormal = palette.buttonNormal;
            button.fillHover = palette.buttonHover;
            button.fillPressed = palette.buttonPressed;
            button.text = palette.textPrimary;
            button.border = palette.border;
            button.glow = palette.buttonGlow;
            button.glass = effects.glassBlurRadius > 0;
        }

        auto &accentButton = outStyle.buttonRoles[static_cast<QC::u32>(QW::ButtonRole::Accent)];
        accentButton.fillNormal = palette.accentPrimary;
        accentButton.fillHover = palette.accentSecondary;
        accentButton.fillPressed = palette.accentPrimary.darker(0.2f);
        accentButton.text = palette.textPrimary;

        auto &destructiveButton = outStyle.buttonRoles[static_cast<QC::u32>(QW::ButtonRole::Destructive)];
        destructiveButton.fillNormal = outStyle.colors.error;
        destructiveButton.fillHover = outStyle.colors.error.lighter(0.12f);
        destructiveButton.fillPressed = outStyle.colors.error.darker(0.15f);
        destructiveButton.text = palette.textPrimary;
    }

    void ThemeService::populateAssets(ThemeID themeId, CitadelThemeAssets &outAssets)
    {
        outAssets = CitadelThemeAssets{};

        outAssets.icons.settings = "/system/icons/SETTINGS.PNG";
        outAssets.icons.terminal = "/system/icons/TERMINAL.PNG";
        outAssets.icons.folder = "/system/icons/FOLDER.PNG";
        outAssets.icons.start = "/system/icons/START.PNG";
        outAssets.icons.shutdown = "/system/icons/svg/power.svg";

        switch (themeId)
        {
        case ThemeID::Winter:
            outAssets.backgrounds.desktopPrimary = "/system/wall/WINTER.PNG";
            break;
        case ThemeID::Spring:
            outAssets.backgrounds.desktopPrimary = "/system/wall/SPRING.PNG";
            break;
        case ThemeID::Summer:
            outAssets.backgrounds.desktopPrimary = "/system/wall/SUMMER.PNG";
            break;
        case ThemeID::Autumn:
            outAssets.backgrounds.desktopPrimary = "/system/wall/AUTUMN.PNG";
            break;
        default:
            break;
        }
    }

    // ---------------------------------------------------------------------------
    // buildStyleSnapshot — assembles a QW::StyleSnapshot from resolved colors,
    // theme overrides, and the desktop accent color.
    // ---------------------------------------------------------------------------
    QW::StyleSnapshot ThemeService::buildStyleSnapshot(const DesktopColors &colors,
                                                       const ThemeOverrides &overrides,
                                                       const QC::Color &accentColor) const
    {
        QW::StyleSnapshot::VistaThemeConfig cfg;
        cfg.windowBackground     = colors.windowBg;
        cfg.windowBorder         = colors.windowBorder;
        cfg.sidebarBackground    = colors.sidebarBg;
        cfg.sidebarHover         = colors.sidebarHover;
        cfg.sidebarSelected      = colors.sidebarSelected;
        cfg.sidebarText          = colors.sidebarText;
        cfg.topBarDivider        = colors.topBarDivider;
        cfg.taskbarBackground    = colors.taskbarBg;
        cfg.taskbarHover         = colors.taskbarHover;
        cfg.taskbarText          = colors.taskbarText;
        cfg.taskbarActiveWindow  = colors.taskbarActiveWindow;
        cfg.desktopBackgroundTop    = colors.bgTop;
        cfg.desktopBackgroundBottom = colors.bgBottom;
        cfg.windowShadow         = colors.windowShadow;
        cfg.accent               = accentColor;

        // Palette overrides
        if (overrides.active)
        {
            if (overrides.palette.accent.set)         cfg.accent           = overrides.palette.accent.value;
            if (overrides.palette.panel.set)          cfg.windowBackground = overrides.palette.panel.value;
            if (overrides.palette.panelBorder.set)    cfg.windowBorder     = overrides.palette.panelBorder.value;
            if (overrides.palette.panel.set)          cfg.sidebarBackground = overrides.palette.panel.value;
            if (overrides.palette.panel.set)          cfg.taskbarBackground = overrides.palette.panel.value;
        }

        QW::StyleSnapshot snapshot = QW::StyleSnapshot::makeVista(cfg);

        // Apply overrides on top
        if (!overrides.active)
            return snapshot;

        // Palette
        if (overrides.palette.accent.set)
        {
            snapshot.palette.accent = overrides.palette.accent.value;
            const QC::Color ac = overrides.palette.accent.value;
            const QC::Color ach = overrides.palette.accentLight.set
                                      ? overrides.palette.accentLight.value
                                      : ac.lighter(0.15f);
            const QC::Color acd = overrides.palette.accentDark.set
                                      ? overrides.palette.accentDark.value
                                      : ac.darker(0.2f);
            // Propagate to accent button role
            auto &ab = snapshot.buttonStyles[static_cast<QC::u32>(QW::ButtonRole::Accent)];
            ab.fillNormal  = ac;
            ab.fillHover   = ach;
            ab.fillPressed = acd;
        }
        if (overrides.palette.panel.set)
        {
            snapshot.palette.windowBackground = overrides.palette.panel.value;
            snapshot.palette.panelBackground  = overrides.palette.panel.value;
        }
        if (overrides.palette.panelBorder.set)
        {
            snapshot.palette.windowBorderActive   = overrides.palette.panelBorder.value;
            snapshot.palette.windowBorderInactive = overrides.palette.panelBorder.value;
        }
        if (overrides.palette.text.set)
            snapshot.palette.controlText = overrides.palette.text.value;
        if (overrides.effects.borderColor.set)
            snapshot.palette.windowBorderActive = overrides.effects.borderColor.value;

        // Metrics
        if (overrides.metrics.cornerRadiusSet)
        {
            snapshot.metrics.windowCornerRadius = overrides.metrics.cornerRadius;
            snapshot.metrics.buttonCornerRadius = overrides.metrics.cornerRadius;
        }
        if (overrides.metrics.buttonCornerRadiusSet)
            snapshot.metrics.buttonCornerRadius = overrides.metrics.buttonCornerRadius;
        if (overrides.metrics.borderWidthSet)
            snapshot.metrics.borderWidth = overrides.metrics.borderWidth;

        // Shadow
        if (overrides.effects.shadow.offsetXSet)
            snapshot.metrics.buttonShadowOffsetX = overrides.effects.shadow.offsetX;
        if (overrides.effects.shadow.offsetYSet)
            snapshot.metrics.buttonShadowOffsetY = overrides.effects.shadow.offsetY;
        if (overrides.effects.shadow.blurSet)
            snapshot.metrics.shadowSize = overrides.effects.shadow.blurRadius;

        // Font scale
        float fontScale = 1.0f;
        if (overrides.font.sizeSet && overrides.font.size > 0)
            fontScale = static_cast<float>(overrides.font.size) / BASE_THEME_FONT_SIZE;
        snapshot.metrics.textScale = fontScale;

        // Per-role button overrides
        for (QC::u32 i = 0; i < static_cast<QC::u32>(QW::ButtonRole::Count); ++i)
        {
            const auto &src = overrides.button[i];
            auto &dst = snapshot.buttonStyles[i];
            if (src.fillNormal.set)  dst.fillNormal  = src.fillNormal.value;
            if (src.fillHover.set)   dst.fillHover   = src.fillHover.value;
            if (src.fillPressed.set) dst.fillPressed  = src.fillPressed.value;
            if (src.text.set)        dst.text        = src.text.value;
            if (src.border.set)      dst.border      = src.border.value;
            if (src.glassSet)        dst.glass       = src.glass;
            if (src.materialSet)
            {
                // Resolve named material
                for (QC::u32 m = 0; m < overrides.materialCount && m < MAX_THEME_MATERIALS; ++m)
                {
                    const auto &mat = overrides.materials[m];
                    if (!mat.used || !mat.hasAny())
                        continue;
                    if (QC::String::strcmp(mat.name, src.material) != 0)
                        continue;
                    if (mat.style.fillNormal.set)  dst.fillNormal  = mat.style.fillNormal.value;
                    if (mat.style.fillHover.set)   dst.fillHover   = mat.style.fillHover.value;
                    if (mat.style.fillPressed.set) dst.fillPressed  = mat.style.fillPressed.value;
                    if (mat.style.text.set)        dst.text        = mat.style.text.value;
                    if (mat.style.border.set)      dst.border      = mat.style.border.value;
                    if (mat.style.glassSet)        dst.glass       = mat.style.glass;
                    if (mat.style.shineSet)
                    {
                        dst.materialLayers.enabled = true;
                        const float shine = clamp01(mat.style.shineIntensity);
                        const QC::u8 a = static_cast<QC::u8>(shine * 80.0f);
                        dst.materialLayers.normal.glossTop    = QC::Color(a, 0xFF, 0xFF, 0xFF);
                        dst.materialLayers.normal.glossBottom = QC::Color(0, 0xFF, 0xFF, 0xFF);
                    }
                    if (mat.layers.normal.glossTop.set || mat.layers.normal.glossBottom.set ||
                        mat.layers.normal.shadeTop.set || mat.layers.normal.shadeBottom.set)
                    {
                        dst.materialLayers.enabled = true;
                        if (mat.layers.normal.glossTop.set)
                            dst.materialLayers.normal.glossTop = mat.layers.normal.glossTop.value;
                        if (mat.layers.normal.glossBottom.set)
                            dst.materialLayers.normal.glossBottom = mat.layers.normal.glossBottom.value;
                        if (mat.layers.normal.shadeTop.set)
                            dst.materialLayers.normal.shadeTop = mat.layers.normal.shadeTop.value;
                        if (mat.layers.normal.shadeBottom.set)
                            dst.materialLayers.normal.shadeBottom = mat.layers.normal.shadeBottom.value;
                    }
                    break;
                }
            }
            if (src.shineSet)
            {
                dst.materialLayers.enabled = true;
                const float shine = clamp01(src.shineIntensity);
                const QC::u8 a = static_cast<QC::u8>(shine * 80.0f);
                dst.materialLayers.normal.glossTop    = QC::Color(a, 0xFF, 0xFF, 0xFF);
                dst.materialLayers.normal.glossBottom = QC::Color(0, 0xFF, 0xFF, 0xFF);
            }
        }

        // Transparency
        if (overrides.transparency.windowOpacitySet)
        {
            snapshot.palette.windowBackground =
                snapshot.palette.windowBackground.withAlpha(overrides.transparency.windowOpacity);
        }

        return snapshot;
    }

    const char *ThemeService::resolvedFontFamily(const ThemeOverrides &overrides) const
    {
        if (overrides.font.familySet && overrides.font.family[0] != '\0')
            return overrides.font.family;
        return "System";
    }

} // namespace QD