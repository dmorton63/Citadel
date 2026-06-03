#include "QDThemeImporter.h"

#include "QCColor.h"
#include "QCQLEngine.h"
#include "QCString.h"
#include "QCTypes.h"

namespace QD
{
    namespace
    {
        // -----------------------------------------------------------------------
        // Low-level helpers
        // -----------------------------------------------------------------------

        static const char kHexDigits[] = "0123456789ABCDEF";

        static void hexByte(QC::u8 v, char *out)
        {
            out[0] = kHexDigits[(v >> 4) & 0xF];
            out[1] = kHexDigits[v & 0xF];
        }

        // Writes "#RRGGBBAA\0" — caller must supply at least 10 chars.
        static void colorToHex(const QC::Color &c, char *out)
        {
            out[0] = '#';
            hexByte(c.r, out + 1);
            hexByte(c.g, out + 3);
            hexByte(c.b, out + 5);
            hexByte(c.a, out + 7);
            out[9] = '\0';
        }

        static QCQL::Cell makeTextCell(const char *str)
        {
            QCQL::Cell cell{};
            cell.type = QCQL::ColumnType::Text;
            if (!str)
                return cell;
            const QC::usize len = QC::String::strlen(str);
            for (QC::usize i = 0; i < len; ++i)
                cell.bytes.push_back(static_cast<QC::u8>(str[i]));
            return cell;
        }

        static bool cellMatchesText(const QCQL::Cell &cell, const char *text)
        {
            if (cell.type != QCQL::ColumnType::Text)
                return false;
            const QC::usize len = text ? QC::String::strlen(text) : 0;
            if (cell.bytes.size() != len)
                return false;
            for (QC::usize i = 0; i < len; ++i)
            {
                if (cell.bytes[i] != static_cast<QC::u8>(text[i]))
                    return false;
            }
            return true;
        }

        static bool cellTextEmpty(const QCQL::Cell &cell)
        {
            if (cell.type != QCQL::ColumnType::Text)
                return true;
            for (QC::usize i = 0; i < cell.bytes.size(); ++i)
            {
                if (cell.bytes[i] != 0)
                    return false;
            }
            return true;
        }

        // Writes "slug:key\0" into buf (max bufLen bytes). Returns false if it won't fit.
        static bool makeTokenId(const char *slug, const char *key,
                                char *buf, QC::usize bufLen)
        {
            const QC::usize sLen = QC::String::strlen(slug);
            const QC::usize kLen = QC::String::strlen(key);
            if (sLen + 1 + kLen + 1 > bufLen)
                return false;
            QC::String::memcpy(buf, slug, sLen);
            buf[sLen] = ':';
            QC::String::memcpy(buf + sLen + 1, key, kLen);
            buf[sLen + 1 + kLen] = '\0';
            return true;
        }

        // -----------------------------------------------------------------------
        // Builtin theme table (mirrors QDThemeService::populateBuiltinTheme)
        // -----------------------------------------------------------------------

        struct BuiltinTheme
        {
            const char *slug;
            const char *displayName;
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

        struct ColorDef
        {
            QC::u8 r;
            QC::u8 g;
            QC::u8 b;
            QC::u8 a;
        };

        struct BuiltinThemeDef
        {
            const char *slug;
            const char *displayName;
            ColorDef windowBackground;
            ColorDef titleBarGradientStart;
            ColorDef titleBarGradientEnd;
            ColorDef buttonNormal;
            ColorDef buttonHover;
            ColorDef buttonPressed;
            ColorDef buttonGlow;
            ColorDef textPrimary;
            ColorDef textSecondary;
            ColorDef border;
            ColorDef shadow;
            ColorDef accentPrimary;
            ColorDef accentSecondary;
        };

        static constexpr ColorDef kGlassNormal  = {0xFF, 0xFF, 0xFF, 0x33};
        static constexpr ColorDef kGlassHover   = {0xFF, 0xFF, 0xFF, 0x4D};
        static constexpr ColorDef kGlassPressed = {0xFF, 0xFF, 0xFF, 0x66};
        static constexpr ColorDef kShadowNone   = {0x00, 0x00, 0x00, 0x00};

