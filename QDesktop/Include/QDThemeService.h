#pragma once

#include "QDAccent.h"
#include "QDTheme.h"
#include "QDThemeOverrides.h"

namespace QC
{
    namespace JSON
    {
        class Value;
    }
}

namespace QCQL
{
    struct Database;
}

namespace QD
{
    struct ThemeLoadResult
    {
        bool loaded = false;
        Theme theme;
        CitadelThemePackage package;
    };

    class ThemeService
    {
    public:
        bool loadTheme(const QC::JSON::Value *themeValue, ThemeLoadResult &outResult) const;
        bool loadThemeFromDatabase(const QCQL::Database &database,
                                   ThemeID themeId,
                                   ThemeLoadResult &outResult) const;
        QW::StyleSnapshot buildStyleSnapshot(const DesktopColors &colors,
                                             const ThemeOverrides &overrides,
                                             const QC::Color &accentColor) const;
        const char *resolvedFontFamily(const ThemeOverrides &overrides) const;

    private:
        static void resetResult(ThemeLoadResult &result);
        static ThemeID resolveThemeId(const QC::JSON::Value *themeValue);
        static bool tryLoadPath(const char *path, ThemeLoadResult &outResult);
        static void populateMetadata(ThemeID themeId, const Theme &theme, CitadelThemeMetadata &outMetadata);
        static void populateStyle(const Theme &theme, CitadelStyle &outStyle);
        static void populateAssets(ThemeID themeId, CitadelThemeAssets &outAssets);
        static bool populateBuiltinTheme(ThemeID id, ThemeLoadResult &outResult);
    };

} // namespace QD