#include "QDThemeAssets.h"

#include "QDTheme.h"
#include "QCString.h"

namespace QD
{
    namespace
    {
        inline bool startsWith(const char *s, const char *prefix)
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                if (*s != *prefix)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        }

        inline bool equalsIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
                char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
                if (ca != cb)
                    return false;
                ++a;
                ++b;
            }
            return *a == '\0' && *b == '\0';
        }

        bool assignOut(const char *path, char *out, QC::usize outCap)
        {
            if (!path || !*path || !out || outCap == 0)
                return false;
            QC::String::strncpy(out, path, outCap - 1);
            out[outCap - 1] = '\0';
            return out[0] != '\0';
        }

        bool resolveFromPackageAssets(const char *token,
                                      const CitadelThemeAssets *assets,
                                      char *out,
                                      QC::usize outCap)
        {
            if (!token || !*token || !assets)
                return false;

            if (equalsIgnoreCase(token, "icons.folder") || equalsIgnoreCase(token, "icon.folder") || equalsIgnoreCase(token, "folder"))
                return assignOut(assets->icons.folder, out, outCap);

            if (equalsIgnoreCase(token, "icons.settings") || equalsIgnoreCase(token, "icon.settings") || equalsIgnoreCase(token, "settings"))
                return assignOut(assets->icons.settings, out, outCap);

            if (equalsIgnoreCase(token, "icons.terminal") || equalsIgnoreCase(token, "icon.terminal") || equalsIgnoreCase(token, "terminal"))
                return assignOut(assets->icons.terminal, out, outCap);

            if (equalsIgnoreCase(token, "icons.start") || equalsIgnoreCase(token, "icon.start") || equalsIgnoreCase(token, "start"))
                return assignOut(assets->icons.start, out, outCap);

            if (equalsIgnoreCase(token, "icons.shutdown") || equalsIgnoreCase(token, "icon.shutdown") ||
                equalsIgnoreCase(token, "icons.power") || equalsIgnoreCase(token, "icon.power") ||
                equalsIgnoreCase(token, "shutdown") || equalsIgnoreCase(token, "power"))
                return assignOut(assets->icons.shutdown, out, outCap);

            if (equalsIgnoreCase(token, "backgrounds.desktopPrimary") || equalsIgnoreCase(token, "background.desktopPrimary"))
                return assignOut(assets->backgrounds.desktopPrimary, out, outCap);

            if (equalsIgnoreCase(token, "backgrounds.desktopSecondary") || equalsIgnoreCase(token, "background.desktopSecondary"))
                return assignOut(assets->backgrounds.desktopSecondary, out, outCap);

            if (equalsIgnoreCase(token, "backgrounds.lockScreen") || equalsIgnoreCase(token, "background.lockScreen"))
                return assignOut(assets->backgrounds.lockScreen, out, outCap);

            if (equalsIgnoreCase(token, "textures.glassNoise") || equalsIgnoreCase(token, "texture.glassNoise"))
                return assignOut(assets->textures.glassNoise, out, outCap);

            if (equalsIgnoreCase(token, "textures.panelOverlay") || equalsIgnoreCase(token, "texture.panelOverlay"))
                return assignOut(assets->textures.panelOverlay, out, outCap);

            if (equalsIgnoreCase(token, "illustrations.boot") || equalsIgnoreCase(token, "illustration.boot"))
                return assignOut(assets->illustrations.boot, out, outCap);

            if (equalsIgnoreCase(token, "illustrations.setup") || equalsIgnoreCase(token, "illustration.setup"))
                return assignOut(assets->illustrations.setup, out, outCap);

            if (equalsIgnoreCase(token, "illustrations.recovery") || equalsIgnoreCase(token, "illustration.recovery"))
                return assignOut(assets->illustrations.recovery, out, outCap);

            return false;
        }
    }

    bool resolveThemeAssetKey(const char *tokenOrPath, char *out, QC::usize outCap)
    {
        if (!out || outCap == 0)
            return false;
        out[0] = '\0';
        if (!tokenOrPath || !*tokenOrPath)
            return false;

        const char *token = tokenOrPath;
        if (startsWith(token, "asset:"))
        {
            token += 6;
        }

        if (equalsIgnoreCase(token, "/system/icons/shutdown.png") ||
            equalsIgnoreCase(token, "/system/icons/shutdown.svg") ||
            equalsIgnoreCase(token, "/system/icons/SHUTDOWN.PNG") ||
            equalsIgnoreCase(token, "/system/icons/svg/shutdown.svg") ||
            equalsIgnoreCase(token, "/system/icons/svg/power.svg"))
        {
            // Some runtime images ship power.svg but not shutdown.svg/SHUTDOWN.PNG.
            QC::String::strncpy(out, "/ICONS/svg/power.svg", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "icons.folder") || equalsIgnoreCase(token, "icon.folder") || equalsIgnoreCase(token, "folder"))
        {
            QC::String::strncpy(out, "/ICONS/FOLDER.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "icons.settings") || equalsIgnoreCase(token, "icon.settings") || equalsIgnoreCase(token, "settings"))
        {
            QC::String::strncpy(out, "/ICONS/SETTINGS.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "icons.terminal") || equalsIgnoreCase(token, "icon.terminal") || equalsIgnoreCase(token, "terminal"))
        {
            QC::String::strncpy(out, "/ICONS/TERMINAL.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "icons.start") || equalsIgnoreCase(token, "icon.start") || equalsIgnoreCase(token, "start"))
        {
            QC::String::strncpy(out, "/ICONS/START.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "icons.shutdown") || equalsIgnoreCase(token, "icon.shutdown") ||
                 equalsIgnoreCase(token, "icons.power") || equalsIgnoreCase(token, "icon.power") ||
                 equalsIgnoreCase(token, "shutdown") || equalsIgnoreCase(token, "power"))
        {
            QC::String::strncpy(out, "/ICONS/svg/power.svg", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "backgrounds.winter") || equalsIgnoreCase(token, "background.winter"))
        {
            QC::String::strncpy(out, "/WALL/WINTER.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "backgrounds.spring") || equalsIgnoreCase(token, "background.spring"))
        {
            QC::String::strncpy(out, "/WALL/SPRING.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "backgrounds.summer") || equalsIgnoreCase(token, "background.summer"))
        {
            QC::String::strncpy(out, "/WALL/SUMMER.PNG", outCap - 1);
        }
        else if (equalsIgnoreCase(token, "backgrounds.autumn") || equalsIgnoreCase(token, "background.autumn") ||
                 equalsIgnoreCase(token, "backgrounds.fall") || equalsIgnoreCase(token, "background.fall"))
        {
            QC::String::strncpy(out, "/WALL/AUTUMN.PNG", outCap - 1);
        }
        else
        {
            return false;
        }

        out[outCap - 1] = '\0';
        return out[0] != '\0';
    }

    bool resolveThemeAssetKey(const char *tokenOrPath,
                              const CitadelThemeAssets *assets,
                              char *out,
                              QC::usize outCap)
    {
        if (!out || outCap == 0)
            return false;
        out[0] = '\0';
        if (!tokenOrPath || !*tokenOrPath)
            return false;

        const char *token = tokenOrPath;
        if (startsWith(token, "asset:"))
            token += 6;

        if (resolveFromPackageAssets(token, assets, out, outCap))
            return true;

        return resolveThemeAssetKey(tokenOrPath, out, outCap);
    }
}