        static const BuiltinThemeDef kThemes[] = {
            {
                "winter", "Citadel Winter",
                {0x1C, 0x24, 0x2C, 0xFF},
                {0x4A, 0x6A, 0x8A, 0xFF},
                {0x2E, 0x3A, 0x45, 0xFF},
                kGlassNormal, kGlassHover, kGlassPressed,
                {0x7A, 0xA0, 0xC8, 0x80},
                {0xFF, 0xFF, 0xFF, 0xFF},
                {0xE6, 0xF0, 0xFA, 0xFF},
                {0x2E, 0x3A, 0x45, 0xFF},
                kShadowNone,
                {0x7A, 0xA0, 0xC8, 0xFF},
                {0xBF, 0xD6, 0xF0, 0xFF},
            },
            {
                "spring", "Citadel Spring",
                {0x1F, 0x2A, 0x1F, 0xFF},
                {0x4A, 0x8F, 0x4A, 0xFF},
                {0x3A, 0x4A, 0x3A, 0xFF},
                kGlassNormal, kGlassHover, kGlassPressed,
                {0x6C, 0xBF, 0x6C, 0x80},
                {0xFF, 0xFF, 0xFF, 0xFF},
                {0xE8, 0xF5, 0xE9, 0xFF},
                {0x3A, 0x4A, 0x3A, 0xFF},
                kShadowNone,
                {0x6C, 0xBF, 0x6C, 0xFF},
                {0xA8, 0xE6, 0xA8, 0xFF},
            },
            {
                "summer", "Citadel Summer",
                {0x1E, 0x2A, 0x33, 0xFF},
                {0x2F, 0x6F, 0xA3, 0xFF},
                {0x2F, 0x3E, 0x4A, 0xFF},
                kGlassNormal, kGlassHover, kGlassPressed,
                {0x4F, 0xA7, 0xE3, 0x80},
                {0xFF, 0xFF, 0xFF, 0xFF},
                {0xE3, 0xF4, 0xFF, 0xFF},
                {0x2F, 0x3E, 0x4A, 0xFF},
                kShadowNone,
                {0x4F, 0xA7, 0xE3, 0xFF},
                {0x8F, 0xD1, 0xFF, 0xFF},
            },
            {
                "autumn", "Citadel Autumn",
                {0x2A, 0x1E, 0x14, 0xFF},
                {0x8A, 0x4F, 0x12, 0xFF},
                {0x3C, 0x2A, 0x1D, 0xFF},
                kGlassNormal, kGlassHover, kGlassPressed,
                {0xD9, 0x82, 0x2B, 0x80},
                {0xFF, 0xFF, 0xFF, 0xFF},
                {0xFF, 0xE8, 0xCC, 0xFF},
                {0x3C, 0x2A, 0x1D, 0xFF},
                kShadowNone,
                {0xD9, 0x82, 0x2B, 0xFF},
                {0xFF, 0xBE, 0x78, 0xFF},
            },
            {
                "standard", "Citadel Standard",
                {0x20, 0x20, 0x20, 0xFF},
                {0x2F, 0x5A, 0x8A, 0xFF},
                {0x30, 0x30, 0x30, 0xFF},
                kGlassNormal, kGlassHover, kGlassPressed,
                {0x4A, 0x90, 0xE2, 0x80},
                {0xFF, 0xFF, 0xFF, 0xFF},
                {0xCC, 0xCC, 0xCC, 0xFF},
                {0x30, 0x30, 0x30, 0xFF},
                kShadowNone,
                {0x4A, 0x90, 0xE2, 0xFF},
                {0x6B, 0xB6, 0xFF, 0xFF},
            },
        };

        static QC::Color makeColor(const ColorDef &def)
        {
            return QC::Color(def.r, def.g, def.b, def.a);
        }

