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

        // Glass-style translucent white button defaults (from Vista base reset).
        static const QC::Color kGlassNormal  = QC::Color(0xFF, 0xFF, 0xFF, 0x33);
        static const QC::Color kGlassHover   = QC::Color(0xFF, 0xFF, 0xFF, 0x4D);
        static const QC::Color kGlassPressed = QC::Color(0xFF, 0xFF, 0xFF, 0x66);
        static const QC::Color kShadowNone   = QC::Color(0x00, 0x00, 0x00, 0x00);

        static const BuiltinTheme kThemes[] = {
            {
                "winter", "Citadel Winter",
                QC::Color(0x1C, 0x24, 0x2C, 0xFF), // windowBackground
                QC::Color(0x4A, 0x6A, 0x8A, 0xFF), // titleBarGradientStart
                QC::Color(0x2E, 0x3A, 0x45, 0xFF), // titleBarGradientEnd
                kGlassNormal, kGlassHover, kGlassPressed,
                QC::Color(0x7A, 0xA0, 0xC8, 0x80), // buttonGlow
                QC::Color(0xFF, 0xFF, 0xFF, 0xFF), // textPrimary
                QC::Color(0xE6, 0xF0, 0xFA, 0xFF), // textSecondary
                QC::Color(0x2E, 0x3A, 0x45, 0xFF), // border
                kShadowNone,
                QC::Color(0x7A, 0xA0, 0xC8, 0xFF), // accentPrimary
                QC::Color(0xBF, 0xD6, 0xF0, 0xFF), // accentSecondary
            },
            {
                "spring", "Citadel Spring",
                QC::Color(0x1F, 0x2A, 0x1F, 0xFF),
                QC::Color(0x4A, 0x8F, 0x4A, 0xFF),
                QC::Color(0x3A, 0x4A, 0x3A, 0xFF),
                kGlassNormal, kGlassHover, kGlassPressed,
                QC::Color(0x6C, 0xBF, 0x6C, 0x80),
                QC::Color(0xFF, 0xFF, 0xFF, 0xFF),
                QC::Color(0xE8, 0xF5, 0xE9, 0xFF),
                QC::Color(0x3A, 0x4A, 0x3A, 0xFF),
                kShadowNone,
                QC::Color(0x6C, 0xBF, 0x6C, 0xFF),
                QC::Color(0xA8, 0xE6, 0xA8, 0xFF),
            },
            {
                "summer", "Citadel Summer",
                QC::Color(0x1E, 0x2A, 0x33, 0xFF),
                QC::Color(0x2F, 0x6F, 0xA3, 0xFF),
                QC::Color(0x2F, 0x3E, 0x4A, 0xFF),
                kGlassNormal, kGlassHover, kGlassPressed,
                QC::Color(0x4F, 0xA7, 0xE3, 0x80),
                QC::Color(0xFF, 0xFF, 0xFF, 0xFF),
                QC::Color(0xE3, 0xF4, 0xFF, 0xFF),
                QC::Color(0x2F, 0x3E, 0x4A, 0xFF),
                kShadowNone,
                QC::Color(0x4F, 0xA7, 0xE3, 0xFF),
                QC::Color(0x8F, 0xD1, 0xFF, 0xFF),
            },
            {
                "autumn", "Citadel Autumn",
                QC::Color(0x2A, 0x1E, 0x14, 0xFF),
                QC::Color(0x8A, 0x4F, 0x12, 0xFF),
                QC::Color(0x3C, 0x2A, 0x1D, 0xFF),
                kGlassNormal, kGlassHover, kGlassPressed,
                QC::Color(0xD9, 0x82, 0x2B, 0x80),
                QC::Color(0xFF, 0xFF, 0xFF, 0xFF),
                QC::Color(0xFF, 0xE8, 0xCC, 0xFF),
                QC::Color(0x3C, 0x2A, 0x1D, 0xFF),
                kShadowNone,
                QC::Color(0xD9, 0x82, 0x2B, 0xFF),
                QC::Color(0xFF, 0xBE, 0x78, 0xFF),
            },
            {
                "standard", "Citadel Standard",
                QC::Color(0x20, 0x20, 0x20, 0xFF),
                QC::Color(0x2F, 0x5A, 0x8A, 0xFF),
                QC::Color(0x30, 0x30, 0x30, 0xFF),
                kGlassNormal, kGlassHover, kGlassPressed,
                QC::Color(0x4A, 0x90, 0xE2, 0x80),
                QC::Color(0xFF, 0xFF, 0xFF, 0xFF),
                QC::Color(0xCC, 0xCC, 0xCC, 0xFF),
                QC::Color(0x30, 0x30, 0x30, 0xFF),
                kShadowNone,
                QC::Color(0x4A, 0x90, 0xE2, 0xFF),
                QC::Color(0x6B, 0xB6, 0xFF, 0xFF),
            },
        };

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

        // -----------------------------------------------------------------------
        // Insert helpers
        // -----------------------------------------------------------------------

        static QCQL::Status insertThemeRow(QCQL::Database &db, const BuiltinTheme &t)
        {
            QCQL::Row row{};
            row.cells.push_back(makeTextCell(t.slug));
            row.cells.push_back(makeTextCell(t.displayName));
            row.cells.push_back(makeTextCell("{}"));
            QC::u32 pageId = 0;
            const QCQL::Status st =
                QCQL::Engine::instance().insertRowByName(db, "Themes", row, &pageId);
            if (st == QCQL::Status::AlreadyExists)
                return QCQL::Status::Success;
            return st;
        }

        static QCQL::Status insertThemeTokens(QCQL::Database &db, const BuiltinTheme &t)
        {
            constexpr QC::usize kFieldCount =
                sizeof(kTokenFields) / sizeof(kTokenFields[0]);

            for (QC::usize i = 0; i < kFieldCount; ++i)
            {
                const TokenField &tf = kTokenFields[i];
                const QC::Color &c = colorAt(t, tf.offset);

                char tokenId[96];
                if (!makeTokenId(t.slug, tf.key, tokenId, sizeof(tokenId)))
                    return QCQL::Status::Error;

                char hexVal[10];
                colorToHex(c, hexVal);

                QCQL::Row row{};
                row.cells.push_back(makeTextCell(tokenId));   // id (PK)
                row.cells.push_back(makeTextCell(t.slug));    // themeId
                row.cells.push_back(makeTextCell(tf.key));    // tokenKey
                row.cells.push_back(makeTextCell(hexVal));    // tokenValue

                QC::u32 pageId = 0;
                const QCQL::Status st =
                    QCQL::Engine::instance().insertRowByName(db, "ThemeTokens", row, &pageId);
                if (st != QCQL::Status::Success && st != QCQL::Status::AlreadyExists)
                    return st;
            }
            return QCQL::Status::Success;
        }

    } // namespace

    // ---------------------------------------------------------------------------

    QCQL::Status ThemeImporter::importBuiltinThemes(QCQL::Database &database)
    {
        constexpr QC::usize kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

        for (QC::usize i = 0; i < kThemeCount; ++i)
        {
            const BuiltinTheme &t = kThemes[i];

            QCQL::Status st = insertThemeRow(database, t);
            if (st != QCQL::Status::Success)
                return st;

            st = insertThemeTokens(database, t);
            if (st != QCQL::Status::Success)
                return st;
        }

        return QCQL::Status::Success;
    }

} // namespace QD
