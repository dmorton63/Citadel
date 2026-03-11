#include "QDTheme.h"

#include "QDColorUtils.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QFSVFS.h"
#include "QFSFile.h"

namespace QD
{
    namespace
    {
        constexpr const char *LOG_MODULE = "QDTheme";

        inline void copyString(char *dest, QC::usize destSize, const char *source)
        {
            if (!dest || destSize == 0)
                return;

            if (!source)
            {
                dest[0] = '\0';
                return;
            }

            QC::String::strncpy(dest, source, destSize - 1);
            dest[destSize - 1] = '\0';
        }

        inline const char *stringOrNull(const QC::JSON::Value *value)
        {
            if (!value || !value->isString())
                return nullptr;
            return value->asString(nullptr);
        }

        bool assignColor(const QC::JSON::Value *object, const char *key, QC::Color &target)
        {
            if (!object || !object->isObject())
                return false;

            const QC::JSON::Value *value = object->find(key);
            const char *text = stringOrNull(value);
            if (!text)
                return false;

            QC::Color parsed;
            if (!parseColorString(text, parsed))
            {
                QC_LOG_WARN(LOG_MODULE, "Invalid color '%s' for key '%s'", text, key);
                return false;
            }

            target = parsed;
            return true;
        }

        template <typename T>
        bool assignNumber(const QC::JSON::Value *object, const char *key, T &target)
        {
            if (!object || !object->isObject())
                return false;
            const QC::JSON::Value *value = object->find(key);
            if (!value || !value->isNumber())
                return false;
            target = static_cast<T>(value->asNumber(static_cast<double>(target)));
            return true;
        }

        void applyColorPalette(const QC::JSON::Value *colors, ThemeColorPalette &palette)
        {
            if (!colors || !colors->isObject())
                return;

            assignColor(colors, "windowBackground", palette.windowBackground);
            assignColor(colors, "titleBarGradientStart", palette.titleBarGradientStart);
            assignColor(colors, "titleBarGradientEnd", palette.titleBarGradientEnd);
            assignColor(colors, "buttonNormal", palette.buttonNormal);
            assignColor(colors, "buttonHover", palette.buttonHover);
            assignColor(colors, "buttonPressed", palette.buttonPressed);
            assignColor(colors, "buttonGlow", palette.buttonGlow);
            assignColor(colors, "textPrimary", palette.textPrimary);
            assignColor(colors, "textSecondary", palette.textSecondary);
            assignColor(colors, "border", palette.border);
            assignColor(colors, "shadow", palette.shadow);
            assignColor(colors, "accentPrimary", palette.accentPrimary);
            assignColor(colors, "accentSecondary", palette.accentSecondary);
        }

        void applyFontSettings(const QC::JSON::Value *fonts, ThemeFont &font)
        {
            if (!fonts || !fonts->isObject())
                return;

            const QC::JSON::Value *primary = fonts->find("primary");
            if (!primary || !primary->isObject())
                return;

            if (const char *family = stringOrNull(primary->find("family")))
            {
                copyString(font.family, sizeof(font.family), family);
            }

            assignNumber(primary, "size", font.size);
        }

        void applyEffects(const QC::JSON::Value *effects, ThemeEffects &effectSettings)
        {
            if (!effects || !effects->isObject())
                return;

            assignNumber(effects, "glassBlur", effectSettings.glassBlurRadius);
            assignNumber(effects, "borderRadius", effectSettings.border.radius);
            assignNumber(effects, "borderWidth", effectSettings.border.width);
            assignColor(effects, "borderColor", effectSettings.border.color);

            assignNumber(effects, "shadowBlur", effectSettings.shadow.blurRadius);
            assignColor(effects, "shadowColor", effectSettings.shadow.color);

            if (const QC::JSON::Value *shadowOffset = effects->find("shadowOffset"))
            {
                assignNumber(shadowOffset, "x", effectSettings.shadow.offsetX);
                assignNumber(shadowOffset, "y", effectSettings.shadow.offsetY);
            }

            assignNumber(effects, "glowRadius", effectSettings.glow.radius);
            assignNumber(effects, "glowIntensity", effectSettings.glow.intensity);
            assignColor(effects, "glowColor", effectSettings.glow.color);

            assignNumber(effects, "windowOpacity", effectSettings.transparency.windowOpacity);
            assignNumber(effects, "panelOpacity", effectSettings.transparency.panelOpacity);
        }

        void applyAnimations(const QC::JSON::Value *animations, ThemeAnimations &anim)
        {
            if (!animations || !animations->isObject())
                return;

            assignNumber(animations, "hoverDuration", anim.hoverDurationMs);
            assignNumber(animations, "pressDuration", anim.pressDurationMs);
            assignNumber(animations, "windowOpenDuration", anim.windowOpenDurationMs);
        }

        const QC::JSON::Value *themeRoot(const QC::JSON::Value &root)
        {
            if (!root.isObject())
                return nullptr;

            const QC::JSON::Value *theme = root.find("theme");
            if (theme && theme->isObject())
                return theme;
            return &root;
        }
    } // namespace

    Theme::Theme()
    {
        reset();
    }