        static BuiltinTheme makeBuiltinTheme(const BuiltinThemeDef &def)
        {
            BuiltinTheme theme{};
            theme.slug = def.slug;
            theme.displayName = def.displayName;
            theme.windowBackground = makeColor(def.windowBackground);
            theme.titleBarGradientStart = makeColor(def.titleBarGradientStart);
            theme.titleBarGradientEnd = makeColor(def.titleBarGradientEnd);
            theme.buttonNormal = makeColor(def.buttonNormal);
            theme.buttonHover = makeColor(def.buttonHover);
            theme.buttonPressed = makeColor(def.buttonPressed);
            theme.buttonGlow = makeColor(def.buttonGlow);
            theme.textPrimary = makeColor(def.textPrimary);
            theme.textSecondary = makeColor(def.textSecondary);
            theme.border = makeColor(def.border);
            theme.shadow = makeColor(def.shadow);
            theme.accentPrimary = makeColor(def.accentPrimary);
            theme.accentSecondary = makeColor(def.accentSecondary);
            return theme;
        }

        struct TokenField
        {
            const char *key;
            // Offset (in bytes) of the QC::Color member inside BuiltinTheme.
            QC::usize offset;
        };

#define TOKEN_OFFSET(field) \
    (reinterpret_cast<QC::usize>(&reinterpret_cast<const char &>(static_cast<const BuiltinTheme *>(nullptr)->field)))

        static const TokenField kTokenFields[] = {
            {"windowBackground",        TOKEN_OFFSET(windowBackground)},
            {"titleBarGradientStart",   TOKEN_OFFSET(titleBarGradientStart)},
            {"titleBarGradientEnd",     TOKEN_OFFSET(titleBarGradientEnd)},
            {"buttonNormal",            TOKEN_OFFSET(buttonNormal)},
            {"buttonHover",             TOKEN_OFFSET(buttonHover)},
            {"buttonPressed",           TOKEN_OFFSET(buttonPressed)},
            {"buttonGlow",              TOKEN_OFFSET(buttonGlow)},
            {"textPrimary",             TOKEN_OFFSET(textPrimary)},
            {"textSecondary",           TOKEN_OFFSET(textSecondary)},
            {"border",                  TOKEN_OFFSET(border)},
            {"shadow",                  TOKEN_OFFSET(shadow)},
            {"accentPrimary",           TOKEN_OFFSET(accentPrimary)},
            {"accentSecondary",         TOKEN_OFFSET(accentSecondary)},
        };
#undef TOKEN_OFFSET

        static const QC::Color &colorAt(const BuiltinTheme &t, QC::usize offset)
        {
            return *reinterpret_cast<const QC::Color *>(
                reinterpret_cast<const char *>(&t) + offset);
        }

        static QCQL::Row makeThemeRow(const BuiltinTheme &t)
        {
            QCQL::Row row{};
            row.cells.push_back(makeTextCell(t.slug));
            row.cells.push_back(makeTextCell(t.displayName));
            row.cells.push_back(makeTextCell("{}"));
            return row;
        }

        static bool themeRowMatches(const QCQL::Row &row, const BuiltinTheme &t)
        {
            return row.cells.size() == 3 &&
                   cellMatchesText(row.cells[0], t.slug) &&
                   cellMatchesText(row.cells[1], t.displayName) &&
                   cellMatchesText(row.cells[2], "{}");
        }

        static QCQL::Row makeThemeTokenRow(const BuiltinTheme &t, const TokenField &tf);
        static bool themeTokenRowMatches(const QCQL::Row &row, const BuiltinTheme &t, const TokenField &tf);

        static bool isKnownThemeSlug(const QCQL::Cell &cell)
        {
            for (QC::usize i = 0; i < sizeof(kThemes) / sizeof(kThemes[0]); ++i)
            {
                if (cellMatchesText(cell, kThemes[i].slug))
                    return true;
            }
            return false;
        }

        static QCQL::Status purgeMalformedThemeRows(QCQL::Database &db)
        {
            auto &engine = QCQL::Engine::instance();
            QC::u32 tableId = 0;
            if (engine.lookupTableId(db, "Themes", tableId) != QCQL::Status::Success)
                return QCQL::Status::Success;

            QCQL::Table *table = nullptr;
            for (QC::usize i = 0; i < db.tables.size(); ++i)
            {
                if (db.tables[i].tableId == tableId)
                {
                    table = &db.tables[i];
                    break;
                }
            }
            if (!table)
                return QCQL::Status::Success;

            QC::Vector<QCQL::Cell> keysToRemove;
            for (QC::usize p = 0; p < table->pages.size(); ++p)
            {
                QCQL::Page page{};
                const QCQL::Status loadSt = engine.loadPage(db, table->pages[p], page);
                if (loadSt != QCQL::Status::Success)
                    continue;

                for (QC::usize r = 0; r < page.rowOffsets.size(); ++r)
                {
                    QCQL::Row row{};
                    const QCQL::Status readSt = engine.readRow(db, table->pages[p], page.rowOffsets[r], row);
                    if (readSt != QCQL::Status::Success || row.tombstone || row.cells.empty())
                        continue;

                    const bool invalid = row.cells.size() < 3 ||
                                         cellTextEmpty(row.cells[0]) ||
                                         cellTextEmpty(row.cells[1]) ||
                                         !isKnownThemeSlug(row.cells[0]);
                    if (!invalid)
                        continue;

                    keysToRemove.push_back(row.cells[0]);
                }
            }

            for (QC::usize i = 0; i < keysToRemove.size(); ++i)
            {
                const QCQL::Status removeSt =
                    engine.removeRowByPrimaryKeyByName(db, "Themes", keysToRemove[i].bytes);
                if (removeSt != QCQL::Status::Success && removeSt != QCQL::Status::NotFound)
                    return removeSt;
            }

            return QCQL::Status::Success;
        }

        static bool validateThemeRow(const QCQL::Database &db, const BuiltinTheme &t)
        {
            QCQL::Row row{};
            const QCQL::Cell keyCell = makeTextCell(t.slug);
            const QCQL::Status st =
                QCQL::Engine::instance().selectRowByPrimaryKeyByName(db, "Themes", keyCell.bytes, row);
            return st == QCQL::Status::Success && themeRowMatches(row, t);
        }

        static bool validateThemeTokenRow(const QCQL::Database &db, const BuiltinTheme &t, const TokenField &tf)
        {
            QCQL::Row expected = makeThemeTokenRow(t, tf);
            if (expected.cells.empty())
                return false;

            QCQL::Row row{};
            const QCQL::Status st =
                QCQL::Engine::instance().selectRowByPrimaryKeyByName(db, "ThemeTokens", expected.cells[0].bytes, row);
            return st == QCQL::Status::Success && themeTokenRowMatches(row, t, tf);
        }

        static QCQL::Status upsertThemeRow(QCQL::Database &db, const BuiltinTheme &t)
        {
            QCQL::Row row = makeThemeRow(t);
            QC::u32 pageId = 0;
            QC::u16 rowOffset = 0;
            const QCQL::Status insertSt =
                QCQL::Engine::instance().insertRowByName(db, "Themes", row, &pageId, &rowOffset);
            if (insertSt == QCQL::Status::Success)
                return QCQL::Status::Success;
            if (insertSt != QCQL::Status::AlreadyExists)
                return insertSt;

            QCQL::Row existing{};
            const QCQL::Cell keyCell = makeTextCell(t.slug);
            const QCQL::Status selectSt =
                QCQL::Engine::instance().selectRowByPrimaryKeyByName(db, "Themes", keyCell.bytes, existing);
            if (selectSt != QCQL::Status::Success)
                return selectSt;
            if (themeRowMatches(existing, t))
                return QCQL::Status::Success;
            return QCQL::Engine::instance().updateRowByPrimaryKeyByName(db, "Themes", keyCell.bytes, row);
        }

        static QCQL::Row makeThemeTokenRow(const BuiltinTheme &t, const TokenField &tf)
        {
            const QC::Color &c = colorAt(t, tf.offset);

            char tokenId[96];
            makeTokenId(t.slug, tf.key, tokenId, sizeof(tokenId));

            char hexVal[10];
            colorToHex(c, hexVal);

            QCQL::Row row{};
            row.cells.push_back(makeTextCell(tokenId));
            row.cells.push_back(makeTextCell(t.slug));
            row.cells.push_back(makeTextCell(tf.key));
            row.cells.push_back(makeTextCell(hexVal));
            return row;
        }