    void Theme::applyVistaDefaults()
    {
        copyString(m_name, sizeof(m_name), "Vista Glass");

        m_colors.windowBackground = QC::Color(0xF0, 0xF0, 0xF0, 0xFF);
        m_colors.titleBarGradientStart = QC::Color(0x3A, 0x6E, 0xA5, 0xCC);
        m_colors.titleBarGradientEnd = QC::Color(0x1E, 0x4A, 0x73, 0xCC);
        m_colors.buttonNormal = QC::Color(0x40, 0xFF, 0xFF, 0xFF);
        m_colors.buttonHover = QC::Color(0x60, 0xFF, 0xFF, 0xFF);
        m_colors.buttonPressed = QC::Color(0x30, 0xFF, 0xFF, 0xFF);
        m_colors.buttonGlow = QC::Color(0x80, 0x52, 0xB4, 0xE5);
        m_colors.textPrimary = QC::Color(0xFF, 0xFF, 0xFF, 0xFF);
        m_colors.textSecondary = QC::Color(0xCC, 0xFF, 0xFF, 0xFF);
        m_colors.border = QC::Color(0x40, 0x00, 0x00, 0x00);
        m_colors.shadow = QC::Color(0x40, 0x00, 0x00, 0x00);
        m_colors.accentPrimary = QC::Color(0x52, 0xB4, 0xE5, 0xFF);
        m_colors.accentSecondary = QC::Color(0x3A, 0x8D, 0xFF, 0xFF);

        copyString(m_font.family, sizeof(m_font.family), "System");
        m_font.size = 12;

        m_effects.glassBlurRadius = 20;
        m_effects.border.radius = 6;
        m_effects.border.width = 1;
        m_effects.border.color = QC::Color(0x80, 0xFF, 0xFF, 0xFF);

        m_effects.shadow.offsetX = 4;
        m_effects.shadow.offsetY = 4;
        m_effects.shadow.blurRadius = 10;
        m_effects.shadow.color = m_colors.shadow;

        m_effects.glow.color = m_colors.buttonGlow;
        m_effects.glow.radius = 8;
        m_effects.glow.intensity = 80;

        m_effects.transparency.windowOpacity = 0xE0;
        m_effects.transparency.panelOpacity = 0xCC;

        m_animations.hoverDurationMs = 150;
        m_animations.pressDurationMs = 50;
        m_animations.windowOpenDurationMs = 200;
    }

    void Theme::reset()
    {
        applyVistaDefaults();
    }

    void Theme::setName(const char *name)
    {
        copyString(m_name, sizeof(m_name), name);
    }

    bool Theme::loadFromJson(const QC::JSON::Value &root)
    {
        reset();

        const QC::JSON::Value *theme = themeRoot(root);
        if (!theme)
        {
            QC_LOG_WARN(LOG_MODULE, "Theme JSON root is not an object");
            return false;
        }

        if (const char *name = stringOrNull(theme->find("name")))
        {
            setName(name);
        }

        applyColorPalette(theme->find("colors"), m_colors);
        applyFontSettings(theme->find("fonts"), m_font);
        applyEffects(theme->find("effects"), m_effects);
        applyAnimations(theme->find("animations"), m_animations);

        return true;
    }

    bool Theme::loadFromFile(const char *path)
    {
        if (!path)
            return false;

        QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
        if (!file)
        {
            QC_LOG_WARN(LOG_MODULE, "Failed to open theme file %s", path);
            return false;
        }

        QC::u64 size64 = file->size();
        if (size64 == 0 || size64 > 1024 * 256)
        {
            QFS::VFS::instance().close(file);
            QC_LOG_WARN(LOG_MODULE, "Theme file %s has invalid size (%llu)", path, static_cast<unsigned long long>(size64));
            return false;
        }

        QC::usize size = static_cast<QC::usize>(size64);
        char *buffer = static_cast<char *>(operator new[](size + 1));
        QC::isize read = file->read(buffer, size);
        QFS::VFS::instance().close(file);

        if (read <= 0)
        {
            operator delete[](buffer);
            QC_LOG_WARN(LOG_MODULE, "Failed to read theme file %s", path);
            return false;
        }

        if (static_cast<QC::usize>(read) < size)
            size = static_cast<QC::usize>(read);

        buffer[size] = '\0';

        QC::JSON::Value root;
        bool ok = QC::JSON::parse(buffer, root);
        operator delete[](buffer);

        if (!ok)
        {
            QC_LOG_WARN(LOG_MODULE, "Theme JSON parse failed for %s", path);
            return false;
        }

        return loadFromJson(root);
    }

    bool loadThemeFromJsonString(const char *text, Theme &outTheme)
    {
        if (!text)
            return false;

        QC::JSON::Value root;
        if (!QC::JSON::parse(text, root))
            return false;

        return outTheme.loadFromJson(root);
    }

    bool loadThemeFromBuffer(const char *buffer, QC::usize length, Theme &outTheme)
    {
        if (!buffer || length == 0)
            return false;

        char *owned = static_cast<char *>(operator new[](length + 1));
        QC::String::memcpy(owned, buffer, length);
        owned[length] = '\0';

        bool result = loadThemeFromJsonString(owned, outTheme);
        operator delete[](owned);
        return result;
    }

} // namespace QD