        static bool themeTokenRowMatches(const QCQL::Row &row, const BuiltinTheme &t, const TokenField &tf)
        {
            const QC::Color &c = colorAt(t, tf.offset);
            char tokenId[96];
            if (!makeTokenId(t.slug, tf.key, tokenId, sizeof(tokenId)))
                return false;

            char hexVal[10];
            colorToHex(c, hexVal);

            return row.cells.size() == 4 &&
                   cellMatchesText(row.cells[0], tokenId) &&
                   cellMatchesText(row.cells[1], t.slug) &&
                   cellMatchesText(row.cells[2], tf.key) &&
                   cellMatchesText(row.cells[3], hexVal);
        }

        static QCQL::Status upsertThemeTokenRow(QCQL::Database &db, const BuiltinTheme &t, const TokenField &tf)
        {
            QCQL::Row row = makeThemeTokenRow(t, tf);
            const QCQL::Cell keyCell = row.cells[0];
            QC::u32 pageId = 0;
            const QCQL::Status insertSt =
                QCQL::Engine::instance().insertRowByName(db, "ThemeTokens", row, &pageId);
            if (insertSt == QCQL::Status::Success)
                return QCQL::Status::Success;
            if (insertSt != QCQL::Status::AlreadyExists)
                return insertSt;

            QCQL::Row existing{};
            const QCQL::Status selectSt =
                QCQL::Engine::instance().selectRowByPrimaryKeyByName(db, "ThemeTokens", keyCell.bytes, existing);
            if (selectSt != QCQL::Status::Success)
                return selectSt;
            if (themeTokenRowMatches(existing, t, tf))
                return QCQL::Status::Success;
            return QCQL::Engine::instance().updateRowByPrimaryKeyByName(db, "ThemeTokens", keyCell.bytes, row);
        }

        // -----------------------------------------------------------------------
        // Insert helpers
        // -----------------------------------------------------------------------

        static QCQL::Status insertThemeRow(QCQL::Database &db, const BuiltinTheme &t)
        {
            return upsertThemeRow(db, t);
        }

        static QCQL::Status insertThemeTokens(QCQL::Database &db, const BuiltinTheme &t)
        {
            constexpr QC::usize kFieldCount =
                sizeof(kTokenFields) / sizeof(kTokenFields[0]);

            for (QC::usize i = 0; i < kFieldCount; ++i)
            {
                const TokenField &tf = kTokenFields[i];
                const QCQL::Status st = upsertThemeTokenRow(db, t, tf);
                if (st != QCQL::Status::Success)
                    return st;
            }
            return QCQL::Status::Success;
        }

    } // namespace

    // ---------------------------------------------------------------------------

    QCQL::Status ThemeImporter::importBuiltinThemes(QCQL::Database &database)
    {
        QCQL::Status st = purgeMalformedThemeRows(database);
        if (st != QCQL::Status::Success)
            return st;

        constexpr QC::usize kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

        for (QC::usize i = 0; i < kThemeCount; ++i)
        {
            const BuiltinTheme t = makeBuiltinTheme(kThemes[i]);

            st = insertThemeRow(database, t);
            if (st != QCQL::Status::Success)
                return st;

            st = insertThemeTokens(database, t);
            if (st != QCQL::Status::Success)
                return st;
        }

        return QCQL::Status::Success;
    }

    bool ThemeImporter::validateBuiltinThemes(const QCQL::Database &database)
    {
        constexpr QC::usize kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
        constexpr QC::usize kTokenCount = sizeof(kTokenFields) / sizeof(kTokenFields[0]);

        for (QC::usize i = 0; i < kThemeCount; ++i)
        {
            const BuiltinTheme theme = makeBuiltinTheme(kThemes[i]);
            if (!validateThemeRow(database, theme))
                return false;
        }

        // A small representative token subset is enough to prove round-trip fidelity.
        for (QC::usize i = 0; i < kThemeCount; ++i)
        {
            const BuiltinTheme theme = makeBuiltinTheme(kThemes[i]);
            if (!validateThemeTokenRow(database, theme, kTokenFields[0]))
                return false;
            if (!validateThemeTokenRow(database, theme, kTokenFields[kTokenCount - 1]))
                return false;
        }

        return true;
    }

} // namespace QD
