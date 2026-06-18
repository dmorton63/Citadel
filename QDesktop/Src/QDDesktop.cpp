// QDesktop Desktop - Implementation using Window and Controls
// Namespace: QD

#include "QDDesktop.h"
#include "QDColorUtils.h"
#include "QDThemeAssets.h"
#include "QWWindowManager.h"
#include "QCJson.h"
#include "QDCommandProcessor.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QCBuiltins.h"
#include "QFSVFS.h"
#include "QFSFile.h"
#include "QWStyleSystem.h"
#include "QWStyleTypes.h"
#include "QDShutdownDialog.h"
#include "QDSetupWizard.h"
#include "QDLoginDialog.h"
#include "QDBrowser.h"
#include "QDCuiMLViewer.h"
#include "QKEventManager.h"
#include "QKShutdownController.h"
#include "QKSecurityCenter.h"
#include "QGPainter.h"
#include "QG/FontManager.h"
#include "QG/Image.h"
#include "QG/SVG.h"
#include "QWControls/Leaf/ImageView.h"

#include "QKBootConfigTier.h"

#include "QDrvTimer.h"
#include "QWControls/Leaf/ScrollBar.h"
#include "QCMSApp.h"
#include "QDDesktopDocumentIO.h"
#include "QDThemeImporter.h"

namespace QD
{
    namespace
    {
        constexpr const char *LOG_MODULE = "QDesktop";
        constexpr const char *CMMS_DB_PATH = "/system/CMMS.QDB";
        constexpr const char *CMMS_DESKTOP_LAYOUT_TABLE = "DesktopLayouts";
        constexpr const char *CMMS_DESKTOP_LAYOUT_CHUNK_TABLE = "DesktopLayoutChunks";
        constexpr const char *CMMS_DESKTOP_CUIML_TABLE = "DesktopCuiml";
        constexpr const char *CMMS_DESKTOP_CUIML_CHUNK_TABLE = "DesktopCuimlChunks";
        constexpr const char *CMMS_DESKTOP_REGION_TABLE = "DesktopRegions";
        constexpr const char *CMMS_DESKTOP_CONTROL_TABLE = "DesktopControls";
        constexpr const char *CMMS_DESKTOP_CONTROL_RUNTIME_TABLE = "DesktopControlRuntime";
        constexpr const char *CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE = "DesktopControlProperties";
        constexpr const char *CMMS_DESKTOP_CONTROL_BINDINGS_TABLE = "DesktopControlBindings";
        constexpr const char *CMMS_DESKTOP_LAYOUT_THEME_TABLE = "DesktopLayoutThemes";
        constexpr const char *CMMS_DESKTOP_LAYOUT_CAPABILITY_TABLE = "DesktopLayoutCapabilities";
        constexpr const char *CMMS_DESKTOP_LAYOUT_ASSET_TABLE = "DesktopLayoutAssets";
        constexpr const char *CMMS_DESKTOP_LAYOUT_MATERIALIZATION_TABLE = "DesktopLayoutMaterialization";
        constexpr const char *CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE = "DesktopControlHierarchy";
        constexpr const char *CMMS_DESKTOP_LAYOUT_PRODUCTION = "production";
        constexpr const char *CMMS_DESKTOP_LAYOUT_GOLDEN = "golden";
        constexpr QC::u32 CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES = 1024;
        constexpr QC::u32 CMMS_LAYOUT_MATERIALIZATION_SCHEMA_VERSION = 2;
        constexpr bool CMMS_MATERIALIZE_RUNTIME_ON_BOOT = false;
        constexpr bool CMMS_ENSURE_ACTIVE_RUNTIME_ON_BOOT = false;
        constexpr float BASE_THEME_FONT_SIZE = 12.0f;
        static bool g_imageCorpusChecked = false;

        static QCQL::Cell makeTextCell(const char *text)
        {
            QCQL::Cell cell{};
            cell.type = QCQL::ColumnType::Text;
            if (!text)
                return cell;

            const QC::usize len = QC::String::strlen(text);
            for (QC::usize i = 0; i < len; ++i)
                cell.bytes.push_back(static_cast<QC::u8>(text[i]));
            return cell;
        }

        // Sidebar item labels
        static const char *SIDEBAR_LABELS[] = {
            "Home",
            "Apps",
            "Settings",
            "Files",
            "Terminal",
            "Power"};

        static bool copyCellText(const QCQL::Cell &cell, char *dst, QC::usize dstSize)
        {
            if (!dst || dstSize == 0 || cell.type != QCQL::ColumnType::Text)
                return false;

            const QC::usize copyLen = (cell.bytes.size() < (dstSize - 1))
                                          ? cell.bytes.size()
                                          : static_cast<QC::usize>(dstSize - 1);
            for (QC::usize i = 0; i < copyLen; ++i)
                dst[i] = static_cast<char>(cell.bytes[i]);
            dst[copyLen] = '\0';
            return true;
        }

        static bool cellMatchesText(const QCQL::Cell &cell, const char *text)
        {
            if (cell.type != QCQL::ColumnType::Text)
                return false;

            const QC::usize textLen = text ? QC::String::strlen(text) : 0;
            if (cell.bytes.size() != textLen)
                return false;

            for (QC::usize i = 0; i < textLen; ++i)
            {
                if (cell.bytes[i] != static_cast<QC::u8>(text[i]))
                    return false;
            }
            return true;
        }

        static QCQL::Cell makeUnsignedTextCell(QC::u32 value)
        {
            char reversed[16];
            QC::usize count = 0;
            do
            {
                reversed[count++] = static_cast<char>('0' + (value % 10u));
                value /= 10u;
            } while (value != 0 && count + 1 < sizeof(reversed));

            char text[16];
            QC::String::memset(text, 0, sizeof(text));
            for (QC::usize i = 0; i < count; ++i)
                text[i] = reversed[count - 1 - i];
            text[count] = '\0';
            return makeTextCell(text);
        }

        static QCQL::Cell makeSignedTextCell(QC::i32 value)
        {
            if (value >= 0)
                return makeUnsignedTextCell(static_cast<QC::u32>(value));

            QCQL::Cell magnitudeCell = makeUnsignedTextCell(static_cast<QC::u32>(-value));
            QCQL::Cell cell{};
            cell.type = QCQL::ColumnType::Text;
            cell.bytes.push_back(static_cast<QC::u8>('-'));
            for (QC::usize i = 0; i < magnitudeCell.bytes.size(); ++i)
                cell.bytes.push_back(magnitudeCell.bytes[i]);
            return cell;
        }

        static const char *backgroundModeName(DesktopBackgroundMode mode)
        {
            switch (mode)
            {
            case DesktopBackgroundMode::Solid:
                return "solid";
            case DesktopBackgroundMode::Gradient:
                return "gradient";
            case DesktopBackgroundMode::Image:
                return "image";
            case DesktopBackgroundMode::None:
            default:
                return "none";
            }
        }

        static const char *assetKindName(DesktopAssetKind kind)
        {
            switch (kind)
            {
            case DesktopAssetKind::Wallpaper:
                return "wallpaper";
            case DesktopAssetKind::Icon:
                return "icon";
            case DesktopAssetKind::Illustration:
                return "illustration";
            case DesktopAssetKind::Font:
                return "font";
            case DesktopAssetKind::Import:
                return "import";
            case DesktopAssetKind::Unknown:
            default:
                return "unknown";
            }
        }

        static bool makeScopedRowId(const char *scope, const char *name, char *out, QC::usize outCap)
        {
            if (!scope || !*scope || !name || !*name || !out || outCap == 0)
                return false;

            const QC::usize scopeLen = QC::String::strlen(scope);
            const QC::usize nameLen = QC::String::strlen(name);
            if (scopeLen + 1 + nameLen + 1 > outCap)
                return false;

            QC::String::memcpy(out, scope, scopeLen);
            out[scopeLen] = ':';
            QC::String::memcpy(out + scopeLen + 1, name, nameLen);
            out[scopeLen + 1 + nameLen] = '\0';
            return true;
        }

        static bool makeScopedIndexedRowId(const char *scope,
                                           const char *prefix,
                                           QC::u32 index,
                                           char *out,
                                           QC::usize outCap)
        {
            if (!scope || !*scope || !prefix || !*prefix || !out || outCap == 0)
                return false;

            char prefixAndIndex[64];
            QC::String::memset(prefixAndIndex, 0, sizeof(prefixAndIndex));
            const QC::usize prefixLen = QC::String::strlen(prefix);
            if (prefixLen + 1 >= sizeof(prefixAndIndex))
                return false;

            QC::String::memcpy(prefixAndIndex, prefix, prefixLen);
            QCQL::Cell indexCell = makeUnsignedTextCell(index);
            const QC::usize indexLen = (indexCell.bytes.size() < (sizeof(prefixAndIndex) - prefixLen - 1))
                                           ? indexCell.bytes.size()
                                           : static_cast<QC::usize>(sizeof(prefixAndIndex) - prefixLen - 1);
            for (QC::usize i = 0; i < indexLen; ++i)
                prefixAndIndex[prefixLen + i] = static_cast<char>(indexCell.bytes[i]);
            prefixAndIndex[prefixLen + indexLen] = '\0';
            return makeScopedRowId(scope, prefixAndIndex, out, outCap);
        }

        static bool parseUnsignedTextCell(const QCQL::Cell &cell, QC::u32 &outValue)
        {
            outValue = 0;
            if (cell.type != QCQL::ColumnType::Text || cell.bytes.empty())
                return false;

            QC::u32 value = 0;
            for (QC::usize i = 0; i < cell.bytes.size(); ++i)
            {
                const char c = static_cast<char>(cell.bytes[i]);
                if (c < '0' || c > '9')
                    return false;
                value = value * 10u + static_cast<QC::u32>(c - '0');
            }

            outValue = value;
            return true;
        }

        static bool makeChunkRowId(const char *documentId, QC::u32 chunkIndex, char *out, QC::usize outCap)
        {
            if (!documentId || !*documentId || !out || outCap == 0)
                return false;

            char indexText[16];
            QC::String::memset(indexText, 0, sizeof(indexText));
            const QCQL::Cell indexCell = makeUnsignedTextCell(chunkIndex);
            const QC::usize indexLen = (indexCell.bytes.size() < (sizeof(indexText) - 1))
                                           ? indexCell.bytes.size()
                                           : static_cast<QC::usize>(sizeof(indexText) - 1);
            for (QC::usize i = 0; i < indexLen; ++i)
                indexText[i] = static_cast<char>(indexCell.bytes[i]);
            indexText[indexLen] = '\0';

            const QC::usize docLen = QC::String::strlen(documentId);
            if (docLen + 1 + indexLen + 1 > outCap)
                return false;

            QC::String::memcpy(out, documentId, docLen);
            out[docLen] = ':';
            QC::String::memcpy(out + docLen + 1, indexText, indexLen);
            out[docLen + 1 + indexLen] = '\0';
            return true;
        }

        static bool loadChunkedDocumentPayload(const QCQL::Database &database,
                                               const char *chunkTableName,
                                               const char *documentId,
                                               QC::u32 chunkCount,
                                               QC::Vector<QC::u8> &outPayload)
        {
            outPayload.clear();
            if (!chunkTableName || !*chunkTableName || !documentId || !*documentId || chunkCount == 0)
                return false;

            QC::u32 tableId = 0;
            if (QCQL::Engine::instance().lookupTableId(database, chunkTableName, tableId) != QCQL::Status::Success)
                return false;

            const QCQL::Table *chunkTable = nullptr;
            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (database.tables[i].tableId == tableId)
                {
                    chunkTable = &database.tables[i];
                    break;
                }
            }
            if (!chunkTable)
                return false;

            QC::Vector<QC::Vector<QC::u8>> chunks;
            chunks.resize(chunkCount);
            QC::Vector<QC::u8> seen;
            seen.resize(chunkCount);
            for (QC::usize i = 0; i < chunkCount; ++i)
                seen[i] = 0;

            QC::usize totalBytes = 0;
            for (QC::usize p = 0; p < chunkTable->pages.size(); ++p)
            {
                QCQL::Page page{};
                if (QCQL::Engine::instance().loadPage(database, chunkTable->pages[p], page) != QCQL::Status::Success)
                    continue;

                for (QC::usize r = 0; r < page.rowOffsets.size(); ++r)
                {
                    QCQL::Row row{};
                    if (QCQL::Engine::instance().readRow(database, chunkTable->pages[p], page.rowOffsets[r], row) != QCQL::Status::Success)
                        continue;
                    if (row.tombstone || row.cells.size() < 4)
                        continue;
                    if (!cellMatchesText(row.cells[1], documentId))
                        continue;

                    QC::u32 chunkIndex = 0;
                    if (!parseUnsignedTextCell(row.cells[2], chunkIndex) || chunkIndex >= chunkCount)
                        continue;
                    if (row.cells[3].type != QCQL::ColumnType::Text || seen[chunkIndex])
                        continue;

                    chunks[chunkIndex] = row.cells[3].bytes;
                    seen[chunkIndex] = 1;
                    totalBytes += chunks[chunkIndex].size();
                }
            }

            for (QC::usize i = 0; i < chunkCount; ++i)
            {
                if (!seen[i])
                    return false;
            }

            outPayload.resize(totalBytes);
            QC::usize offset = 0;
            for (QC::usize i = 0; i < chunkCount; ++i)
            {
                for (QC::usize j = 0; j < chunks[i].size(); ++j)
                    outPayload[offset + j] = chunks[i][j];
                offset += chunks[i].size();
            }
            return true;
        }

        static QW::Controls::ControlId hashControlId(const char *text)
        {
            if (!text || !*text)
                return QW::Controls::InvalidControlId;

            // FNV-1a 32-bit
            QC::u32 hash = 2166136261u;
            for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
            {
                hash ^= static_cast<QC::u32>(*p);
                hash *= 16777619u;
            }

            // 0 is reserved as InvalidControlId.
            return (hash == 0) ? 1u : hash;
        }

        static void onJsonSliderOpenLogin(QW::Controls::ScrollBar *slider, void *userData)
        {
            auto *desktop = static_cast<Desktop *>(userData);
            if (!desktop || !slider)
                return;

            if (slider->value() >= slider->maximum())
            {
                desktop->showLoginDialog();
            }
            else if (slider->value() <= slider->minimum())
            {
                desktop->hideLoginDialog();
            }
        }

        inline QC::i32 parseInt(const char *s, bool *ok)
        {
            if (ok)
                *ok = false;
            if (!s || !*s)
                return 0;

            bool neg = false;
            QC::usize i = 0;
            if (s[0] == '-')
            {
                neg = true;
                i = 1;
            }

            if (!s[i])
                return 0;

            QC::i32 v = 0;
            for (; s[i]; ++i)
            {
                if (s[i] < '0' || s[i] > '9')
                    return 0;
                v = v * 10 + (s[i] - '0');
            }

            if (ok)
                *ok = true;
            return neg ? -v : v;
        }

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

        static bool readVfsFileToBytes(const char *path, QC::Vector<QC::u8> &out)
        {
            out.clear();
            if (!path || !*path)
                return false;

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                return false;

            const QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 8 * 1024 * 1024)
            {
                QFS::VFS::instance().close(file);
                return false;
            }

            out.resize(static_cast<QC::usize>(size64));
            const QC::isize n = file->read(out.data(), static_cast<QC::usize>(size64));
            QFS::VFS::instance().close(file);

            if (n <= 0)
            {
                out.clear();
                return false;
            }

            out.resize(static_cast<QC::usize>(n));
            return true;
        }

        static bool buildFontPath(char *out, QC::usize outCap,
                                  const char *dir,
                                  const char *family,
                                  const char *ext)
        {
            if (!out || outCap == 0 || !dir || !family || !ext)
                return false;

            const QC::usize dirLen = QC::String::strlen(dir);
            const QC::usize famLen = QC::String::strlen(family);
            const QC::usize extLen = QC::String::strlen(ext);
            const bool needsSlash = (dirLen > 0 && dir[dirLen - 1] != '/');
            const QC::usize total = dirLen + (needsSlash ? 1u : 0u) + famLen + extLen;
            if (total + 1 > outCap)
                return false;

            QC::usize pos = 0;
            if (dirLen)
            {
                QC::String::memcpy(out + pos, dir, dirLen);
                pos += dirLen;
            }
            if (needsSlash)
                out[pos++] = '/';
            if (famLen)
            {
                QC::String::memcpy(out + pos, family, famLen);
                pos += famLen;
            }
            if (extLen)
            {
                QC::String::memcpy(out + pos, ext, extLen);
                pos += extLen;
            }
            out[pos] = '\0';
            return true;
        }

        static bool tryLoadFontFamilyFromVfs(const char *family, QC::Vector<QC::u8> &outBytes)
        {
            outBytes.clear();
            if (!family || !*family)
                return false;

            const char *dirs[] = {"/FONTS", "/FONTS/static", "/system/fonts", "/system/fonts/static", "/shared/fonts", "/shared/fonts/static"};
            const char *exts[] = {".ttf", ".otf"};

            char path[160];
            for (QC::usize di = 0; di < (sizeof(dirs) / sizeof(dirs[0])); ++di)
            {
                for (QC::usize ei = 0; ei < 2; ++ei)
                {
                    if (!buildFontPath(path, sizeof(path), dirs[di], family, exts[ei]))
                        continue;
                    if (readVfsFileToBytes(path, outBytes) && !outBytes.empty())
                        return true;
                }
            }

            // FAT 8.3 compatibility aliases (no LFN): map friendly family names to stable short filenames.
            // build.sh packs RobotoMono-Regular.ttf under both /FONTS/RMONO.TTF and
            // /system/fonts/RMONO.TTF so the desktop can survive /system volume shadowing.
            if (equalsIgnoreCase(family, "RobotoMono") || equalsIgnoreCase(family, "Roboto Mono"))
            {
                if (readVfsFileToBytes("/FONTS/RMONO.TTF", outBytes) && !outBytes.empty())
                    return true;
                if (readVfsFileToBytes("/system/fonts/RMONO.TTF", outBytes) && !outBytes.empty())
                    return true;
            }

            // Convenience fallback for common VariableFont naming.
            // If the user asks for "OpenSans" but only has
            // "OpenSans-VariableFont_wdth,wght.ttf", try that.
            const char *suffixes[] = {
                "-VariableFont_wght",
                "-VariableFont_opsz,wght",
                "-VariableFont_wdth,wght",
            };

            char base[96];
            const QC::usize famLen = QC::String::strlen(family);
            for (QC::usize si = 0; si < 3; ++si)
            {
                const char *suffix = suffixes[si];
                const QC::usize sufLen = QC::String::strlen(suffix);
                if (famLen + sufLen + 1 > sizeof(base))
                    continue;

                QC::String::memcpy(base, family, famLen);
                QC::String::memcpy(base + famLen, suffix, sufLen);
                base[famLen + sufLen] = '\0';

                for (QC::usize di = 0; di < (sizeof(dirs) / sizeof(dirs[0])); ++di)
                {
                    if (!buildFontPath(path, sizeof(path), dirs[di], base, ".ttf"))
                        continue;
                    if (readVfsFileToBytes(path, outBytes) && !outBytes.empty())
                        return true;
                }
            }

            return false;
        }

        enum class Season
        {
            Unknown,
            Spring,
            Summer,
            Autumn,
            Winter,
        };

        struct RtcDateTime
        {
            QC::u8 second = 0;
            QC::u8 minute = 0;
            QC::u8 hour = 0;
            QC::u8 day = 0;
            QC::u8 month = 0;
            QC::u8 year = 0;
            QC::u8 statusB = 0;
        };

        static inline void ioWait()
        {
            // Classic ISA "wait" port.
            QC::outb(0x80, 0);
        }

        static QC::u8 cmosRead(QC::u8 reg)
        {
            // Disable NMI (bit 7) during access.
            QC::outb(0x70, static_cast<QC::u8>(0x80u | reg));
            ioWait();
            return QC::inb(0x71);
        }

        static inline bool rtcUpdateInProgress()
        {
            return (cmosRead(0x0A) & 0x80u) != 0;
        }

        static inline QC::u8 bcdToBin(QC::u8 v)
        {
            return static_cast<QC::u8>((v & 0x0Fu) + ((v >> 4) * 10u));
        }

        static bool tryReadRtcStable(RtcDateTime &out)
        {
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                while (rtcUpdateInProgress())
                {
                }

                RtcDateTime a{};
                a.second = cmosRead(0x00);
                a.minute = cmosRead(0x02);
                a.hour = cmosRead(0x04);
                a.day = cmosRead(0x07);
                a.month = cmosRead(0x08);
                a.year = cmosRead(0x09);
                a.statusB = cmosRead(0x0B);

                while (rtcUpdateInProgress())
                {
                }

                RtcDateTime b{};
                b.second = cmosRead(0x00);
                b.minute = cmosRead(0x02);
                b.hour = cmosRead(0x04);
                b.day = cmosRead(0x07);
                b.month = cmosRead(0x08);
                b.year = cmosRead(0x09);
                b.statusB = cmosRead(0x0B);

                const bool same = (a.second == b.second) && (a.minute == b.minute) && (a.hour == b.hour) &&
                                  (a.day == b.day) && (a.month == b.month) && (a.year == b.year) &&
                                  (a.statusB == b.statusB);
                if (same)
                {
                    out = a;
                    return true;
                }
            }
            return false;
        }

        static bool normalizeRtcToBinaryAnd24h(RtcDateTime &dt)
        {
            // Status B:
            // - bit 1 (0x02): 24-hour mode when set
            // - bit 2 (0x04): binary mode when set; BCD when clear
            const bool is24h = (dt.statusB & 0x02u) != 0;
            const bool isBinary = (dt.statusB & 0x04u) != 0;

            QC::u8 hour = dt.hour;
            const bool pm = (!is24h && (hour & 0x80u) != 0);
            if (!is24h)
            {
                hour &= 0x7Fu;
            }

            if (!isBinary)
            {
                dt.second = bcdToBin(dt.second);
                dt.minute = bcdToBin(dt.minute);
                hour = bcdToBin(hour);
                dt.day = bcdToBin(dt.day);
                dt.month = bcdToBin(dt.month);
                dt.year = bcdToBin(dt.year);
            }

            if (!is24h)
            {
                // Convert 12-hour -> 24-hour.
                // 12:xx AM => 00:xx; 12:xx PM stays 12:xx.
                if (hour == 12)
                    hour = 0;
                if (pm)
                    hour = static_cast<QC::u8>((hour + 12) % 24);
            }

            dt.hour = hour;

            if (dt.month < 1 || dt.month > 12)
                return false;
            if (dt.day < 1 || dt.day > 31)
                return false;
            if (dt.hour > 23 || dt.minute > 59 || dt.second > 59)
                return false;

            return true;
        }

        static bool tryGetRtcMonthDay(QC::u8 &outMonth, QC::u8 &outDay)
        {
            RtcDateTime dt{};
            if (!tryReadRtcStable(dt))
                return false;
            if (!normalizeRtcToBinaryAnd24h(dt))
                return false;
            outMonth = dt.month;
            outDay = dt.day;
            return true;
        }

        static Season seasonFromMonth(QC::u8 month)
        {
            if (month >= 3 && month <= 5)
                return Season::Spring;
            if (month >= 6 && month <= 8)
                return Season::Summer;
            if (month >= 9 && month <= 11)
                return Season::Autumn;
            return Season::Winter;
        }

        static const char *seasonToken(Season season)
        {
            switch (season)
            {
            case Season::Spring:
                return "spring";
            case Season::Summer:
                return "summer";
            case Season::Autumn:
                return "autumn";
            case Season::Winter:
                return "winter";
            default:
                return "unknown";
            }
        }

        static Season parseSeasonToken(const char *s)
        {
            if (!s || !*s)
                return Season::Unknown;
            if (equalsIgnoreCase(s, "spring"))
                return Season::Spring;
            if (equalsIgnoreCase(s, "summer"))
                return Season::Summer;
            if (equalsIgnoreCase(s, "autumn") || equalsIgnoreCase(s, "fall"))
                return Season::Autumn;
            if (equalsIgnoreCase(s, "winter"))
                return Season::Winter;
            return Season::Unknown;
        }

        static void seasonCandidatePaths(Season season, const char **outPaths, QC::usize outCap, QC::usize *outCount)
        {
            if (!outPaths || !outCount || outCap == 0)
                return;
            *outCount = 0;

            const bool forceGolden = (QK::Boot::Config::GetActiveConfigTier() == QK::Boot::Config::ConfigTier::Golden);

            switch (season)
            {
            case Season::Spring:
            {
                static const char *kPathsProdFirst[] = {"/PROD/DSPRING.JSN", "/GOLDEN/DSPRING.JSN", "/DSPRING.JSN"};
                static const char *kPathsGoldenOnly[] = {"/GOLDEN/DSPRING.JSN", "/DSPRING.JSN"};

                const char **paths = forceGolden ? kPathsGoldenOnly : kPathsProdFirst;
                const QC::usize n = forceGolden ? (sizeof(kPathsGoldenOnly) / sizeof(kPathsGoldenOnly[0]))
                                                : (sizeof(kPathsProdFirst) / sizeof(kPathsProdFirst[0]));
                for (QC::usize i = 0; i < n && *outCount < outCap; ++i)
                    outPaths[(*outCount)++] = paths[i];
                break;
            }
            case Season::Summer:
            {
                static const char *kPathsProdFirst[] = {"/PROD/DSUMMER.JSN", "/GOLDEN/DSUMMER.JSN", "/DSUMMER.JSN"};
                static const char *kPathsGoldenOnly[] = {"/GOLDEN/DSUMMER.JSN", "/DSUMMER.JSN"};

                const char **paths = forceGolden ? kPathsGoldenOnly : kPathsProdFirst;
                const QC::usize n = forceGolden ? (sizeof(kPathsGoldenOnly) / sizeof(kPathsGoldenOnly[0]))
                                                : (sizeof(kPathsProdFirst) / sizeof(kPathsProdFirst[0]));
                for (QC::usize i = 0; i < n && *outCount < outCap; ++i)
                    outPaths[(*outCount)++] = paths[i];
                break;
            }
            case Season::Autumn:
            {
                static const char *kPathsProdFirst[] = {"/PROD/DAUTUMN.JSN", "/GOLDEN/DAUTUMN.JSN", "/DAUTUMN.JSN"};
                static const char *kPathsGoldenOnly[] = {"/GOLDEN/DAUTUMN.JSN", "/DAUTUMN.JSN"};

                const char **paths = forceGolden ? kPathsGoldenOnly : kPathsProdFirst;
                const QC::usize n = forceGolden ? (sizeof(kPathsGoldenOnly) / sizeof(kPathsGoldenOnly[0]))
                                                : (sizeof(kPathsProdFirst) / sizeof(kPathsProdFirst[0]));
                for (QC::usize i = 0; i < n && *outCount < outCap; ++i)
                    outPaths[(*outCount)++] = paths[i];
                break;
            }
            case Season::Winter:
            {
                static const char *kPathsProdFirst[] = {"/PROD/DWINTER.JSN", "/GOLDEN/DWINTER.JSN", "/DWINTER.JSN"};
                static const char *kPathsGoldenOnly[] = {"/GOLDEN/DWINTER.JSN", "/DWINTER.JSN"};

                const char **paths = forceGolden ? kPathsGoldenOnly : kPathsProdFirst;
                const QC::usize n = forceGolden ? (sizeof(kPathsGoldenOnly) / sizeof(kPathsGoldenOnly[0]))
                                                : (sizeof(kPathsProdFirst) / sizeof(kPathsProdFirst[0]));
                for (QC::usize i = 0; i < n && *outCount < outCap; ++i)
                    outPaths[(*outCount)++] = paths[i];
                break;
            }
            default:
                break;
            }
        }

        static bool looksLikeFullThemeDefinition(const QC::JSON::Value *themeValue)
        {
            if (!themeValue || !themeValue->isObject())
                return false;
            return themeValue->find("base") || themeValue->find("overrides") || themeValue->find("file") || themeValue->find("path") ||
                   themeValue->find("definition") || themeValue->find("colors") || themeValue->find("effects") || themeValue->find("animations") ||
                   themeValue->find("id");
        }

        inline QW::ButtonRole roleForJsonButton(const char *id, const QC::JSON::Value *controlValue)
        {
            if (controlValue)
            {
                if (const QC::JSON::Value *roleValue = controlValue->find("role"))
                {
                    const char *roleText = roleValue->isString() ? roleValue->asString(nullptr) : nullptr;
                    if (roleText)
                    {
                        QW::ButtonRole parsed;
                        if (QW::buttonRoleFromString(roleText, &parsed))
                        {
                            return parsed;
                        }

                        const char *warnId = id ? id : "<unnamed>";
                        QC_LOG_WARN(LOG_MODULE, "Unknown button role '%s' on control '%s'", roleText, warnId);
                    }
                }
            }

            if (id)
            {
                if (QC::String::strcmp(id, "shutDownButton") == 0)
                    return QW::ButtonRole::Destructive;
                if (QC::String::strcmp(id, "startButton") == 0)
                    return QW::ButtonRole::Accent;
                if (startsWith(id, "btn"))
                    return QW::ButtonRole::Sidebar;
            }

            return QW::ButtonRole::Default;
        }

        inline QC::i32 clampNonNegative(QC::i32 v)
        {
            return v < 0 ? 0 : v;
        }

        inline bool parseHexByte(char hi, char lo, QC::u8 *out)
        {
            auto hex = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F')
                    return 10 + (c - 'A');
                return -1;
            };

            int h = hex(hi);
            int l = hex(lo);
            if (h < 0 || l < 0)
                return false;

            *out = static_cast<QC::u8>((h << 4) | l);
            return true;
        }

        inline bool parseHexColor(const char *s, QW::Color *out)
        {
            if (!s || !out)
                return false;
            // #RRGGBB only (matches current desktop.json)
            if (s[0] != '#' || !s[1] || !s[2] || !s[3] || !s[4] || !s[5] || !s[6] || s[7] != '\0')
                return false;

            QC::u8 r, g, b;
            if (!parseHexByte(s[1], s[2], &r))
                return false;
            if (!parseHexByte(s[3], s[4], &g))
                return false;
            if (!parseHexByte(s[5], s[6], &b))
                return false;

            *out = QW::Color(r, g, b, 255);
            return true;
        }

        inline bool evalLayoutValue(const QC::JSON::Value *value, QC::i32 parentW, QC::i32 parentH, bool isX, bool isY, bool isWidth, bool isHeight, QC::i32 *out)
        {
            if (!value || !out)
                return false;

            if (value->isNumber())
            {
                *out = static_cast<QC::i32>(value->asNumber(0.0));
                return true;
            }

            if (!value->isString())
                return false;

            const char *s = value->asString(nullptr);
            if (!s || !*s)
                return false;

            if (isX && startsWith(s, "right-"))
            {
                bool ok = false;
                QC::i32 n = parseInt(s + 6, &ok);
                if (!ok)
                    return false;
                *out = parentW - n;
                return true;
            }

            if (isY && startsWith(s, "bottom-"))
            {
                bool ok = false;
                QC::i32 n = parseInt(s + 7, &ok);
                if (!ok)
                    return false;
                *out = parentH - n;
                return true;
            }

            // Percent or percent +/- constant: e.g. "100%", "100%-48", "50%+10"
            const char *percent = nullptr;
            for (const char *p = s; *p; ++p)
            {
                if (*p == '%')
                {
                    percent = p;
                    break;
                }
            }

            if (percent)
            {
                // Parse leading int (no spaces)
                QC::i32 pValue = 0;
                {
                    bool ok = false;
                    // Copy substring [s, percent)
                    char tmp[16];
                    QC::usize len = static_cast<QC::usize>(percent - s);
                    if (len == 0 || len >= sizeof(tmp))
                        return false;
                    for (QC::usize i = 0; i < len; ++i)
                        tmp[i] = s[i];
                    tmp[len] = '\0';
                    pValue = parseInt(tmp, &ok);
                    if (!ok)
                        return false;
                }

                QC::i32 baseDim = (isX || isWidth) ? parentW : parentH;
                QC::i32 v = (baseDim * pValue) / 100;

                const char *tail = percent + 1;
                if (*tail == '\0')
                {
                    *out = v;
                    return true;
                }

                char op = *tail;
                if (op != '+' && op != '-')
                    return false;
                ++tail;
                bool ok = false;
                QC::i32 n = parseInt(tail, &ok);
                if (!ok)
                    return false;
                *out = (op == '+') ? (v + n) : (v - n);
                return true;
            }

            // Plain integer string
            bool ok = false;
            QC::i32 v = parseInt(s, &ok);
            if (!ok)
                return false;
            *out = v;
            return true;
        }

        inline QW::Rect parseBounds(const QC::JSON::Value *obj, QC::i32 parentW, QC::i32 parentH, const char *type)
        {
            QC::i32 x = 0;
            QC::i32 y = 0;

            QC::i32 w = 0;
            QC::i32 h = 0;

            // Defaults by type
            if (type && QC::String::strcmp(type, "label") == 0)
            {
                w = 200;
                h = 16;
            }
            else if (type && QC::String::strcmp(type, "button") == 0)
            {
                w = 120;
                h = 32;
            }
            else
            {
                w = parentW;
                h = parentH;
            }

            if (obj && obj->isObject())
            {
                if (const QC::JSON::Value *vx = obj->find("x"))
                    (void)evalLayoutValue(vx, parentW, parentH, true, false, false, false, &x);
                if (const QC::JSON::Value *vy = obj->find("y"))
                    (void)evalLayoutValue(vy, parentW, parentH, false, true, false, false, &y);
                if (const QC::JSON::Value *vw = obj->find("width"))
                    (void)evalLayoutValue(vw, parentW, parentH, false, false, true, false, &w);
                if (const QC::JSON::Value *vh = obj->find("height"))
                    (void)evalLayoutValue(vh, parentW, parentH, false, false, false, true, &h);
            }

            w = clampNonNegative(w);
            h = clampNonNegative(h);

            return QW::Rect{x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)};
        }

        inline const char *stringOrNull(const QC::JSON::Value *v)
        {
            return (v && v->isString()) ? v->asString(nullptr) : nullptr;
        }

    } // namespace

    Desktop::Desktop()
        : m_initialized(false),
          m_screenWidth(0),
          m_screenHeight(0),
          m_desktopWindow(nullptr),
          m_jsonDriven(false),
          m_topBar(nullptr),
          m_sidebar(nullptr),
          m_taskbar(nullptr),
          m_jsonStartButton(nullptr),
          m_jsonShutdownButton(nullptr),
          m_jsonWallpaperView(nullptr),
          m_logoButton(nullptr),
          m_titleLabel(nullptr),
          m_clockLabel(nullptr),
          m_taskbarWindowBaseX(4),
          m_selectedSidebarItem(SidebarItem::Home),
          m_taskbarWindowCount(0),
          m_hours(10),
          m_minutes(32),
          m_terminal(nullptr),
                    m_browser(nullptr),
                    m_cuimlViewer(nullptr),
          m_shutdownDialog(nullptr),
          m_setupWizard(nullptr),
            m_loginDialog(nullptr)
    {
        for (QC::u8 i = 0; i < static_cast<QC::u8>(SidebarItem::Count); ++i)
        {
            m_sidebarButtons[i] = nullptr;
        }

        for (QC::u32 i = 0; i < MAX_TASKBAR_WINDOWS; ++i)
        {
            m_taskbarEntries[i].windowId = 0;
            m_taskbarEntries[i].button = nullptr;
            m_taskbarEntries[i].width = 0;
            m_taskbarEntries[i].height = 0;
            m_taskbarEntries[i].isActive = false;
        }

        resetThemeOverrides();
        resetBackgroundConfig();
    }

    Desktop::~Desktop()
    {
        shutdown();
    }

    void Desktop::initialize(QC::u32 screenWidth, QC::u32 screenHeight)
    {
        if (m_initialized)
            return;

        if (!g_imageCorpusChecked)
        {
            QG::ImageDecodeCorpusReport report;
            const bool ok = QG::runPngDecoderCorpus(report);
            QC_LOG_INFO(LOG_MODULE,
                        "PNG decoder corpus: total=%u passed=%u failed=%u status=%s",
                        report.total,
                        report.passed,
                        report.failed,
                        ok ? "PASS" : "FAIL");
            g_imageCorpusChecked = true;
        }

        m_screenWidth = screenWidth;
        m_screenHeight = screenHeight;

        // Create fullscreen desktop window via WindowManager.
        // Keep it hidden until initialization completes to avoid synchronous paints
        // from control/theme setup invalidations.
        QW::Rect desktopBounds = {0, 0, screenWidth, screenHeight};
        m_desktopWindow = QW::WindowManager::instance().createWindow("Desktop", desktopBounds);
        // Desktop is a background surface: never takes focus and is pinned to the bottom.
        // Keep it hidden until initialization completes.
        m_desktopWindow->setFlags(QW::WindowFlags::AlwaysBottom | QW::WindowFlags::NoFocus);

        // Item 33: QCQL-backed layout/theme is now primary; file import is provisioning-only fallback.
        // tryInitializeFromJson() attempts QCQL lookup first (primary), then falls back to file import.
        // tryInitializeFromCuiML() provides alternative file-based provisioning.
        // Boot loading order (enforced in tryInitializeFromJson):
        // 1. QCQL DesktopLayoutTable lookup (primary).
        // 2. If QCQL data is missing/incomplete, fallback to file import (CUI-ML, then JSON).
        // 3. If both fail, use hardcoded defaults.
        const bool initializedFromCuiML = tryInitializeFromCuiML();
        const bool initializedFromJson = !initializedFromCuiML && tryInitializeFromJson();
        if (!initializedFromCuiML && !initializedFromJson)
        {
            // Create the panels
            createTopBar();
            createSidebar();
            createTaskbar();
            recomputeTaskbarWindowBase();

            // Apply colors based on current style
            applyColors();
        }

        if (initializedFromCuiML)
            QC_LOG_INFO(LOG_MODULE, "Desktop mode: CUI-ML\n");
        else if (initializedFromJson)
            QC_LOG_INFO(LOG_MODULE, "Desktop mode: JSON (with QCQL fallback)\n");
        else
            QC_LOG_INFO(LOG_MODULE, "Desktop mode: hardcoded fallback\n");

        QK::Shutdown::Controller::instance().registerUIHandler(&Desktop::onShutdownRequested, this);

        ensureWindowEventListener();

        // Ensure command processor is available for terminals and JSON/app-driven command clients.
        QD::CommandProcessor::instance().initialize();

        // In BYPASS mode we skip owner setup prompts during startup.
        // Enrollment can be completed later when SC backend support is available.
        const bool bypass = QK::SecurityCenter::instance().bypassEnabled();
        if (!bypass && !isOwnerEnrolled())
        {
            showSetupWizard();
        }
        else
        {
            // If SC is enforcing and an owner exists, prompt to unlock early.
            if (!QK::SecurityCenter::instance().bypassEnabled())
            {
                showLoginDialog();
            }
        }

        if (m_desktopWindow)
        {
            const QC::u64 t0 = QDrv::Timer::instance().milliseconds();
            m_desktopWindow->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::AlwaysBottom | QW::WindowFlags::NoFocus);
            QW::WindowManager::instance().sendToBack(m_desktopWindow);
            const QC::u64 t1 = QDrv::Timer::instance().milliseconds();
            QC_LOG_INFO(LOG_MODULE, "Desktop show request dt=%llums\n", static_cast<unsigned long long>(t1 - t0));
        }

        m_initialized = true;
    }

    void Desktop::resize(QC::u32 screenWidth, QC::u32 screenHeight)
    {
        m_screenWidth = screenWidth;
        m_screenHeight = screenHeight;

        if (m_desktopWindow)
        {
            QW::Rect bounds = {0, 0, screenWidth, screenHeight};
            m_desktopWindow->setBounds(bounds);
        }

        // Ensure command processor is available for JSON/app-driven terminals.
        QD::CommandProcessor::instance().initialize();
        updateLayout();
    }

    void Desktop::shutdown()
    {
        if (!m_initialized)
            return;

        QK::Shutdown::Controller::instance().registerUIHandler(nullptr, nullptr);

        if (m_windowListenerId != QK::Event::InvalidListenerId)
        {
            QK::Event::EventManager::instance().removeListener(m_windowListenerId);
            m_windowListenerId = QK::Event::InvalidListenerId;
        }

        if (m_inputListenerId != QK::Event::InvalidListenerId)
        {
            QK::Event::EventManager::instance().removeListener(m_inputListenerId);
            m_inputListenerId = QK::Event::InvalidListenerId;
        }

        if (m_shutdownDialog)
        {
            delete m_shutdownDialog;
            m_shutdownDialog = nullptr;
        }

        if (m_setupWizard)
        {
            delete m_setupWizard;
            m_setupWizard = nullptr;
        }

        if (m_loginDialog)
        {
            delete m_loginDialog;
            m_loginDialog = nullptr;
        }

        if (m_browser)
        {
            delete m_browser;
            m_browser = nullptr;
        }

        if (m_cuimlViewer)
        {
            delete m_cuimlViewer;
            m_cuimlViewer = nullptr;
        }

        if (m_terminal)
        {
            delete m_terminal;
            m_terminal = nullptr;
        }

        if (m_cmmsDatabaseReady)
        {
            QCQL::Engine::instance().closeDatabase(m_cmmsDatabase);
            m_cmmsDatabaseReady = false;
        }

        if (m_jsonDriven)
        {
            clearJsonDesktopState();

            // Clean up window (via WindowManager since it was created there)
            if (m_desktopWindow)
            {
                QW::WindowManager::instance().destroyWindow(m_desktopWindow);
                m_desktopWindow = nullptr;
            }

            m_initialized = false;
            return;
        }

        // Clean up taskbar buttons
        for (QC::u32 i = 0; i < m_taskbarWindowCount; ++i)
        {
            if (m_taskbarEntries[i].button)
            {
                delete m_taskbarEntries[i].button;
                m_taskbarEntries[i].button = nullptr;
            }
        }
        m_taskbarWindowCount = 0;

        // Clean up sidebar buttons
        for (QC::u8 i = 0; i < static_cast<QC::u8>(SidebarItem::Count); ++i)
        {
            if (m_sidebarButtons[i])
            {
                delete m_sidebarButtons[i];
                m_sidebarButtons[i] = nullptr;
            }
        }

        // Clean up labels
        delete m_clockLabel;
        m_clockLabel = nullptr;
        delete m_titleLabel;
        m_titleLabel = nullptr;
        delete m_logoButton;
        m_logoButton = nullptr;

        // Clean up panels
        if (m_desktopWindow && m_desktopWindow->root())
        {
            m_desktopWindow->root()->clearChildren();
        }

        delete m_taskbar;
        m_taskbar = nullptr;
        delete m_sidebar;
        m_sidebar = nullptr;
        delete m_topBar;
        m_topBar = nullptr;

        // Clean up window (via WindowManager since it was created there)
        if (m_desktopWindow)
        {
            QW::WindowManager::instance().destroyWindow(m_desktopWindow);
            m_desktopWindow = nullptr;
        }

        m_initialized = false;

        releaseImageAssets();
        resetBackgroundConfig();
    }

    bool Desktop::isOwnerEnrolled() const
    {
        return QK::SecurityCenter::instance().ownerIsEnrolled();
    }

    void Desktop::showSetupWizard()
    {
        if (!m_setupWizard)
        {
            m_setupWizard = new SetupWizard(this);
        }

        if (m_setupWizard)
        {
            m_setupWizard->open();
        }
    }

    void Desktop::showLoginDialog()
    {
        if (!m_loginDialog)
        {
            m_loginDialog = new LoginDialog(this);
        }

        if (m_loginDialog)
        {
            m_loginDialog->open();
        }
    }

    void Desktop::hideLoginDialog()
    {
        if (m_loginDialog)
        {
            m_loginDialog->close();
        }
    }

    void Desktop::openTerminal()
    {
        if (!m_terminal)
        {
            m_terminal = new Terminal(this);
        }
        if (m_terminal)
        {
            m_terminal->open();
        }
    }

    void Desktop::toggleTerminal()
    {
        if (!m_terminal)
        {
            m_terminal = new Terminal(this);
        }

        if (!m_terminal)
            return;

        if (m_terminal->isOpen())
        {
            m_terminal->close();
        }
        else
        {
            m_terminal->open();
        }
    }

    void Desktop::openBrowser()
    {
        if (!m_browser)
        {
            m_browser = new Browser(this);
        }
        // Window is created lazily by openFile.
    }

    bool Desktop::ensureCmmsDatabaseReady()
    {
        if (m_cmmsDatabaseReady)
            return true;

        auto &engine = QCQL::Engine::instance();
        engine.initialize();

        auto initializeCmmsDatabase = [&](bool recreate) -> bool
        {
            const QC::u64 initStartMs = QDrv::Timer::instance().milliseconds();
            auto findTable = [&](const char *tableName) -> QCQL::Table *
            {
                if (!tableName || !*tableName)
                    return nullptr;

                for (QC::usize i = 0; i < m_cmmsDatabase.tables.size(); ++i)
                {
                    if (QC::String::strcmp(m_cmmsDatabase.tables[i].name, tableName) == 0)
                        return &m_cmmsDatabase.tables[i];
                }
                return nullptr;
            };

            auto hasForeignKey = [&](const QCQL::TableSchema &schema,
                                     const char *columnName,
                                     const char *referencedTable,
                                     const char *referencedColumn) -> bool
            {
                for (QC::usize i = 0; i < schema.foreignKeys.size(); ++i)
                {
                    const QCQL::ForeignKey &foreignKey = schema.foreignKeys[i];
                    if (QC::String::strcmp(foreignKey.columnName, columnName) != 0)
                        continue;
                    if (QC::String::strcmp(foreignKey.referencedTable, referencedTable) != 0)
                        continue;
                    if (QC::String::strcmp(foreignKey.referencedColumn, referencedColumn) != 0)
                        continue;
                    return true;
                }
                return false;
            };

            bool cmmsSchemaChanged = false;

            auto attachForeignKey = [&](const char *tableName,
                                        const char *columnName,
                                        const char *referencedTable,
                                        const char *referencedColumn) -> bool
            {
                QCQL::Table *table = findTable(tableName);
                if (!table)
                    return false;
                if (hasForeignKey(table->schema, columnName, referencedTable, referencedColumn))
                    return true;

                QCQL::ForeignKey foreignKey{};
                QC::String::strncpy(foreignKey.columnName, columnName, sizeof(foreignKey.columnName) - 1);
                QC::String::strncpy(foreignKey.referencedTable, referencedTable, sizeof(foreignKey.referencedTable) - 1);
                QC::String::strncpy(foreignKey.referencedColumn, referencedColumn, sizeof(foreignKey.referencedColumn) - 1);
                table->schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(foreignKey));
                cmmsSchemaChanged = true;
                return true;
            };

            auto ensureDesktopDocumentTable = [&](const char *tableName) -> bool
            {
                if (!tableName || !*tableName)
                    return false;

                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, tableName, tableId);
                if (lookupSt == QCQL::Status::Success)
                    return true;
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, tableName, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column sourcePathCol{};
                QC::String::strncpy(sourcePathCol.name, "sourcePath", sizeof(sourcePathCol.name) - 1);
                sourcePathCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(sourcePathCol));

                QCQL::Column payloadCol{};
                QC::String::strncpy(payloadCol.name, "payload", sizeof(payloadCol.name) - 1);
                payloadCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(payloadCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopChunkTable = [&](const char *tableName, const char *documentTableName) -> bool
            {
                if (!tableName || !*tableName || !documentTableName || !*documentTableName)
                    return false;

                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, tableName, tableId);
                if (lookupSt == QCQL::Status::Success)
                    return attachForeignKey(tableName, "documentId", documentTableName, "id");
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, tableName, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column documentIdCol{};
                QC::String::strncpy(documentIdCol.name, "documentId", sizeof(documentIdCol.name) - 1);
                documentIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(documentIdCol));

                QCQL::ForeignKey documentForeignKey{};
                QC::String::strncpy(documentForeignKey.columnName, "documentId", sizeof(documentForeignKey.columnName) - 1);
                QC::String::strncpy(documentForeignKey.referencedTable, documentTableName, sizeof(documentForeignKey.referencedTable) - 1);
                QC::String::strncpy(documentForeignKey.referencedColumn, "id", sizeof(documentForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(documentForeignKey));

                QCQL::Column chunkIndexCol{};
                QC::String::strncpy(chunkIndexCol.name, "chunkIndex", sizeof(chunkIndexCol.name) - 1);
                chunkIndexCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(chunkIndexCol));

                QCQL::Column payloadCol{};
                QC::String::strncpy(payloadCol.name, "payload", sizeof(payloadCol.name) - 1);
                payloadCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(payloadCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopRegionTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_REGION_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                    return attachForeignKey(CMMS_DESKTOP_REGION_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id");
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_REGION_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column regionKeyCol{};
                QC::String::strncpy(regionKeyCol.name, "regionKey", sizeof(regionKeyCol.name) - 1);
                regionKeyCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(regionKeyCol));

                QCQL::Column displayNameCol{};
                QC::String::strncpy(displayNameCol.name, "displayName", sizeof(displayNameCol.name) - 1);
                displayNameCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(displayNameCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopControlTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_CONTROL_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_CONTROL_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_CONTROL_TABLE, "regionId", CMMS_DESKTOP_REGION_TABLE, "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_CONTROL_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column regionIdCol{};
                QC::String::strncpy(regionIdCol.name, "regionId", sizeof(regionIdCol.name) - 1);
                regionIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(regionIdCol));

                QCQL::ForeignKey regionForeignKey{};
                QC::String::strncpy(regionForeignKey.columnName, "regionId", sizeof(regionForeignKey.columnName) - 1);
                QC::String::strncpy(regionForeignKey.referencedTable, CMMS_DESKTOP_REGION_TABLE, sizeof(regionForeignKey.referencedTable) - 1);
                QC::String::strncpy(regionForeignKey.referencedColumn, "id", sizeof(regionForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(regionForeignKey));

                QCQL::Column controlTypeCol{};
                QC::String::strncpy(controlTypeCol.name, "controlType", sizeof(controlTypeCol.name) - 1);
                controlTypeCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(controlTypeCol));

                QCQL::Column controlKeyCol{};
                QC::String::strncpy(controlKeyCol.name, "controlKey", sizeof(controlKeyCol.name) - 1);
                controlKeyCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(controlKeyCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopControlRuntimeTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, "controlId", CMMS_DESKTOP_CONTROL_TABLE, "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column controlIdCol{};
                QC::String::strncpy(controlIdCol.name, "controlId", sizeof(controlIdCol.name) - 1);
                controlIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(controlIdCol));

                QCQL::ForeignKey controlForeignKey{};
                QC::String::strncpy(controlForeignKey.columnName, "controlId", sizeof(controlForeignKey.columnName) - 1);
                QC::String::strncpy(controlForeignKey.referencedTable, CMMS_DESKTOP_CONTROL_TABLE, sizeof(controlForeignKey.referencedTable) - 1);
                QC::String::strncpy(controlForeignKey.referencedColumn, "id", sizeof(controlForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(controlForeignKey));

                auto addTextColumn = [&](const char *name) {
                    QCQL::Column column{};
                    QC::String::strncpy(column.name, name, sizeof(column.name) - 1);
                    column.type = QCQL::ColumnType::Text;
                    schema.columns.push_back(static_cast<QCQL::Column &&>(column));
                };

                addTextColumn("x");
                addTextColumn("y");
                addTextColumn("width");
                addTextColumn("height");
                addTextColumn("zIndex");
                addTextColumn("visible");
                addTextColumn("enabled");
                addTextColumn("styleClass");
                addTextColumn("text");
                addTextColumn("iconPath");

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopControlPropertiesTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, "controlId", CMMS_DESKTOP_CONTROL_TABLE, "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column controlIdCol{};
                QC::String::strncpy(controlIdCol.name, "controlId", sizeof(controlIdCol.name) - 1);
                controlIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(controlIdCol));

                QCQL::ForeignKey controlForeignKey{};
                QC::String::strncpy(controlForeignKey.columnName, "controlId", sizeof(controlForeignKey.columnName) - 1);
                QC::String::strncpy(controlForeignKey.referencedTable, CMMS_DESKTOP_CONTROL_TABLE, sizeof(controlForeignKey.referencedTable) - 1);
                QC::String::strncpy(controlForeignKey.referencedColumn, "id", sizeof(controlForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(controlForeignKey));

                QCQL::Column keyCol{};
                QC::String::strncpy(keyCol.name, "propertyKey", sizeof(keyCol.name) - 1);
                keyCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(keyCol));

                QCQL::Column valueCol{};
                QC::String::strncpy(valueCol.name, "propertyValue", sizeof(valueCol.name) - 1);
                valueCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(valueCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopControlBindingsTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, "controlId", CMMS_DESKTOP_CONTROL_TABLE, "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column controlIdCol{};
                QC::String::strncpy(controlIdCol.name, "controlId", sizeof(controlIdCol.name) - 1);
                controlIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(controlIdCol));

                QCQL::ForeignKey controlForeignKey{};
                QC::String::strncpy(controlForeignKey.columnName, "controlId", sizeof(controlForeignKey.columnName) - 1);
                QC::String::strncpy(controlForeignKey.referencedTable, CMMS_DESKTOP_CONTROL_TABLE, sizeof(controlForeignKey.referencedTable) - 1);
                QC::String::strncpy(controlForeignKey.referencedColumn, "id", sizeof(controlForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(controlForeignKey));

                QCQL::Column eventCol{};
                QC::String::strncpy(eventCol.name, "eventName", sizeof(eventCol.name) - 1);
                eventCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(eventCol));

                QCQL::Column actionCol{};
                QC::String::strncpy(actionCol.name, "actionName", sizeof(actionCol.name) - 1);
                actionCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(actionCol));

                QCQL::Column argumentCol{};
                QC::String::strncpy(argumentCol.name, "argument", sizeof(argumentCol.name) - 1);
                argumentCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(argumentCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopLayoutThemeTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_LAYOUT_THEME_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_LAYOUT_THEME_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_LAYOUT_THEME_TABLE, "themeId", "Themes", "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_LAYOUT_THEME_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column themeIdCol{};
                QC::String::strncpy(themeIdCol.name, "themeId", sizeof(themeIdCol.name) - 1);
                themeIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(themeIdCol));

                QCQL::ForeignKey themeForeignKey{};
                QC::String::strncpy(themeForeignKey.columnName, "themeId", sizeof(themeForeignKey.columnName) - 1);
                QC::String::strncpy(themeForeignKey.referencedTable, "Themes", sizeof(themeForeignKey.referencedTable) - 1);
                QC::String::strncpy(themeForeignKey.referencedColumn, "id", sizeof(themeForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(themeForeignKey));

                QCQL::Column variantCol{};
                QC::String::strncpy(variantCol.name, "variant", sizeof(variantCol.name) - 1);
                variantCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(variantCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopLayoutCapabilityTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_LAYOUT_CAPABILITY_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_LAYOUT_CAPABILITY_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_LAYOUT_CAPABILITY_TABLE, "capabilityId", "Capabilities", "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_LAYOUT_CAPABILITY_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column capabilityIdCol{};
                QC::String::strncpy(capabilityIdCol.name, "capabilityId", sizeof(capabilityIdCol.name) - 1);
                capabilityIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(capabilityIdCol));

                QCQL::ForeignKey capabilityForeignKey{};
                QC::String::strncpy(capabilityForeignKey.columnName, "capabilityId", sizeof(capabilityForeignKey.columnName) - 1);
                QC::String::strncpy(capabilityForeignKey.referencedTable, "Capabilities", sizeof(capabilityForeignKey.referencedTable) - 1);
                QC::String::strncpy(capabilityForeignKey.referencedColumn, "id", sizeof(capabilityForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(capabilityForeignKey));

                QCQL::Column isRequiredCol{};
                QC::String::strncpy(isRequiredCol.name, "isRequired", sizeof(isRequiredCol.name) - 1);
                isRequiredCol.type = QCQL::ColumnType::Bool;
                schema.columns.push_back(static_cast<QCQL::Column &&>(isRequiredCol));

                QCQL::Column rationaleCol{};
                QC::String::strncpy(rationaleCol.name, "rationale", sizeof(rationaleCol.name) - 1);
                rationaleCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(rationaleCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopLayoutAssetTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_LAYOUT_ASSET_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                    return attachForeignKey(CMMS_DESKTOP_LAYOUT_ASSET_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id");
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_LAYOUT_ASSET_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column assetRoleCol{};
                QC::String::strncpy(assetRoleCol.name, "assetRole", sizeof(assetRoleCol.name) - 1);
                assetRoleCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(assetRoleCol));

                QCQL::Column assetKindCol{};
                QC::String::strncpy(assetKindCol.name, "assetKind", sizeof(assetKindCol.name) - 1);
                assetKindCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(assetKindCol));

                QCQL::Column assetPathCol{};
                QC::String::strncpy(assetPathCol.name, "assetPath", sizeof(assetPathCol.name) - 1);
                assetPathCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(assetPathCol));

                QCQL::Column backgroundModeCol{};
                QC::String::strncpy(backgroundModeCol.name, "backgroundMode", sizeof(backgroundModeCol.name) - 1);
                backgroundModeCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(backgroundModeCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopLayoutMaterializationTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_LAYOUT_MATERIALIZATION_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                    return attachForeignKey(CMMS_DESKTOP_LAYOUT_MATERIALIZATION_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id");
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_LAYOUT_MATERIALIZATION_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column sourcePathCol{};
                QC::String::strncpy(sourcePathCol.name, "sourcePath", sizeof(sourcePathCol.name) - 1);
                sourcePathCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(sourcePathCol));

                QCQL::Column chunkCountCol{};
                QC::String::strncpy(chunkCountCol.name, "chunkCount", sizeof(chunkCountCol.name) - 1);
                chunkCountCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(chunkCountCol));

                QCQL::Column schemaVersionCol{};
                QC::String::strncpy(schemaVersionCol.name, "schemaVersion", sizeof(schemaVersionCol.name) - 1);
                schemaVersionCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(schemaVersionCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto ensureDesktopControlHierarchyTable = [&]() -> bool
            {
                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, tableId);
                if (lookupSt == QCQL::Status::Success)
                {
                    return attachForeignKey(CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, "layoutId", CMMS_DESKTOP_LAYOUT_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, "parentControlId", CMMS_DESKTOP_CONTROL_TABLE, "id") &&
                           attachForeignKey(CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, "childControlId", CMMS_DESKTOP_CONTROL_TABLE, "id");
                }
                if (lookupSt != QCQL::Status::NotFound)
                    return false;

                QCQL::TableSchema schema{};
                QC::String::strncpy(schema.tableName, CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, sizeof(schema.tableName) - 1);

                QCQL::Column idCol{};
                QC::String::strncpy(idCol.name, "id", sizeof(idCol.name) - 1);
                idCol.type = QCQL::ColumnType::Text;
                idCol.isPrimaryKey = true;
                schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

                QCQL::Column layoutIdCol{};
                QC::String::strncpy(layoutIdCol.name, "layoutId", sizeof(layoutIdCol.name) - 1);
                layoutIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(layoutIdCol));

                QCQL::ForeignKey layoutForeignKey{};
                QC::String::strncpy(layoutForeignKey.columnName, "layoutId", sizeof(layoutForeignKey.columnName) - 1);
                QC::String::strncpy(layoutForeignKey.referencedTable, CMMS_DESKTOP_LAYOUT_TABLE, sizeof(layoutForeignKey.referencedTable) - 1);
                QC::String::strncpy(layoutForeignKey.referencedColumn, "id", sizeof(layoutForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(layoutForeignKey));

                QCQL::Column parentControlIdCol{};
                QC::String::strncpy(parentControlIdCol.name, "parentControlId", sizeof(parentControlIdCol.name) - 1);
                parentControlIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(parentControlIdCol));

                QCQL::ForeignKey parentForeignKey{};
                QC::String::strncpy(parentForeignKey.columnName, "parentControlId", sizeof(parentForeignKey.columnName) - 1);
                QC::String::strncpy(parentForeignKey.referencedTable, CMMS_DESKTOP_CONTROL_TABLE, sizeof(parentForeignKey.referencedTable) - 1);
                QC::String::strncpy(parentForeignKey.referencedColumn, "id", sizeof(parentForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(parentForeignKey));

                QCQL::Column childControlIdCol{};
                QC::String::strncpy(childControlIdCol.name, "childControlId", sizeof(childControlIdCol.name) - 1);
                childControlIdCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(childControlIdCol));

                QCQL::ForeignKey childForeignKey{};
                QC::String::strncpy(childForeignKey.columnName, "childControlId", sizeof(childForeignKey.columnName) - 1);
                QC::String::strncpy(childForeignKey.referencedTable, CMMS_DESKTOP_CONTROL_TABLE, sizeof(childForeignKey.referencedTable) - 1);
                QC::String::strncpy(childForeignKey.referencedColumn, "id", sizeof(childForeignKey.referencedColumn) - 1);
                schema.foreignKeys.push_back(static_cast<QCQL::ForeignKey &&>(childForeignKey));

                QCQL::Column childOrderCol{};
                QC::String::strncpy(childOrderCol.name, "childOrder", sizeof(childOrderCol.name) - 1);
                childOrderCol.type = QCQL::ColumnType::Text;
                schema.columns.push_back(static_cast<QCQL::Column &&>(childOrderCol));

                schema.primaryKeyIndex = 0;
                const QCQL::Status createSt = engine.createTable(m_cmmsDatabase, schema);
                return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
            };

            auto seedDesktopDocumentRow = [&](const char *tableName, const char *chunkTableName, const char *documentId, const char *sourcePath, QC::u64 maxBytes) -> bool
            {
                if (!tableName || !*tableName || !chunkTableName || !*chunkTableName || !documentId || !*documentId || !sourcePath || !*sourcePath)
                    return false;

                const QCQL::Cell keyCell = makeTextCell(documentId);
                QCQL::Row existing{};
                const QCQL::Status existingSt =
                    engine.selectRowByPrimaryKeyByName(m_cmmsDatabase, tableName, keyCell.bytes, existing);

                QC::u32 existingChunkCount = 0;
                if (existingSt == QCQL::Status::Success && !existing.tombstone && existing.cells.size() >= 3 &&
                    parseUnsignedTextCell(existing.cells[2], existingChunkCount) && existingChunkCount > 0)
                {
                    return true;
                }

                QFS::File *file = QFS::VFS::instance().open(sourcePath, QFS::OpenMode::Read);
                if (!file)
                    return true;

                const QC::u64 size64 = file->size();
                if (size64 == 0 || size64 > maxBytes)
                {
                    QFS::VFS::instance().close(file);
                    return true;
                }

                QC::Vector<QC::u8> payloadBytes;
                payloadBytes.resize(static_cast<QC::usize>(size64));
                const QC::isize readCount = file->read(reinterpret_cast<char *>(payloadBytes.data()), payloadBytes.size());
                QFS::VFS::instance().close(file);
                if (readCount <= 0)
                    return true;

                if (static_cast<QC::usize>(readCount) < payloadBytes.size())
                    payloadBytes.resize(static_cast<QC::usize>(readCount));

                const QC::u32 chunkCount = static_cast<QC::u32>((payloadBytes.size() + CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES - 1) / CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES);
                if (chunkCount == 0)
                    return true;

                QCQL::Row row{};
                row.cells.push_back(keyCell);
                row.cells.push_back(makeTextCell(sourcePath));

                row.cells.push_back(makeUnsignedTextCell(chunkCount));

                bool metadataOk = false;

                if (existingSt == QCQL::Status::Success && !existing.tombstone)
                    metadataOk = (engine.updateRowByPrimaryKeyByName(m_cmmsDatabase, tableName, keyCell.bytes, row) == QCQL::Status::Success);
                else
                {
                    QC::u32 pageId = 0;
                    metadataOk = (engine.insertRowByName(m_cmmsDatabase, tableName, row, &pageId) == QCQL::Status::Success);
                }

                if (!metadataOk)
                    return false;

                for (QC::u32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
                {
                    const QC::usize start = static_cast<QC::usize>(chunkIndex) * CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES;
                    QC::usize length = payloadBytes.size() - start;
                    if (length > CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES)
                        length = CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES;

                    char chunkId[48];
                    QC::String::memset(chunkId, 0, sizeof(chunkId));
                    if (!makeChunkRowId(documentId, chunkIndex, chunkId, sizeof(chunkId)))
                        return false;

                    QCQL::Row chunkRow{};
                    chunkRow.cells.push_back(makeTextCell(chunkId));
                    chunkRow.cells.push_back(makeTextCell(documentId));
                    chunkRow.cells.push_back(makeUnsignedTextCell(chunkIndex));

                    QCQL::Cell payloadCell{};
                    payloadCell.type = QCQL::ColumnType::Text;
                    for (QC::usize i = 0; i < length; ++i)
                        payloadCell.bytes.push_back(payloadBytes[start + i]);
                    chunkRow.cells.push_back(static_cast<QCQL::Cell &&>(payloadCell));

                    const QCQL::Cell chunkKeyCell = makeTextCell(chunkId);
                    QCQL::Row existingChunk{};
                    const QCQL::Status existingChunkSt =
                        engine.selectRowByPrimaryKeyByName(m_cmmsDatabase, chunkTableName, chunkKeyCell.bytes, existingChunk);
                    if (existingChunkSt == QCQL::Status::Success && !existingChunk.tombstone)
                    {
                        if (engine.updateRowByPrimaryKeyByName(m_cmmsDatabase, chunkTableName, chunkKeyCell.bytes, chunkRow) != QCQL::Status::Success)
                            return false;
                    }
                    else
                    {
                        QC::u32 pageId = 0;
                        if (engine.insertRowByName(m_cmmsDatabase, chunkTableName, chunkRow, &pageId) != QCQL::Status::Success)
                            return false;
                    }
                }

                return true;
            };

            auto ensureDesktopLayoutTable = [&](QCQL::Database &db) -> bool
            {
                (void)db;
                return ensureDesktopDocumentTable(CMMS_DESKTOP_LAYOUT_TABLE);
            };

            auto seedDesktopLayoutRow = [&](QCQL::Database &db, const char *layoutId, const char *sourcePath) -> bool
            {
                (void)db;
                return seedDesktopDocumentRow(CMMS_DESKTOP_LAYOUT_TABLE, CMMS_DESKTOP_LAYOUT_CHUNK_TABLE, layoutId, sourcePath, 1024 * 256);
            };

            auto collectLayoutScopedKeys = [&](const char *tableName,
                                               QC::usize layoutColumnIndex,
                                               const char *layoutId,
                                               QC::Vector<QC::Vector<QC::u8>> &outKeys,
                                               bool *outSawCorruptRows = nullptr) -> bool
            {
                outKeys.clear();
                if (outSawCorruptRows)
                    *outSawCorruptRows = false;
                if (!tableName || !*tableName || !layoutId || !*layoutId)
                    return false;

                QC::u32 tableId = 0;
                const QCQL::Status lookupSt = engine.lookupTableId(m_cmmsDatabase, tableName, tableId);
                if (lookupSt == QCQL::Status::NotFound)
                    return true;
                if (lookupSt != QCQL::Status::Success)
                {
                    QC_LOG_WARN(LOG_MODULE,
                                "CMMS runtime key scan lookup failed table=%s layout=%s status=%d",
                                tableName,
                                layoutId,
                                static_cast<int>(lookupSt));
                    return false;
                }

                QCQL::Table *table = findTable(tableName);
                if (!table)
                {
                    QC_LOG_WARN(LOG_MODULE,
                                "CMMS runtime key scan missing table metadata table=%s layout=%s tableId=%u",
                                tableName,
                                layoutId,
                                static_cast<unsigned>(tableId));
                    return false;
                }

                QC::u32 corruptRowsSkipped = 0;

                for (QC::usize pageIndex = 0; pageIndex < table->pages.size(); ++pageIndex)
                {
                    QCQL::Page page{};
                    const QCQL::Status loadPageSt = engine.loadPage(m_cmmsDatabase, table->pages[pageIndex], page);
                    if (loadPageSt != QCQL::Status::Success)
                    {
                        QC_LOG_WARN(LOG_MODULE,
                                    "CMMS runtime key scan page load failed table=%s layout=%s page=%u status=%d",
                                    tableName,
                                    layoutId,
                                    static_cast<unsigned>(table->pages[pageIndex]),
                                    static_cast<int>(loadPageSt));
                        return false;
                    }

                    for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                    {
                        QCQL::Row row{};
                        const QCQL::Status readRowSt = engine.readRow(m_cmmsDatabase, page.header.pageId, page.rowOffsets[rowIndex], row);
                        if (readRowSt == QCQL::Status::Corrupt)
                        {
                            ++corruptRowsSkipped;
                            if (outSawCorruptRows)
                                *outSawCorruptRows = true;
                            continue;
                        }
                        if (readRowSt != QCQL::Status::Success)
                        {
                            QC_LOG_WARN(LOG_MODULE,
                                        "CMMS runtime key scan row read failed table=%s layout=%s page=%u row_offset=%u status=%d",
                                        tableName,
                                        layoutId,
                                        static_cast<unsigned>(page.header.pageId),
                                        static_cast<unsigned>(page.rowOffsets[rowIndex]),
                                        static_cast<int>(readRowSt));
                            return false;
                        }
                        if (row.tombstone || row.cells.size() <= layoutColumnIndex)
                            continue;
                        if (!cellMatchesText(row.cells[layoutColumnIndex], layoutId))
                            continue;
                        outKeys.push_back(row.cells[0].bytes);
                    }
                }

                if (corruptRowsSkipped > 0)
                {
                    QC_LOG_WARN(LOG_MODULE,
                                "CMMS runtime key scan skipped corrupt rows table=%s layout=%s count=%u",
                                tableName,
                                layoutId,
                                static_cast<unsigned>(corruptRowsSkipped));
                }

                return true;
            };

            auto removeRowsByKey = [&](const char *tableName,
                                       const QC::Vector<QC::Vector<QC::u8>> &keys) -> bool
            {
                for (QC::usize i = 0; i < keys.size(); ++i)
                {
                    const QCQL::Status removeSt = engine.removeRowByPrimaryKeyByName(m_cmmsDatabase, tableName, keys[i]);
                    if (removeSt != QCQL::Status::Success && removeSt != QCQL::Status::NotFound)
                        return false;
                }
                return true;
            };

            auto upsertRowById = [&](const char *tableName, const char *rowId, const QCQL::Row &row) -> bool
            {
                if (!tableName || !*tableName || !rowId || !*rowId)
                    return false;

                const QCQL::Cell keyCell = makeTextCell(rowId);
                QCQL::Row existing{};
                const QCQL::Status existingSt = engine.selectRowByPrimaryKeyByName(m_cmmsDatabase, tableName, keyCell.bytes, existing);
                if (existingSt == QCQL::Status::Success && !existing.tombstone)
                    return engine.updateRowByPrimaryKeyByName(m_cmmsDatabase, tableName, keyCell.bytes, row) == QCQL::Status::Success;
                if (existingSt != QCQL::Status::NotFound)
                    return false;

                QC::u32 pageId = 0;
                return engine.insertRowByName(m_cmmsDatabase, tableName, row, &pageId) == QCQL::Status::Success;
            };

            auto resolveControlRowId = [&](const DesktopDocument &document,
                                           const DesktopControlModel &control,
                                           QC::usize controlIndex,
                                           char *out,
                                           QC::usize outCap) -> bool
            {
                if (control.id[0])
                    return makeScopedRowId(document.documentId, control.id, out, outCap);
                return makeScopedIndexedRowId(document.documentId, "control", static_cast<QC::u32>(controlIndex), out, outCap);
            };

            auto findControlRowIdBySourceId = [&](const DesktopDocument &document,
                                                  const char *sourceControlId,
                                                  char *out,
                                                  QC::usize outCap) -> bool
            {
                if (!sourceControlId || !*sourceControlId)
                    return false;

                for (QC::usize i = 0; i < document.controls.size(); ++i)
                {
                    if (QC::String::strcmp(document.controls[i].id, sourceControlId) != 0)
                        continue;
                    return resolveControlRowId(document, document.controls[i], i, out, outCap);
                }

                return false;
            };

            auto materializeLayoutRuntimeRows = [&](const char *layoutId) -> bool
            {
                if (!layoutId || !*layoutId)
                    return false;

                const QCQL::Cell layoutKeyCell = makeTextCell(layoutId);
                QCQL::Row layoutMetadataRow{};
                const QCQL::Status layoutMetadataSt = engine.selectRowByPrimaryKeyByName(m_cmmsDatabase,
                                                                                          CMMS_DESKTOP_LAYOUT_TABLE,
                                                                                          layoutKeyCell.bytes,
                                                                                          layoutMetadataRow);
                if (layoutMetadataSt != QCQL::Status::Success || layoutMetadataRow.tombstone || layoutMetadataRow.cells.size() < 3)
                {
                    QC_LOG_WARN(LOG_MODULE,
                                "CMMS runtime materialization missing layout metadata layout=%s status=%d tombstone=%d cells=%u",
                                layoutId,
                                static_cast<int>(layoutMetadataSt),
                                layoutMetadataRow.tombstone ? 1 : 0,
                                static_cast<unsigned>(layoutMetadataRow.cells.size()));
                    return false;
                }

                char layoutSourcePath[192];
                QC::String::memset(layoutSourcePath, 0, sizeof(layoutSourcePath));
                if (!copyCellText(layoutMetadataRow.cells[1], layoutSourcePath, sizeof(layoutSourcePath)))
                    layoutSourcePath[0] = '\0';

                QC::u32 layoutChunkCount = 0;
                if (!parseUnsignedTextCell(layoutMetadataRow.cells[2], layoutChunkCount))
                    layoutChunkCount = 0;

                QCQL::Row materializationRow{};
                const QCQL::Status materializationSt = engine.selectRowByPrimaryKeyByName(m_cmmsDatabase,
                                                                                           CMMS_DESKTOP_LAYOUT_MATERIALIZATION_TABLE,
                                                                                           layoutKeyCell.bytes,
                                                                                           materializationRow);
                QC::u32 materializedSchemaVersion = 0;
                QC::u32 materializedChunkCount = 0;
                char materializedSourcePath[192];
                QC::String::memset(materializedSourcePath, 0, sizeof(materializedSourcePath));
                const bool haveMaterializationState =
                    materializationSt == QCQL::Status::Success &&
                    !materializationRow.tombstone &&
                    materializationRow.cells.size() >= 5 &&
                    copyCellText(materializationRow.cells[2], materializedSourcePath, sizeof(materializedSourcePath)) &&
                    parseUnsignedTextCell(materializationRow.cells[3], materializedChunkCount) &&
                    parseUnsignedTextCell(materializationRow.cells[4], materializedSchemaVersion);

                if (haveMaterializationState &&
                    materializedSchemaVersion == CMMS_LAYOUT_MATERIALIZATION_SCHEMA_VERSION &&
                    materializedChunkCount == layoutChunkCount &&
                    QC::String::strcmp(materializedSourcePath, layoutSourcePath) == 0)
                {
                    QC_LOG_INFO(LOG_MODULE,
                                "CMMS runtime materialization skipped layout=%s source=%s chunks=%u schema=%u\n",
                                layoutId,
                                layoutSourcePath[0] ? layoutSourcePath : "<unknown>",
                                static_cast<unsigned>(layoutChunkCount),
                                static_cast<unsigned>(materializedSchemaVersion));
                    return true;
                }

                DesktopDocumentImportResult importResult{};
                if (!DesktopDocumentIO::importCmmsJson(m_cmmsDatabase, layoutId, importResult) || !importResult.loaded)
                {
                    DesktopDocumentImportResult cuimlImportResult{};
                    if (!DesktopDocumentIO::importCmmsCuiml(m_cmmsDatabase, layoutId, cuimlImportResult) || !cuimlImportResult.loaded)
                    {
                        QC_LOG_WARN(LOG_MODULE,
                                    "CMMS runtime materialization import failed layout=%s json_error='%s' cuiml_error='%s'",
                                    layoutId,
                                    importResult.error,
                                    cuimlImportResult.error);
                        return false;
                    }
                    importResult = static_cast<DesktopDocumentImportResult &&>(cuimlImportResult);
                }

                QC::Vector<QC::Vector<QC::u8>> themeKeys;
                QC::Vector<QC::Vector<QC::u8>> assetKeys;
                QC::Vector<QC::Vector<QC::u8>> propertyKeys;
                QC::Vector<QC::Vector<QC::u8>> bindingKeys;
                QC::Vector<QC::Vector<QC::u8>> hierarchyKeys;
                QC::Vector<QC::Vector<QC::u8>> runtimeKeys;
                QC::Vector<QC::Vector<QC::u8>> controlKeys;
                QC::Vector<QC::Vector<QC::u8>> regionKeys;
                bool cleanupScanSawCorruptRows = false;
                bool tableSawCorruptRows = false;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_LAYOUT_THEME_TABLE, 1, layoutId, themeKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_LAYOUT_ASSET_TABLE, 1, layoutId, assetKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, 1, layoutId, propertyKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, 1, layoutId, bindingKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, 1, layoutId, runtimeKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, 1, layoutId, hierarchyKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_CONTROL_TABLE, 1, layoutId, controlKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }
                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;
                if (!collectLayoutScopedKeys(CMMS_DESKTOP_REGION_TABLE, 1, layoutId, regionKeys, &tableSawCorruptRows))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization key collection failed layout=%s", layoutId);
                    return false;
                }

                cleanupScanSawCorruptRows = cleanupScanSawCorruptRows || tableSawCorruptRows;

                if (cleanupScanSawCorruptRows)
                {
                    QC_LOG_WARN(LOG_MODULE,
                                "CMMS runtime cleanup bypassed layout=%s due_to=corrupt_legacy_rows",
                                layoutId);
                }
                else if (!removeRowsByKey(CMMS_DESKTOP_LAYOUT_THEME_TABLE, themeKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_LAYOUT_ASSET_TABLE, assetKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, propertyKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, bindingKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, runtimeKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, hierarchyKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_CONTROL_TABLE, controlKeys) ||
                         !removeRowsByKey(CMMS_DESKTOP_REGION_TABLE, regionKeys))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization row cleanup failed layout=%s", layoutId);
                    return false;
                }

                if (importResult.document.themeRef.themeId[0])
                {
                    char themeRowId[96];
                    QC::String::memset(themeRowId, 0, sizeof(themeRowId));
                    if (!makeScopedRowId(layoutId, "theme", themeRowId, sizeof(themeRowId)))
                        return false;

                    QCQL::Row themeRow{};
                    themeRow.cells.push_back(makeTextCell(themeRowId));
                    themeRow.cells.push_back(makeTextCell(layoutId));
                    themeRow.cells.push_back(makeTextCell(importResult.document.themeRef.themeId));
                    themeRow.cells.push_back(makeTextCell(importResult.document.themeRef.variant));
                    if (!upsertRowById(CMMS_DESKTOP_LAYOUT_THEME_TABLE, themeRowId, themeRow))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization theme upsert failed layout=%s row=%s", layoutId, themeRowId);
                        return false;
                    }
                }

                if (importResult.document.backgroundMode != DesktopBackgroundMode::None ||
                    importResult.document.backgroundAsset.path[0])
                {
                    char assetRowId[96];
                    QC::String::memset(assetRowId, 0, sizeof(assetRowId));
                    if (!makeScopedRowId(layoutId, "background", assetRowId, sizeof(assetRowId)))
                        return false;

                    QCQL::Row assetRow{};
                    assetRow.cells.push_back(makeTextCell(assetRowId));
                    assetRow.cells.push_back(makeTextCell(layoutId));
                    assetRow.cells.push_back(makeTextCell("background"));
                    assetRow.cells.push_back(makeTextCell(assetKindName(importResult.document.backgroundAsset.kind)));
                    assetRow.cells.push_back(makeTextCell(importResult.document.backgroundAsset.path));
                    assetRow.cells.push_back(makeTextCell(backgroundModeName(importResult.document.backgroundMode)));
                    if (!upsertRowById(CMMS_DESKTOP_LAYOUT_ASSET_TABLE, assetRowId, assetRow))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization asset upsert failed layout=%s row=%s", layoutId, assetRowId);
                        return false;
                    }
                }

                char regionId[96];
                QC::String::memset(regionId, 0, sizeof(regionId));
                if (!makeScopedRowId(layoutId, "root", regionId, sizeof(regionId)))
                    return false;

                QCQL::Row regionRow{};
                regionRow.cells.push_back(makeTextCell(regionId));
                regionRow.cells.push_back(makeTextCell(layoutId));
                regionRow.cells.push_back(makeTextCell("root"));
                regionRow.cells.push_back(makeTextCell("Root"));
                if (!upsertRowById(CMMS_DESKTOP_REGION_TABLE, regionId, regionRow))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization region upsert failed layout=%s row=%s", layoutId, regionId);
                    return false;
                }

                for (QC::usize i = 0; i < importResult.document.controls.size(); ++i)
                {
                    const DesktopControlModel &control = importResult.document.controls[i];
                    char controlRowId[128];
                    QC::String::memset(controlRowId, 0, sizeof(controlRowId));
                    if (!resolveControlRowId(importResult.document, control, i, controlRowId, sizeof(controlRowId)))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization control row id failed layout=%s index=%u", layoutId, static_cast<unsigned>(i));
                        return false;
                    }

                    QCQL::Row controlRow{};
                    controlRow.cells.push_back(makeTextCell(controlRowId));
                    controlRow.cells.push_back(makeTextCell(layoutId));
                    controlRow.cells.push_back(makeTextCell(regionId));
                    controlRow.cells.push_back(makeTextCell(desktopControlKindName(control.kind)));
                    controlRow.cells.push_back(makeTextCell(control.id[0] ? control.id : control.name));
                    if (!upsertRowById(CMMS_DESKTOP_CONTROL_TABLE, controlRowId, controlRow))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization control upsert failed layout=%s row=%s", layoutId, controlRowId);
                        return false;
                    }

                    QCQL::Row runtimeRow{};
                    runtimeRow.cells.push_back(makeTextCell(controlRowId));
                    runtimeRow.cells.push_back(makeTextCell(layoutId));
                    runtimeRow.cells.push_back(makeTextCell(controlRowId));
                    runtimeRow.cells.push_back(makeSignedTextCell(control.layout.x));
                    runtimeRow.cells.push_back(makeSignedTextCell(control.layout.y));
                    runtimeRow.cells.push_back(makeUnsignedTextCell(control.layout.width));
                    runtimeRow.cells.push_back(makeUnsignedTextCell(control.layout.height));
                    runtimeRow.cells.push_back(makeSignedTextCell(control.zIndex));
                    runtimeRow.cells.push_back(makeTextCell(control.visible ? "true" : "false"));
                    runtimeRow.cells.push_back(makeTextCell(control.enabled ? "true" : "false"));
                    runtimeRow.cells.push_back(makeTextCell(control.styleClass));
                    runtimeRow.cells.push_back(makeTextCell(control.text));
                    runtimeRow.cells.push_back(makeTextCell(control.iconRef.path));
                    if (!upsertRowById(CMMS_DESKTOP_CONTROL_RUNTIME_TABLE, controlRowId, runtimeRow))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization runtime upsert failed layout=%s row=%s", layoutId, controlRowId);
                        return false;
                    }

                    for (QC::usize propertyIndex = 0; propertyIndex < control.properties.size(); ++propertyIndex)
                    {
                        char propertyRowId[160];
                        QC::String::memset(propertyRowId, 0, sizeof(propertyRowId));
                        if (!makeScopedIndexedRowId(controlRowId, "prop", static_cast<QC::u32>(propertyIndex), propertyRowId, sizeof(propertyRowId)))
                        {
                            QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization property row id failed layout=%s control=%s index=%u", layoutId, controlRowId, static_cast<unsigned>(propertyIndex));
                            return false;
                        }

                        QCQL::Row propertyRow{};
                        propertyRow.cells.push_back(makeTextCell(propertyRowId));
                        propertyRow.cells.push_back(makeTextCell(layoutId));
                        propertyRow.cells.push_back(makeTextCell(controlRowId));
                        propertyRow.cells.push_back(makeTextCell(control.properties[propertyIndex].key));
                        propertyRow.cells.push_back(makeTextCell(control.properties[propertyIndex].value));
                        if (!upsertRowById(CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE, propertyRowId, propertyRow))
                        {
                            QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization property upsert failed layout=%s row=%s", layoutId, propertyRowId);
                            return false;
                        }
                    }

                    for (QC::usize bindingIndex = 0; bindingIndex < control.bindings.size(); ++bindingIndex)
                    {
                        char bindingRowId[160];
                        QC::String::memset(bindingRowId, 0, sizeof(bindingRowId));
                        if (!makeScopedIndexedRowId(controlRowId, "bind", static_cast<QC::u32>(bindingIndex), bindingRowId, sizeof(bindingRowId)))
                        {
                            QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization binding row id failed layout=%s control=%s index=%u", layoutId, controlRowId, static_cast<unsigned>(bindingIndex));
                            return false;
                        }

                        QCQL::Row bindingRow{};
                        bindingRow.cells.push_back(makeTextCell(bindingRowId));
                        bindingRow.cells.push_back(makeTextCell(layoutId));
                        bindingRow.cells.push_back(makeTextCell(controlRowId));
                        bindingRow.cells.push_back(makeTextCell(control.bindings[bindingIndex].event));
                        bindingRow.cells.push_back(makeTextCell(control.bindings[bindingIndex].action));
                        bindingRow.cells.push_back(makeTextCell(control.bindings[bindingIndex].argument));
                        if (!upsertRowById(CMMS_DESKTOP_CONTROL_BINDINGS_TABLE, bindingRowId, bindingRow))
                        {
                            QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization binding upsert failed layout=%s row=%s", layoutId, bindingRowId);
                            return false;
                        }
                    }
                }

                for (QC::usize i = 0; i < importResult.document.controls.size(); ++i)
                {
                    const DesktopControlModel &control = importResult.document.controls[i];
                    if (!control.parentId[0])
                        continue;

                    char childRowId[128];
                    char parentRowId[128];
                    char hierarchyRowId[160];
                    QC::String::memset(childRowId, 0, sizeof(childRowId));
                    QC::String::memset(parentRowId, 0, sizeof(parentRowId));
                    QC::String::memset(hierarchyRowId, 0, sizeof(hierarchyRowId));
                    if (!resolveControlRowId(importResult.document, control, i, childRowId, sizeof(childRowId)))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization child row id failed layout=%s index=%u", layoutId, static_cast<unsigned>(i));
                        return false;
                    }
                    if (!findControlRowIdBySourceId(importResult.document, control.parentId, parentRowId, sizeof(parentRowId)))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization parent lookup failed layout=%s child=%s parent=%s", layoutId, childRowId, control.parentId);
                        return false;
                    }
                    if (!makeScopedRowId(parentRowId, childRowId, hierarchyRowId, sizeof(hierarchyRowId)))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization hierarchy row id failed layout=%s parent=%s child=%s", layoutId, parentRowId, childRowId);
                        return false;
                    }

                    QC::u32 childOrder = 0;
                    for (QC::usize j = 0; j < i; ++j)
                    {
                        if (QC::String::strcmp(importResult.document.controls[j].parentId, control.parentId) == 0)
                            ++childOrder;
                    }

                    QCQL::Row hierarchyRow{};
                    hierarchyRow.cells.push_back(makeTextCell(hierarchyRowId));
                    hierarchyRow.cells.push_back(makeTextCell(layoutId));
                    hierarchyRow.cells.push_back(makeTextCell(parentRowId));
                    hierarchyRow.cells.push_back(makeTextCell(childRowId));
                    hierarchyRow.cells.push_back(makeUnsignedTextCell(childOrder));
                    if (!upsertRowById(CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE, hierarchyRowId, hierarchyRow))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization hierarchy upsert failed layout=%s row=%s", layoutId, hierarchyRowId);
                        return false;
                    }
                }

                QCQL::Row newMaterializationRow{};
                DesktopDocumentSaveResult cuimlSaveResult{};
                if (!DesktopDocumentIO::saveCmmsCuiml(m_cmmsDatabase, importResult.document, cuimlSaveResult) ||
                    !cuimlSaveResult.saved)
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization CUI-ML cache refresh failed layout=%s error='%s'", layoutId, cuimlSaveResult.error);
                    return false;
                }

                newMaterializationRow.cells.push_back(makeTextCell(layoutId));
                newMaterializationRow.cells.push_back(makeTextCell(layoutId));
                newMaterializationRow.cells.push_back(makeTextCell(layoutSourcePath));
                newMaterializationRow.cells.push_back(makeUnsignedTextCell(layoutChunkCount));
                newMaterializationRow.cells.push_back(makeUnsignedTextCell(CMMS_LAYOUT_MATERIALIZATION_SCHEMA_VERSION));
                if (!upsertRowById(CMMS_DESKTOP_LAYOUT_MATERIALIZATION_TABLE, layoutId, newMaterializationRow))
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization state upsert failed layout=%s", layoutId);
                    return false;
                }

                QC_LOG_INFO(LOG_MODULE,
                            "CMMS runtime materialized layout=%s source=%s chunks=%u schema=%u controls=%u\n",
                            layoutId,
                            layoutSourcePath[0] ? layoutSourcePath : "<unknown>",
                            static_cast<unsigned>(layoutChunkCount),
                            static_cast<unsigned>(CMMS_LAYOUT_MATERIALIZATION_SCHEMA_VERSION),
                            static_cast<unsigned>(importResult.document.controls.size()));

                return true;
            };

            if (recreate)
            {
                engine.closeDatabase(m_cmmsDatabase);
                m_cmmsDatabase = QCQL::Database{};
                const QC::Status removeSt = QFS::VFS::instance().remove(CMMS_DB_PATH);
                if (removeSt != QC::Status::Success && removeSt != QC::Status::NotFound)
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS DB remove failed path=%s status=%d", CMMS_DB_PATH, static_cast<int>(removeSt));
                    return false;
                }
            }

            const QC::u64 openStartMs = QDrv::Timer::instance().milliseconds();
            QCQL::OpenStats openStats{};
            QCQL::Status st = engine.openDatabase(CMMS_DB_PATH, m_cmmsDatabase, &openStats);
            if (st == QCQL::Status::NotFound || recreate)
            {
                st = engine.createDatabase(CMMS_DB_PATH, m_cmmsDatabase);
            }

            if (st != QCQL::Status::Success)
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB open/create failed path=%s status=%d", CMMS_DB_PATH, static_cast<int>(st));
                return false;
            }
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS init phase open_create=%llums recreate=%u",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - openStartMs),
                        recreate ? 1u : 0u);
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS open stats tables=%u meta_page_headers=%u pk_tables=%u pk_pages=%u pk_rows=%u pk_indexed=%u",
                        static_cast<unsigned>(openStats.metadataTablesLoaded),
                        static_cast<unsigned>(openStats.metadataPageHeadersScanned),
                        static_cast<unsigned>(openStats.pkTablesRebuilt),
                        static_cast<unsigned>(openStats.pkPagesLoaded),
                        static_cast<unsigned>(openStats.pkRowsScanned),
                        static_cast<unsigned>(openStats.pkRowsIndexed));

            const QC::u64 systemTablesStartMs = QDrv::Timer::instance().milliseconds();
            st = engine.initializeSystemTables(m_cmmsDatabase);
            if (st != QCQL::Status::Success)
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB system table init failed status=%d", static_cast<int>(st));
                return false;
            }
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS init phase system_tables=%llums",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - systemTablesStartMs));

            const QC::u64 themeImportStartMs = QDrv::Timer::instance().milliseconds();
            st = ThemeImporter::importBuiltinThemes(m_cmmsDatabase);
            if (st != QCQL::Status::Success)
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB theme import failed status=%d", static_cast<int>(st));
                return false;
            }
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS init phase builtin_theme_import=%llums",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - themeImportStartMs));

            const QC::u64 schemaEnsureStartMs = QDrv::Timer::instance().milliseconds();
            if (!ensureDesktopLayoutTable(m_cmmsDatabase))
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop layout table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopDocumentTable(CMMS_DESKTOP_CUIML_TABLE))
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop CUI-ML table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopChunkTable(CMMS_DESKTOP_LAYOUT_CHUNK_TABLE, CMMS_DESKTOP_LAYOUT_TABLE))
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop layout chunk table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopChunkTable(CMMS_DESKTOP_CUIML_CHUNK_TABLE, CMMS_DESKTOP_CUIML_TABLE))
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop CUI-ML chunk table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopRegionTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop region table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopControlTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop control table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopControlRuntimeTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop control runtime table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopControlPropertiesTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop control properties table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopControlBindingsTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop control bindings table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopLayoutThemeTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop layout theme table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopLayoutCapabilityTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop layout capability table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopLayoutAssetTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop layout asset table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopLayoutMaterializationTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop layout materialization table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }

            if (!ensureDesktopControlHierarchyTable())
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB desktop control hierarchy table init failed diag=%s", engine.lastDiagnostic());
                return false;
            }
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS init phase schema_tables=%llums",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - schemaEnsureStartMs));

            if (cmmsSchemaChanged && m_cmmsDatabase.header.version > QCQL::kFileVersion1)
            {
                const QC::u64 persistMetadataStartMs = QDrv::Timer::instance().milliseconds();
                if (engine.persistMetadata(m_cmmsDatabase) != QCQL::Status::Success)
                {
                    QC_LOG_WARN(LOG_MODULE, "CMMS DB schema metadata persist failed");
                    return false;
                }
                QC_LOG_INFO(LOG_MODULE,
                            "CMMS init phase persist_metadata=%llums",
                            static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - persistMetadataStartMs));
            }

            const QC::u64 seedStartMs = QDrv::Timer::instance().milliseconds();
            (void)seedDesktopDocumentRow(CMMS_DESKTOP_LAYOUT_TABLE, CMMS_DESKTOP_LAYOUT_CHUNK_TABLE, CMMS_DESKTOP_LAYOUT_PRODUCTION, "/PROD/DESKTOP.JSN", 1024 * 256);
            (void)seedDesktopDocumentRow(CMMS_DESKTOP_LAYOUT_TABLE, CMMS_DESKTOP_LAYOUT_CHUNK_TABLE, CMMS_DESKTOP_LAYOUT_GOLDEN, "/GOLDEN/DESKTOP.JSN", 1024 * 256);
            (void)seedDesktopDocumentRow(CMMS_DESKTOP_CUIML_TABLE, CMMS_DESKTOP_CUIML_CHUNK_TABLE, CMMS_DESKTOP_LAYOUT_PRODUCTION, "/PROD/DESKTOP.CML", 1024 * 1024);
            (void)seedDesktopDocumentRow(CMMS_DESKTOP_CUIML_TABLE, CMMS_DESKTOP_CUIML_CHUNK_TABLE, CMMS_DESKTOP_LAYOUT_GOLDEN, "/GOLDEN/DESKTOP.CML", 1024 * 1024);
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS init phase seed_documents=%llums",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - seedStartMs));

            if (CMMS_MATERIALIZE_RUNTIME_ON_BOOT)
            {
                if (!materializeLayoutRuntimeRows(CMMS_DESKTOP_LAYOUT_PRODUCTION))
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization failed layout=%s", CMMS_DESKTOP_LAYOUT_PRODUCTION);
                if (!materializeLayoutRuntimeRows(CMMS_DESKTOP_LAYOUT_GOLDEN))
                    QC_LOG_WARN(LOG_MODULE, "CMMS runtime materialization failed layout=%s", CMMS_DESKTOP_LAYOUT_GOLDEN);
            }
            else
            {
                QC_LOG_INFO(LOG_MODULE, "CMMS runtime materialization deferred during desktop startup");
                if (CMMS_ENSURE_ACTIVE_RUNTIME_ON_BOOT)
                {
                    const bool forceGolden = (QK::Boot::Config::GetActiveConfigTier() == QK::Boot::Config::ConfigTier::Golden);
                    const char *activeLayoutId = forceGolden ? CMMS_DESKTOP_LAYOUT_GOLDEN : CMMS_DESKTOP_LAYOUT_PRODUCTION;
                    if (!materializeLayoutRuntimeRows(activeLayoutId))
                    {
                        QC_LOG_WARN(LOG_MODULE, "CMMS active runtime ensure failed layout=%s", activeLayoutId);
                    }
                }
            }

            QC_LOG_INFO(LOG_MODULE,
                        "CMMS init phase total=%llums recreate=%u",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - initStartMs),
                        recreate ? 1u : 0u);

            return true;
        };

        const QC::u64 ensureStartMs = QDrv::Timer::instance().milliseconds();
        if (!initializeCmmsDatabase(false))
            return false;

        auto hasThemeTables = [&](QCQL::Database &db) -> bool
        {
            QC::u32 tableId = 0;
            if (engine.lookupTableId(db, "Themes", tableId) != QCQL::Status::Success)
                return false;
            if (engine.lookupTableId(db, "ThemeTokens", tableId) != QCQL::Status::Success)
                return false;

            return true;
        };

        auto hasThemeRows = [&](QCQL::Database &db) -> bool
        {
            // Validate content by scanning for at least one non-tombstoned theme row.
            for (QC::usize t = 0; t < db.tables.size(); ++t)
            {
                if (QC::String::strcmp(db.tables[t].name, "Themes") != 0)
                    continue;

                QCQL::Table &themes = db.tables[t];
                for (QC::usize p = 0; p < themes.pages.size(); ++p)
                {
                    QCQL::Page page{};
                    if (engine.loadPage(db, themes.pages[p], page) != QCQL::Status::Success)
                        continue;

                    for (QC::usize r = 0; r < page.rowOffsets.size(); ++r)
                    {
                        QCQL::Row row{};
                        if (engine.readRow(db, themes.pages[p], page.rowOffsets[r], row) != QCQL::Status::Success)
                            continue;
                        if (row.tombstone)
                            continue;
                        return true;
                    }
                }
            }

            return false;
        };

        if (!hasThemeTables(m_cmmsDatabase))
        {
            QC_LOG_WARN(LOG_MODULE, "CMMS DB missing expected theme tables after initialization");
            return false;
        }

        if (!hasThemeRows(m_cmmsDatabase))
        {
            // Non-destructive retry: seed themes again, but keep DB available even if still empty.
            const QC::u64 reseedStartMs = QDrv::Timer::instance().milliseconds();
            const QCQL::Status reseedStatus = ThemeImporter::importBuiltinThemes(m_cmmsDatabase);
            if (reseedStatus != QCQL::Status::Success)
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB reseed attempt failed status=%d", static_cast<int>(reseedStatus));
            }
            QC_LOG_INFO(LOG_MODULE,
                        "CMMS ensure phase reseed_themes=%llums status=%d",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - reseedStartMs),
                        static_cast<int>(reseedStatus));
        }

        const QC::u64 validateThemesStartMs = QDrv::Timer::instance().milliseconds();
        if (!ThemeImporter::validateBuiltinThemes(m_cmmsDatabase))
        {
            QC_LOG_WARN(LOG_MODULE, "CMMS DB validation failed; rebuilding database from scratch");
            if (!initializeCmmsDatabase(true))
                return false;
            if (!ThemeImporter::validateBuiltinThemes(m_cmmsDatabase))
            {
                QC_LOG_WARN(LOG_MODULE, "CMMS DB validation still failed after rebuild");
                return false;
            }
        }
        QC_LOG_INFO(LOG_MODULE,
                    "CMMS ensure phase validate_themes=%llums",
                    static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - validateThemesStartMs));

        m_cmmsDatabaseReady = true;
        QC_LOG_INFO(LOG_MODULE,
                    "CMMS ensure total=%llums",
                    static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - ensureStartMs));
        return true;
    }

    void Desktop::openCMMS()
    {
        const QC::Rect area = workArea();
        QCQL::Database *database = nullptr;

        if (ensureCmmsDatabaseReady())
        {
            (void)ThemeImporter::importBuiltinThemes(m_cmmsDatabase);
            database = &m_cmmsDatabase;
        }
        QCMS::App::instance().open(database, area);
    }

    void Desktop::openBrowserFile(const char *path)
    {
        openBrowser();
        if (m_browser)
        {
            m_browser->openFile(path);
        }
    }

    void Desktop::openBrowserUrl(const char *url)
    {
        openBrowser();
        if (m_browser)
        {
            m_browser->openUrl(url);
        }
    }

    void Desktop::openBrowserHtmlText(const char *htmlText)
    {
        openBrowser();
        if (m_browser)
        {
            m_browser->openHtmlText(htmlText);
        }
    }

    void Desktop::openHelpWindow()
    {
        if (m_helpInlineHtml && m_helpInlineHtml[0])
        {
            openBrowserHtmlText(m_helpInlineHtml);
            return;
        }

        if (!m_helpSrcOrUrl || !m_helpSrcOrUrl[0])
            return;

        auto startsWithIgnoreCaseAscii = [](const char *s, const char *prefix) -> bool
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                char a = *s;
                char b = *prefix;
                if (!a)
                    return false;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        };

        if (startsWithIgnoreCaseAscii(m_helpSrcOrUrl, "http://") || startsWithIgnoreCaseAscii(m_helpSrcOrUrl, "https://"))
        {
            openBrowserUrl(m_helpSrcOrUrl);
            return;
        }
        openBrowserFile(m_helpSrcOrUrl);
    }

    void Desktop::onHelpClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return;
        desktop->openHelpWindow();
    }

    void Desktop::openCuiMLFile(const char *path)
    {
        if (!m_cuimlViewer)
        {
            m_cuimlViewer = new CuiMLViewer(this);
        }

        if (m_cuimlViewer)
        {
            m_cuimlViewer->openFile(path);
        }
    }

    void Desktop::recomputeTaskbarWindowBase()
    {
        m_taskbarWindowBaseX = 4;

        if (!m_taskbar)
            return;

        auto considerControl = [&](QW::Controls::IControl *ctrl)
        {
            if (!ctrl)
                return;

            QW::Rect bounds = ctrl->bounds();
            QC::i32 right = bounds.x + static_cast<QC::i32>(bounds.width) + 8;
            if (right > m_taskbarWindowBaseX)
            {
                m_taskbarWindowBaseX = right;
            }
        };

        considerControl(m_jsonStartButton);
        considerControl(m_jsonShutdownButton);
    }

    bool Desktop::applyThemeStateOnce(ThemeID activeThemeId)
    {
        auto seasonFromThemeId = [](ThemeID id) -> Season
        {
            switch (id)
            {
            case ThemeID::Spring:
                return Season::Spring;
            case ThemeID::Summer:
                return Season::Summer;
            case ThemeID::Autumn:
                return Season::Autumn;
            case ThemeID::Winter:
                return Season::Winter;
            default:
                return Season::Unknown;
            }
        };

        auto loadThemeById = [&](ThemeID id) -> bool
        {
            const char *themeIdText = themeIdToString(id);
            if (!themeIdText || !*themeIdText)
                return false;

            if (ensureCmmsDatabaseReady() && m_themeService.loadThemeFromDatabase(m_cmmsDatabase, id, m_loadedTheme))
            {
                applyLoadedThemeToOverrides();
                return true;
            }

            const char *themePathHint = nullptr;
            char pathBuf[96];
            QC::String::memset(pathBuf, 0, sizeof(pathBuf));

            const Season season = seasonFromThemeId(id);
            if (season != Season::Unknown)
            {
                const char *paths[4] = {};
                QC::usize pathCount = 0;
                seasonCandidatePaths(season, paths, sizeof(paths) / sizeof(paths[0]), &pathCount);
                for (QC::usize i = 0; i < pathCount; ++i)
                {
                    QFS::File *f = QFS::VFS::instance().open(paths[i], QFS::OpenMode::Read);
                    if (!f)
                        continue;
                    QFS::VFS::instance().close(f);

                    QC::String::strncpy(pathBuf, paths[i], sizeof(pathBuf) - 1);
                    pathBuf[sizeof(pathBuf) - 1] = '\0';
                    themePathHint = pathBuf;
                    break;
                }
            }

            char json[256];
            QC::String::memset(json, 0, sizeof(json));
            const char *prefix = "{\"id\":\"";
            const char *mid = "\",\"path\":\"";
            const char *suffix = "\"}";
            const QC::usize prefixLen = QC::String::strlen(prefix);
            const QC::usize idLen = QC::String::strlen(themeIdText);
            const QC::usize midLen = QC::String::strlen(mid);
            const QC::usize pathLen = themePathHint ? QC::String::strlen(themePathHint) : 0;
            const QC::usize suffixLen = QC::String::strlen(suffix);
            const QC::usize totalLen = prefixLen + idLen + (themePathHint ? (midLen + pathLen) : 0) + suffixLen;
            if (totalLen + 1 > sizeof(json))
                return false;

            QC::String::memcpy(json, prefix, prefixLen);
            QC::String::memcpy(json + prefixLen, themeIdText, idLen);

            QC::usize cursor = prefixLen + idLen;
            if (themePathHint)
            {
                QC::String::memcpy(json + cursor, mid, midLen);
                cursor += midLen;
                QC::String::memcpy(json + cursor, themePathHint, pathLen);
                cursor += pathLen;
            }

            QC::String::memcpy(json + cursor, suffix, suffixLen);
            cursor += suffixLen;
            json[cursor] = '\0';

            QC::JSON::Value themeValue;
            if (!QC::JSON::parse(json, themeValue) || !themeValue.isObject())
                return false;

            if (!loadThemeDefinition(&themeValue))
                return false;

            applyLoadedThemeToOverrides();
            return true;
        };

        if (activeThemeId == ThemeID::Default)
            activeThemeId = ThemeID::Standard;

        resetThemeOverrides();

        // Deterministic runtime theme flow:
        // load default -> load active -> merge into one effective style snapshot.
        if (!loadThemeById(ThemeID::Standard))
            return false;

        if (activeThemeId != ThemeID::Standard)
        {
            if (!loadThemeById(activeThemeId))
                return false;
        }

        // Keep desktop background aligned with the currently active theme assets.
        const char *wallpaperPath = m_loadedTheme.package.assets.backgrounds.desktopPrimary;
        if (wallpaperPath && *wallpaperPath)
        {
            if (ImageAsset *asset = loadImageAsset(wallpaperPath))
            {
                m_backgroundConfig.mode = BackgroundMode::Image;
                m_backgroundConfig.image = asset;
                m_backgroundConfig.scaleMode = QG::ImageScaleMode::Stretch;
                if (m_jsonDriven && m_jsonWallpaperView)
                {
                    m_jsonWallpaperView->setVisible(true);
                    m_jsonWallpaperView->setScaleMode(m_backgroundConfig.scaleMode);
                    m_jsonWallpaperView->setImage(&asset->surface);
                }
            }
        }

        // Clear panel background overrides so the new snapshot can repaint chrome colors.
        if (m_sidebar)
            m_sidebar->clearBackgroundColor();
        if (m_topBar)
            m_topBar->clearBackgroundColor();
        if (m_taskbar)
            m_taskbar->clearBackgroundColor();

        applyColors();

        if (m_desktopWindow)
        {
            const QW::Rect full = {0, 0, m_screenWidth, m_screenHeight};
            m_desktopWindow->invalidateRect(full);
        }

        QW::WindowManager::instance().render();
        return true;
    }

    bool Desktop::applyThemeByIdString(const char *themeIdText)
    {
        if (!themeIdText || !*themeIdText)
            return false;

        ThemeID target = ThemeID::Standard;
        if (!themeIdFromString(themeIdText, &target))
            return false;

        return applyThemeStateOnce(target);
    }

    void Desktop::cycleThemeFromSettings()
    {
        ThemeID next = ThemeID::Standard;
        switch (m_loadedTheme.package.id)
        {
        case ThemeID::Default:
            next = ThemeID::Standard;
            break;
        case ThemeID::Standard:
            next = ThemeID::Winter;
            break;
        case ThemeID::Winter:
            next = ThemeID::Spring;
            break;
        case ThemeID::Spring:
            next = ThemeID::Summer;
            break;
        case ThemeID::Summer:
            next = ThemeID::Autumn;
            break;
        case ThemeID::Autumn:
            next = ThemeID::Standard;
            break;
        default:
            next = ThemeID::Standard;
            break;
        }

        const char *nextIdText = themeIdToString(next);
        if (!applyThemeByIdString(nextIdText))
            return;

        if (m_titleLabel)
        {
            char label[96];
            QC::String::memset(label, 0, sizeof(label));
            const char *prefix = "CITADEL Desktop - Theme: ";
            const QC::usize prefixLen = QC::String::strlen(prefix);
            const QC::usize idLen = QC::String::strlen(nextIdText);
            if (prefixLen + idLen + 1 <= sizeof(label))
            {
                QC::String::memcpy(label, prefix, prefixLen);
                QC::String::memcpy(label + prefixLen, nextIdText, idLen);
                label[prefixLen + idLen] = '\0';
                m_titleLabel->setText(label);
            }
        }
    }

    void Desktop::showShutdownPrompt(QK::Shutdown::Reason reason)
    {
        if (!m_shutdownDialog)
        {
            m_shutdownDialog = new ShutdownDialog(this);
        }

        if (m_shutdownDialog)
        {
            m_shutdownDialog->open(reason);
        }
    }

    void Desktop::clearJsonDesktopState()
    {
        if (m_desktopWindow && m_desktopWindow->root())
        {
            // JSON controls are added to the root for input routing; remove them before deletion.
            m_desktopWindow->root()->clearChildren();
        }

        // Clear taskbar bookkeeping in case callers use it later
        for (QC::u32 i = 0; i < MAX_TASKBAR_WINDOWS; ++i)
        {
            m_taskbarEntries[i].windowId = 0;
            m_taskbarEntries[i].button = nullptr;
            m_taskbarEntries[i].isActive = false;
        }
        m_taskbarWindowCount = 0;

        for (QC::u8 i = 0; i < static_cast<QC::u8>(SidebarItem::Count); ++i)
        {
            m_sidebarButtons[i] = nullptr;
        }

        // Delete all JSON-created controls
        for (QC::isize i = static_cast<QC::isize>(m_jsonControls.size()) - 1; i >= 0; --i)
        {
            delete m_jsonControls[static_cast<QC::usize>(i)];
        }
        m_jsonControls.clear();
        m_jsonRootControls.clear();

        m_topBar = nullptr;
        m_sidebar = nullptr;
        m_taskbar = nullptr;
        m_logoButton = nullptr;
        m_titleLabel = nullptr;
        m_clockLabel = nullptr;
        m_jsonStartButton = nullptr;
        m_jsonShutdownButton = nullptr;
        m_jsonWallpaperView = nullptr;
        m_taskbarWindowBaseX = 4;

        m_helpButton = nullptr;
        if (m_helpTitle)
        {
            operator delete[](m_helpTitle);
            m_helpTitle = nullptr;
        }
        if (m_helpSrcOrUrl)
        {
            operator delete[](m_helpSrcOrUrl);
            m_helpSrcOrUrl = nullptr;
        }
        if (m_helpInlineHtml)
        {
            operator delete[](m_helpInlineHtml);
            m_helpInlineHtml = nullptr;
        }

        m_jsonDriven = false;

        resetThemeOverrides();
        resetBackgroundConfig();
        releaseImageAssets();
    }

    void Desktop::resetThemeOverrides()
    {
        m_themeOverrides = ThemeOverrides{};
        m_loadedTheme = ThemeLoadResult{};
        installDefaultMaterials();
    }

    void Desktop::installDefaultMaterials()
    {
        m_themeOverrides.materialCount = 0;

        // Built-in baseline material: cool blue / steel neutral glass.
        // This lets JSON overrides reference "glass.default" without needing to define it.
        if (m_themeOverrides.materialCount >= MAX_THEME_MATERIALS)
            return;

        auto &mat = m_themeOverrides.materials[m_themeOverrides.materialCount++];
        mat = ButtonMaterialDefinition{};
        mat.used = true;
        QC::String::strncpy(mat.name, "glass.default", sizeof(mat.name) - 1);
        mat.name[sizeof(mat.name) - 1] = '\0';

        auto setColor = [](ColorOverride &dst, QC::Color c)
        {
            dst.set = true;
            dst.value = c;
        };

        // Base button colors (mostly transparent steel)
        setColor(mat.style.fillNormal, QC::Color(52, 74, 92, 92));
        setColor(mat.style.fillHover, QC::Color(66, 92, 114, 112));
        setColor(mat.style.fillPressed, QC::Color(40, 58, 74, 132));
        setColor(mat.style.text, QC::Color(255, 255, 255, 255));
        // Avoid a uniform border line; glass should read as edge reflections.
        setColor(mat.style.border, QC::Color(255, 255, 255, 0));
        mat.style.glassSet = true;
        mat.style.glass = true;

        // Explicit layer recipe (normal/hover/pressed)
        setColor(mat.layers.normal.glossTop, QC::Color(235, 248, 255, 96));
        setColor(mat.layers.normal.glossBottom, QC::Color(235, 248, 255, 0));
        setColor(mat.layers.normal.shadeTop, QC::Color(0, 0, 0, 0));
        setColor(mat.layers.normal.shadeBottom, QC::Color(0, 0, 0, 92));

        setColor(mat.layers.hover.glossTop, QC::Color(245, 252, 255, 112));
        setColor(mat.layers.hover.glossBottom, QC::Color(245, 252, 255, 0));
        setColor(mat.layers.hover.shadeTop, QC::Color(0, 0, 0, 0));
        setColor(mat.layers.hover.shadeBottom, QC::Color(0, 0, 0, 104));

        setColor(mat.layers.pressed.glossTop, QC::Color(225, 242, 255, 84));
        setColor(mat.layers.pressed.glossBottom, QC::Color(225, 242, 255, 0));
        setColor(mat.layers.pressed.shadeTop, QC::Color(0, 0, 0, 0));
        setColor(mat.layers.pressed.shadeBottom, QC::Color(0, 0, 0, 120));
    }

    void Desktop::resetBackgroundConfig()
    {
        m_backgroundConfig.mode = BackgroundMode::Gradient;
        m_backgroundConfig.image = nullptr;
        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Stretch;
        m_backgroundConfig.topColor = QW::Color();
        m_backgroundConfig.bottomColor = QW::Color();
        m_backgroundConfig.topOverride = false;
        m_backgroundConfig.bottomOverride = false;
    }

    void Desktop::releaseImageAssets()
    {
        for (QC::usize i = 0; i < m_imageAssets.size(); ++i)
        {
            delete m_imageAssets[i];
        }
        m_imageAssets.clear();
    }

    Desktop::ImageAsset *Desktop::findImageAsset(const char *path) const
    {
        if (!path || !*path)
            return nullptr;
        for (QC::usize i = 0; i < m_imageAssets.size(); ++i)
        {
            if (QC::String::strcmp(m_imageAssets[i]->path, path) == 0)
                return m_imageAssets[i];
        }
        return nullptr;
    }

    bool Desktop::readFileBytes(const char *path, QC::Vector<QC::u8> &outBuffer, bool logFailure) const
    {
        outBuffer.clear();
        if (!path || !*path)
            return false;
        QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
        if (!file)
        {
            if (logFailure)
                QC_LOG_WARN(LOG_MODULE, "Image file %s not found", path);
            return false;
        }
        const QC::u64 size64 = file->size();
        if (size64 == 0 || size64 > 4 * 1024 * 1024)
        {
            QFS::VFS::instance().close(file);
            if (logFailure)
                QC_LOG_WARN(LOG_MODULE, "Image file %s has invalid size", path);
            return false;
        }
        const QC::usize size = static_cast<QC::usize>(size64);
        outBuffer.resize(size);
        const QC::isize read = file->read(outBuffer.data(), size);
        QFS::VFS::instance().close(file);
        if (read != static_cast<QC::isize>(size))
        {
            outBuffer.clear();
            if (logFailure)
                QC_LOG_WARN(LOG_MODULE, "Failed to read image file %s", path);
            return false;
        }
        return true;
    }

    Desktop::ImageAsset *Desktop::loadImageAsset(const char *path)
    {
        if (ImageAsset *cached = findImageAsset(path))
            return cached;

        auto endsWithIgnoreCaseAscii = [](const char *text, const char *suffix) -> bool {
            if (!text || !suffix)
                return false;
            const QC::usize tl = QC::String::strlen(text);
            const QC::usize sl = QC::String::strlen(suffix);
            if (sl > tl)
                return false;
            const char *a = text + (tl - sl);
            for (QC::usize i = 0; i < sl; ++i)
            {
                char c1 = a[i];
                char c2 = suffix[i];
                if (c1 >= 'A' && c1 <= 'Z')
                    c1 = static_cast<char>(c1 - 'A' + 'a');
                if (c2 >= 'A' && c2 <= 'Z')
                    c2 = static_cast<char>(c2 - 'A' + 'a');
                if (c1 != c2)
                    return false;
            }
            return true;
        };

        auto isWallpaperPath = [](const char *p) -> bool {
            if (!p)
                return false;
            const char *prefix = "/system/wall/";
            for (QC::usize i = 0; prefix[i] != '\0'; ++i)
            {
                if (p[i] == '\0' || p[i] != prefix[i])
                    return false;
            }
            return true;
        };

        auto startsWithAscii = [](const char *text, const char *prefix) -> bool {
            if (!text || !prefix)
                return false;
            for (QC::usize i = 0; prefix[i] != '\0'; ++i)
            {
                if (text[i] == '\0' || text[i] != prefix[i])
                    return false;
            }
            return true;
        };

        const bool timing = isWallpaperPath(path);
        const QC::u64 t0 = timing ? QDrv::Timer::instance().milliseconds() : 0;

        QC::Vector<QC::u8> buffer;
        char resolvedPathStorage[192];
        QC::String::memset(resolvedPathStorage, 0, sizeof(resolvedPathStorage));
        const char *resolvedPath = path;

        auto storeResolvedPath = [&](const char *candidatePath) -> bool {
            if (!candidatePath)
                return false;

            const QC::usize len = QC::String::strlen(candidatePath);
            if (len + 1 > sizeof(resolvedPathStorage))
                return false;

            QC::String::memcpy(resolvedPathStorage, candidatePath, len);
            resolvedPathStorage[len] = '\0';
            resolvedPath = resolvedPathStorage;
            return true;
        };

        if (!readFileBytes(path, buffer, false))
        {
            auto tryShadowedSystemAlias = [&](const char *sourcePath) -> bool {
                if (!sourcePath)
                    return false;

                const char *systemWallPrefix = "/system/wall/";
                const char *systemIconsPrefix = "/system/icons/";
                const char *systemIconSvgPrefix = "/system/icons/svg/";
                const char *aliasPrefix = nullptr;
                const char *tail = nullptr;

                if (startsWithAscii(sourcePath, systemIconSvgPrefix))
                {
                    aliasPrefix = "/ICONS/SVG/";
                    tail = sourcePath + QC::String::strlen(systemIconSvgPrefix);
                }
                else if (startsWithAscii(sourcePath, systemIconsPrefix))
                {
                    aliasPrefix = "/ICONS/";
                    tail = sourcePath + QC::String::strlen(systemIconsPrefix);
                }
                else if (startsWithAscii(sourcePath, systemWallPrefix))
                {
                    aliasPrefix = "/WALL/";
                    tail = sourcePath + QC::String::strlen(systemWallPrefix);
                }

                if (!aliasPrefix || !tail || !*tail)
                    return false;

                char aliasPath[192];
                QC::String::memset(aliasPath, 0, sizeof(aliasPath));
                const QC::usize aliasLen = QC::String::strlen(aliasPrefix);
                const QC::usize tailLen = QC::String::strlen(tail);
                if (aliasLen + tailLen + 1 > sizeof(aliasPath))
                    return false;

                QC::String::memcpy(aliasPath, aliasPrefix, aliasLen);
                QC::String::memcpy(aliasPath + aliasLen, tail, tailLen);
                aliasPath[aliasLen + tailLen] = '\0';

                if (!readFileBytes(aliasPath, buffer, false))
                    return false;

                return storeResolvedPath(aliasPath);
            };

            // Compatibility fallback for icon paths in JSON that use lowercase PNG names
            // while the shipped corpus uses uppercase PNG and/or SVG aliases.
            bool recovered = tryShadowedSystemAlias(path);
            if (path && startsWithAscii(path, "/system/icons/"))
            {
                const char *lastSlash = nullptr;
                const char *lastDot = nullptr;
                for (const char *p = path; *p; ++p)
                {
                    if (*p == '/')
                        lastSlash = p;
                    else if (*p == '.')
                        lastDot = p;
                }

                if (lastSlash && lastDot && lastDot > lastSlash + 1)
                {
                    char stemUpper[64];
                    char stemLower[64];
                    QC::String::memset(stemUpper, 0, sizeof(stemUpper));
                    QC::String::memset(stemLower, 0, sizeof(stemLower));

                    QC::usize n = 0;
                    for (const char *p = lastSlash + 1; p < lastDot && n + 1 < sizeof(stemUpper); ++p)
                    {
                        char c = *p;
                        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
                        {
                            if (c >= 'a' && c <= 'z')
                                stemUpper[n] = static_cast<char>(c - 'a' + 'A');
                            else
                                stemUpper[n] = c;

                            if (c >= 'A' && c <= 'Z')
                                stemLower[n] = static_cast<char>(c - 'A' + 'a');
                            else
                                stemLower[n] = c;
                            ++n;
                        }
                    }
                    stemUpper[n] = '\0';
                    stemLower[n] = '\0';

                    if (stemUpper[0])
                    {
                        char candidatePng[128];
                        QC::String::memset(candidatePng, 0, sizeof(candidatePng));
                        const char *pngPrefix = "/system/icons/";
                        const char *pngSuffix = ".PNG";
                        const QC::usize preLen = QC::String::strlen(pngPrefix);
                        const QC::usize stemLen = QC::String::strlen(stemUpper);
                        const QC::usize sufLen = QC::String::strlen(pngSuffix);
                        if (preLen + stemLen + sufLen + 1 <= sizeof(candidatePng))
                        {
                            QC::String::memcpy(candidatePng, pngPrefix, preLen);
                            QC::String::memcpy(candidatePng + preLen, stemUpper, stemLen);
                            QC::String::memcpy(candidatePng + preLen + stemLen, pngSuffix, sufLen);
                            candidatePng[preLen + stemLen + sufLen] = '\0';
                            if (readFileBytes(candidatePng, buffer, false))
                            {
                                recovered = storeResolvedPath(candidatePng);
                            }
                        }

                        if (!recovered)
                        {
                            char candidateSvg[160];
                            QC::String::memset(candidateSvg, 0, sizeof(candidateSvg));
                            const char *svgPrefix = "/system/icons/svg/";
                            const char *svgSuffix = ".svg";
                            const QC::usize sPreLen = QC::String::strlen(svgPrefix);
                            const QC::usize sStemLen = QC::String::strlen(stemLower);
                            const QC::usize sSufLen = QC::String::strlen(svgSuffix);
                            if (sPreLen + sStemLen + sSufLen + 1 <= sizeof(candidateSvg))
                            {
                                QC::String::memcpy(candidateSvg, svgPrefix, sPreLen);
                                QC::String::memcpy(candidateSvg + sPreLen, stemLower, sStemLen);
                                QC::String::memcpy(candidateSvg + sPreLen + sStemLen, svgSuffix, sSufLen);
                                candidateSvg[sPreLen + sStemLen + sSufLen] = '\0';
                                if (readFileBytes(candidateSvg, buffer, false))
                                {
                                    recovered = storeResolvedPath(candidateSvg);
                                }
                            }
                        }

                        if (!recovered && QC::String::strcmp(stemLower, "shutdown") == 0)
                        {
                            const char *fallbackPower = "/ICONS/svg/power.svg";
                            if (readFileBytes(fallbackPower, buffer, false))
                            {
                                recovered = storeResolvedPath(fallbackPower);
                            }
                        }
                    }
                }
            }

            if (!recovered)
            {
                (void)readFileBytes(path, buffer, true);
                return nullptr;
            }

            QC_LOG_WARN(LOG_MODULE, "Image path fallback: %s -> %s", path ? path : "<null>", resolvedPath ? resolvedPath : "<null>");
        }

        const QC::u64 t1 = timing ? QDrv::Timer::instance().milliseconds() : 0;
        if (timing)
        {
            QC_LOG_INFO(LOG_MODULE, "Wallpaper IO %s bytes=%u dt=%llums\n",
                        path ? path : "<null>",
                        static_cast<unsigned>(buffer.size()),
                        static_cast<unsigned long long>(t1 - t0));
        }

        auto *asset = new ImageAsset();
        asset->path[0] = '\0';
        if (path)
        {
            QC::String::strncpy(asset->path, path, sizeof(asset->path) - 1);
            asset->path[sizeof(asset->path) - 1] = '\0';
        }

        const QC::u64 t2 = timing ? QDrv::Timer::instance().milliseconds() : 0;

        const bool isSvg = endsWithIgnoreCaseAscii(resolvedPath, ".svg");
        bool decoded = false;
        if (isSvg)
        {
            decoded = QG::decodeSVG(buffer, asset->surface);
            if (!decoded)
            QC_LOG_WARN(LOG_MODULE, "Failed to decode SVG %s", resolvedPath ? resolvedPath : "<null>");
        }
        else
        {
            decoded = QG::decodePNG(buffer, asset->surface);
            if (!decoded)
            {
                bool recoveredWithSvg = false;
                if (resolvedPath && (startsWithAscii(resolvedPath, "/system/icons/") || startsWithAscii(resolvedPath, "/ICONS/")))
                {
                    const char *iconPrefix = startsWithAscii(resolvedPath, "/ICONS/") ? "/ICONS/" : "/system/icons/";
                    const char *svgPrefix = startsWithAscii(resolvedPath, "/ICONS/") ? "/ICONS/svg/" : "/system/icons/svg/";
                    const char *tail = resolvedPath + QC::String::strlen(iconPrefix);
                    const char *dot = nullptr;
                    for (const char *p = tail; *p; ++p)
                    {
                        if (*p == '.')
                            dot = p;
                    }

                    if (tail && *tail && dot && dot > tail)
                    {
                        char svgPath[192];
                        QC::String::memset(svgPath, 0, sizeof(svgPath));
                        const QC::usize prefixLen = QC::String::strlen(svgPrefix);
                        const QC::usize stemLen = static_cast<QC::usize>(dot - tail);
                        const QC::usize suffixLen = 4;
                        if (prefixLen + stemLen + suffixLen + 1 <= sizeof(svgPath))
                        {
                            QC::String::memcpy(svgPath, svgPrefix, prefixLen);
                            for (QC::usize i = 0; i < stemLen; ++i)
                            {
                                char c = tail[i];
                                if (c >= 'A' && c <= 'Z')
                                    c = static_cast<char>(c - 'A' + 'a');
                                svgPath[prefixLen + i] = c;
                            }
                            QC::String::memcpy(svgPath + prefixLen + stemLen, ".svg", suffixLen);
                            svgPath[prefixLen + stemLen + suffixLen] = '\0';

                            QC::Vector<QC::u8> svgBuffer;
                            if (readFileBytes(svgPath, svgBuffer, false) && QG::decodeSVG(svgBuffer, asset->surface))
                            {
                                buffer = svgBuffer;
                                recoveredWithSvg = storeResolvedPath(svgPath);
                                decoded = recoveredWithSvg;
                            }
                        }
                    }
                }

                if (!decoded)
                    QC_LOG_WARN(LOG_MODULE, "Failed to decode PNG %s", resolvedPath ? resolvedPath : "<null>");
                else if (recoveredWithSvg)
                    QC_LOG_WARN(LOG_MODULE, "Image decode fallback: %s -> %s", path ? path : "<null>", resolvedPath ? resolvedPath : "<null>");
            }
        }

        if (!decoded)
        {
            delete asset;
            return nullptr;
        }

        const QC::u64 t3 = timing ? QDrv::Timer::instance().milliseconds() : 0;
        if (timing)
        {
            QC_LOG_INFO(LOG_MODULE, "Wallpaper decode %s dt=%llums\n",
                        path ? path : "<null>",
                        static_cast<unsigned long long>(t3 - t2));
        }

        if (isWallpaperPath(path))
        {
            QC_LOG_INFO(LOG_MODULE, "Loaded wallpaper %s (%ux%u)\n", path ? path : "<null>",
                        static_cast<unsigned>(asset->surface.width), static_cast<unsigned>(asset->surface.height));
        }

        m_imageAssets.push_back(asset);
        return asset;
    }

    float Desktop::clamp01(float value)
    {
        if (value < 0.0f)
            return 0.0f;
        if (value > 1.0f)
            return 1.0f;
        return value;
    }

    QC::u8 Desktop::clampToByte(QC::u32 value)
    {
        return value > 255 ? 255 : static_cast<QC::u8>(value);
    }

    bool Desktop::parseColorOverride(const QC::JSON::Value *object, const char *key, ColorOverride &target)
    {
        if (!object || !object->isObject())
            return false;
        const QC::JSON::Value *value = object->find(key);
        const char *text = stringOrNull(value);
        if (!text)
            return false;
        QC::Color parsed;
        if (!parseColorString(text, parsed))
            return false;
        target.set = true;
        target.value = parsed;
        return true;
    }

    bool Desktop::parseUnsignedOverride(const QC::JSON::Value *object, const char *key, QC::u32 &outValue)
    {
        if (!object || !object->isObject())
            return false;
        const QC::JSON::Value *value = object->find(key);
        if (!value || !value->isNumber())
            return false;
        double number = value->asNumber(static_cast<double>(outValue));
        if (number < 0.0)
            number = 0.0;
        outValue = static_cast<QC::u32>(number);
        return true;
    }

    bool Desktop::parseSignedOverride(const QC::JSON::Value *object, const char *key, QC::i32 &outValue)
    {
        if (!object || !object->isObject())
            return false;
        const QC::JSON::Value *value = object->find(key);
        if (!value || !value->isNumber())
            return false;
        outValue = static_cast<QC::i32>(value->asNumber(static_cast<double>(outValue)));
        return true;
    }

    bool Desktop::parseBoolOverride(const QC::JSON::Value *object, const char *key, bool &outValue)
    {
        if (!object || !object->isObject())
            return false;
        const QC::JSON::Value *value = object->find(key);
        if (!value || !value->isBool())
            return false;
        outValue = value->asBool(false);
        return true;
    }

    bool Desktop::parseButtonStyleOverride(const QC::JSON::Value *buttons,
                                           const char *key,
                                           ButtonStyleOverrides &out)
    {
        out = ButtonStyleOverrides{};
        if (!buttons || !buttons->isObject())
            return false;

        const QC::JSON::Value *value = buttons->find(key);
        if (!value || !value->isObject())
            return false;

        bool changed = false;
        changed |= parseColorOverride(value, "fillNormal", out.fillNormal);
        changed |= parseColorOverride(value, "fillHover", out.fillHover);
        changed |= parseColorOverride(value, "fillPressed", out.fillPressed);
        changed |= parseColorOverride(value, "text", out.text);
        changed |= parseColorOverride(value, "border", out.border);

        bool glass = false;
        if (parseBoolOverride(value, "glass", glass))
        {
            out.glassSet = true;
            out.glass = glass;
            changed = true;
        }

        const QC::JSON::Value *shine = value->find("shineIntensity");
        if (shine && shine->isNumber())
        {
            out.shineSet = true;
            out.shineIntensity = static_cast<float>(shine->asNumber(out.shineIntensity));
            changed = true;
        }

        if (const QC::JSON::Value *material = value->find("material"); material && material->isString())
        {
            const char *matName = material->asString(nullptr);
            out.materialSet = true;
            if (matName)
            {
                QC::String::strncpy(out.material, matName, sizeof(out.material) - 1);
                out.material[sizeof(out.material) - 1] = '\0';
            }
            else
            {
                out.material[0] = '\0';
            }
            changed = true;
        }

        if (!changed)
        {
            out = ButtonStyleOverrides{};
        }

        return changed;
    }

    bool Desktop::loadThemeDefinition(const QC::JSON::Value *themeValue)
    {
        return m_themeService.loadTheme(themeValue, m_loadedTheme);
    }

    void Desktop::applyLoadedThemeToOverrides()
    {
        if (!m_loadedTheme.loaded)
            return;

        const auto applyColor = [](ColorOverride &target, const QC::Color &value)
        {
            target.set = true;
            target.value = value;
        };

        const ThemeColorPalette &palette = m_loadedTheme.theme.colors();
        applyColor(m_themeOverrides.palette.accent, palette.accentPrimary);
        applyColor(m_themeOverrides.palette.accentLight, palette.accentSecondary);
        applyColor(m_themeOverrides.palette.accentDark, palette.accentPrimary.darker(0.2f));
        applyColor(m_themeOverrides.palette.panel, palette.windowBackground);
        applyColor(m_themeOverrides.palette.panelBorder, palette.border);
        applyColor(m_themeOverrides.palette.text, palette.textPrimary);
        applyColor(m_themeOverrides.palette.textSecondary, palette.textSecondary);

        const ThemeEffects &effects = m_loadedTheme.theme.effects();
        m_themeOverrides.metrics.cornerRadiusSet = true;
        m_themeOverrides.metrics.cornerRadius = effects.border.radius;
        m_themeOverrides.metrics.buttonCornerRadiusSet = true;
        m_themeOverrides.metrics.buttonCornerRadius = effects.border.radius;
        m_themeOverrides.metrics.borderWidthSet = true;
        m_themeOverrides.metrics.borderWidth = effects.border.width;
        applyColor(m_themeOverrides.effects.borderColor, effects.border.color);

        auto &shadow = m_themeOverrides.effects.shadow;
        shadow.offsetXSet = true;
        shadow.offsetX = effects.shadow.offsetX;
        shadow.offsetYSet = true;
        shadow.offsetY = effects.shadow.offsetY;
        shadow.blurSet = true;
        shadow.blurRadius = effects.shadow.blurRadius;
        applyColor(shadow.color, effects.shadow.color);

        auto &glow = m_themeOverrides.effects.glow;
        glow.radiusSet = true;
        glow.radius = effects.glow.radius;
        glow.intensitySet = true;
        glow.intensity = effects.glow.intensity;
        applyColor(glow.color, effects.glow.color);

        auto &transparency = m_themeOverrides.transparency;
        transparency.windowOpacitySet = true;
        transparency.windowOpacity = effects.transparency.windowOpacity;
        transparency.panelOpacitySet = true;
        transparency.panelOpacity = effects.transparency.panelOpacity;

        auto assignButton = [&](QW::ButtonRole role,
                                const QC::Color &fillNormal,
                                const QC::Color &fillHover,
                                const QC::Color &fillPressed,
                                const QC::Color &textColor,
                                const QC::Color &borderColor,
                                bool glass)
        {
            auto &entry = m_themeOverrides.button[static_cast<QC::u32>(role)];
            entry.fillNormal.set = true;
            entry.fillNormal.value = fillNormal;
            entry.fillHover.set = true;
            entry.fillHover.value = fillHover;
            entry.fillPressed.set = true;
            entry.fillPressed.value = fillPressed;
            entry.text.set = true;
            entry.text.value = textColor;
            entry.border.set = true;
            entry.border.value = borderColor;
            entry.glassSet = true;
            entry.glass = glass;
        };

        assignButton(QW::ButtonRole::Default,
                     palette.buttonNormal,
                     palette.buttonHover,
                     palette.buttonPressed,
                     palette.textPrimary,
                     palette.border,
                     false);

        assignButton(QW::ButtonRole::Sidebar,
                     palette.buttonNormal,
                     palette.buttonHover,
                     palette.buttonPressed,
                     palette.textSecondary,
                     palette.border,
                     false);

        assignButton(QW::ButtonRole::Accent,
                     palette.accentPrimary,
                     palette.accentSecondary,
                     palette.accentPrimary.darker(0.25f),
                     palette.textPrimary,
                     palette.accentPrimary.darker(0.3f),
                     true);

        auto &fontOverrides = m_themeOverrides.font;
        const ThemeFont &themeFont = m_loadedTheme.theme.font();
        if (themeFont.family[0] != '\0')
        {
            fontOverrides.familySet = true;
            QC::String::strncpy(fontOverrides.family, themeFont.family, sizeof(fontOverrides.family) - 1);
            fontOverrides.family[sizeof(fontOverrides.family) - 1] = '\0';
        }
        fontOverrides.sizeSet = true;
        fontOverrides.size = themeFont.size;

        m_themeOverrides.active = true;
    }

    void Desktop::parseThemeOverrides(const QC::JSON::Value *themeValue)
    {
        resetThemeOverrides();

        if (!themeValue)
            return;

        if (loadThemeDefinition(themeValue))
        {
            applyLoadedThemeToOverrides();
        }

        if (!themeValue->isObject())
            return;

        // Support a compact palette-only form:
        // { "accent": "#...", "panel": "#...", ... }
        // This is used by seasonal desktop presets.
        {
            bool changed = false;
            changed |= parseColorOverride(themeValue, "accent", m_themeOverrides.palette.accent);
            changed |= parseColorOverride(themeValue, "accentLight", m_themeOverrides.palette.accentLight);
            changed |= parseColorOverride(themeValue, "accentDark", m_themeOverrides.palette.accentDark);
            changed |= parseColorOverride(themeValue, "panel", m_themeOverrides.palette.panel);
            changed |= parseColorOverride(themeValue, "panelBorder", m_themeOverrides.palette.panelBorder);
            changed |= parseColorOverride(themeValue, "text", m_themeOverrides.palette.text);
            changed |= parseColorOverride(themeValue, "textSecondary", m_themeOverrides.palette.textSecondary);
            if (changed)
                m_themeOverrides.active = true;
        }

        const QC::JSON::Value *overrides = themeValue->find("overrides");
        if (!overrides || !overrides->isObject())
            return;

        if (const QC::JSON::Value *materials = overrides->find("materials"); materials && materials->isObject())
        {
            const QC::JSON::Object *mats = materials->asObject();
            const QC::usize mn = mats ? mats->size() : 0;

            auto findIndexByName = [&](const char *name) -> QC::i32
            {
                if (!name || !*name)
                    return -1;
                for (QC::u32 i = 0; i < m_themeOverrides.materialCount && i < MAX_THEME_MATERIALS; ++i)
                {
                    if (!m_themeOverrides.materials[i].used)
                        continue;
                    if (QC::String::strcmp(m_themeOverrides.materials[i].name, name) == 0)
                        return static_cast<QC::i32>(i);
                }
                return -1;
            };

            auto parseLayerSet = [&](const QC::JSON::Value *layerObj, ButtonMaterialLayerSet &outSet) -> bool
            {
                if (!layerObj || !layerObj->isObject())
                    return false;
                bool any = false;
                any |= parseColorOverride(layerObj, "glossTop", outSet.glossTop);
                any |= parseColorOverride(layerObj, "glossBottom", outSet.glossBottom);
                any |= parseColorOverride(layerObj, "shadeTop", outSet.shadeTop);
                any |= parseColorOverride(layerObj, "shadeBottom", outSet.shadeBottom);
                return any;
            };

            for (QC::usize i = 0; i < mn; ++i)
            {
                const auto &ent = (*mats)[i];
                if (!ent.key || !ent.value || !ent.value->isObject())
                    continue;

                ButtonMaterialDefinition parsed;
                parsed.used = true;
                QC::String::strncpy(parsed.name, ent.key, sizeof(parsed.name) - 1);
                parsed.name[sizeof(parsed.name) - 1] = '\0';

                bool changed = false;
                changed |= parseColorOverride(ent.value, "fillNormal", parsed.style.fillNormal);
                changed |= parseColorOverride(ent.value, "fillHover", parsed.style.fillHover);
                changed |= parseColorOverride(ent.value, "fillPressed", parsed.style.fillPressed);
                changed |= parseColorOverride(ent.value, "text", parsed.style.text);
                changed |= parseColorOverride(ent.value, "border", parsed.style.border);

                bool glass = false;
                if (parseBoolOverride(ent.value, "glass", glass))
                {
                    parsed.style.glassSet = true;
                    parsed.style.glass = glass;
                    changed = true;
                }

                const QC::JSON::Value *shine = ent.value->find("shineIntensity");
                if (shine && shine->isNumber())
                {
                    parsed.style.shineSet = true;
                    parsed.style.shineIntensity = static_cast<float>(shine->asNumber(parsed.style.shineIntensity));
                    changed = true;
                }

                if (const QC::JSON::Value *layers = ent.value->find("layers"); layers && layers->isObject())
                {
                    changed |= parseLayerSet(layers->find("normal"), parsed.layers.normal);
                    changed |= parseLayerSet(layers->find("hover"), parsed.layers.hover);
                    changed |= parseLayerSet(layers->find("pressed"), parsed.layers.pressed);
                }

                if (!changed)
                    continue;

                const QC::i32 existing = findIndexByName(parsed.name);
                if (existing >= 0)
                {
                    m_themeOverrides.materials[static_cast<QC::u32>(existing)] = parsed;
                }
                else if (m_themeOverrides.materialCount < MAX_THEME_MATERIALS)
                {
                    m_themeOverrides.materials[m_themeOverrides.materialCount++] = parsed;
                }

                m_themeOverrides.active = true;
            }
        }

        if (const QC::JSON::Value *palette = overrides->find("palette"))
        {
            bool changed = false;
            changed |= parseColorOverride(palette, "accent", m_themeOverrides.palette.accent);
            changed |= parseColorOverride(palette, "accentLight", m_themeOverrides.palette.accentLight);
            changed |= parseColorOverride(palette, "accentDark", m_themeOverrides.palette.accentDark);
            changed |= parseColorOverride(palette, "panel", m_themeOverrides.palette.panel);
            changed |= parseColorOverride(palette, "panelBorder", m_themeOverrides.palette.panelBorder);
            changed |= parseColorOverride(palette, "text", m_themeOverrides.palette.text);
            changed |= parseColorOverride(palette, "textSecondary", m_themeOverrides.palette.textSecondary);
            if (changed)
                m_themeOverrides.active = true;
        }

        if (const QC::JSON::Value *metrics = overrides->find("metrics"))
        {
            QC::u32 value = 0;
            if (parseUnsignedOverride(metrics, "cornerRadius", value))
            {
                m_themeOverrides.metrics.cornerRadiusSet = true;
                m_themeOverrides.metrics.cornerRadius = value;
                m_themeOverrides.active = true;
            }

            value = 0;
            if (parseUnsignedOverride(metrics, "buttonCornerRadius", value))
            {
                m_themeOverrides.metrics.buttonCornerRadiusSet = true;
                m_themeOverrides.metrics.buttonCornerRadius = value;
                m_themeOverrides.active = true;
            }

            value = 0;
            if (parseUnsignedOverride(metrics, "borderWidth", value))
            {
                m_themeOverrides.metrics.borderWidthSet = true;
                m_themeOverrides.metrics.borderWidth = value;
                m_themeOverrides.active = true;
            }
        }

        if (const QC::JSON::Value *buttons = overrides->find("button"))
        {
            auto assign = [&](const char *key, QW::ButtonRole role)
            {
                if (parseButtonStyleOverride(buttons, key, m_themeOverrides.button[static_cast<QC::u32>(role)]))
                {
                    m_themeOverrides.active = true;
                }
            };

            assign("sidebar", QW::ButtonRole::Sidebar);
            assign("accent", QW::ButtonRole::Accent);
            assign("destructive", QW::ButtonRole::Destructive);
        }

        if (const QC::JSON::Value *effects = overrides->find("effects"))
        {
            if (const QC::JSON::Value *border = effects->find("border"))
            {
                bool changed = false;
                changed |= parseColorOverride(border, "color", m_themeOverrides.effects.borderColor);

                QC::u32 value = 0;
                if (parseUnsignedOverride(border, "width", value))
                {
                    m_themeOverrides.metrics.borderWidthSet = true;
                    m_themeOverrides.metrics.borderWidth = value;
                    changed = true;
                }

                value = 0;
                if (parseUnsignedOverride(border, "radius", value))
                {
                    m_themeOverrides.metrics.cornerRadiusSet = true;
                    m_themeOverrides.metrics.cornerRadius = value;
                    m_themeOverrides.metrics.buttonCornerRadiusSet = true;
                    m_themeOverrides.metrics.buttonCornerRadius = value;
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }

            if (const QC::JSON::Value *shadow = effects->find("shadow"))
            {
                bool changed = false;
                QC::i32 signedValue = 0;
                if (parseSignedOverride(shadow, "offsetX", signedValue))
                {
                    m_themeOverrides.effects.shadow.offsetXSet = true;
                    m_themeOverrides.effects.shadow.offsetX = signedValue;
                    changed = true;
                }

                signedValue = 0;
                if (parseSignedOverride(shadow, "offsetY", signedValue))
                {
                    m_themeOverrides.effects.shadow.offsetYSet = true;
                    m_themeOverrides.effects.shadow.offsetY = signedValue;
                    changed = true;
                }

                QC::u32 blur = 0;
                if (parseUnsignedOverride(shadow, "blur", blur))
                {
                    m_themeOverrides.effects.shadow.blurSet = true;
                    m_themeOverrides.effects.shadow.blurRadius = blur;
                    changed = true;
                }

                if (parseColorOverride(shadow, "color", m_themeOverrides.effects.shadow.color))
                {
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }

            if (const QC::JSON::Value *glow = effects->find("glow"))
            {
                bool changed = false;
                QC::u32 value = 0;
                if (parseUnsignedOverride(glow, "radius", value))
                {
                    m_themeOverrides.effects.glow.radiusSet = true;
                    m_themeOverrides.effects.glow.radius = value;
                    changed = true;
                }

                value = 0;
                if (parseUnsignedOverride(glow, "intensity", value))
                {
                    m_themeOverrides.effects.glow.intensitySet = true;
                    m_themeOverrides.effects.glow.intensity = value;
                    changed = true;
                }

                if (parseColorOverride(glow, "color", m_themeOverrides.effects.glow.color))
                {
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }

            if (const QC::JSON::Value *transparency = effects->find("transparency"))
            {
                bool changed = false;
                QC::u32 value = 0;
                if (parseUnsignedOverride(transparency, "windowOpacity", value))
                {
                    m_themeOverrides.transparency.windowOpacitySet = true;
                    m_themeOverrides.transparency.windowOpacity = clampToByte(value);
                    changed = true;
                }

                value = 0;
                if (parseUnsignedOverride(transparency, "panelOpacity", value))
                {
                    m_themeOverrides.transparency.panelOpacitySet = true;
                    m_themeOverrides.transparency.panelOpacity = clampToByte(value);
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }
        }

        if (const QC::JSON::Value *font = overrides->find("font"))
        {
            bool changed = false;
            if (const char *family = stringOrNull(font->find("family")))
            {
                m_themeOverrides.font.familySet = true;
                QC::String::strncpy(m_themeOverrides.font.family, family, sizeof(m_themeOverrides.font.family) - 1);
                m_themeOverrides.font.family[sizeof(m_themeOverrides.font.family) - 1] = '\0';
                changed = true;
            }

            QC::u32 sizeValue = m_themeOverrides.font.size;
            if (parseUnsignedOverride(font, "size", sizeValue))
            {
                if (sizeValue == 0)
                {
                    sizeValue = 1;
                }
                if (sizeValue > 255)
                {
                    sizeValue = 255;
                }
                m_themeOverrides.font.sizeSet = true;
                m_themeOverrides.font.size = static_cast<QC::u8>(sizeValue);
                changed = true;
            }

            if (changed)
                m_themeOverrides.active = true;
        }
    }

    void Desktop::parseThemeOverridesMerge(const QC::JSON::Value *themeValue)
    {
        if (!themeValue || !themeValue->isObject())
            return;

        // Allow desktop-overrides theme to switch the full theme package
        // (id/file/definition/assets), not only palette deltas.
        if (looksLikeFullThemeDefinition(themeValue))
        {
            parseThemeOverrides(themeValue);
            return;
        }

        // Merge-only: do not reset, do not load theme definitions.
        {
            bool changed = false;
            changed |= parseColorOverride(themeValue, "accent", m_themeOverrides.palette.accent);
            changed |= parseColorOverride(themeValue, "accentLight", m_themeOverrides.palette.accentLight);
            changed |= parseColorOverride(themeValue, "accentDark", m_themeOverrides.palette.accentDark);
            changed |= parseColorOverride(themeValue, "panel", m_themeOverrides.palette.panel);
            changed |= parseColorOverride(themeValue, "panelBorder", m_themeOverrides.palette.panelBorder);
            changed |= parseColorOverride(themeValue, "text", m_themeOverrides.palette.text);
            changed |= parseColorOverride(themeValue, "textSecondary", m_themeOverrides.palette.textSecondary);
            if (changed)
                m_themeOverrides.active = true;
        }

        const QC::JSON::Value *overrides = themeValue->find("overrides");
        if (!overrides || !overrides->isObject())
            return;

        if (const QC::JSON::Value *materials = overrides->find("materials"); materials && materials->isObject())
        {
            const QC::JSON::Object *mats = materials->asObject();
            const QC::usize mn = mats ? mats->size() : 0;

            auto findIndexByName = [&](const char *name) -> QC::i32
            {
                if (!name || !*name)
                    return -1;
                for (QC::u32 i = 0; i < m_themeOverrides.materialCount && i < MAX_THEME_MATERIALS; ++i)
                {
                    if (!m_themeOverrides.materials[i].used)
                        continue;
                    if (QC::String::strcmp(m_themeOverrides.materials[i].name, name) == 0)
                        return static_cast<QC::i32>(i);
                }
                return -1;
            };

            auto parseLayerSet = [&](const QC::JSON::Value *layerObj, ButtonMaterialLayerSet &outSet) -> bool
            {
                if (!layerObj || !layerObj->isObject())
                    return false;
                bool any = false;
                any |= parseColorOverride(layerObj, "glossTop", outSet.glossTop);
                any |= parseColorOverride(layerObj, "glossBottom", outSet.glossBottom);
                any |= parseColorOverride(layerObj, "shadeTop", outSet.shadeTop);
                any |= parseColorOverride(layerObj, "shadeBottom", outSet.shadeBottom);
                return any;
            };

            for (QC::usize i = 0; i < mn; ++i)
            {
                const auto &ent = (*mats)[i];
                if (!ent.key || !ent.value || !ent.value->isObject())
                    continue;

                ButtonMaterialDefinition parsed;
                parsed.used = true;
                QC::String::strncpy(parsed.name, ent.key, sizeof(parsed.name) - 1);
                parsed.name[sizeof(parsed.name) - 1] = '\0';

                bool changed = false;
                changed |= parseColorOverride(ent.value, "fillNormal", parsed.style.fillNormal);
                changed |= parseColorOverride(ent.value, "fillHover", parsed.style.fillHover);
                changed |= parseColorOverride(ent.value, "fillPressed", parsed.style.fillPressed);
                changed |= parseColorOverride(ent.value, "text", parsed.style.text);
                changed |= parseColorOverride(ent.value, "border", parsed.style.border);

                bool glass = false;
                if (parseBoolOverride(ent.value, "glass", glass))
                {
                    parsed.style.glassSet = true;
                    parsed.style.glass = glass;
                    changed = true;
                }

                const QC::JSON::Value *shine = ent.value->find("shineIntensity");
                if (shine && shine->isNumber())
                {
                    parsed.style.shineSet = true;
                    parsed.style.shineIntensity = static_cast<float>(shine->asNumber(parsed.style.shineIntensity));
                    changed = true;
                }

                if (const QC::JSON::Value *layers = ent.value->find("layers"); layers && layers->isObject())
                {
                    changed |= parseLayerSet(layers->find("normal"), parsed.layers.normal);
                    changed |= parseLayerSet(layers->find("hover"), parsed.layers.hover);
                    changed |= parseLayerSet(layers->find("pressed"), parsed.layers.pressed);
                }

                if (!changed)
                    continue;

                const QC::i32 existing = findIndexByName(parsed.name);
                if (existing >= 0)
                {
                    m_themeOverrides.materials[static_cast<QC::u32>(existing)] = parsed;
                }
                else if (m_themeOverrides.materialCount < MAX_THEME_MATERIALS)
                {
                    m_themeOverrides.materials[m_themeOverrides.materialCount++] = parsed;
                }

                m_themeOverrides.active = true;
            }
        }

        if (const QC::JSON::Value *palette = overrides->find("palette"))
        {
            bool changed = false;
            changed |= parseColorOverride(palette, "accent", m_themeOverrides.palette.accent);
            changed |= parseColorOverride(palette, "accentLight", m_themeOverrides.palette.accentLight);
            changed |= parseColorOverride(palette, "accentDark", m_themeOverrides.palette.accentDark);
            changed |= parseColorOverride(palette, "panel", m_themeOverrides.palette.panel);
            changed |= parseColorOverride(palette, "panelBorder", m_themeOverrides.palette.panelBorder);
            changed |= parseColorOverride(palette, "text", m_themeOverrides.palette.text);
            changed |= parseColorOverride(palette, "textSecondary", m_themeOverrides.palette.textSecondary);
            if (changed)
                m_themeOverrides.active = true;
        }

        if (const QC::JSON::Value *metrics = overrides->find("metrics"))
        {
            QC::u32 value = 0;
            if (parseUnsignedOverride(metrics, "cornerRadius", value))
            {
                m_themeOverrides.metrics.cornerRadiusSet = true;
                m_themeOverrides.metrics.cornerRadius = value;
                m_themeOverrides.active = true;
            }

            value = 0;
            if (parseUnsignedOverride(metrics, "buttonCornerRadius", value))
            {
                m_themeOverrides.metrics.buttonCornerRadiusSet = true;
                m_themeOverrides.metrics.buttonCornerRadius = value;
                m_themeOverrides.active = true;
            }

            value = 0;
            if (parseUnsignedOverride(metrics, "borderWidth", value))
            {
                m_themeOverrides.metrics.borderWidthSet = true;
                m_themeOverrides.metrics.borderWidth = value;
                m_themeOverrides.active = true;
            }
        }

        if (const QC::JSON::Value *buttons = overrides->find("button"))
        {
            auto assign = [&](const char *key, QW::ButtonRole role)
            {
                if (parseButtonStyleOverride(buttons, key, m_themeOverrides.button[static_cast<QC::u32>(role)]))
                {
                    m_themeOverrides.active = true;
                }
            };

            assign("sidebar", QW::ButtonRole::Sidebar);
            assign("accent", QW::ButtonRole::Accent);
            assign("destructive", QW::ButtonRole::Destructive);
        }

        if (const QC::JSON::Value *effects = overrides->find("effects"))
        {
            if (const QC::JSON::Value *border = effects->find("border"))
            {
                bool changed = false;
                changed |= parseColorOverride(border, "color", m_themeOverrides.effects.borderColor);

                QC::u32 value = 0;
                if (parseUnsignedOverride(border, "width", value))
                {
                    m_themeOverrides.metrics.borderWidthSet = true;
                    m_themeOverrides.metrics.borderWidth = value;
                    changed = true;
                }

                value = 0;
                if (parseUnsignedOverride(border, "radius", value))
                {
                    m_themeOverrides.metrics.cornerRadiusSet = true;
                    m_themeOverrides.metrics.cornerRadius = value;
                    m_themeOverrides.metrics.buttonCornerRadiusSet = true;
                    m_themeOverrides.metrics.buttonCornerRadius = value;
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }

            if (const QC::JSON::Value *shadow = effects->find("shadow"))
            {
                bool changed = false;
                QC::i32 signedValue = 0;
                if (parseSignedOverride(shadow, "offsetX", signedValue))
                {
                    m_themeOverrides.effects.shadow.offsetXSet = true;
                    m_themeOverrides.effects.shadow.offsetX = signedValue;
                    changed = true;
                }

                signedValue = 0;
                if (parseSignedOverride(shadow, "offsetY", signedValue))
                {
                    m_themeOverrides.effects.shadow.offsetYSet = true;
                    m_themeOverrides.effects.shadow.offsetY = signedValue;
                    changed = true;
                }

                QC::u32 blur = 0;
                if (parseUnsignedOverride(shadow, "blur", blur))
                {
                    m_themeOverrides.effects.shadow.blurSet = true;
                    m_themeOverrides.effects.shadow.blurRadius = blur;
                    changed = true;
                }

                if (parseColorOverride(shadow, "color", m_themeOverrides.effects.shadow.color))
                {
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }

            if (const QC::JSON::Value *glow = effects->find("glow"))
            {
                bool changed = false;
                QC::u32 value = 0;
                if (parseUnsignedOverride(glow, "radius", value))
                {
                    m_themeOverrides.effects.glow.radiusSet = true;
                    m_themeOverrides.effects.glow.radius = value;
                    changed = true;
                }

                value = 0;
                if (parseUnsignedOverride(glow, "intensity", value))
                {
                    m_themeOverrides.effects.glow.intensitySet = true;
                    m_themeOverrides.effects.glow.intensity = value;
                    changed = true;
                }

                if (parseColorOverride(glow, "color", m_themeOverrides.effects.glow.color))
                {
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }

            if (const QC::JSON::Value *transparency = effects->find("transparency"))
            {
                bool changed = false;
                QC::u32 value = 0;
                if (parseUnsignedOverride(transparency, "windowOpacity", value))
                {
                    m_themeOverrides.transparency.windowOpacitySet = true;
                    m_themeOverrides.transparency.windowOpacity = clampToByte(value);
                    changed = true;
                }

                value = 0;
                if (parseUnsignedOverride(transparency, "panelOpacity", value))
                {
                    m_themeOverrides.transparency.panelOpacitySet = true;
                    m_themeOverrides.transparency.panelOpacity = clampToByte(value);
                    changed = true;
                }

                if (changed)
                    m_themeOverrides.active = true;
            }
        }

    }

    void Desktop::parseBackground(const QC::JSON::Value *backgroundValue)
    {
        resetBackgroundConfig();
        if (!backgroundValue || !backgroundValue->isObject())
        {
            if (m_jsonDriven && m_jsonWallpaperView)
            {
                m_jsonWallpaperView->setVisible(false);
                m_jsonWallpaperView->setImage(nullptr);
            }
            return;
        }

        const char *type = stringOrNull(backgroundValue->find("type"));

        if (type && equalsIgnoreCase(type, "image"))
        {
            const char *path = stringOrNull(backgroundValue->find("path"));
            char resolvedPath[192];
            QC::String::memset(resolvedPath, 0, sizeof(resolvedPath));
            const char *pathToLoad = path;
            if (resolveThemeAssetKey(path, &m_loadedTheme.package.assets, resolvedPath, sizeof(resolvedPath)))
            {
                pathToLoad = resolvedPath;
            }

            if (ImageAsset *asset = loadImageAsset(pathToLoad))
            {
                m_backgroundConfig.mode = BackgroundMode::Image;
                m_backgroundConfig.image = asset;

                const char *modeText = stringOrNull(backgroundValue->find("mode"));
                if (modeText)
                {
                    if (equalsIgnoreCase(modeText, "fit"))
                        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Fit;
                    else if (equalsIgnoreCase(modeText, "center"))
                        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Center;
                    else if (equalsIgnoreCase(modeText, "tile"))
                        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Tile;
                    else if (equalsIgnoreCase(modeText, "fill"))
                        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Fill;
                    else if (equalsIgnoreCase(modeText, "original"))
                        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Original;
                    else
                        m_backgroundConfig.scaleMode = QG::ImageScaleMode::Stretch;
                }

                if (m_jsonDriven && m_jsonWallpaperView)
                {
                    m_jsonWallpaperView->setVisible(true);
                    m_jsonWallpaperView->setScaleMode(m_backgroundConfig.scaleMode);
                    m_jsonWallpaperView->setImage(&asset->surface);
                }
            }
            else if (m_jsonDriven && m_jsonWallpaperView)
            {
                m_jsonWallpaperView->setVisible(false);
                m_jsonWallpaperView->setImage(nullptr);
            }
            return;
        }

        m_backgroundConfig.mode = BackgroundMode::Gradient;
        if (const char *top = stringOrNull(backgroundValue->find("top")))
        {
            QW::Color c;
            if (parseHexColor(top, &c))
            {
                m_backgroundConfig.topColor = c;
                m_backgroundConfig.topOverride = true;
            }
        }

        if (const char *bottom = stringOrNull(backgroundValue->find("bottom")))
        {
            QW::Color c;
            if (parseHexColor(bottom, &c))
            {
                m_backgroundConfig.bottomColor = c;
                m_backgroundConfig.bottomOverride = true;
            }
        }

        if (m_jsonDriven && m_jsonWallpaperView)
        {
            m_jsonWallpaperView->setVisible(false);
            m_jsonWallpaperView->setImage(nullptr);
        }
    }

    void Desktop::applyThemeToDesktopColors(DesktopColors &colors) const
    {
        if (!m_themeOverrides.active)
            return;

        const auto &palette = m_themeOverrides.palette;

        if (palette.panel.set)
        {
            const QC::u8 topAlpha = colors.topBarBg.a;
            const QC::u8 sidebarAlpha = colors.sidebarBg.a;
            const QC::u8 taskbarAlpha = colors.taskbarBg.a;
            colors.topBarBg = palette.panel.value;
            colors.topBarBg.a = topAlpha;
            colors.sidebarBg = palette.panel.value.darker(0.05f);
            colors.sidebarBg.a = sidebarAlpha;
            colors.taskbarBg = palette.panel.value.darker(0.1f);
            colors.taskbarBg.a = taskbarAlpha;
            colors.windowBg = palette.panel.value.lighter(0.05f);
            colors.bgTop = palette.panel.value.lighter(0.08f);
            colors.bgBottom = palette.panel.value.darker(0.08f);
        }

        if (palette.panelBorder.set)
        {
            colors.topBarDivider = palette.panelBorder.value;
            colors.windowBorder = palette.panelBorder.value;
        }

        if (m_themeOverrides.effects.borderColor.set)
        {
            colors.topBarDivider = m_themeOverrides.effects.borderColor.value;
            colors.windowBorder = m_themeOverrides.effects.borderColor.value;
        }

        if (palette.text.set)
        {
            colors.topBarText = palette.text.value;
            colors.taskbarText = palette.text.value;
            colors.windowTitleText = palette.text.value;
        }

        if (palette.textSecondary.set)
        {
            colors.sidebarText = palette.textSecondary.value;
        }

        if (palette.accent.set)
        {
            colors.sidebarSelected = palette.accent.value;
            colors.windowBorder = palette.accent.value;
            QC::Color accentActive = palette.accent.value;
            accentActive.a = colors.taskbarActiveWindow.a;
            colors.taskbarActiveWindow = accentActive;
        }

        if (palette.accentLight.set)
        {
            QC::Color sidebarHover = palette.accentLight.value;
            sidebarHover.a = colors.sidebarHover.a;
            colors.sidebarHover = sidebarHover;

            QC::Color taskbarHover = palette.accentLight.value;
            taskbarHover.a = colors.taskbarHover.a;
            colors.taskbarHover = taskbarHover;
        }

        if (palette.accentDark.set)
        {
            colors.windowTitleBg = palette.accentDark.value;
        }

        if (m_themeOverrides.effects.shadow.color.set)
        {
            colors.windowShadow = m_themeOverrides.effects.shadow.color.value;
        }
        else if (m_themeOverrides.effects.shadow.blurSet && m_themeOverrides.effects.shadow.blurRadius == 0)
        {
            colors.windowShadow.a = 0;
        }

        if (m_themeOverrides.transparency.panelOpacitySet)
        {
            const QC::u8 alpha = m_themeOverrides.transparency.panelOpacity;
            colors.topBarBg.a = alpha;
            colors.sidebarBg.a = alpha;
            colors.taskbarBg.a = alpha;
        }

        if (m_themeOverrides.transparency.windowOpacitySet)
        {
            const QC::u8 alpha = m_themeOverrides.transparency.windowOpacity;
            colors.windowBg.a = alpha;
            colors.windowTitleBg.a = alpha;
        }
    }

    bool Desktop::tryInitializeFromCuiML()
    {
        const QC::u64 cuimlInitStartMs = QDrv::Timer::instance().milliseconds();
        resetThemeOverrides();
        resetBackgroundConfig();

        m_helpButton = nullptr;
        if (m_helpTitle)
        {
            operator delete[](m_helpTitle);
            m_helpTitle = nullptr;
        }
        if (m_helpSrcOrUrl)
        {
            operator delete[](m_helpSrcOrUrl);
            m_helpSrcOrUrl = nullptr;
        }
        if (m_helpInlineHtml)
        {
            operator delete[](m_helpInlineHtml);
            m_helpInlineHtml = nullptr;
        }

        // NOTE: Our FAT32 layer currently does not implement Long File Name (LFN) entries.
        // build.sh stores the desktop CUI-ML as an 8.3 name: /DESKTOP.CML

        const bool forceGolden = (QK::Boot::Config::GetActiveConfigTier() == QK::Boot::Config::ConfigTier::Golden);

        // Preferred trusted path (Option B): /system/ui/DESKTOP.CML (8.3 segments)
        const char *cuimlPathsProdFirst[] = {"/SYSTEM/UI/DESKTOP.CML", "/PROD/DESKTOP.CML", "/GOLDEN/DESKTOP.CML", "/desktop.cuiml", "/DESKTOP.CML"};
        const char *cuimlPathsGoldenOnly[] = {"/SYSTEM/UI/DESKTOP.CML", "/GOLDEN/DESKTOP.CML", "/desktop.cuiml", "/DESKTOP.CML"};

        const char **cuimlPaths = forceGolden ? cuimlPathsGoldenOnly : cuimlPathsProdFirst;
        const QC::usize cuimlPathCount = forceGolden ? (sizeof(cuimlPathsGoldenOnly) / sizeof(cuimlPathsGoldenOnly[0]))
                                                     : (sizeof(cuimlPathsProdFirst) / sizeof(cuimlPathsProdFirst[0]));

        const char *openedPath = nullptr;
        char openedPathStorage[192];
        QC::String::memset(openedPathStorage, 0, sizeof(openedPathStorage));
        QC::usize openedBytes = 0;
        QC::u64 openedIoMs = 0;
        char *cuimlText = nullptr;
        bool openedFromDatabase = false;
        bool openedFromRuntimeRows = false;

        auto skipWs = [](const char *p) -> const char *
        {
            while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                ++p;
            return p;
        };

        auto findStr = [](const char *haystack, const char *needle) -> const char *
        {
            if (!haystack || !needle || !*needle)
                return nullptr;
            for (const char *p = haystack; *p; ++p)
            {
                const char *a = p;
                const char *b = needle;
                while (*a && *b && *a == *b)
                {
                    ++a;
                    ++b;
                }
                if (*b == '\0')
                    return p;
            }
            return nullptr;
        };

        auto dupOwnedStringN = [](const char *src, QC::usize len) -> char *
        {
            if (!src)
                return nullptr;
            char *out = static_cast<char *>(operator new[](len + 1));
            for (QC::usize i = 0; i < len; ++i)
                out[i] = src[i];
            out[len] = '\0';
            return out;
        };

        auto dupOwnedString = [&](const char *src) -> char *
        {
            if (!src)
                return nullptr;
            return dupOwnedStringN(src, QC::String::strlen(src));
        };

        auto startsWithIgnoreCaseAscii = [](const char *s, const char *prefix) -> bool
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                char a = *s;
                char b = *prefix;
                if (!a)
                    return false;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        };

        auto streqIgnoreCaseAscii = [](const char *a, const char *b) -> bool
        {
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                char ca = *a;
                char cb = *b;
                if (ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca + 32);
                if (cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb + 32);
                if (ca != cb)
                    return false;
                ++a;
                ++b;
            }
            return *a == '\0' && *b == '\0';
        };

        auto parseAttrValue = [&](const char *tagRaw, const char *key, char *out, QC::usize outSize) -> bool
        {
            if (!tagRaw || !key || !out || outSize == 0)
                return false;
            out[0] = '\0';

            const char *p = tagRaw;
            const QC::usize keyLen = QC::String::strlen(key);

            while (*p)
            {
                if ((p[0] == ' ' || p[0] == '\t' || p[0] == '\n' || p[0] == '\r') && keyLen > 0)
                {
                    bool match = true;
                    for (QC::usize i = 0; i < keyLen; ++i)
                    {
                        if (p[1 + i] != key[i])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (!match)
                    {
                        ++p;
                        continue;
                    }

                    const char *k = p + 1 + keyLen;
                    k = skipWs(k);
                    if (*k != '=')
                    {
                        ++p;
                        continue;
                    }
                    ++k;
                    k = skipWs(k);

                    QC::usize i = 0;
                    if (*k == '"' || *k == '\'')
                    {
                        const char quote = *k;
                        ++k;
                        while (*k && *k != quote && i + 1 < outSize)
                            out[i++] = *k++;
                        out[i] = '\0';
                        return true;
                    }

                    // Unquoted attribute values: read until whitespace or tag end.
                    while (*k && *k != ' ' && *k != '\t' && *k != '\r' && *k != '\n' && *k != '>' && *k != '/' && i + 1 < outSize)
                        out[i++] = *k++;
                    out[i] = '\0';
                    return true;
                }
                ++p;
            }
            return false;
        };

        auto parseInt = [](const char *s, bool &ok) -> QC::i32
        {
            ok = false;
            if (!s || !*s)
                return 0;
            bool neg = false;
            QC::usize i = 0;
            if (s[0] == '-')
            {
                neg = true;
                i = 1;
            }
            if (!s[i])
                return 0;
            QC::i32 v = 0;
            for (; s[i]; ++i)
            {
                if (s[i] < '0' || s[i] > '9')
                    return 0;
                v = v * 10 + (s[i] - '0');
            }
            ok = true;
            return neg ? -v : v;
        };

        auto parseFontSizePx = [&](const char *fontSpec, QC::u32 &outPx) -> bool
        {
            outPx = 0;
            if (!fontSpec || !*fontSpec)
                return false;

            const char *lastDash = nullptr;
            for (const char *q = fontSpec; *q; ++q)
            {
                if (*q == '-')
                    lastDash = q;
            }
            if (!lastDash || !lastDash[1])
                return false;

            bool ok = false;
            const QC::i32 v = parseInt(lastDash + 1, ok);
            if (!ok || v <= 0)
                return false;
            outPx = static_cast<QC::u32>(v);
            return true;
        };

        auto textScaleFromFontPx = [&](QC::u32 px) -> float
        {
            if (px == 0)
                return 0.0f;
            // Theme/UI text scale is based on a 12px baseline.
            return static_cast<float>(px) / BASE_THEME_FONT_SIZE;
        };

        auto lineHeightForTextScale = [&](float scale) -> QC::u32
        {
            if (scale <= 0.0f)
                return 16;
            float clamped = scale;
            if (clamped < 0.5f)
                clamped = 0.5f;

            if (clamped > 4.0f)
                clamped = 4.0f;

            QC::i32 pixelHeight = static_cast<QC::i32>(BASE_THEME_FONT_SIZE * clamped + 0.5f);
            if (pixelHeight < 6)
                pixelHeight = 6;
            if (pixelHeight > 96)
                pixelHeight = 96;

            QG::FontManager &fm = QG::FontManager::instance();
            if (fm.hasDefaultFont())
            {
                QG::FontManager::Metrics m;
                if (fm.getMetrics(pixelHeight, &m) && m.lineAdvancePx > 0)
                {
                    return static_cast<QC::u32>(m.lineAdvancePx);
                }
            }

            // Fallback fixed bitmap font: 8px cell height times integer scale.
            QC::i32 rounded = static_cast<QC::i32>(clamped + 0.5f);
            if (rounded < 1)
                rounded = 1;
            return static_cast<QC::u32>(rounded * 8);
        };

        auto textPixelScaleForTextScale = [&](float scale) -> QC::u32
        {
            if (scale <= 0.0f)
                return 1;
            float clamped = scale;
            if (clamped < 0.5f)
                clamped = 0.5f;
            QC::i32 rounded = static_cast<QC::i32>(clamped + 0.5f);
            if (rounded < 1)
                rounded = 1;
            return static_cast<QC::u32>(rounded);
        };

        auto measureTextMono5x7Px = [&](const char *text, QC::u32 pixelScale) -> QC::Size
        {
            if (!text || !*text)
                return QC::Size(0, 0);

            QC::u32 maxLineChars = 0;
            QC::u32 lineChars = 0;
            QC::u32 lines = 1;
            for (const char *t = text; *t; ++t)
            {
                if (*t == '\n')
                {
                    if (lineChars > maxLineChars)
                        maxLineChars = lineChars;
                    lineChars = 0;
                    ++lines;
                }
                else
                {
                    ++lineChars;
                }
            }
            if (lineChars > maxLineChars)
                maxLineChars = lineChars;

            constexpr QC::u32 kGlyphW = 6;
            constexpr QC::u32 kGlyphH = 8;
            return QC::Size(maxLineChars * kGlyphW * pixelScale,
                            lines * kGlyphH * pixelScale);
        };

        auto measureTextForTextScale = [&](const char *text, float scale) -> QC::Size
        {
            float clamped = scale;
            if (clamped <= 0.0f)
                clamped = 1.0f;
            if (clamped < 0.5f)
                clamped = 0.5f;
            if (clamped > 4.0f)
                clamped = 4.0f;

            QG::FontManager &fm = QG::FontManager::instance();
            if (fm.hasDefaultFont())
            {
                QC::i32 pixelHeight = static_cast<QC::i32>(BASE_THEME_FONT_SIZE * clamped + 0.5f);
                if (pixelHeight < 6)
                    pixelHeight = 6;
                if (pixelHeight > 96)
                    pixelHeight = 96;

                QG::FontManager::Metrics m;
                if (fm.getMetrics(pixelHeight, &m) && m.fixedAdvancePx > 0 && m.lineAdvancePx > 0)
                {
                    if (!text)
                        return QC::Size(0, m.lineAdvancePx);

                    QC::u32 maxLineChars = 0;
                    QC::u32 lineChars = 0;
                    QC::u32 lines = 1;
                    for (const char *t = text; *t; ++t)
                    {
                        if (*t == '\n')
                        {
                            if (lineChars > maxLineChars)
                                maxLineChars = lineChars;
                            lineChars = 0;
                            ++lines;
                        }
                        else
                        {
                            ++lineChars;
                        }
                    }
                    if (lineChars > maxLineChars)
                        maxLineChars = lineChars;

                    return QC::Size(static_cast<QC::i32>(maxLineChars) * m.fixedAdvancePx,
                                    static_cast<QC::i32>(lines) * m.lineAdvancePx);
                }
            }

            const QC::u32 pixelScale = textPixelScaleForTextScale(clamped);
            return measureTextMono5x7Px(text, pixelScale);
        };

        auto evalDim = [&](const char *expr, QC::i32 base, const char *edgeKeyword) -> QC::i32
        {
            if (!expr || !*expr)
                return 0;

            if (edgeKeyword && startsWithIgnoreCaseAscii(expr, edgeKeyword))
            {
                const char *p = expr + QC::String::strlen(edgeKeyword);
                if (*p == '-')
                    ++p;
                bool ok = false;
                const QC::i32 n = parseInt(p, ok);
                if (ok)
                    return base - n;
                return base;
            }

            const char *pct = findStr(expr, "%");
            if (pct)
            {
                QC::u32 percent = 0;
                const char *p = expr;
                while (*p >= '0' && *p <= '9')
                {
                    percent = percent * 10u + static_cast<QC::u32>(*p - '0');
                    ++p;
                }
                QC::i32 v = static_cast<QC::i32>((static_cast<QC::i64>(base) * percent) / 100);

                const char *after = pct + 1;
                after = skipWs(after);
                if (*after == '-')
                {
                    ++after;
                    after = skipWs(after);
                    bool ok = false;
                    const QC::i32 n = parseInt(after, ok);
                    if (ok)
                        v -= n;
                }
                else if (*after == '+')
                {
                    ++after;
                    after = skipWs(after);
                    bool ok = false;
                    const QC::i32 n = parseInt(after, ok);
                    if (ok)
                        v += n;
                }

                if (v < 0)
                    v = 0;
                return v;
            }

            bool ok = false;
            const QC::i32 v = parseInt(expr, ok);
            return ok ? v : 0;
        };

        auto guessTextHeightPx = [](const char *text) -> QC::u32
        {
            if (!text)
                return 16;
            QC::u32 lines = 1;
            for (const char *p = text; *p; ++p)
            {
                if (*p == '\n')
                    ++lines;
            }
            const QC::u32 lineH = 16;
            return lines * lineH;
        };

        struct CuiMLStyleProps
        {
            bool hasTextColor = false;
            QC::Color textColor{};

            bool hasBackground = false;
            QC::Color background{};

            bool hasBorderColor = false;
            QC::Color borderColor{};

            bool hasBorderWidth = false;
            QC::u32 borderWidth = 1;

            bool hasFont = false;
            char font[128]{};

            // Optional split font fields.
            // When both are set, the effective font spec becomes: <fontFamily>-<fontSizePx>
            // Example: font-family: OpenSans-regular; font-size: 14; -> OpenSans-regular-14
            bool hasFontFamily = false;
            char fontFamily[96]{};

            bool hasFontSize = false;
            QC::u32 fontSizePx = 0;

            bool hasPadding = false;
            QC::u32 padL = 0, padT = 0, padR = 0, padB = 0;

            bool hasTextAlign = false;
            QW::Controls::TextAlign textAlign = QW::Controls::TextAlign::Left;

            bool hasEnabled = false;
            bool enabled = true;

            bool hasVisible = false;
            bool visible = true;

            bool hasOpacity = false;
            QC::u8 opacity = 255;

            bool hasRole = false;
            char role[32]{};
        };

        enum class CuiMLSelectorType : QC::u8
        {
            Element,
            Class,
            Id,
        };

        struct CuiMLStyleRule
        {
            CuiMLSelectorType type = CuiMLSelectorType::Element;
            char key[64]{};
            CuiMLStyleProps props;
        };

        auto trimInPlace = [&](char *s)
        {
            if (!s)
                return;
            QC::usize len = QC::String::strlen(s);
            while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
                s[--len] = '\0';
            QC::usize start = 0;
            while (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')
                ++start;
            if (start > 0)
            {
                QC::usize i = 0;
                for (; s[start + i]; ++i)
                    s[i] = s[start + i];
                s[i] = '\0';
            }
        };

        auto stripCdataInPlace = [&](char *s)
        {
            if (!s)
                return;

            trimInPlace(s);
            if (!startsWithIgnoreCaseAscii(s, "<![CDATA["))
                return;

            char *start = s + 9; // <![CDATA[
            const char *end = findStr(start, "]]>");
            const QC::usize len = end ? static_cast<QC::usize>(end - start) : QC::String::strlen(start);

            for (QC::usize i = 0; i < len; ++i)
                s[i] = start[i];
            s[len] = '\0';
            trimInPlace(s);
        };

        auto parseBool = [&](const char *v, bool &out) -> bool
        {
            if (!v || !*v)
                return false;
            if (streqIgnoreCaseAscii(v, "true") || streqIgnoreCaseAscii(v, "1") || streqIgnoreCaseAscii(v, "yes") || streqIgnoreCaseAscii(v, "on"))
            {
                out = true;
                return true;
            }
            if (streqIgnoreCaseAscii(v, "false") || streqIgnoreCaseAscii(v, "0") || streqIgnoreCaseAscii(v, "no") || streqIgnoreCaseAscii(v, "off"))
            {
                out = false;
                return true;
            }
            return false;
        };

        auto parseOpacity = [&](const char *v, QC::u8 &outA) -> bool
        {
            if (!v || !*v)
                return false;

            const char *pct = findStr(v, "%");
            if (pct)
            {
                char buf[32];
                QC::String::memset(buf, 0, sizeof(buf));
                QC::usize n = static_cast<QC::usize>(pct - v);
                if (n >= sizeof(buf))
                    n = sizeof(buf) - 1;
                for (QC::usize i = 0; i < n; ++i)
                    buf[i] = v[i];
                buf[n] = '\0';
                trimInPlace(buf);
                bool ok = false;
                const QC::i32 p = parseInt(buf, ok);
                if (!ok)
                    return false;
                QC::i32 clamped = p;
                if (clamped < 0)
                    clamped = 0;
                if (clamped > 100)
                    clamped = 100;
                outA = static_cast<QC::u8>((clamped * 255) / 100);
                return true;
            }

            const char *dot = findStr(v, ".");
            if (dot)
            {
                QC::i32 whole = 0;
                bool okWhole = false;
                {
                    char buf[16];
                    QC::String::memset(buf, 0, sizeof(buf));
                    QC::usize n = static_cast<QC::usize>(dot - v);
                    if (n >= sizeof(buf))
                        n = sizeof(buf) - 1;
                    for (QC::usize i = 0; i < n; ++i)
                        buf[i] = v[i];
                    buf[n] = '\0';
                    trimInPlace(buf);
                    whole = parseInt(buf, okWhole);
                }
                if (!okWhole)
                    return false;

                const char *fracStart = dot + 1;
                QC::i32 frac = 0;
                QC::i32 denom = 1;
                for (int i = 0; fracStart[i] && i < 6; ++i)
                {
                    if (fracStart[i] < '0' || fracStart[i] > '9')
                        break;
                    frac = frac * 10 + (fracStart[i] - '0');
                    denom *= 10;
                }

                float f = static_cast<float>(whole) + (denom > 1 ? static_cast<float>(frac) / static_cast<float>(denom) : 0.0f);
                if (f < 0.0f)
                    f = 0.0f;
                if (f > 1.0f)
                    f = 1.0f;
                outA = static_cast<QC::u8>(f * 255.0f + 0.5f);
                return true;
            }

            bool ok = false;
            const QC::i32 n = parseInt(v, ok);
            if (!ok)
                return false;
            QC::i32 clamped = n;
            if (clamped < 0)
                clamped = 0;
            if (clamped > 255)
                clamped = 255;
            outA = static_cast<QC::u8>(clamped);
            return true;
        };

        auto mergeProps = [&](CuiMLStyleProps &dst, const CuiMLStyleProps &src)
        {
            if (src.hasTextColor)
                dst.hasTextColor = true, dst.textColor = src.textColor;
            if (src.hasBackground)
                dst.hasBackground = true, dst.background = src.background;
            if (src.hasBorderColor)
                dst.hasBorderColor = true, dst.borderColor = src.borderColor;
            if (src.hasBorderWidth)
                dst.hasBorderWidth = true, dst.borderWidth = src.borderWidth;
            if (src.hasFont)
            {
                dst.hasFont = true;
                QC::String::strncpy(dst.font, src.font, sizeof(dst.font) - 1);
                dst.font[sizeof(dst.font) - 1] = '\0';
            }
            if (src.hasFontFamily)
            {
                dst.hasFontFamily = true;
                QC::String::strncpy(dst.fontFamily, src.fontFamily, sizeof(dst.fontFamily) - 1);
                dst.fontFamily[sizeof(dst.fontFamily) - 1] = '\0';
            }
            if (src.hasFontSize)
            {
                dst.hasFontSize = true;
                dst.fontSizePx = src.fontSizePx;
            }
            if (src.hasPadding)
                dst.hasPadding = true, dst.padL = src.padL, dst.padT = src.padT, dst.padR = src.padR, dst.padB = src.padB;
            if (src.hasTextAlign)
                dst.hasTextAlign = true, dst.textAlign = src.textAlign;
            if (src.hasEnabled)
                dst.hasEnabled = true, dst.enabled = src.enabled;
            if (src.hasVisible)
                dst.hasVisible = true, dst.visible = src.visible;
            if (src.hasOpacity)
                dst.hasOpacity = true, dst.opacity = src.opacity;
            if (src.hasRole)
            {
                dst.hasRole = true;
                QC::String::strncpy(dst.role, src.role, sizeof(dst.role) - 1);
                dst.role[sizeof(dst.role) - 1] = '\0';
            }
        };

        auto stripTrailingDashIntInPlace = [&](char *s)
        {
            if (!s || !*s)
                return;

            const char *lastDash = nullptr;
            for (const char *q = s; *q; ++q)
            {
                if (*q == '-')
                    lastDash = q;
            }
            if (!lastDash || !lastDash[1])
                return;

            bool ok = false;
            (void)parseInt(lastDash + 1, ok);
            if (!ok)
                return;

            const QC::usize keep = static_cast<QC::usize>(lastDash - s);
            s[keep] = '\0';
            trimInPlace(s);
        };

        auto buildFontSpecFromFamilySize = [&](const char *familyBase, QC::u32 sizePx, char *out, QC::usize outCap) -> bool
        {
            if (!out || outCap == 0)
                return false;
            out[0] = '\0';
            if (!familyBase || !*familyBase || sizePx == 0)
                return false;

            char base[96];
            QC::String::memset(base, 0, sizeof(base));
            QC::String::strncpy(base, familyBase, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';
            trimInPlace(base);
            stripTrailingDashIntInPlace(base);
            if (!base[0])
                return false;

            char sz[16];
            QC::String::memset(sz, 0, sizeof(sz));
            {
                // u32 to decimal string
                char tmp[16];
                QC::String::memset(tmp, 0, sizeof(tmp));
                QC::usize tn = 0;
                QC::u32 v = sizePx;
                while (v > 0 && tn + 1 < sizeof(tmp))
                {
                    tmp[tn++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
                if (tn == 0)
                    return false;
                for (QC::usize i = 0; i < tn && i + 1 < sizeof(sz); ++i)
                    sz[i] = tmp[tn - 1 - i];
                sz[tn] = '\0';
            }

            const QC::usize baseLen = QC::String::strlen(base);
            const QC::usize szLen = QC::String::strlen(sz);
            if (baseLen + 1 + szLen + 1 > outCap)
                return false;

            QC::String::memcpy(out, base, baseLen);
            out[baseLen] = '-';
            QC::String::memcpy(out + baseLen + 1, sz, szLen);
            out[baseLen + 1 + szLen] = '\0';
            return true;
        };

        auto applyOpacityToColor = [&](QC::Color &c, QC::u8 opacity)
        {
            const QC::u32 a = static_cast<QC::u32>(c.a);
            c.a = static_cast<QC::u8>((a * opacity) / 255u);
        };

        auto applyOpacityToProps = [&](CuiMLStyleProps &p)
        {
            if (!p.hasOpacity)
                return;
            if (p.hasTextColor)
                applyOpacityToColor(p.textColor, p.opacity);
            if (p.hasBackground)
                applyOpacityToColor(p.background, p.opacity);
            if (p.hasBorderColor)
                applyOpacityToColor(p.borderColor, p.opacity);
        };

        QC::Vector<CuiMLStyleRule> styleRules;

        auto parseCuimlssText = [&](const char *cssText)
        {
            if (!cssText)
                return;

            auto isNameChar = [&](char c) -> bool
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            };

            auto skipCssWsComments = [&](const char *s) -> const char *
            {
                while (s && *s)
                {
                    if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
                    {
                        ++s;
                        continue;
                    }
                    if (s[0] == '/' && s[1] == '*')
                    {
                        const char *end = findStr(s + 2, "*/");
                        s = end ? (end + 2) : (s + QC::String::strlen(s));
                        continue;
                    }
                    if (s[0] == '/' && s[1] == '/')
                    {
                        while (*s && *s != '\n')
                            ++s;
                        continue;
                    }
                    break;
                }
                return s;
            };

            const char *s = cssText;
            while (s && *s)
            {
                s = skipCssWsComments(s);
                if (!s || !*s)
                    break;

                CuiMLStyleRule rule{};

                if (*s == '#')
                    rule.type = CuiMLSelectorType::Id, ++s;
                else if (*s == '.')
                    rule.type = CuiMLSelectorType::Class, ++s;
                else
                    rule.type = CuiMLSelectorType::Element;

                QC::usize k = 0;
                while (*s && isNameChar(*s) && k + 1 < sizeof(rule.key))
                    rule.key[k++] = *s++;
                rule.key[k] = '\0';

                s = skipCssWsComments(s);
                if (!rule.key[0] || !s || *s != '{')
                {
                    while (*s && *s != '{' && *s != '\n')
                        ++s;
                    if (*s == '{')
                        ++s;
                    continue;
                }
                ++s; // '{'

                while (s && *s)
                {
                    s = skipCssWsComments(s);
                    if (!s || !*s)
                        break;
                    if (*s == '}')
                    {
                        ++s;
                        break;
                    }

                    char prop[64];
                    QC::String::memset(prop, 0, sizeof(prop));
                    QC::usize pn = 0;
                    while (*s && isNameChar(*s) && pn + 1 < sizeof(prop))
                        prop[pn++] = *s++;
                    prop[pn] = '\0';
                    trimInPlace(prop);

                    s = skipCssWsComments(s);
                    if (!prop[0] || !s || *s != ':')
                    {
                        while (*s && *s != ';' && *s != '}')
                            ++s;
                        if (*s == ';')
                            ++s;
                        continue;
                    }
                    ++s; // ':'

                    char val[256];
                    QC::String::memset(val, 0, sizeof(val));
                    QC::usize vn = 0;
                    while (*s && *s != ';' && *s != '}' && vn + 1 < sizeof(val))
                        val[vn++] = *s++;
                    val[vn] = '\0';
                    trimInPlace(val);

                    if (streqIgnoreCaseAscii(prop, "color"))
                    {
                        QC::Color c;
                        if (parseColorString(val, c))
                            rule.props.hasTextColor = true, rule.props.textColor = c;
                    }
                    else if (streqIgnoreCaseAscii(prop, "background") || streqIgnoreCaseAscii(prop, "background-color"))
                    {
                        QC::Color c;
                        if (parseColorString(val, c))
                            rule.props.hasBackground = true, rule.props.background = c;
                    }
                    else if (streqIgnoreCaseAscii(prop, "border") || streqIgnoreCaseAscii(prop, "border-color"))
                    {
                        QC::Color c;
                        if (parseColorString(val, c))
                            rule.props.hasBorderColor = true, rule.props.borderColor = c;
                    }
                    else if (streqIgnoreCaseAscii(prop, "border-width"))
                    {
                        bool ok = false;
                        const QC::i32 n = parseInt(val, ok);
                        if (ok && n > 0)
                            rule.props.hasBorderWidth = true, rule.props.borderWidth = static_cast<QC::u32>(n);
                    }
                    else if (streqIgnoreCaseAscii(prop, "font"))
                    {
                        rule.props.hasFont = true;
                        QC::String::strncpy(rule.props.font, val, sizeof(rule.props.font) - 1);
                        rule.props.font[sizeof(rule.props.font) - 1] = '\0';

                        // Also populate split fields so other rules can override size/family.
                        rule.props.hasFontFamily = true;
                        QC::String::strncpy(rule.props.fontFamily, val, sizeof(rule.props.fontFamily) - 1);
                        rule.props.fontFamily[sizeof(rule.props.fontFamily) - 1] = '\0';
                        trimInPlace(rule.props.fontFamily);
                        stripTrailingDashIntInPlace(rule.props.fontFamily);

                        QC::u32 px = 0;
                        if (parseFontSizePx(val, px) && px > 0)
                        {
                            rule.props.hasFontSize = true;
                            rule.props.fontSizePx = px;
                        }
                    }
                    else if (streqIgnoreCaseAscii(prop, "font-family"))
                    {
                        rule.props.hasFontFamily = true;
                        QC::String::strncpy(rule.props.fontFamily, val, sizeof(rule.props.fontFamily) - 1);
                        rule.props.fontFamily[sizeof(rule.props.fontFamily) - 1] = '\0';
                        trimInPlace(rule.props.fontFamily);
                        rule.props.hasFont = false;
                        rule.props.font[0] = '\0';
                    }
                    else if (streqIgnoreCaseAscii(prop, "font-size"))
                    {
                        bool ok = false;
                        const QC::i32 n = parseInt(val, ok);
                        if (ok && n > 0)
                        {
                            rule.props.hasFontSize = true;
                            rule.props.fontSizePx = static_cast<QC::u32>(n);
                            rule.props.hasFont = false;
                            rule.props.font[0] = '\0';
                        }
                    }
                    else if (streqIgnoreCaseAscii(prop, "padding"))
                    {
                        bool ok = false;
                        const QC::i32 n = parseInt(val, ok);
                        if (ok && n >= 0)
                        {
                            rule.props.hasPadding = true;
                            const QC::u32 u = static_cast<QC::u32>(n);
                            rule.props.padL = u;
                            rule.props.padT = u;
                            rule.props.padR = u;
                            rule.props.padB = u;
                        }
                    }
                    else if (streqIgnoreCaseAscii(prop, "padding-left") || streqIgnoreCaseAscii(prop, "padding-top") ||
                             streqIgnoreCaseAscii(prop, "padding-right") || streqIgnoreCaseAscii(prop, "padding-bottom"))
                    {
                        bool ok = false;
                        const QC::i32 n = parseInt(val, ok);
                        if (ok && n >= 0)
                        {
                            if (!rule.props.hasPadding)
                                rule.props.hasPadding = true;
                            const QC::u32 u = static_cast<QC::u32>(n);
                            if (streqIgnoreCaseAscii(prop, "padding-left"))
                                rule.props.padL = u;
                            else if (streqIgnoreCaseAscii(prop, "padding-top"))
                                rule.props.padT = u;
                            else if (streqIgnoreCaseAscii(prop, "padding-right"))
                                rule.props.padR = u;
                            else
                                rule.props.padB = u;
                        }
                    }
                    else if (streqIgnoreCaseAscii(prop, "text-align"))
                    {
                        if (streqIgnoreCaseAscii(val, "left"))
                            rule.props.hasTextAlign = true, rule.props.textAlign = QW::Controls::TextAlign::Left;
                        else if (streqIgnoreCaseAscii(val, "center"))
                            rule.props.hasTextAlign = true, rule.props.textAlign = QW::Controls::TextAlign::Center;
                        else if (streqIgnoreCaseAscii(val, "right"))
                            rule.props.hasTextAlign = true, rule.props.textAlign = QW::Controls::TextAlign::Right;
                    }
                    else if (streqIgnoreCaseAscii(prop, "enabled"))
                    {
                        bool b = true;
                        if (parseBool(val, b))
                            rule.props.hasEnabled = true, rule.props.enabled = b;
                    }
                    else if (streqIgnoreCaseAscii(prop, "visibility"))
                    {
                        if (streqIgnoreCaseAscii(val, "hidden"))
                            rule.props.hasVisible = true, rule.props.visible = false;
                        else if (streqIgnoreCaseAscii(val, "visible"))
                            rule.props.hasVisible = true, rule.props.visible = true;
                    }
                    else if (streqIgnoreCaseAscii(prop, "opacity"))
                    {
                        QC::u8 a = 255;
                        if (parseOpacity(val, a))
                            rule.props.hasOpacity = true, rule.props.opacity = a;
                    }
                    else if (streqIgnoreCaseAscii(prop, "role"))
                    {
                        rule.props.hasRole = true;
                        QC::String::strncpy(rule.props.role, val, sizeof(rule.props.role) - 1);
                        rule.props.role[sizeof(rule.props.role) - 1] = '\0';
                        trimInPlace(rule.props.role);
                    }

                    if (*s == ';')
                        ++s;
                }

                applyOpacityToProps(rule.props);
                styleRules.push_back(rule);
            }
        };

        auto readFileToOwnedCStringVfs = [&](const char *path) -> char *
        {
            if (!path || !*path)
                return nullptr;
            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
            {
                QC_LOG_WARN(LOG_MODULE, "CUIMLSS: failed to open '%s'\n", path);
                return nullptr;
            }
            QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 128)
            {
                QFS::VFS::instance().close(file);
                QC_LOG_WARN(LOG_MODULE, "CUIMLSS: invalid size for '%s' (%llu)\n", path, static_cast<unsigned long long>(size64));
                return nullptr;
            }
            QC::usize size = static_cast<QC::usize>(size64);
            char *text = static_cast<char *>(operator new[](size + 1));
            QC::isize readCount = file->read(text, size);
            QFS::VFS::instance().close(file);
            if (readCount <= 0)
            {
                operator delete[](text);
                QC_LOG_WARN(LOG_MODULE, "CUIMLSS: read failed for '%s'\n", path);
                return nullptr;
            }
            if (static_cast<QC::usize>(readCount) < size)
                size = static_cast<QC::usize>(readCount);
            text[size] = '\0';
            return text;
        };

        auto loadCuimlssFromPath = [&](const char *path)
        {
            if (!path || !*path)
                return;
            if (startsWithIgnoreCaseAscii(path, "http://") || startsWithIgnoreCaseAscii(path, "https://"))
                return;

            char *css = readFileToOwnedCStringVfs(path);
            if (!css)
                return;
            parseCuimlssText(css);
            operator delete[](css);
        };

        auto scanStyleImports = [&](const char *srcText)
        {
            const char *sp = srcText;
            while (sp && *sp)
            {
                if (*sp != '<')
                {
                    ++sp;
                    continue;
                }
                if (startsWithIgnoreCaseAscii(sp, "<!--"))
                {
                    const char *end = findStr(sp, "-->");
                    if (!end)
                        break;
                    sp = end + 3;
                    continue;
                }

                const char *gt = findStr(sp, ">");
                if (!gt)
                    break;

                const QC::usize len = static_cast<QC::usize>(gt - sp + 1);
                if (len > 1023)
                {
                    sp = gt + 1;
                    continue;
                }

                char tag[1024];
                QC::String::memset(tag, 0, sizeof(tag));
                for (QC::usize i = 0; i < len; ++i)
                    tag[i] = sp[i];
                tag[len] = '\0';

                bool isClose = false;
                const char *ns = sp + 1;
                ns = skipWs(ns);
                if (*ns == '/')
                    isClose = true, ++ns, ns = skipWs(ns);

                const char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '>' && *ne != '/')
                    ++ne;

                char name[64];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize nlen = static_cast<QC::usize>(ne - ns);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                for (QC::usize i = 0; i < nlen; ++i)
                    name[i] = ns[i];
                name[nlen] = '\0';

                if (!isClose && name[0])
                {
                    if (startsWithIgnoreCaseAscii(name, "ImportStyle"))
                    {
                        char src[256];
                        QC::String::memset(src, 0, sizeof(src));
                        (void)parseAttrValue(tag, "src", src, sizeof(src));
                        if (src[0])
                            loadCuimlssFromPath(src);
                    }
                    else if (startsWithIgnoreCaseAscii(name, "link"))
                    {
                        char rel[64];
                        char type[64];
                        char href[256];
                        QC::String::memset(rel, 0, sizeof(rel));
                        QC::String::memset(type, 0, sizeof(type));
                        QC::String::memset(href, 0, sizeof(href));
                        (void)parseAttrValue(tag, "rel", rel, sizeof(rel));
                        (void)parseAttrValue(tag, "type", type, sizeof(type));
                        (void)parseAttrValue(tag, "href", href, sizeof(href));
                        if (rel[0] && type[0] && href[0] && streqIgnoreCaseAscii(rel, "stylesheet") && streqIgnoreCaseAscii(type, "text/cuimlss"))
                            loadCuimlssFromPath(href);
                    }
                }

                sp = gt + 1;
            }
        };

        if (ensureCmmsDatabaseReady())
        {
            const QC::u64 sourceLoadStartMs = QDrv::Timer::instance().milliseconds();
            const char *documentIdsProdFirst[] = {CMMS_DESKTOP_LAYOUT_PRODUCTION, CMMS_DESKTOP_LAYOUT_GOLDEN};
            const char *documentIdsGoldenOnly[] = {CMMS_DESKTOP_LAYOUT_GOLDEN};

            const char **documentIds = forceGolden ? documentIdsGoldenOnly : documentIdsProdFirst;
            const QC::usize documentIdCount = forceGolden ? (sizeof(documentIdsGoldenOnly) / sizeof(documentIdsGoldenOnly[0]))
                                                          : (sizeof(documentIdsProdFirst) / sizeof(documentIdsProdFirst[0]));

            for (QC::usize i = 0; i < documentIdCount; ++i)
            {
                const QCQL::Cell keyCell = makeTextCell(documentIds[i]);
                QCQL::Row row{};
                const QCQL::Status rowSt = QCQL::Engine::instance().selectRowByPrimaryKeyByName(
                    m_cmmsDatabase,
                    CMMS_DESKTOP_CUIML_TABLE,
                    keyCell.bytes,
                    row);
                if (rowSt == QCQL::Status::Success && !row.tombstone && row.cells.size() >= 3 && row.cells[2].type == QCQL::ColumnType::Text)
                {
                    QC::u32 chunkCount = 0;
                    if (parseUnsignedTextCell(row.cells[2], chunkCount) && chunkCount > 0)
                    {
                        QC::Vector<QC::u8> payloadBytes;
                        if (loadChunkedDocumentPayload(m_cmmsDatabase, CMMS_DESKTOP_CUIML_CHUNK_TABLE, documentIds[i], chunkCount, payloadBytes))
                        {
                            const QC::usize size = payloadBytes.size();
                            if (size > 0 && size <= 1024 * 1024)
                            {
                                char *text = static_cast<char *>(operator new[](size + 1));
                                for (QC::usize n = 0; n < size; ++n)
                                    text[n] = static_cast<char>(payloadBytes[n]);
                                text[size] = '\0';

                                if (!copyCellText(row.cells[1], openedPathStorage, sizeof(openedPathStorage)) || !openedPathStorage[0])
                                {
                                    QC::String::strncpy(openedPathStorage, "CMMS DB", sizeof(openedPathStorage) - 1);
                                    openedPathStorage[sizeof(openedPathStorage) - 1] = '\0';
                                }

                                openedPath = openedPathStorage;
                                openedBytes = size;
                                openedIoMs = 0;
                                cuimlText = text;
                                openedFromDatabase = true;
                                break;
                            }
                        }
                    }
                }

                DesktopDocumentImportResult runtimeImport{};
                DesktopDocumentExportResult runtimeExport{};
                if (DesktopDocumentIO::importCmmsRuntime(m_cmmsDatabase, documentIds[i], runtimeImport) &&
                    runtimeImport.loaded &&
                    DesktopDocumentIO::exportCuimlText(runtimeImport.document, runtimeExport) &&
                    runtimeExport.generated &&
                    !runtimeExport.text.empty())
                {
                    QC::usize size = runtimeExport.text.size();
                    if (size > 0 && runtimeExport.text[size - 1] == '\0')
                        --size;
                    if (size > 0)
                    {
                        char *text = static_cast<char *>(operator new[](size + 1));
                        for (QC::usize n = 0; n < size; ++n)
                            text[n] = runtimeExport.text[n];
                        text[size] = '\0';

                        if (runtimeImport.sourcePath[0])
                        {
                            QC::String::strncpy(openedPathStorage, runtimeImport.sourcePath, sizeof(openedPathStorage) - 1);
                            openedPathStorage[sizeof(openedPathStorage) - 1] = '\0';
                        }
                        else
                        {
                            QC::String::strncpy(openedPathStorage, "CMMS runtime rows", sizeof(openedPathStorage) - 1);
                            openedPathStorage[sizeof(openedPathStorage) - 1] = '\0';
                        }

                        openedPath = openedPathStorage;
                        openedBytes = size;
                        openedIoMs = 0;
                        cuimlText = text;
                        openedFromDatabase = true;
                        openedFromRuntimeRows = true;
                        break;
                    }
                }
            }

            QC_LOG_INFO(LOG_MODULE, "CUI-ML stage source_load=%llums source=%s\n",
                        static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - sourceLoadStartMs),
                        openedFromRuntimeRows ? "runtime_rows" : (openedFromDatabase ? "cmms_db" : "vfs"));
        }

        for (QC::usize i = 0; !openedPath && i < cuimlPathCount; ++i)
        {
            const char *path = cuimlPaths[i];
            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                continue;

            const QC::u64 tIo0 = QDrv::Timer::instance().milliseconds();

            QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 1024)
            {
                QFS::VFS::instance().close(file);
                continue;
            }

            QC::usize size = static_cast<QC::usize>(size64);
            char *text = static_cast<char *>(operator new[](size + 1));
            QC::isize readCount = file->read(text, size);
            QFS::VFS::instance().close(file);

            const QC::u64 tIo1 = QDrv::Timer::instance().milliseconds();

            if (readCount <= 0)
            {
                operator delete[](text);
                continue;
            }

            if (static_cast<QC::usize>(readCount) < size)
                size = static_cast<QC::usize>(readCount);
            text[size] = '\0';

            openedPath = path;
            openedBytes = size;
            openedIoMs = tIo1 - tIo0;
            cuimlText = text;
            break;
        }

        if (!openedPath || !cuimlText)
            return false;

        if (!openedFromDatabase && !openedFromRuntimeRows)
        {
            QC_LOG_INFO(LOG_MODULE, "CUI-ML stage source_load=%llums source=%s\n",
                        static_cast<unsigned long long>(openedIoMs),
                        "vfs");
        }

        if (openedFromRuntimeRows)
        {
            QC_LOG_INFO(LOG_MODULE, "Loading desktop definition from CMMS runtime rows (%s, generated CUI-ML bytes=%u io=%llums)\n",
                        openedPath,
                        static_cast<unsigned>(openedBytes),
                        static_cast<unsigned long long>(openedIoMs));
        }
        else if (openedFromDatabase)
        {
            QC_LOG_INFO(LOG_MODULE, "Loading desktop definition from CMMS DB (%s, CUI-ML bytes=%u io=%llums)\n",
                        openedPath,
                        static_cast<unsigned>(openedBytes),
                        static_cast<unsigned long long>(openedIoMs));
        }
        else
        {
            QC_LOG_INFO(LOG_MODULE, "Loading desktop definition from %s (CUI-ML bytes=%u io=%llums)\n",
                        openedPath,
                        static_cast<unsigned>(openedBytes),
                        static_cast<unsigned long long>(openedIoMs));
        }

        if (!m_desktopWindow || !m_desktopWindow->root())
        {
            operator delete[](cuimlText);
            return false;
        }

        bool isHtmlCuiml = false;
        bool inBody = true;
        {
            const char *s = skipWs(cuimlText);
            if (startsWithIgnoreCaseAscii(s, "<html"))
            {
                const char *gt = findStr(s, ">");
                if (gt)
                {
                    const QC::usize len = static_cast<QC::usize>(gt - s + 1);
                    if (len <= 1023)
                    {
                        char tag[1024];
                        QC::String::memset(tag, 0, sizeof(tag));
                        for (QC::usize i = 0; i < len; ++i)
                            tag[i] = s[i];
                        tag[len] = '\0';

                        char lang[64];
                        QC::String::memset(lang, 0, sizeof(lang));
                        (void)parseAttrValue(tag, "lang", lang, sizeof(lang));
                        if (lang[0] && streqIgnoreCaseAscii(lang, "cuiml"))
                        {
                            isHtmlCuiml = true;
                            inBody = false;
                        }
                    }
                }
            }
        }

        const QC::u64 styleScanStartMs = QDrv::Timer::instance().milliseconds();
        scanStyleImports(cuimlText);
        QC_LOG_INFO(LOG_MODULE, "CUIMLSS: parsed %u style rules\n", static_cast<unsigned>(styleRules.size()));
        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage style_scan=%llums\n",
                static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - styleScanStartMs));

        // ----------------------------
        // Optional imports: icon aliases, roles, components
        // ----------------------------
        struct CuiMLIconAlias
        {
            char id[64]{};
            char path[192]{};
        };

        struct CuiMLRoleDef
        {
            char id[64]{};
            bool hasBackground = false;
            QC::Color background{};
            bool hasColor = false;
            QC::Color color{};
            bool hasBorder = false;
            QC::Color border{};
        };

        struct CuiMLComponentTemplate
        {
            char name[64]{};
            char base[32]{};
            char templateTagRaw[1024]{};
        };

        QC::Vector<CuiMLIconAlias> iconAliases;
        QC::Vector<CuiMLRoleDef> roleDefs;
        QC::Vector<CuiMLComponentTemplate> componentTemplates;

        auto addIconAlias = [&](const char *id, const char *path)
        {
            if (!id || !*id || !path || !*path)
                return;

            // Update existing entry if present.
            for (QC::usize i = 0; i < iconAliases.size(); ++i)
            {
                if (streqIgnoreCaseAscii(iconAliases[i].id, id))
                {
                    QC::String::strncpy(iconAliases[i].path, path, sizeof(iconAliases[i].path) - 1);
                    iconAliases[i].path[sizeof(iconAliases[i].path) - 1] = '\0';
                    trimInPlace(iconAliases[i].path);
                    return;
                }
            }

            CuiMLIconAlias a;
            QC::String::strncpy(a.id, id, sizeof(a.id) - 1);
            a.id[sizeof(a.id) - 1] = '\0';
            trimInPlace(a.id);
            QC::String::strncpy(a.path, path, sizeof(a.path) - 1);
            a.path[sizeof(a.path) - 1] = '\0';
            trimInPlace(a.path);
            if (a.id[0] && a.path[0])
                iconAliases.push_back(a);
        };

        auto lookupIconAliasPath = [&](const char *token) -> const char *
        {
            if (!token || !*token)
                return nullptr;

            char t[64];
            QC::String::memset(t, 0, sizeof(t));
            QC::String::strncpy(t, token, sizeof(t) - 1);
            t[sizeof(t) - 1] = '\0';
            trimInPlace(t);
            if (!t[0])
                return nullptr;

            for (QC::usize i = 0; i < iconAliases.size(); ++i)
            {
                if (streqIgnoreCaseAscii(iconAliases[i].id, t))
                    return iconAliases[i].path;
            }

            return nullptr;
        };

        auto scanIconAliasesFromText = [&](const char *srcText)
        {
            const char *sp = srcText;
            while (sp && *sp)
            {
                if (*sp != '<')
                {
                    ++sp;
                    continue;
                }

                if (startsWithIgnoreCaseAscii(sp, "<!--"))
                {
                    const char *end = findStr(sp, "-->");
                    if (!end)
                        break;
                    sp = end + 3;
                    continue;
                }

                const char *gt = findStr(sp, ">");
                if (!gt)
                    break;

                const QC::usize len = static_cast<QC::usize>(gt - sp + 1);
                if (len > 1023)
                {
                    sp = gt + 1;
                    continue;
                }

                char tag[1024];
                QC::String::memset(tag, 0, sizeof(tag));
                for (QC::usize i = 0; i < len; ++i)
                    tag[i] = sp[i];
                tag[len] = '\0';

                bool isClose = false;
                const char *ns = sp + 1;
                ns = skipWs(ns);
                if (*ns == '/')
                    isClose = true, ++ns, ns = skipWs(ns);

                const char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '>' && *ne != '/')
                    ++ne;

                char name[64];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize nlen = static_cast<QC::usize>(ne - ns);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                for (QC::usize i = 0; i < nlen; ++i)
                    name[i] = ns[i];
                name[nlen] = '\0';

                if (!isClose && name[0] && streqIgnoreCaseAscii(name, "Icon"))
                {
                    char id[96];
                    char path[256];
                    QC::String::memset(id, 0, sizeof(id));
                    QC::String::memset(path, 0, sizeof(path));
                    (void)parseAttrValue(tag, "id", id, sizeof(id));
                    (void)parseAttrValue(tag, "path", path, sizeof(path));
                    if (id[0] && path[0])
                        addIconAlias(id, path);
                }

                sp = gt + 1;
            }
        };

        auto addOrUpdateRoleDef = [&](const char *id, const char *background, const char *color, const char *border)
        {
            if (!id || !*id)
                return;

            CuiMLRoleDef def;
            QC::String::strncpy(def.id, id, sizeof(def.id) - 1);
            def.id[sizeof(def.id) - 1] = '\0';
            trimInPlace(def.id);
            if (!def.id[0])
                return;

            if (background && *background)
            {
                QC::Color c;
                if (parseColorString(background, c))
                {
                    def.hasBackground = true;
                    def.background = c;
                }
            }

            if (color && *color)
            {
                QC::Color c;
                if (parseColorString(color, c))
                {
                    def.hasColor = true;
                    def.color = c;
                }
            }

            if (border && *border)
            {
                QC::Color c;
                if (parseColorString(border, c))
                {
                    def.hasBorder = true;
                    def.border = c;
                }
            }

            for (QC::usize i = 0; i < roleDefs.size(); ++i)
            {
                if (streqIgnoreCaseAscii(roleDefs[i].id, def.id))
                {
                    roleDefs[i] = def;
                    return;
                }
            }

            roleDefs.push_back(def);
        };

        auto scanRoleDefsFromText = [&](const char *srcText)
        {
            const char *sp = srcText;
            while (sp && *sp)
            {
                if (*sp != '<')
                {
                    ++sp;
                    continue;
                }

                if (startsWithIgnoreCaseAscii(sp, "<!--"))
                {
                    const char *end = findStr(sp, "-->");
                    if (!end)
                        break;
                    sp = end + 3;
                    continue;
                }

                const char *gt = findStr(sp, ">");
                if (!gt)
                    break;

                const QC::usize len = static_cast<QC::usize>(gt - sp + 1);
                if (len > 1023)
                {
                    sp = gt + 1;
                    continue;
                }

                char tag[1024];
                QC::String::memset(tag, 0, sizeof(tag));
                for (QC::usize i = 0; i < len; ++i)
                    tag[i] = sp[i];
                tag[len] = '\0';

                bool isClose = false;
                const char *ns = sp + 1;
                ns = skipWs(ns);
                if (*ns == '/')
                    isClose = true, ++ns, ns = skipWs(ns);

                const char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '>' && *ne != '/')
                    ++ne;

                char name[64];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize nlen = static_cast<QC::usize>(ne - ns);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                for (QC::usize i = 0; i < nlen; ++i)
                    name[i] = ns[i];
                name[nlen] = '\0';

                if (!isClose && name[0] && streqIgnoreCaseAscii(name, "Role"))
                {
                    char id[96];
                    char bg[128];
                    char fg[128];
                    char border[128];
                    QC::String::memset(id, 0, sizeof(id));
                    QC::String::memset(bg, 0, sizeof(bg));
                    QC::String::memset(fg, 0, sizeof(fg));
                    QC::String::memset(border, 0, sizeof(border));
                    (void)parseAttrValue(tag, "id", id, sizeof(id));
                    (void)parseAttrValue(tag, "background", bg, sizeof(bg));
                    (void)parseAttrValue(tag, "color", fg, sizeof(fg));
                    (void)parseAttrValue(tag, "border", border, sizeof(border));
                    addOrUpdateRoleDef(id, bg, fg, border);
                }

                sp = gt + 1;
            }
        };

        auto findStrIgnoreCaseAsciiLocal = [&](const char *hay, const char *needle) -> const char *
        {
            if (!hay || !needle || !*needle)
                return hay;

            const QC::usize nlen = QC::String::strlen(needle);
            for (const char *p = hay; *p; ++p)
            {
                QC::usize i = 0;
                for (; i < nlen; ++i)
                {
                    char a = p[i];
                    char b = needle[i];
                    if (!a)
                        break;
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    if (a != b)
                        break;
                }
                if (i == nlen)
                    return p;
            }
            return nullptr;
        };

        auto addOrUpdateComponentTemplate = [&](const char *name, const char *base, const char *templateTagRaw)
        {
            if (!name || !*name || !base || !*base || !templateTagRaw || !*templateTagRaw)
                return;

            CuiMLComponentTemplate c;
            QC::String::strncpy(c.name, name, sizeof(c.name) - 1);
            c.name[sizeof(c.name) - 1] = '\0';
            trimInPlace(c.name);

            QC::String::strncpy(c.base, base, sizeof(c.base) - 1);
            c.base[sizeof(c.base) - 1] = '\0';
            trimInPlace(c.base);

            QC::String::strncpy(c.templateTagRaw, templateTagRaw, sizeof(c.templateTagRaw) - 1);
            c.templateTagRaw[sizeof(c.templateTagRaw) - 1] = '\0';
            trimInPlace(c.templateTagRaw);

            if (!c.name[0] || !c.base[0] || !c.templateTagRaw[0])
                return;

            for (QC::usize i = 0; i < componentTemplates.size(); ++i)
            {
                if (streqIgnoreCaseAscii(componentTemplates[i].name, c.name))
                {
                    componentTemplates[i] = c;
                    return;
                }
            }

            componentTemplates.push_back(c);
        };

        auto scanComponentsFromText = [&](const char *srcText)
        {
            const char *sp = srcText;
            while (sp && *sp)
            {
                if (*sp != '<')
                {
                    ++sp;
                    continue;
                }

                if (startsWithIgnoreCaseAscii(sp, "<!--"))
                {
                    const char *end = findStr(sp, "-->");
                    if (!end)
                        break;
                    sp = end + 3;
                    continue;
                }

                const char *gt = findStr(sp, ">");
                if (!gt)
                    break;

                const QC::usize len = static_cast<QC::usize>(gt - sp + 1);
                if (len > 1023)
                {
                    sp = gt + 1;
                    continue;
                }

                char tag[1024];
                QC::String::memset(tag, 0, sizeof(tag));
                for (QC::usize i = 0; i < len; ++i)
                    tag[i] = sp[i];
                tag[len] = '\0';

                bool isClose = false;
                const char *ns = sp + 1;
                ns = skipWs(ns);
                if (*ns == '/')
                    isClose = true, ++ns, ns = skipWs(ns);

                const char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '>' && *ne != '/')
                    ++ne;

                char name[64];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize nlen = static_cast<QC::usize>(ne - ns);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                for (QC::usize i = 0; i < nlen; ++i)
                    name[i] = ns[i];
                name[nlen] = '\0';

                if (isClose || !name[0] || !streqIgnoreCaseAscii(name, "Component"))
                {
                    sp = gt + 1;
                    continue;
                }

                char compName[96];
                QC::String::memset(compName, 0, sizeof(compName));
                (void)parseAttrValue(tag, "name", compName, sizeof(compName));
                trimInPlace(compName);
                if (!compName[0])
                {
                    sp = gt + 1;
                    continue;
                }

                const char *blockStart = gt + 1;
                const char *closeStart = findStrIgnoreCaseAsciiLocal(blockStart, "</Component");
                if (!closeStart)
                {
                    sp = gt + 1;
                    continue;
                }

                const char *cp = blockStart;
                while (cp < closeStart)
                {
                    if (*cp != '<')
                    {
                        ++cp;
                        continue;
                    }

                    if (startsWithIgnoreCaseAscii(cp, "<!--"))
                    {
                        const char *end = findStr(cp, "-->");
                        if (!end || end >= closeStart)
                            break;
                        cp = end + 3;
                        continue;
                    }

                    const char *childGt = findStr(cp, ">");
                    if (!childGt || childGt > closeStart)
                        break;

                    const QC::usize childLen = static_cast<QC::usize>(childGt - cp + 1);
                    if (childLen > 1023)
                        break;

                    char childTag[1024];
                    QC::String::memset(childTag, 0, sizeof(childTag));
                    for (QC::usize i = 0; i < childLen; ++i)
                        childTag[i] = cp[i];
                    childTag[childLen] = '\0';

                    const char *cns = cp + 1;
                    cns = skipWs(cns);
                    if (*cns == '/')
                    {
                        cp = childGt + 1;
                        continue;
                    }

                    const char *cne = cns;
                    while (*cne && *cne != ' ' && *cne != '\t' && *cne != '\r' && *cne != '\n' && *cne != '>' && *cne != '/')
                        ++cne;

                    char base[32];
                    QC::String::memset(base, 0, sizeof(base));
                    QC::usize blen = static_cast<QC::usize>(cne - cns);
                    if (blen >= sizeof(base))
                        blen = sizeof(base) - 1;
                    for (QC::usize i = 0; i < blen; ++i)
                        base[i] = cns[i];
                    base[blen] = '\0';
                    trimInPlace(base);

                    if (base[0])
                        addOrUpdateComponentTemplate(compName, base, childTag);

                    break;
                }

                sp = closeStart;
            }
        };

        auto applyRoleDefsToThemeOverrides = [&]()
        {
            if (roleDefs.size() == 0)
                return;

            for (QC::usize i = 0; i < roleDefs.size(); ++i)
            {
                const CuiMLRoleDef &r = roleDefs[i];
                QW::ButtonRole role = QW::ButtonRole::Default;
                if (!QW::buttonRoleFromString(r.id, &role))
                    continue;

                auto &dst = m_themeOverrides.button[static_cast<QC::u32>(role)];

                if (r.hasBackground)
                {
                    if (!dst.fillNormal.set)
                    {
                        dst.fillNormal.set = true;
                        dst.fillNormal.value = r.background;
                    }
                    if (!dst.fillHover.set)
                    {
                        dst.fillHover.set = true;
                        dst.fillHover.value = r.background.lighter(0.08f);
                    }
                    if (!dst.fillPressed.set)
                    {
                        dst.fillPressed.set = true;
                        dst.fillPressed.value = r.background.darker(0.25f);
                    }
                    if (!dst.border.set)
                    {
                        dst.border.set = true;
                        dst.border.value = r.hasBorder ? r.border : r.background.darker(0.3f);
                    }
                }

                if (r.hasColor)
                {
                    if (!dst.text.set)
                    {
                        dst.text.set = true;
                        dst.text.value = r.color;
                    }
                }

                m_themeOverrides.active = true;
            }
        };

        auto scanCuimlImportsForIcons = [&](const char *srcText)
        {
            const char *sp = srcText;
            while (sp && *sp)
            {
                if (*sp != '<')
                {
                    ++sp;
                    continue;
                }

                if (startsWithIgnoreCaseAscii(sp, "<!--"))
                {
                    const char *end = findStr(sp, "-->");
                    if (!end)
                        break;
                    sp = end + 3;
                    continue;
                }

                const char *gt = findStr(sp, ">");
                if (!gt)
                    break;

                const QC::usize len = static_cast<QC::usize>(gt - sp + 1);
                if (len > 1023)
                {
                    sp = gt + 1;
                    continue;
                }

                char tag[1024];
                QC::String::memset(tag, 0, sizeof(tag));
                for (QC::usize i = 0; i < len; ++i)
                    tag[i] = sp[i];
                tag[len] = '\0';

                bool isClose = false;
                const char *ns = sp + 1;
                ns = skipWs(ns);
                if (*ns == '/')
                    isClose = true, ++ns, ns = skipWs(ns);

                const char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '>' && *ne != '/')
                    ++ne;

                char name[64];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize nlen = static_cast<QC::usize>(ne - ns);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                for (QC::usize i = 0; i < nlen; ++i)
                    name[i] = ns[i];
                name[nlen] = '\0';

                if (!isClose && name[0] && streqIgnoreCaseAscii(name, "Import"))
                {
                    char src[256];
                    QC::String::memset(src, 0, sizeof(src));
                    (void)parseAttrValue(tag, "src", src, sizeof(src));
                    trimInPlace(src);
                    if (src[0] && !startsWithIgnoreCaseAscii(src, "http://") && !startsWithIgnoreCaseAscii(src, "https://"))
                    {
                        char *importText = readFileToOwnedCStringVfs(src);
                        if (importText)
                        {
                            scanIconAliasesFromText(importText);
                            scanRoleDefsFromText(importText);
                            scanComponentsFromText(importText);
                            operator delete[](importText);
                        }
                    }
                }

                sp = gt + 1;
            }
        };

        // Build optional maps from the main CUIML and any imported .cui files.
        const QC::u64 importScanStartMs = QDrv::Timer::instance().milliseconds();
        scanCuimlImportsForIcons(cuimlText);
        scanIconAliasesFromText(cuimlText);
        scanRoleDefsFromText(cuimlText);
        scanComponentsFromText(cuimlText);
        applyRoleDefsToThemeOverrides();
        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage import_scan=%llums\n",
                static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - importScanStartMs));

        auto lookupComponentTemplate = [&](const char *token) -> const CuiMLComponentTemplate *
        {
            if (!token || !*token)
                return nullptr;
            for (QC::usize i = 0; i < componentTemplates.size(); ++i)
            {
                if (streqIgnoreCaseAscii(componentTemplates[i].name, token))
                    return &componentTemplates[i];
            }
            return nullptr;
        };

        // Derive a global theme font family/size based on CUIMLSS defaults.
        const QC::u64 themeDeriveStartMs = QDrv::Timer::instance().milliseconds();
        // CUIMLSS uses a simple token like: <family>-<style>-<sizePx>, e.g. OpenSans-regular-14.
        // The family is taken from the first token before '-' ("system" -> built-in).
        bool haveDerivedThemeFont = false;
        char derivedThemeFontFamily[48];
        QC::u32 derivedThemeFontPx = 0;
        QC::String::memset(derivedThemeFontFamily, 0, sizeof(derivedThemeFontFamily));
        {
            auto tryParseFontFamilyFromSpec = [&](const char *fontSpec, char *outFamily, QC::usize outCap) -> bool
            {
                if (!fontSpec || !*fontSpec || !outFamily || outCap == 0)
                    return false;
                outFamily[0] = '\0';

                // Keep the full base (e.g. roboto-regular) by stripping only the trailing -NN size.
                QC::String::strncpy(outFamily, fontSpec, outCap - 1);
                outFamily[outCap - 1] = '\0';
                trimInPlace(outFamily);
                stripTrailingDashIntInPlace(outFamily);

                // Normalize any system-* spec to just "system".
                if (startsWithIgnoreCaseAscii(outFamily, "system") && (outFamily[6] == '\0' || outFamily[6] == '-'))
                {
                    QC::String::strncpy(outFamily, "system", outCap - 1);
                    outFamily[outCap - 1] = '\0';
                }

                return outFamily[0] != '\0';
            };

            const char *pickedFontSpec = nullptr;
            char pickedFontSpecBuf[128];
            QC::String::memset(pickedFontSpecBuf, 0, sizeof(pickedFontSpecBuf));

            auto tryPickSpecFromProps = [&](const CuiMLStyleProps &p) -> const char *
            {
                if (p.hasFontFamily && p.hasFontSize)
                {
                    if (buildFontSpecFromFamilySize(p.fontFamily, p.fontSizePx, pickedFontSpecBuf, sizeof(pickedFontSpecBuf)))
                        return pickedFontSpecBuf;
                }
                if (p.hasFont && p.font[0])
                    return p.font;
                return nullptr;
            };

            for (QC::usize i = 0; i < styleRules.size(); ++i)
            {
                const CuiMLStyleRule &r = styleRules[i];
                if (r.type == CuiMLSelectorType::Element && streqIgnoreCaseAscii(r.key, "Label"))
                {
                    const char *spec = tryPickSpecFromProps(r.props);
                    if (spec)
                    {
                        pickedFontSpec = spec;
                        break;
                    }
                }
            }
            if (!pickedFontSpec)
            {
                for (QC::usize i = 0; i < styleRules.size(); ++i)
                {
                    const CuiMLStyleRule &r = styleRules[i];
                    const char *spec = tryPickSpecFromProps(r.props);
                    if (spec)
                    {
                        pickedFontSpec = spec;
                        break;
                    }
                }
            }

            if (pickedFontSpec)
            {
                char familyTok[48];
                QC::String::memset(familyTok, 0, sizeof(familyTok));
                if (tryParseFontFamilyFromSpec(pickedFontSpec, familyTok, sizeof(familyTok)))
                {
                    // Stash derived font to apply after seasonal/theme overrides.
                    QC::String::strncpy(derivedThemeFontFamily, familyTok, sizeof(derivedThemeFontFamily) - 1);
                    derivedThemeFontFamily[sizeof(derivedThemeFontFamily) - 1] = '\0';
                    haveDerivedThemeFont = true;

                    QC::u32 px = 0;
                    if (parseFontSizePx(pickedFontSpec, px))
                    {
                        derivedThemeFontPx = px;
                    }
                }
            }
            else
            {
                QC_LOG_WARN(LOG_MODULE, "CUI-ML theme font: no CUIMLSS font spec found; using System\n");
            }
        }

        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage theme_derive=%llums\n",
                static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - themeDeriveStartMs));

        // Preload the derived theme font (if any) before creating controls.
        // This lets auto-sizing use real font metrics instead of the bitmap fallback.
        const QC::u64 themePreloadStartMs = QDrv::Timer::instance().milliseconds();
        if (haveDerivedThemeFont)
        {
            if (!streqIgnoreCaseAscii(derivedThemeFontFamily, "system") && !streqIgnoreCaseAscii(derivedThemeFontFamily, "System"))
            {
                QC::Vector<QC::u8> bytes;
                if (tryLoadFontFamilyFromVfs(derivedThemeFontFamily, bytes))
                {
                    if (QG::FontManager::instance().setDefaultFontFromBytes(bytes))
                    {
                        m_lastAppliedFontFamilySet = true;
                        QC::String::strncpy(m_lastAppliedFontFamily, derivedThemeFontFamily, sizeof(m_lastAppliedFontFamily) - 1);
                        m_lastAppliedFontFamily[sizeof(m_lastAppliedFontFamily) - 1] = '\0';
                    }
                    else
                    {
                        QG::FontManager::instance().clearDefaultFont();
                        m_lastAppliedFontFamilySet = false;
                        m_lastAppliedFontFamily[0] = '\0';
                    }
                }
                else
                {
                    QG::FontManager::instance().clearDefaultFont();
                    m_lastAppliedFontFamilySet = false;
                    m_lastAppliedFontFamily[0] = '\0';
                }
            }
            else
            {
                QG::FontManager::instance().clearDefaultFont();
                m_lastAppliedFontFamilySet = true;
                QC::String::strncpy(m_lastAppliedFontFamily, "System", sizeof(m_lastAppliedFontFamily) - 1);
                m_lastAppliedFontFamily[sizeof(m_lastAppliedFontFamily) - 1] = '\0';
            }
        }
        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage theme_preload=%llums\n",
                    static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - themePreloadStartMs));

        auto classListContains = [&](const char *classList, const char *key) -> bool
        {
            if (!classList || !*classList || !key || !*key)
                return false;
            const QC::usize keyLen = QC::String::strlen(key);
            const char *s = classList;
            while (s && *s)
            {
                s = skipWs(s);
                if (!s || !*s)
                    break;
                const char *start = s;
                while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
                    ++s;
                const QC::usize len = static_cast<QC::usize>(s - start);
                if (len == keyLen && QC::String::memcmp(start, key, keyLen) == 0)
                    return true;
            }
            return false;
        };

        auto computeStyleProps = [&](const char *elementName, const char *id, const char *classList) -> CuiMLStyleProps
        {
            CuiMLStyleProps out{};

            // Deterministic application order (not full CSS): Element -> Class -> Id
            for (QC::usize i = 0; i < styleRules.size(); ++i)
            {
                const CuiMLStyleRule &r = styleRules[i];
                if (r.type == CuiMLSelectorType::Element && streqIgnoreCaseAscii(r.key, elementName))
                    mergeProps(out, r.props);
            }

            for (QC::usize i = 0; i < styleRules.size(); ++i)
            {
                const CuiMLStyleRule &r = styleRules[i];
                if (r.type == CuiMLSelectorType::Class && classListContains(classList, r.key))
                    mergeProps(out, r.props);
            }

            if (id && *id)
            {
                for (QC::usize i = 0; i < styleRules.size(); ++i)
                {
                    const CuiMLStyleRule &r = styleRules[i];
                    if (r.type == CuiMLSelectorType::Id && QC::String::strcmp(r.key, id) == 0)
                        mergeProps(out, r.props);
                }
            }

            // If family+size are present, synthesize a full spec for downstream code paths.
            if (out.hasFontFamily && out.hasFontSize)
            {
                char spec[128];
                QC::String::memset(spec, 0, sizeof(spec));
                if (buildFontSpecFromFamilySize(out.fontFamily, out.fontSizePx, spec, sizeof(spec)))
                {
                    out.hasFont = true;
                    QC::String::strncpy(out.font, spec, sizeof(out.font) - 1);
                    out.font[sizeof(out.font) - 1] = '\0';
                }
            }

            return out;
        };

        // Create wallpaper view behind all controls (populated by <Background .../> if present).
        {
            QW::Rect bounds = {0, 0, m_screenWidth, m_screenHeight};
            auto *wall = new QW::Controls::ImageView(m_desktopWindow, bounds);
            wall->setScaleMode(QG::ImageScaleMode::Stretch);
            wall->setImage(nullptr);
            wall->setVisible(false);
            wall->setId(hashControlId("wallpaper"));

            m_jsonWallpaperView = wall;
            m_jsonControls.push_back(wall);
            m_jsonRootControls.push_back(wall);
            m_desktopWindow->root()->addChild(wall);
        }

        m_jsonDriven = true;

        struct Frame
        {
            QW::Controls::Panel *panel = nullptr;
            QC::i32 w = 0;
            QC::i32 h = 0;
        };

        QC::Vector<Frame> stack;
        {
            Frame f;
            f.panel = m_desktopWindow->root();
            f.w = static_cast<QC::i32>(m_screenWidth);
            f.h = static_cast<QC::i32>(m_screenHeight);
            stack.push_back(f);
        }

        const QC::u64 controlBuildStartMs = QDrv::Timer::instance().milliseconds();
        const char *p = cuimlText;
        while (*p)
        {
            if (*p != '<')
            {
                ++p;
                continue;
            }

            if (startsWithIgnoreCaseAscii(p, "<!--"))
            {
                const char *end = findStr(p, "-->");
                if (!end)
                    break;
                p = end + 3;
                continue;
            }

            const char *gt = findStr(p, ">");
            if (!gt)
                break;

            const QC::usize tagLen = static_cast<QC::usize>(gt - p + 1);
            if (tagLen > 1023)
            {
                p = gt + 1;
                continue;
            }

            char tagRaw[1024];
            QC::String::memset(tagRaw, 0, sizeof(tagRaw));
            for (QC::usize i = 0; i < tagLen; ++i)
                tagRaw[i] = p[i];
            tagRaw[tagLen] = '\0';

            bool isClose = false;
            bool selfClose = false;

            const char *nameStart = p + 1;
            nameStart = skipWs(nameStart);
            if (*nameStart == '/')
            {
                isClose = true;
                ++nameStart;
                nameStart = skipWs(nameStart);
            }

            const char *nameEnd = nameStart;
            while (*nameEnd && *nameEnd != ' ' && *nameEnd != '\t' && *nameEnd != '\r' && *nameEnd != '\n' && *nameEnd != '>' && *nameEnd != '/')
                ++nameEnd;

            char name[64];
            QC::String::memset(name, 0, sizeof(name));
            QC::usize nlen = static_cast<QC::usize>(nameEnd - nameStart);
            if (nlen >= sizeof(name))
                nlen = sizeof(name) - 1;
            for (QC::usize i = 0; i < nlen; ++i)
                name[i] = nameStart[i];
            name[nlen] = '\0';

            const CuiMLComponentTemplate *componentTemplate = lookupComponentTemplate(name);
            const char *componentTemplateTagRaw = componentTemplate ? componentTemplate->templateTagRaw : nullptr;
            char effectiveName[64];
            QC::String::memset(effectiveName, 0, sizeof(effectiveName));
            QC::String::strncpy(effectiveName, componentTemplate ? componentTemplate->base : name, sizeof(effectiveName) - 1);
            effectiveName[sizeof(effectiveName) - 1] = '\0';
            trimInPlace(effectiveName);

            const char *scan = gt;
            while (scan > p && (*scan == '>' || *scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n'))
                --scan;
            if (*scan == '/')
                selfClose = true;

            p = gt + 1;

            if (name[0] == '\0')
                continue;

            if (isClose)
            {
                if (isHtmlCuiml && startsWithIgnoreCaseAscii(name, "body"))
                {
                    // End-of-body terminates CUIML parsing for html-wrapped files.
                    break;
                }
                if (startsWithIgnoreCaseAscii(effectiveName, "Panel"))
                {
                    if (stack.size() > 1)
                        stack.pop_back();
                }
                continue;
            }

            if (isHtmlCuiml)
            {
                if (startsWithIgnoreCaseAscii(name, "html") || startsWithIgnoreCaseAscii(name, "head"))
                    continue;
                if (startsWithIgnoreCaseAscii(name, "body"))
                {
                    inBody = true;
                    continue;
                }
                if (!inBody)
                    continue;
            }

            if (startsWithIgnoreCaseAscii(name, "help-window") || startsWithIgnoreCaseAscii(name, "HelpWindow") || startsWithIgnoreCaseAscii(name, "helpwindow"))
            {
                // Desktop boot path: capture help spec for a '?' button (do not auto-open, and do not parse nested HTML).
                char title[128];
                char src[256];
                char href[256];
                QC::String::memset(title, 0, sizeof(title));
                QC::String::memset(src, 0, sizeof(src));
                QC::String::memset(href, 0, sizeof(href));
                (void)parseAttrValue(tagRaw, "title", title, sizeof(title));
                (void)parseAttrValue(tagRaw, "src", src, sizeof(src));
                (void)parseAttrValue(tagRaw, "href", href, sizeof(href));

                if (!m_helpTitle && title[0])
                    m_helpTitle = dupOwnedString(title);

                if (!m_helpSrcOrUrl)
                {
                    if (src[0])
                        m_helpSrcOrUrl = dupOwnedString(src);
                    else if (href[0])
                        m_helpSrcOrUrl = dupOwnedString(href);
                }

                if (!selfClose)
                {
                    const char *close1 = "</HelpWindow>";
                    const char *close2 = "</help-window>";
                    const char *close3 = "</helpwindow>";
                    const char *end1 = findStr(p, close1);
                    const char *end2 = findStr(p, close2);
                    const char *end3 = findStr(p, close3);

                    const char *end = end1;
                    const char *chosenClose = close1;
                    if (!end || (end2 && end2 < end))
                        end = end2, chosenClose = close2;
                    if (!end || (end3 && end3 < end))
                        end = end3, chosenClose = close3;

                    if (end)
                    {
                        if (!m_helpInlineHtml && end > p)
                        {
                            m_helpInlineHtml = dupOwnedStringN(p, static_cast<QC::usize>(end - p));
                            stripCdataInPlace(m_helpInlineHtml);
                        }
                        p = end + QC::String::strlen(chosenClose);
                    }
                }
                continue;
            }

            if (startsWithIgnoreCaseAscii(name, "Theme"))
            {
                auto setOverride = [&](const char *key, ColorOverride &dst)
                {
                    char buf[128];
                    QC::String::memset(buf, 0, sizeof(buf));
                    if (!parseAttrValue(tagRaw, key, buf, sizeof(buf)))
                        return;
                    QC::Color parsed;
                    if (parseColorString(buf, parsed))
                    {
                        dst.set = true;
                        dst.value = parsed;
                        m_themeOverrides.active = true;
                    }
                };

                setOverride("accent", m_themeOverrides.palette.accent);
                setOverride("accentLight", m_themeOverrides.palette.accentLight);
                setOverride("accentDark", m_themeOverrides.palette.accentDark);
                setOverride("panel", m_themeOverrides.palette.panel);
                setOverride("panelBorder", m_themeOverrides.palette.panelBorder);
                setOverride("text", m_themeOverrides.palette.text);
                setOverride("textSecondary", m_themeOverrides.palette.textSecondary);
                continue;
            }

            if (startsWithIgnoreCaseAscii(name, "Background"))
            {
                char type[64];
                char pathBuf[256];
                char mode[64];
                QC::String::memset(type, 0, sizeof(type));
                QC::String::memset(pathBuf, 0, sizeof(pathBuf));
                QC::String::memset(mode, 0, sizeof(mode));
                (void)parseAttrValue(tagRaw, "type", type, sizeof(type));
                (void)parseAttrValue(tagRaw, "path", pathBuf, sizeof(pathBuf));
                (void)parseAttrValue(tagRaw, "mode", mode, sizeof(mode));

                if (type[0] && startsWithIgnoreCaseAscii(type, "image") && pathBuf[0] && m_jsonWallpaperView)
                {
                    if (ImageAsset *asset = loadImageAsset(pathBuf))
                    {
                        QG::ImageScaleMode scale = QG::ImageScaleMode::Stretch;
                        if (mode[0])
                        {
                            if (streqIgnoreCaseAscii(mode, "fit"))
                                scale = QG::ImageScaleMode::Fit;
                            else if (streqIgnoreCaseAscii(mode, "center"))
                                scale = QG::ImageScaleMode::Center;
                            else if (streqIgnoreCaseAscii(mode, "tile"))
                                scale = QG::ImageScaleMode::Tile;
                            else if (streqIgnoreCaseAscii(mode, "fill"))
                                scale = QG::ImageScaleMode::Fill;
                            else if (streqIgnoreCaseAscii(mode, "original"))
                                scale = QG::ImageScaleMode::Original;
                            else
                                scale = QG::ImageScaleMode::Stretch;
                        }

                        m_jsonWallpaperView->setScaleMode(scale);
                        m_jsonWallpaperView->setImage(&asset->surface);
                        m_jsonWallpaperView->setVisible(true);
                    }
                }
                continue;
            }

            // Ignore structural tags.
            if (startsWithIgnoreCaseAscii(name, "cui-ml") || startsWithIgnoreCaseAscii(name, "Desktop") || startsWithIgnoreCaseAscii(name, "Layout"))
            {
                continue;
            }

            if (stack.size() == 0)
                continue;
            Frame &parent = stack.back();
            QW::Controls::Panel *parentPanel = parent.panel;
            const QC::i32 parentW = parent.w;
            const QC::i32 parentH = parent.h;

            char xBuf[64];
            char yBuf[64];
            char wBuf[64];
            char hBuf[64];
            QC::String::memset(xBuf, 0, sizeof(xBuf));
            QC::String::memset(yBuf, 0, sizeof(yBuf));
            QC::String::memset(wBuf, 0, sizeof(wBuf));
            QC::String::memset(hBuf, 0, sizeof(hBuf));

            (void)parseAttrValue(tagRaw, "x", xBuf, sizeof(xBuf));
            (void)parseAttrValue(tagRaw, "y", yBuf, sizeof(yBuf));
            (void)parseAttrValue(tagRaw, "width", wBuf, sizeof(wBuf));
            (void)parseAttrValue(tagRaw, "height", hBuf, sizeof(hBuf));

            const QC::i32 x = evalDim(xBuf, parentW, "right");
            const QC::i32 y = evalDim(yBuf, parentH, "bottom");

            QC::i32 w = 0;
            QC::i32 h = 0;
            if (wBuf[0] != '\0')
                w = evalDim(wBuf, parentW, nullptr);
            if (hBuf[0] != '\0')
                h = evalDim(hBuf, parentH, nullptr);

            char idAttr[128];
            char actionAttr[64];
            char classAttr[256];
            QC::String::memset(idAttr, 0, sizeof(idAttr));
            QC::String::memset(actionAttr, 0, sizeof(actionAttr));
            QC::String::memset(classAttr, 0, sizeof(classAttr));
            (void)parseAttrValue(tagRaw, "id", idAttr, sizeof(idAttr));
            (void)parseAttrValue(tagRaw, "action", actionAttr, sizeof(actionAttr));
            (void)parseAttrValue(tagRaw, "class", classAttr, sizeof(classAttr));

            const CuiMLStyleProps styleProps = computeStyleProps(effectiveName[0] ? effectiveName : name, idAttr, classAttr);

            auto parseAttrMerged = [&](const char *key, char *out, QC::usize cap)
            {
                if (!out || cap == 0)
                    return;
                out[0] = '\0';
                (void)parseAttrValue(tagRaw, key, out, cap);
                if (out[0] == '\0' && componentTemplateTagRaw)
                    (void)parseAttrValue(componentTemplateTagRaw, key, out, cap);
            };

            QW::Controls::IControl *created = nullptr;

            if (startsWithIgnoreCaseAscii(effectiveName, "Panel"))
            {
                if (w <= 0)
                    w = parentW;
                if (h <= 0)
                    h = parentH;

                auto *panel = new QW::Controls::Panel(m_desktopWindow, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                panel->setPadding(0);
                panel->setBorderStyle(QW::Controls::BorderStyle::None);
                panel->setFrameVisible(false);

                if (styleProps.hasPadding)
                    panel->setPadding(styleProps.padL, styleProps.padT, styleProps.padR, styleProps.padB);
                if (styleProps.hasBackground)
                    panel->setBackgroundColor(styleProps.background);
                if (styleProps.hasBorderWidth)
                {
                    panel->setFrameVisible(true);
                    panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                    panel->setBorderWidth(styleProps.borderWidth);
                }
                if (styleProps.hasBorderColor)
                {
                    panel->setFrameVisible(true);
                    panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                    panel->setBorderColor(styleProps.borderColor);
                    if (styleProps.hasBorderWidth)
                        panel->setBorderWidth(styleProps.borderWidth);
                }
                if (styleProps.hasEnabled)
                    panel->setEnabled(styleProps.enabled);
                if (styleProps.hasVisible)
                    panel->setVisible(styleProps.visible);

                char bg[128];
                QC::String::memset(bg, 0, sizeof(bg));
                parseAttrMerged("background", bg, sizeof(bg));
                if (bg[0] != '\0')
                {
                    QC::Color c;
                    if (parseColorString(bg, c))
                        panel->setBackgroundColor(c);
                }

                char border[128];
                QC::String::memset(border, 0, sizeof(border));
                parseAttrMerged("border", border, sizeof(border));
                if (border[0] == '\0')
                    parseAttrMerged("borderTop", border, sizeof(border));
                if (border[0] == '\0')
                    parseAttrMerged("borderBottom", border, sizeof(border));
                if (border[0] == '\0')
                    parseAttrMerged("borderLeft", border, sizeof(border));
                if (border[0] == '\0')
                    parseAttrMerged("borderRight", border, sizeof(border));
                if (border[0] != '\0')
                {
                    QC::Color c;
                    if (parseColorString(border, c))
                    {
                        panel->setFrameVisible(true);
                        panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        panel->setBorderColor(c);
                        panel->setBorderWidth(styleProps.hasBorderWidth ? styleProps.borderWidth : 1);
                    }
                }

                created = panel;
            }
            else if (startsWithIgnoreCaseAscii(effectiveName, "Image") || startsWithIgnoreCaseAscii(effectiveName, "ImageView"))
            {
                char pathAttr[256];
                char mode[64];
                QC::String::memset(pathAttr, 0, sizeof(pathAttr));
                QC::String::memset(mode, 0, sizeof(mode));

                (void)parseAttrValue(tagRaw, "path", pathAttr, sizeof(pathAttr));
                if (pathAttr[0] == '\0')
                    (void)parseAttrValue(tagRaw, "src", pathAttr, sizeof(pathAttr));
                (void)parseAttrValue(tagRaw, "mode", mode, sizeof(mode));
                if (mode[0] == '\0')
                    (void)parseAttrValue(tagRaw, "scale", mode, sizeof(mode));

                if (w <= 0)
                    w = 24;
                if (h <= 0)
                    h = 24;

                auto *img = new QW::Controls::ImageView(m_desktopWindow, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});

                QG::ImageScaleMode scale = QG::ImageScaleMode::Fit;
                if (mode[0])
                {
                    if (streqIgnoreCaseAscii(mode, "stretch"))
                        scale = QG::ImageScaleMode::Stretch;
                    else if (streqIgnoreCaseAscii(mode, "center"))
                        scale = QG::ImageScaleMode::Center;
                    else if (streqIgnoreCaseAscii(mode, "tile"))
                        scale = QG::ImageScaleMode::Tile;
                    else if (streqIgnoreCaseAscii(mode, "fill"))
                        scale = QG::ImageScaleMode::Fill;
                    else if (streqIgnoreCaseAscii(mode, "original"))
                        scale = QG::ImageScaleMode::Original;
                    else if (streqIgnoreCaseAscii(mode, "fit"))
                        scale = QG::ImageScaleMode::Fit;
                }
                img->setScaleMode(scale);

                if (pathAttr[0])
                {
                    if (ImageAsset *asset = loadImageAsset(pathAttr))
                    {
                        img->setImage(&asset->surface);
                        img->setVisible(true);
                    }
                    else
                    {
                        img->setImage(nullptr);
                    }
                }
                else
                {
                    img->setImage(nullptr);
                }

                if (styleProps.hasEnabled)
                    img->setEnabled(styleProps.enabled);
                if (styleProps.hasVisible)
                    img->setVisible(styleProps.visible);

                created = img;
            }
            else if (startsWithIgnoreCaseAscii(effectiveName, "Label"))
            {
                char textAttr[512];
                QC::String::memset(textAttr, 0, sizeof(textAttr));
                parseAttrMerged("text", textAttr, sizeof(textAttr));

                char fontAttr[128];
                QC::String::memset(fontAttr, 0, sizeof(fontAttr));
                parseAttrMerged("font", fontAttr, sizeof(fontAttr));

                char fontSizeAttr[64];
                QC::String::memset(fontSizeAttr, 0, sizeof(fontSizeAttr));
                parseAttrMerged("font-size", fontSizeAttr, sizeof(fontSizeAttr));

                if (fontAttr[0] == '\0' && styleProps.hasFont)
                {
                    QC::String::strncpy(fontAttr, styleProps.font, sizeof(fontAttr) - 1);
                    fontAttr[sizeof(fontAttr) - 1] = '\0';
                }

                if (fontSizeAttr[0] != '\0' && fontAttr[0] != '\0')
                {
                    bool ok = false;
                    const QC::i32 sz = parseInt(fontSizeAttr, ok);
                    if (ok && sz > 0)
                    {
                        char spec[128];
                        QC::String::memset(spec, 0, sizeof(spec));
                        if (buildFontSpecFromFamilySize(fontAttr, static_cast<QC::u32>(sz), spec, sizeof(spec)))
                        {
                            QC::String::strncpy(fontAttr, spec, sizeof(fontAttr) - 1);
                            fontAttr[sizeof(fontAttr) - 1] = '\0';
                        }
                    }
                }

                QC::u32 fontPx = 0;
                const bool hasFontPx = parseFontSizePx(fontAttr, fontPx);
                const float fontScale = hasFontPx ? textScaleFromFontPx(fontPx) : 0.0f;
                  const float layoutScale = (fontScale > 0.0f)
                                   ? fontScale
                                   : ((haveDerivedThemeFont && derivedThemeFontPx > 0)
                                       ? (static_cast<float>(derivedThemeFontPx) / BASE_THEME_FONT_SIZE)
                                       : 0.0f);

                if (w <= 0)
                    w = parentW - x;
                if (w < 0)
                    w = 0;
                if (h <= 0)
                {
                    // Default label height should reflect the requested font size so text doesn't clip.
                    const QC::u32 lineH = lineHeightForTextScale(layoutScale);
                    QC::u32 lines = 1;
                    for (const char *t = textAttr; *t; ++t)
                        if (*t == '\n')
                            ++lines;
                    h = static_cast<QC::i32>(lines * lineH);
                }

                auto *label = new QW::Controls::Label(m_desktopWindow, textAttr, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                label->setTransparent(true);

                if (styleProps.hasTextAlign)
                    label->setTextAlign(styleProps.textAlign);
                if (styleProps.hasTextColor)
                    label->setTextColor(styleProps.textColor);
                if (styleProps.hasBackground)
                {
                    label->setTransparent(false);
                    label->setBackgroundColor(styleProps.background);
                }
                if (styleProps.hasEnabled)
                    label->setEnabled(styleProps.enabled);
                if (styleProps.hasVisible)
                    label->setVisible(styleProps.visible);

                if (fontScale > 0.0f)
                {
                    label->setTextScaleOverride(fontScale);
                }

                char color[128];
                QC::String::memset(color, 0, sizeof(color));
                parseAttrMerged("color", color, sizeof(color));
                if (color[0] != '\0')
                {
                    QC::Color c;
                    if (parseColorString(color, c))
                        label->setTextColor(c);
                }

                created = label;
            }
            else if (startsWithIgnoreCaseAscii(effectiveName, "Button"))
            {
                char textAttr[256];
                QC::String::memset(textAttr, 0, sizeof(textAttr));
                parseAttrMerged("text", textAttr, sizeof(textAttr));

                char fontAttr[128];
                QC::String::memset(fontAttr, 0, sizeof(fontAttr));
                parseAttrMerged("font", fontAttr, sizeof(fontAttr));

                char fontSizeAttr[64];
                QC::String::memset(fontSizeAttr, 0, sizeof(fontSizeAttr));
                parseAttrMerged("font-size", fontSizeAttr, sizeof(fontSizeAttr));

                if (fontAttr[0] == '\0' && styleProps.hasFont)
                {
                    QC::String::strncpy(fontAttr, styleProps.font, sizeof(fontAttr) - 1);
                    fontAttr[sizeof(fontAttr) - 1] = '\0';
                }

                if (fontSizeAttr[0] != '\0' && fontAttr[0] != '\0')
                {
                    bool ok = false;
                    const QC::i32 sz = parseInt(fontSizeAttr, ok);
                    if (ok && sz > 0)
                    {
                        char spec[128];
                        QC::String::memset(spec, 0, sizeof(spec));
                        if (buildFontSpecFromFamilySize(fontAttr, static_cast<QC::u32>(sz), spec, sizeof(spec)))
                        {
                            QC::String::strncpy(fontAttr, spec, sizeof(fontAttr) - 1);
                            fontAttr[sizeof(fontAttr) - 1] = '\0';
                        }
                    }
                }

                char role[64];
                QC::String::memset(role, 0, sizeof(role));
                parseAttrMerged("role", role, sizeof(role));

                if (role[0] == '\0' && styleProps.hasRole)
                {
                    QC::String::strncpy(role, styleProps.role, sizeof(role) - 1);
                    role[sizeof(role) - 1] = '\0';
                }

                char iconAttr[256];
                QC::String::memset(iconAttr, 0, sizeof(iconAttr));
                parseAttrMerged("icon", iconAttr, sizeof(iconAttr));

                auto resolveIconPath = [&](const char *iconTokenOrPath, char *out, QC::usize outCap) -> bool
                {
                    if (!out || outCap == 0)
                        return false;
                    out[0] = '\0';
                    if (!iconTokenOrPath || !*iconTokenOrPath)
                        return false;

                    if (resolveThemeAssetKey(iconTokenOrPath, &m_loadedTheme.package.assets, out, outCap))
                        return true;

                    if (const char *alias = lookupIconAliasPath(iconTokenOrPath))
                        iconTokenOrPath = alias;

                    // If it looks like a path, use as-is.
                    for (const char *p = iconTokenOrPath; *p; ++p)
                    {
                        if (*p == '/')
                        {
                            QC::String::strncpy(out, iconTokenOrPath, outCap - 1);
                            out[outCap - 1] = '\0';
                            trimInPlace(out);
                            return out[0] != '\0';
                        }
                    }

                    // Prefer SVG icons (if present): /ICONS/svg/<token>.svg
                    {
                        char token[128];
                        QC::String::memset(token, 0, sizeof(token));
                        QC::String::strncpy(token, iconTokenOrPath, sizeof(token) - 1);
                        token[sizeof(token) - 1] = '\0';
                        trimInPlace(token);
                        if (token[0])
                        {
                            const char *prefix = "/ICONS/svg/";
                            const char *suffix = ".svg";
                            const QC::usize preLen = QC::String::strlen(prefix);
                            const QC::usize nameLen = QC::String::strlen(token);
                            const QC::usize sufLen = QC::String::strlen(suffix);
                            if (preLen + nameLen + sufLen + 1 <= outCap)
                            {
                                char svgPath[192];
                                QC::String::memset(svgPath, 0, sizeof(svgPath));
                                QC::String::memcpy(svgPath, prefix, preLen);
                                QC::String::memcpy(svgPath + preLen, token, nameLen);
                                QC::String::memcpy(svgPath + preLen + nameLen, suffix, sufLen);
                                svgPath[preLen + nameLen + sufLen] = '\0';

                                QFS::File *f = QFS::VFS::instance().open(svgPath, QFS::OpenMode::Read);
                                if (f)
                                {
                                    QFS::VFS::instance().close(f);
                                    QC::String::strncpy(out, svgPath, outCap - 1);
                                    out[outCap - 1] = '\0';
                                    return true;
                                }
                            }
                        }
                    }

                    // Otherwise treat it as a short icon name token and map to /ICONS/<NAME>.PNG
                    char up[9];
                    QC::String::memset(up, 0, sizeof(up));
                    QC::usize n = 0;
                    for (const char *p = iconTokenOrPath; *p && n < 8; ++p)
                    {
                        char c = *p;
                        if (c >= 'a' && c <= 'z')
                            c = static_cast<char>(c - 'a' + 'A');
                        // Skip characters outside our simple 8.3-friendly token set.
                        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
                        if (!ok)
                            continue;
                        up[n++] = c;
                    }
                    up[n] = '\0';
                    if (!up[0])
                        return false;

                    const char *prefix = "/ICONS/";
                    const char *suffix = ".PNG";
                    const QC::usize preLen = QC::String::strlen(prefix);
                    const QC::usize nameLen = QC::String::strlen(up);
                    const QC::usize sufLen = QC::String::strlen(suffix);
                    if (preLen + nameLen + sufLen + 1 > outCap)
                        return false;
                    QC::String::memcpy(out, prefix, preLen);
                    QC::String::memcpy(out + preLen, up, nameLen);
                    QC::String::memcpy(out + preLen + nameLen, suffix, sufLen);
                    out[preLen + nameLen + sufLen] = '\0';
                    return true;
                };

                const bool isHelp = (role[0] && startsWithIgnoreCaseAscii(role, "help")) ||
                                    (actionAttr[0] && startsWithIgnoreCaseAscii(actionAttr, "help")) ||
                                    (idAttr[0] && (QC::String::strcmp(idAttr, "help") == 0 || QC::String::strcmp(idAttr, "helpButton") == 0));

                if (isHelp && textAttr[0] == '\0')
                {
                    textAttr[0] = '?';
                    textAttr[1] = '\0';
                }

                QC::u32 fontPx = 0;
                const bool hasFontPx = parseFontSizePx(fontAttr, fontPx);
                const float fontScale = hasFontPx ? textScaleFromFontPx(fontPx) : 0.0f;
                  const float layoutScale = (fontScale > 0.0f)
                                   ? fontScale
                                   : ((haveDerivedThemeFont && derivedThemeFontPx > 0)
                                       ? (static_cast<float>(derivedThemeFontPx) / BASE_THEME_FONT_SIZE)
                                       : 0.0f);

                const bool hasWidthAttr = (wBuf[0] != '\0');
                const bool hasHeightAttr = (hBuf[0] != '\0');

                // Default sizing, plus minimums based on text size when a font override is provided.
                if (w <= 0)
                    w = isHelp ? 20 : 120;
                if (h <= 0)
                    h = isHelp ? 20 : 28;

                if (fontScale > 0.0f || !hasWidthAttr || !hasHeightAttr)
                {
                    const QC::Size textSize = measureTextForTextScale(textAttr, layoutScale);
                    const QC::u32 padW = isHelp ? 10 : 24;
                    const QC::u32 padH = isHelp ? 8 : 20;
                    const QC::u32 minW = textSize.width + padW;
                    const QC::u32 minH = textSize.height + padH;

                    if (static_cast<QC::u32>(w) < minW)
                        w = static_cast<QC::i32>(minW);
                    if (static_cast<QC::u32>(h) < minH)
                        h = static_cast<QC::i32>(minH);
                }

                auto *btn = new QW::Controls::Button(m_desktopWindow, textAttr, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                btn->setContentMode(QW::ButtonContentMode::Text);
                btn->setVariant(QW::ButtonVariant::Borderless);

                if (fontScale > 0.0f)
                {
                    btn->setTextScaleOverride(fontScale);
                }

                if (styleProps.hasEnabled)
                    btn->setEnabled(styleProps.enabled);
                if (styleProps.hasVisible)
                    btn->setVisible(styleProps.visible);

                {
                    QW::ButtonRole parsed = QW::ButtonRole::Default;
                    bool parsedOk = false;
                    if (role[0] != '\0')
                    {
                        parsedOk = QW::buttonRoleFromString(role, &parsed);
                    }
                    if (!parsedOk)
                    {
                        if (idAttr[0] && QC::String::strcmp(idAttr, "shutDownButton") == 0)
                            parsed = QW::ButtonRole::Destructive;
                        else if (idAttr[0] && QC::String::strcmp(idAttr, "startButton") == 0)
                            parsed = QW::ButtonRole::Accent;
                        else if (idAttr[0] && startsWith(idAttr, "btn"))
                            parsed = QW::ButtonRole::Sidebar;
                        else
                            parsed = QW::ButtonRole::Default;
                    }
                    btn->setRole(parsed);
                }

                if (iconAttr[0])
                {
                    char iconPath[192];
                    QC::String::memset(iconPath, 0, sizeof(iconPath));
                    if (resolveIconPath(iconAttr, iconPath, sizeof(iconPath)))
                    {
                        if (ImageAsset *asset = loadImageAsset(iconPath))
                        {
                            btn->setIcon(&asset->surface);
                        }
                        else
                        {
                            const char *warnId = idAttr[0] ? idAttr : "<unnamed>";
                            QC_LOG_WARN(LOG_MODULE, "Button '%s' icon missing or failed to load '%s'", warnId, iconPath);
                        }
                    }
                }

                // Wire up known desktop actions (trusted parent desktop).
                if (idAttr[0] && QC::String::strcmp(idAttr, "btnTerminal") == 0)
                {
                    btn->setClickHandler(onJsonTerminalClick, this);
                }
                else if (idAttr[0] && QC::String::strcmp(idAttr, "btnSettings") == 0)
                {
                    btn->setClickHandler(onJsonSettingsClick, this);
                }
                else if ((idAttr[0] && QC::String::strcmp(idAttr, "btnCMMS") == 0) ||
                         (actionAttr[0] && streqIgnoreCaseAscii(actionAttr, "cmms")))
                {
                    btn->setClickHandler(onJsonCMMSClick, this);
                    QC_LOG_INFO(LOG_MODULE,
                                "CUI-ML: bound CMMS Button id='%s' action='%s' at x=%d y=%d w=%d h=%d",
                                idAttr[0] ? idAttr : "<none>",
                                actionAttr[0] ? actionAttr : "<none>",
                                x, y, w, h);
                }
                else if ((idAttr[0] && QC::String::strcmp(idAttr, "shutDownButton") == 0) ||
                         (actionAttr[0] && streqIgnoreCaseAscii(actionAttr, "shutdown")))
                {
                    btn->setClickHandler(onJsonShutdownClick, this);
                }
                else if (isHelp && !m_helpButton)
                {
                    btn->setClickHandler(onHelpClick, this);
                    m_helpButton = btn;
                    if (QC::String::strcmp(textAttr, "?") != 0)
                        btn->setText("?");
                }

                created = btn;
            }
            else if (startsWithIgnoreCaseAscii(effectiveName, "IconButton"))
            {
                // Backward-compatible parser alias: old IconButton markup now
                // produces a standard Button configured for icon-only content.
                char textAttr[256];
                QC::String::memset(textAttr, 0, sizeof(textAttr));
                parseAttrMerged("text", textAttr, sizeof(textAttr));

                char fontAttr[128];
                QC::String::memset(fontAttr, 0, sizeof(fontAttr));
                parseAttrMerged("font", fontAttr, sizeof(fontAttr));

                char fontSizeAttr[64];
                QC::String::memset(fontSizeAttr, 0, sizeof(fontSizeAttr));
                parseAttrMerged("font-size", fontSizeAttr, sizeof(fontSizeAttr));

                if (fontAttr[0] == '\0' && styleProps.hasFont)
                {
                    QC::String::strncpy(fontAttr, styleProps.font, sizeof(fontAttr) - 1);
                    fontAttr[sizeof(fontAttr) - 1] = '\0';
                }

                if (fontSizeAttr[0] != '\0' && fontAttr[0] != '\0')
                {
                    bool ok = false;
                    const QC::i32 sz = parseInt(fontSizeAttr, ok);
                    if (ok && sz > 0)
                    {
                        char spec[128];
                        QC::String::memset(spec, 0, sizeof(spec));
                        if (buildFontSpecFromFamilySize(fontAttr, static_cast<QC::u32>(sz), spec, sizeof(spec)))
                        {
                            QC::String::strncpy(fontAttr, spec, sizeof(fontAttr) - 1);
                            fontAttr[sizeof(fontAttr) - 1] = '\0';
                        }
                    }
                }

                char role[64];
                QC::String::memset(role, 0, sizeof(role));
                parseAttrMerged("role", role, sizeof(role));

                if (role[0] == '\0' && styleProps.hasRole)
                {
                    QC::String::strncpy(role, styleProps.role, sizeof(role) - 1);
                    role[sizeof(role) - 1] = '\0';
                }

                char iconAttr[256];
                QC::String::memset(iconAttr, 0, sizeof(iconAttr));
                parseAttrMerged("icon", iconAttr, sizeof(iconAttr));

                auto resolveIconPath = [&](const char *iconTokenOrPath, char *out, QC::usize outCap) -> bool
                {
                    if (!out || outCap == 0)
                        return false;
                    out[0] = '\0';
                    if (!iconTokenOrPath || !*iconTokenOrPath)
                        return false;

                    if (resolveThemeAssetKey(iconTokenOrPath, &m_loadedTheme.package.assets, out, outCap))
                        return true;

                    if (const char *alias = lookupIconAliasPath(iconTokenOrPath))
                        iconTokenOrPath = alias;

                    for (const char *p = iconTokenOrPath; *p; ++p)
                    {
                        if (*p == '/')
                        {
                            QC::String::strncpy(out, iconTokenOrPath, outCap - 1);
                            out[outCap - 1] = '\0';
                            trimInPlace(out);
                            return out[0] != '\0';
                        }
                    }

                    // Prefer SVG icons (if present): /ICONS/svg/<token>.svg
                    {
                        char token[128];
                        QC::String::memset(token, 0, sizeof(token));
                        QC::String::strncpy(token, iconTokenOrPath, sizeof(token) - 1);
                        token[sizeof(token) - 1] = '\0';
                        trimInPlace(token);
                        if (token[0])
                        {
                            const char *prefix = "/ICONS/svg/";
                            const char *suffix = ".svg";
                            const QC::usize preLen = QC::String::strlen(prefix);
                            const QC::usize nameLen = QC::String::strlen(token);
                            const QC::usize sufLen = QC::String::strlen(suffix);
                            if (preLen + nameLen + sufLen + 1 <= outCap)
                            {
                                char svgPath[192];
                                QC::String::memset(svgPath, 0, sizeof(svgPath));
                                QC::String::memcpy(svgPath, prefix, preLen);
                                QC::String::memcpy(svgPath + preLen, token, nameLen);
                                QC::String::memcpy(svgPath + preLen + nameLen, suffix, sufLen);
                                svgPath[preLen + nameLen + sufLen] = '\0';

                                QFS::File *f = QFS::VFS::instance().open(svgPath, QFS::OpenMode::Read);
                                if (f)
                                {
                                    QFS::VFS::instance().close(f);
                                    QC::String::strncpy(out, svgPath, outCap - 1);
                                    out[outCap - 1] = '\0';
                                    return true;
                                }
                            }
                        }
                    }

                    char up[9];
                    QC::String::memset(up, 0, sizeof(up));
                    QC::usize n = 0;
                    for (const char *p = iconTokenOrPath; *p && n < 8; ++p)
                    {
                        char c = *p;
                        if (c >= 'a' && c <= 'z')
                            c = static_cast<char>(c - 'a' + 'A');
                        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
                        if (!ok)
                            continue;
                        up[n++] = c;
                    }
                    up[n] = '\0';
                    if (!up[0])
                        return false;

                    const char *prefix = "/ICONS/";
                    const char *suffix = ".PNG";
                    const QC::usize preLen = QC::String::strlen(prefix);
                    const QC::usize nameLen = QC::String::strlen(up);
                    const QC::usize sufLen = QC::String::strlen(suffix);
                    if (preLen + nameLen + sufLen + 1 > outCap)
                        return false;
                    QC::String::memcpy(out, prefix, preLen);
                    QC::String::memcpy(out + preLen, up, nameLen);
                    QC::String::memcpy(out + preLen + nameLen, suffix, sufLen);
                    out[preLen + nameLen + sufLen] = '\0';
                    return true;
                };

                QC::u32 fontPx = 0;
                const bool hasFontPx = parseFontSizePx(fontAttr, fontPx);
                const float fontScale = hasFontPx ? textScaleFromFontPx(fontPx) : 0.0f;
                const float layoutScale = (fontScale > 0.0f)
                                              ? fontScale
                                              : ((haveDerivedThemeFont && derivedThemeFontPx > 0)
                                                     ? (static_cast<float>(derivedThemeFontPx) / BASE_THEME_FONT_SIZE)
                                                     : 0.0f);

                const bool hasWidthAttr = (wBuf[0] != '\0');
                const bool hasHeightAttr = (hBuf[0] != '\0');
                const bool hasText = (textAttr[0] != '\0');

                // Icon-style buttons are icon-only; text is treated as tooltip.
                // Keep layout size deterministic: default to a compact square when not specified.
                if (w <= 0)
                    w = 32;
                if (h <= 0)
                    h = 32;

                auto *btn = new QW::Controls::Button(m_desktopWindow, nullptr, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                btn->setContentMode(QW::ButtonContentMode::Icon);
                btn->setVariant(QW::ButtonVariant::Icon);

                if (hasText)
                    btn->setTooltipText(textAttr);

                if (fontScale > 0.0f)
                {
                    btn->setTextScaleOverride(fontScale);
                }

                {
                    QW::ButtonRole parsed = QW::ButtonRole::Default;
                    bool parsedOk = false;
                    if (role[0] != '\0')
                        parsedOk = QW::buttonRoleFromString(role, &parsed);
                    if (!parsedOk)
                    {
                        if (idAttr[0] && QC::String::strcmp(idAttr, "shutDownButton") == 0)
                            parsed = QW::ButtonRole::Destructive;
                        else if (idAttr[0] && QC::String::strcmp(idAttr, "startButton") == 0)
                            parsed = QW::ButtonRole::Accent;
                        else if (idAttr[0] && startsWith(idAttr, "btn"))
                            parsed = QW::ButtonRole::Sidebar;
                        else
                            parsed = QW::ButtonRole::Default;
                    }
                    btn->setRole(parsed);
                }

                if (iconAttr[0])
                {
                    char iconPath[192];
                    QC::String::memset(iconPath, 0, sizeof(iconPath));
                    if (resolveIconPath(iconAttr, iconPath, sizeof(iconPath)))
                    {
                        if (ImageAsset *asset = loadImageAsset(iconPath))
                        {
                            btn->setIcon(&asset->surface);
                        }
                        else
                        {
                            const char *warnId = idAttr[0] ? idAttr : "<unnamed>";
                            QC_LOG_WARN(LOG_MODULE, "Icon button '%s' icon missing or failed to load '%s'", warnId, iconPath);
                        }
                    }
                }

                if (styleProps.hasEnabled)
                    btn->setEnabled(styleProps.enabled);
                if (styleProps.hasVisible)
                    btn->setVisible(styleProps.visible);

                if (idAttr[0] && QC::String::strcmp(idAttr, "btnTerminal") == 0)
                {
                    btn->setClickHandler(onJsonTerminalButtonClick, this);
                }
                else if (idAttr[0] && QC::String::strcmp(idAttr, "btnSettings") == 0)
                {
                    btn->setClickHandler(onJsonSettingsButtonClick, this);
                }
                else if ((idAttr[0] && QC::String::strcmp(idAttr, "btnCMMS") == 0) ||
                         (actionAttr[0] && streqIgnoreCaseAscii(actionAttr, "cmms")))
                {
                    btn->setClickHandler(onJsonCMMSButtonClick, this);
                    QC_LOG_INFO(LOG_MODULE,
                                "CUI-ML: bound CMMS icon button id='%s' action='%s' at x=%d y=%d w=%d h=%d",
                                idAttr[0] ? idAttr : "<none>",
                                actionAttr[0] ? actionAttr : "<none>",
                                x, y, w, h);
                }
                else if ((idAttr[0] && QC::String::strcmp(idAttr, "shutDownButton") == 0) ||
                         (actionAttr[0] && streqIgnoreCaseAscii(actionAttr, "shutdown")))
                {
                    btn->setClickHandler(onJsonShutdownButtonClick, this);
                }

                created = btn;
            }

            if (!created)
                continue;

            if (idAttr[0])
                created->setId(hashControlId(idAttr));

            m_jsonControls.push_back(created);

            if (parentPanel)
                parentPanel->addChild(created);

            if (stack.size() == 1)
                m_jsonRootControls.push_back(created);

            // Capture well-known pointers used by Desktop logic.
            if (idAttr[0])
            {
                if (!m_topBar && QC::String::strcmp(idAttr, "headerBar") == 0)
                    m_topBar = created->asPanel();
                if (!m_sidebar && QC::String::strcmp(idAttr, "sidebar") == 0)
                    m_sidebar = created->asPanel();
                if (!m_taskbar && QC::String::strcmp(idAttr, "taskbar") == 0)
                    m_taskbar = created->asPanel();

                if (!m_titleLabel && QC::String::strcmp(idAttr, "headerTitle") == 0)
                    m_titleLabel = static_cast<QW::Controls::Label *>(created);
                if (!m_clockLabel && QC::String::strcmp(idAttr, "clockLabel") == 0)
                    m_clockLabel = static_cast<QW::Controls::Label *>(created);

                if (!m_jsonStartButton && QC::String::strcmp(idAttr, "startButton") == 0)
                    m_jsonStartButton = created;
                if (!m_jsonShutdownButton && QC::String::strcmp(idAttr, "shutDownButton") == 0)
                    m_jsonShutdownButton = created;
            }

            if (created->asPanel() && !selfClose)
            {
                Frame f;
                f.panel = created->asPanel();
                f.w = w;
                f.h = h;
                stack.push_back(f);
            }
        }
        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage control_build=%llums\n",
                static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - controlBuildStartMs));

        operator delete[](cuimlText);

        // Expect at least one non-wallpaper control.
        if (m_jsonRootControls.size() <= 1)
        {
            QC_LOG_WARN(LOG_MODULE, "desktop CUI-ML produced no controls; skipping\n");
            clearJsonDesktopState();
            return false;
        }

        // Optional runtime overrides (validated early by BootGate if present in production).
        // Applies to both CUI-ML and JSON desktop layouts.
        const QC::u64 overrideStageStartMs = QDrv::Timer::instance().milliseconds();
        {
            bool backgroundApplied = false;
            QC::JSON::Value ovrRoot;
            const char *ovrOpenedPath = nullptr;
            if (tryLoadDesktopOverridesFromVfs(ovrRoot, ovrOpenedPath) && ovrOpenedPath)
            {
                QC_LOG_INFO(LOG_MODULE, "Applying desktop overrides from %s\n", ovrOpenedPath);
                applyDesktopOverridesObject(ovrRoot, backgroundApplied);
            }
        }
        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage overrides=%llums\n",
                    static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - overrideStageStartMs));

        // Apply CUIMLSS-derived font family/size last so it wins over seasonal theme presets.
        if (haveDerivedThemeFont)
        {
            if (derivedThemeFontPx > 0)
            {
                QC::u32 clamped = derivedThemeFontPx;
                if (clamped < 1)
                    clamped = 1;
                if (clamped > 255)
                    clamped = 255;
                m_themeOverrides.font.sizeSet = true;
                m_themeOverrides.font.size = static_cast<QC::u8>(clamped);
                m_themeOverrides.active = true;
            }

            if (!streqIgnoreCaseAscii(derivedThemeFontFamily, "system") && !streqIgnoreCaseAscii(derivedThemeFontFamily, "System"))
            {
                m_themeOverrides.font.familySet = true;
                QC::String::strncpy(m_themeOverrides.font.family, derivedThemeFontFamily, sizeof(m_themeOverrides.font.family) - 1);
                m_themeOverrides.font.family[sizeof(m_themeOverrides.font.family) - 1] = '\0';
                m_themeOverrides.active = true;
            }
            else
            {
                m_themeOverrides.font.familySet = false;
                m_themeOverrides.font.family[0] = '\0';
            }

            QC_LOG_INFO(LOG_MODULE, "CUI-ML theme font: spec='%s' family='%s' px=%u\n",
                        derivedThemeFontFamily[0] ? derivedThemeFontFamily : "<none>",
                        (m_themeOverrides.font.familySet && m_themeOverrides.font.family[0]) ? m_themeOverrides.font.family : "System",
                        static_cast<unsigned>(derivedThemeFontPx));
        }

        // If we have a help spec but no explicit help button, inject a small '?' button.
        if (!m_helpButton && (m_helpInlineHtml || m_helpSrcOrUrl))
        {
            const QC::i32 helpW = 20;
            const QC::i32 helpH = 20;
            QC::i32 helpX = static_cast<QC::i32>(m_screenWidth) - helpW - 4;
            if (helpX < 0)
                helpX = 0;

            QW::Rect bounds = {helpX, 4, static_cast<QC::u32>(helpW), static_cast<QC::u32>(helpH)};
            auto *btn = new QW::Controls::Button(m_desktopWindow, "?", bounds);
            btn->setContentMode(QW::ButtonContentMode::Text);
            btn->setVariant(QW::ButtonVariant::Compact);
            btn->setClickHandler(onHelpClick, this);

            m_helpButton = btn;
            m_jsonControls.push_back(btn);
            m_jsonRootControls.push_back(btn);
            m_desktopWindow->root()->addChild(btn);
        }

        const QC::u64 finalizeStageStartMs = QDrv::Timer::instance().milliseconds();
        recomputeTaskbarWindowBase();
        applyColors();
        QC_LOG_INFO(LOG_MODULE, "CUI-ML stage finalize=%llums total=%llums\n",
                static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - finalizeStageStartMs),
                static_cast<unsigned long long>(QDrv::Timer::instance().milliseconds() - cuimlInitStartMs));

        QC_LOG_INFO(LOG_MODULE, "Desktop initialized from CUI-ML (%u controls)\n", static_cast<unsigned>(m_jsonControls.size()));
        return true;
    }

    bool Desktop::tryLoadDesktopOverridesFromVfs(QC::JSON::Value &outRoot, const char *&outOpenedPath) const
    {
        outRoot = QC::JSON::Value{};
        outOpenedPath = nullptr;

        const bool forceGoldenOverrides = (QK::Boot::Config::GetActiveConfigTier() == QK::Boot::Config::ConfigTier::Golden);

        // If boot-time tier validation selected GOLDEN, do not load overrides from /PROD.
        const char *overridePathsProdFirst[] = {"/PROD/DESKOVR.JSN", "/GOLDEN/DESKOVR.JSN", "/desktop-overrides.json", "/DESKOVR.JSN", "/DESKOVR~1.JSO"};
        const char *overridePathsGoldenOnly[] = {"/GOLDEN/DESKOVR.JSN", "/desktop-overrides.json", "/DESKOVR.JSN", "/DESKOVR~1.JSO"};

        const char **overridePaths = forceGoldenOverrides ? overridePathsGoldenOnly : overridePathsProdFirst;
        const QC::usize overridePathCount = forceGoldenOverrides ? (sizeof(overridePathsGoldenOnly) / sizeof(overridePathsGoldenOnly[0]))
                                                                 : (sizeof(overridePathsProdFirst) / sizeof(overridePathsProdFirst[0]));

        for (QC::usize i = 0; i < overridePathCount; ++i)
        {
            const char *path = overridePaths[i];
            QFS::File *ovrFile = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!ovrFile)
                continue;

            QC::u64 size64 = ovrFile->size();
            if (size64 == 0 || size64 > 1024 * 64)
            {
                QFS::VFS::instance().close(ovrFile);
                QC_LOG_WARN(LOG_MODULE, "Desktop overrides %s has invalid size (%llu); skipping\n", path, static_cast<unsigned long long>(size64));
                continue;
            }

            QC::usize size = static_cast<QC::usize>(size64);
            char *jsonText = static_cast<char *>(operator new[](size + 1));
            QC::isize readCount = ovrFile->read(jsonText, size);
            QFS::VFS::instance().close(ovrFile);

            if (readCount <= 0)
            {
                operator delete[](jsonText);
                QC_LOG_WARN(LOG_MODULE, "Failed to read desktop overrides %s; skipping\n", path);
                continue;
            }

            if (static_cast<QC::usize>(readCount) < size)
                size = static_cast<QC::usize>(readCount);
            jsonText[size] = '\0';

            outRoot = QC::JSON::Value{};
            const bool ok = QC::JSON::parse(jsonText, outRoot);
            operator delete[](jsonText);

            if (!ok || !outRoot.isObject())
            {
                QC_LOG_WARN(LOG_MODULE, "Failed to parse desktop overrides %s; skipping\n", path);
                continue;
            }

            outOpenedPath = path;
            return true;
        }

        return false;
    }

    void Desktop::applyDesktopOverridesObject(const QC::JSON::Value &ovrRoot, bool &ioBackgroundApplied)
    {
        auto hasCustomBackground = [&]() -> bool
        {
            if (m_backgroundConfig.mode == BackgroundMode::Image)
                return true;
            return m_backgroundConfig.topOverride || m_backgroundConfig.bottomOverride;
        };

        // Optional: season-based theme-only switching.
        // If present, loads one of the fixed seasonal desktop presets and applies only
        // its background + theme palette.
        if (const QC::JSON::Value *seasonV = ovrRoot.find("season"); seasonV)
        {
            if (!seasonV->isString())
            {
                QC_LOG_WARN(LOG_MODULE, "desktop-overrides.json: season must be a string\n");
            }
            else
            {
                const char *seasonText = seasonV->asString(nullptr);

                auto applySeasonalPreset = [&](Season season, const char *label) -> bool
                {
                    const char *paths[4] = {};
                    QC::usize pathCount = 0;
                    seasonCandidatePaths(season, paths, sizeof(paths) / sizeof(paths[0]), &pathCount);

                    const char *appliedPath = nullptr;

                    for (QC::usize i = 0; i < pathCount; ++i)
                    {
                        QFS::File *seasonFile = QFS::VFS::instance().open(paths[i], QFS::OpenMode::Read);
                        if (!seasonFile)
                            continue;

                        const QC::u64 tIo0 = QDrv::Timer::instance().milliseconds();

                        QC::u64 size64 = seasonFile->size();
                        if (size64 == 0 || size64 > 1024 * 256)
                        {
                            QFS::VFS::instance().close(seasonFile);
                            continue;
                        }

                        QC::usize size2 = static_cast<QC::usize>(size64);
                        char *seasonJson = static_cast<char *>(operator new[](size2 + 1));
                        QC::isize rc = seasonFile->read(seasonJson, size2);
                        QFS::VFS::instance().close(seasonFile);

                        const QC::u64 tIo1 = QDrv::Timer::instance().milliseconds();

                        if (rc <= 0)
                        {
                            operator delete[](seasonJson);
                            continue;
                        }

                        if (static_cast<QC::usize>(rc) < size2)
                            size2 = static_cast<QC::usize>(rc);
                        seasonJson[size2] = '\0';

                        QC::JSON::Value seasonRoot;
                        const QC::u64 tParse0 = QDrv::Timer::instance().milliseconds();
                        const bool ok = QC::JSON::parse(seasonJson, seasonRoot);
                        const QC::u64 tParse1 = QDrv::Timer::instance().milliseconds();
                        operator delete[](seasonJson);
                        if (!ok || !seasonRoot.isObject())
                            continue;

                        const QC::JSON::Value *seasonDesktop = seasonRoot.find("desktop");
                        if (!seasonDesktop || !seasonDesktop->isObject())
                            continue;

                        parseBackground(seasonDesktop->find("background"));
                        ioBackgroundApplied = hasCustomBackground();

                        if (const QC::JSON::Value *theme = seasonDesktop->find("theme"); theme)
                        {
                            if (looksLikeFullThemeDefinition(theme))
                            {
                                parseThemeOverrides(theme);
                            }
                            else if (theme->isObject())
                            {
                                bool changed = false;
                                changed |= parseColorOverride(theme, "accent", m_themeOverrides.palette.accent);
                                changed |= parseColorOverride(theme, "accentLight", m_themeOverrides.palette.accentLight);
                                changed |= parseColorOverride(theme, "accentDark", m_themeOverrides.palette.accentDark);
                                changed |= parseColorOverride(theme, "panel", m_themeOverrides.palette.panel);
                                changed |= parseColorOverride(theme, "panelBorder", m_themeOverrides.palette.panelBorder);
                                changed |= parseColorOverride(theme, "text", m_themeOverrides.palette.text);
                                changed |= parseColorOverride(theme, "textSecondary", m_themeOverrides.palette.textSecondary);
                                if (changed)
                                    m_themeOverrides.active = true;
                            }
                        }

                        appliedPath = paths[i];

                        QC_LOG_INFO(LOG_MODULE, "Seasonal preset %s bytes=%u io=%llums parse=%llums\n",
                                    appliedPath ? appliedPath : "<unknown>",
                                    static_cast<unsigned>(size2),
                                    static_cast<unsigned long long>(tIo1 - tIo0),
                                    static_cast<unsigned long long>(tParse1 - tParse0));
                        break;
                    }

                    if (appliedPath)
                    {
                        QC_LOG_INFO(LOG_MODULE, "Applied seasonal theme '%s' from %s\n",
                                    label ? label : "<unknown>",
                                    appliedPath ? appliedPath : "<unknown>");
                        return true;
                    }

                    QC_LOG_WARN(LOG_MODULE, "Season '%s' requested, but no seasonal preset found\n",
                                label ? label : "<unknown>");
                    return false;
                };

                if (seasonText && equalsIgnoreCase(seasonText, "auto"))
                {
                    QC::u8 month = 0;
                    QC::u8 day = 0;
                    if (!tryGetRtcMonthDay(month, day))
                    {
                        QC_LOG_WARN(LOG_MODULE, "desktop-overrides.json: season=auto requested, but RTC date is unavailable\n");
                    }
                    else
                    {
                        const Season derived = seasonFromMonth(month);
                        if (derived == Season::Unknown)
                        {
                            QC_LOG_WARN(LOG_MODULE, "desktop-overrides.json: season=auto could not derive season (month=%u day=%u)\n",
                                        static_cast<unsigned>(month), static_cast<unsigned>(day));
                        }
                        else
                        {
                            QC_LOG_INFO(LOG_MODULE, "desktop-overrides.json: season=auto -> %s (month=%u day=%u)\n",
                                        seasonToken(derived), static_cast<unsigned>(month), static_cast<unsigned>(day));
                            applySeasonalPreset(derived, seasonToken(derived));
                        }
                    }
                }
                else
                {
                    const Season season = parseSeasonToken(seasonText);
                    if (season == Season::Unknown)
                    {
                        QC_LOG_WARN(LOG_MODULE, "desktop-overrides.json: unknown season '%s'\n", seasonText ? seasonText : "<null>");
                    }
                    else
                    {
                        applySeasonalPreset(season, seasonText);
                    }
                }
            }
        }

        // banner_text -> headerTitle label
        if (const QC::JSON::Value *banner = ovrRoot.find("banner_text"); banner && banner->isString())
        {
            const char *text = banner->asString(nullptr);
            if (text && *text && m_titleLabel)
                m_titleLabel->setText(text);
        }

        // colors -> light palette tweaks + a couple of obvious chrome helpers
        if (const QC::JSON::Value *colors = ovrRoot.find("colors"); colors && colors->isObject())
        {
            QC::Color parsed;

            if (const char *bg = stringOrNull(colors->find("background")); bg && parseColorString(bg, parsed))
            {
                m_themeOverrides.palette.panel.set = true;
                m_themeOverrides.palette.panel.value = parsed;
                m_themeOverrides.active = true;

                if (m_topBar)
                    m_topBar->setBackgroundColor(parsed);
                if (m_sidebar)
                    m_sidebar->setBackgroundColor(parsed);
                if (m_taskbar)
                    m_taskbar->setBackgroundColor(parsed);
            }

            if (const char *accent = stringOrNull(colors->find("accent")); accent && parseColorString(accent, parsed))
            {
                m_themeOverrides.palette.accent.set = true;
                m_themeOverrides.palette.accent.value = parsed;
                m_themeOverrides.active = true;
            }

            if (const char *text = stringOrNull(colors->find("text")); text && parseColorString(text, parsed))
            {
                m_themeOverrides.palette.text.set = true;
                m_themeOverrides.palette.text.value = parsed;
                m_themeOverrides.active = true;

                if (m_titleLabel)
                    m_titleLabel->setTextColor(parsed);
                if (m_clockLabel)
                    m_clockLabel->setTextColor(parsed);
            }
        }

        // theme -> optional theme overrides (merged on top of any base theme)
        if (const QC::JSON::Value *theme = ovrRoot.find("theme"); theme && theme->isObject())
        {
            parseThemeOverridesMerge(theme);
        }

        // layout -> per-control bounds overrides by id (same syntax as desktop.json)
        if (const QC::JSON::Value *layout = ovrRoot.find("layout"); layout && layout->isObject())
        {
            const QC::JSON::Object *layoutObj = layout->asObject();
            const QC::usize n = layoutObj ? layoutObj->size() : 0;
            for (QC::usize i = 0; i < n; ++i)
            {
                const auto &ent = (*layoutObj)[i];
                if (!ent.key || !ent.value || !ent.value->isObject())
                    continue;

                QW::Controls::ControlId cid = hashControlId(ent.key);
                if (cid == QW::Controls::InvalidControlId)
                    continue;

                QW::Controls::IControl *ctrl = nullptr;
                if (m_desktopWindow && m_desktopWindow->root())
                    ctrl = m_desktopWindow->root()->findChild(cid);

                if (!ctrl)
                    continue;

                QW::Rect oldB = ctrl->bounds();
                QW::Controls::Panel *parent = ctrl->parent();
                const QW::Rect parentB = parent ? parent->bounds() : QW::Rect{0, 0, m_screenWidth, m_screenHeight};
                const QC::i32 parentW = static_cast<QC::i32>(parentB.width);
                const QC::i32 parentH = static_cast<QC::i32>(parentB.height);

                QC::i32 x = oldB.x;
                QC::i32 y = oldB.y;
                QC::i32 w = static_cast<QC::i32>(oldB.width);
                QC::i32 h = static_cast<QC::i32>(oldB.height);

                const QC::JSON::Value *ovr = ent.value;
                if (const QC::JSON::Value *vx = ovr->find("x"))
                    (void)evalLayoutValue(vx, parentW, parentH, true, false, false, false, &x);
                if (const QC::JSON::Value *vy = ovr->find("y"))
                    (void)evalLayoutValue(vy, parentW, parentH, false, true, false, false, &y);
                if (const QC::JSON::Value *vw = ovr->find("width"))
                    (void)evalLayoutValue(vw, parentW, parentH, false, false, true, false, &w);
                if (const QC::JSON::Value *vh = ovr->find("height"))
                    (void)evalLayoutValue(vh, parentW, parentH, false, false, false, true, &h);

                w = clampNonNegative(w);
                h = clampNonNegative(h);

                ctrl->setBounds(QW::Rect{x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                ctrl->invalidate();
            }

            recomputeTaskbarWindowBase();
        }
    }

    bool Desktop::tryInitializeFromJson()
    {
        resetThemeOverrides();
        resetBackgroundConfig();

        // NOTE: Our FAT32 layer currently does not implement Long File Name (LFN) entries.
        // build.sh copies project-root desktop.json into the ramdisk as an 8.3 name: /DESKTOP.JSN

        const bool forceGolden = (QK::Boot::Config::GetActiveConfigTier() == QK::Boot::Config::ConfigTier::Golden);

        // If boot-time tier validation selected GOLDEN, do not load from /PROD.
        const char *jsonPathsProdFirst[] = {"/PROD/DESKTOP.JSN", "/GOLDEN/DESKTOP.JSN", "/desktop.json", "/DESKTOP.JSN", "/DESKTO~1.JSO"};
        const char *jsonPathsGoldenOnly[] = {"/GOLDEN/DESKTOP.JSN", "/desktop.json", "/DESKTOP.JSN", "/DESKTO~1.JSO"};

        const char **jsonPaths = forceGolden ? jsonPathsGoldenOnly : jsonPathsProdFirst;
        const QC::usize jsonPathCount = forceGolden ? (sizeof(jsonPathsGoldenOnly) / sizeof(jsonPathsGoldenOnly[0]))
                                                    : (sizeof(jsonPathsProdFirst) / sizeof(jsonPathsProdFirst[0]));

        QC::JSON::Value root;
        const QC::JSON::Value *desktop = nullptr;
        const QC::JSON::Value *layout = nullptr;
        const QC::JSON::Value *controls = nullptr;
        const char *openedPath = nullptr;
        char openedPathStorage[192];
        QC::String::memset(openedPathStorage, 0, sizeof(openedPathStorage));
        QC::usize openedBytes = 0;
        QC::u64 openedIoMs = 0;
        QC::u64 openedParseMs = 0;
        bool openedFromDatabase = false;

        if (ensureCmmsDatabaseReady())
        {
            const char *layoutIdsProdFirst[] = {CMMS_DESKTOP_LAYOUT_PRODUCTION, CMMS_DESKTOP_LAYOUT_GOLDEN};
            const char *layoutIdsGoldenOnly[] = {CMMS_DESKTOP_LAYOUT_GOLDEN};
            const char **layoutIds = forceGolden ? layoutIdsGoldenOnly : layoutIdsProdFirst;
            const QC::usize layoutIdCount = forceGolden ? (sizeof(layoutIdsGoldenOnly) / sizeof(layoutIdsGoldenOnly[0]))
                                                        : (sizeof(layoutIdsProdFirst) / sizeof(layoutIdsProdFirst[0]));

            for (QC::usize i = 0; i < layoutIdCount; ++i)
            {
                const QCQL::Cell keyCell = makeTextCell(layoutIds[i]);
                QCQL::Row row{};
                const QCQL::Status rowSt =
                    QCQL::Engine::instance().selectRowByPrimaryKeyByName(m_cmmsDatabase, CMMS_DESKTOP_LAYOUT_TABLE, keyCell.bytes, row);
                if (rowSt != QCQL::Status::Success || row.tombstone || row.cells.size() < 3 || row.cells[2].type != QCQL::ColumnType::Text)
                    continue;

                QC::u32 chunkCount = 0;
                if (!parseUnsignedTextCell(row.cells[2], chunkCount) || chunkCount == 0)
                    continue;

                QC::Vector<QC::u8> payloadBytes;
                if (!loadChunkedDocumentPayload(m_cmmsDatabase, CMMS_DESKTOP_LAYOUT_CHUNK_TABLE, layoutIds[i], chunkCount, payloadBytes))
                    continue;

                QC::usize size = payloadBytes.size();
                if (size == 0 || size > 1024 * 256)
                    continue;

                char *jsonText = static_cast<char *>(operator new[](size + 1));
                for (QC::usize n = 0; n < size; ++n)
                    jsonText[n] = static_cast<char>(payloadBytes[n]);
                jsonText[size] = '\0';

                root = QC::JSON::Value{};
                const QC::u64 tParse0 = QDrv::Timer::instance().milliseconds();
                const bool ok = QC::JSON::parse(jsonText, root);
                const QC::u64 tParse1 = QDrv::Timer::instance().milliseconds();
                operator delete[](jsonText);
                if (!ok)
                    continue;

                desktop = root.find("desktop");
                if (!desktop || !desktop->isObject())
                    continue;

                layout = desktop->find("layout");
                controls = layout ? layout->find("controls") : nullptr;
                if (!controls || !controls->isArray())
                    continue;

                if (!copyCellText(row.cells[1], openedPathStorage, sizeof(openedPathStorage)) || !openedPathStorage[0])
                {
                    QC::String::strncpy(openedPathStorage, "CMMS DB", sizeof(openedPathStorage) - 1);
                    openedPathStorage[sizeof(openedPathStorage) - 1] = '\0';
                }

                openedPath = openedPathStorage;
                openedBytes = size;
                openedIoMs = 0;
                openedParseMs = tParse1 - tParse0;
                openedFromDatabase = true;
                break;
            }
        }

        for (QC::usize i = 0; !openedPath && i < jsonPathCount; ++i)
        {
            const char *path = jsonPaths[i];
            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                continue;

            const QC::u64 tIo0 = QDrv::Timer::instance().milliseconds();

            QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 256)
            {
                QFS::VFS::instance().close(file);
                QC_LOG_WARN(LOG_MODULE, "Desktop JSON %s has invalid size (%llu); skipping\n", path, static_cast<unsigned long long>(size64));
                continue;
            }

            QC::usize size = static_cast<QC::usize>(size64);
            char *jsonText = static_cast<char *>(operator new[](size + 1));
            QC::isize readCount = file->read(jsonText, size);
            QFS::VFS::instance().close(file);

            const QC::u64 tIo1 = QDrv::Timer::instance().milliseconds();

            if (readCount <= 0)
            {
                operator delete[](jsonText);
                QC_LOG_WARN(LOG_MODULE, "Failed to read desktop JSON %s; skipping\n", path);
                continue;
            }

            if (static_cast<QC::usize>(readCount) < size)
                size = static_cast<QC::usize>(readCount);
            jsonText[size] = '\0';

            root = QC::JSON::Value{};

            const QC::u64 tParse0 = QDrv::Timer::instance().milliseconds();
            const bool ok = QC::JSON::parse(jsonText, root);
            const QC::u64 tParse1 = QDrv::Timer::instance().milliseconds();
            operator delete[](jsonText);

            if (!ok)
            {
                QC_LOG_WARN(LOG_MODULE, "Failed to parse desktop JSON %s; skipping\n", path);
                continue;
            }

            desktop = root.find("desktop");
            if (!desktop || !desktop->isObject())
            {
                QC_LOG_WARN(LOG_MODULE, "Desktop JSON %s missing 'desktop' object; skipping\n", path);
                continue;
            }

            layout = desktop->find("layout");
            controls = layout ? layout->find("controls") : nullptr;
            if (!controls || !controls->isArray())
            {
                QC_LOG_WARN(LOG_MODULE, "Desktop JSON %s missing layout.controls array; skipping\n", path);
                continue;
            }

            openedPath = path;
            openedBytes = size;
            openedIoMs = tIo1 - tIo0;
            openedParseMs = tParse1 - tParse0;
            break;
        }

        if (!openedPath)
        {
            // Item 36: Validation failure - fail closed with recovery guidance
            // Item 37: Diagnostic: no valid layout source found (neither QCQL nor file)
            // Item 38: Structured boot event for fallback exhaustion
            QC_LOG_INFO(LOG_MODULE, "Desktop boot failure: no valid layout data (QCQL exhausted, no fallback files)\n");
            QC_LOG_INFO(LOG_MODULE, "Recovery: run 'migrate-desktop provision auto' to seed QCQL from /PROD/DESKTOP.JSN or /GOLDEN/DESKTOP.JSN\n");
            QC_LOG_INFO(LOG_MODULE, "Fallback: using hardcoded desktop layout\n");
            
            // Emit structured boot event (Item 38)
            // Event: desktop_layout_fallback reason=no_valid_source
            return false;
        }

        if (openedFromDatabase)
        {
            QC_LOG_INFO(LOG_MODULE, "Loading desktop definition from CMMS DB (%s, bytes=%u io=%llums parse=%llums)\n",
                        openedPath,
                        static_cast<unsigned>(openedBytes),
                        static_cast<unsigned long long>(openedIoMs),
                        static_cast<unsigned long long>(openedParseMs));
        }
        else
        {
            QC_LOG_INFO(LOG_MODULE, "Loading desktop definition from %s (bytes=%u io=%llums parse=%llums)\n",
                        openedPath,
                        static_cast<unsigned>(openedBytes),
                        static_cast<unsigned long long>(openedIoMs),
                        static_cast<unsigned long long>(openedParseMs));
        }

        // Item 36/37: Validate JSON structure completely before applying
        // Check mandatory structure: desktop.theme, desktop.layout, desktop.layout.controls
        if (!desktop || !desktop->isObject())
        {
            QC_LOG_INFO(LOG_MODULE, "Desktop boot validation failed: missing 'desktop' object\n");
            QC_LOG_INFO(LOG_MODULE, "Recovery: check JSON structure at %s - must contain {\"desktop\": {...}}\n", openedPath);
            return false;
        }

        const QC::JSON::Value *theme = desktop->find("theme");
        if (!theme || !theme->isObject())
        {
            QC_LOG_INFO(LOG_MODULE, "Desktop boot validation failed: missing 'desktop.theme' object\n");
            QC_LOG_INFO(LOG_MODULE, "Recovery: add theme definition to JSON at %s\n", openedPath);
            return false;
        }

        if (!layout || !layout->isObject())
        {
            QC_LOG_INFO(LOG_MODULE, "Desktop boot validation failed: missing 'desktop.layout' object\n");
            QC_LOG_INFO(LOG_MODULE, "Recovery: add layout definition to JSON at %s\n", openedPath);
            return false;
        }

        const QC::JSON::Array *controlsArray = controls ? controls->asArray() : nullptr;
        if (!controlsArray || controlsArray->size() == 0)
        {
            QC_LOG_INFO(LOG_MODULE, "Desktop boot validation failed: missing or empty 'desktop.layout.controls' array\n");
            QC_LOG_INFO(LOG_MODULE, "Recovery: add at least one control to desktop.layout.controls in JSON at %s\n", openedPath);
            return false;
        }

        bool backgroundApplied = false;
        auto hasCustomBackground = [&]() -> bool
        {
            if (m_backgroundConfig.mode == BackgroundMode::Image)
                return true;
            return m_backgroundConfig.topOverride || m_backgroundConfig.bottomOverride;
        };

        // Build controls
        m_jsonDriven = true;

        // JSON mode wallpaper: paint it as a root ImageView behind all controls.
        // It starts hidden and will be populated by parseBackground() once we pick the final background.
        if (m_desktopWindow && m_desktopWindow->root())
        {
            QW::Rect bounds = {0, 0, m_screenWidth, m_screenHeight};
            auto *wall = new QW::Controls::ImageView(m_desktopWindow, bounds);
            wall->setScaleMode(QG::ImageScaleMode::Stretch);
            wall->setImage(nullptr);
            wall->setVisible(false);
            wall->setId(hashControlId("wallpaper"));

            m_jsonWallpaperView = wall;
            m_jsonControls.push_back(wall);
            m_jsonRootControls.push_back(wall);
            m_desktopWindow->root()->addChild(wall);
        }

        auto buildControl = [&](auto &&self, const QC::JSON::Value *controlValue, QW::Controls::Panel *parentPanel, QC::i32 parentW, QC::i32 parentH) -> void
        {
            if (!controlValue || !controlValue->isObject())
                return;

            const char *type = stringOrNull(controlValue->find("type"));
            if (!type)
                return;

            const char *id = stringOrNull(controlValue->find("id"));

            QW::Rect bounds = parseBounds(controlValue, parentW, parentH, type);

            QW::Controls::IControl *created = nullptr;

            if (QC::String::strcmp(type, "panel") == 0)
            {
                auto *panel = new QW::Controls::Panel(m_desktopWindow, bounds);
                panel->setBorderStyle(QW::Controls::BorderStyle::None);
                panel->setFrameVisible(false);

                if (const char *bg = stringOrNull(controlValue->find("background")))
                {
                    QC::Color parsed;
                    if (parseColorString(bg, parsed))
                        panel->setBackgroundColor(parsed);
                }

                // Any border hint -> simple flat border
                const char *border = stringOrNull(controlValue->find("border"));
                const char *borderTop = stringOrNull(controlValue->find("borderTop"));
                const char *borderBottom = stringOrNull(controlValue->find("borderBottom"));
                const char *borderLeft = stringOrNull(controlValue->find("borderLeft"));
                const char *borderRight = stringOrNull(controlValue->find("borderRight"));
                const char *borderAny = border ? border : (borderTop ? borderTop : (borderBottom ? borderBottom : (borderLeft ? borderLeft : borderRight)));
                if (borderAny)
                {
                    QC::Color parsed;
                    if (parseColorString(borderAny, parsed))
                    {
                        panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        panel->setBorderColor(parsed);
                        panel->setBorderWidth(1);
                        panel->setFrameVisible(true);
                    }
                }

                created = panel;
            }
            else if (QC::String::strcmp(type, "label") == 0)
            {
                const char *text = stringOrNull(controlValue->find("text"));
                auto *label = new QW::Controls::Label(m_desktopWindow, text ? text : "", bounds);
                label->setTransparent(true);
                if (const char *color = stringOrNull(controlValue->find("color")))
                {
                    QC::Color parsed;
                    if (parseColorString(color, parsed))
                        label->setTextColor(parsed);
                }
                created = label;
            }
            else if (QC::String::strcmp(type, "button") == 0)
            {
                const char *text = stringOrNull(controlValue->find("text"));
                const char *action = stringOrNull(controlValue->find("action"));
                auto *button = new QW::Controls::Button(m_desktopWindow, text ? text : "", bounds);
                button->setContentMode(QW::ButtonContentMode::Text);

                button->setRole(roleForJsonButton(id, controlValue));

                // Wire up known desktop actions.
                if (id && QC::String::strcmp(id, "btnTerminal") == 0)
                {
                    button->setClickHandler(onJsonTerminalClick, this);
                }
                else if ((id && QC::String::strcmp(id, "btnCMMS") == 0) ||
                         (action && equalsIgnoreCase(action, "cmms")))
                {
                    button->setClickHandler(onJsonCMMSClick, this);
                }
                else if (id && QC::String::strcmp(id, "shutDownButton") == 0)
                {
                    button->setClickHandler(onJsonShutdownClick, this);
                }

                created = button;
            }
            else if (QC::String::strcmp(type, "image") == 0)
            {
                auto *imageView = new QW::Controls::ImageView(m_desktopWindow, bounds);

                if (const QC::JSON::Value *visibleValue = controlValue->find("visible"))
                {
                    if (visibleValue->isBool())
                        imageView->setVisible(visibleValue->asBool(true));
                }

                if (const char *modeText = stringOrNull(controlValue->find("mode")))
                {
                    QG::ImageScaleMode mode = QG::ImageScaleMode::Stretch;
                    if (equalsIgnoreCase(modeText, "fit"))
                        mode = QG::ImageScaleMode::Fit;
                    else if (equalsIgnoreCase(modeText, "center"))
                        mode = QG::ImageScaleMode::Center;
                    else if (equalsIgnoreCase(modeText, "tile"))
                        mode = QG::ImageScaleMode::Tile;
                    else if (equalsIgnoreCase(modeText, "fill"))
                        mode = QG::ImageScaleMode::Fill;
                    else if (equalsIgnoreCase(modeText, "original"))
                        mode = QG::ImageScaleMode::Original;
                    imageView->setScaleMode(mode);
                }

                const char *path = stringOrNull(controlValue->find("path"));
                if (ImageAsset *asset = loadImageAsset(path))
                {
                    imageView->setImage(&asset->surface);
                }
                else if (path)
                {
                    const char *warnId = id ? id : "<unnamed>";
                    QC_LOG_WARN(LOG_MODULE, "Image control '%s' missing or failed to load '%s'", warnId, path);
                }

                created = imageView;
            }
            else if (QC::String::strcmp(type, "slider") == 0)
            {
                // Slider is backed by ScrollBar (horizontal by default)
                QW::Controls::ScrollOrientation orient = QW::Controls::ScrollOrientation::Horizontal;
                if (const char *orientText = stringOrNull(controlValue->find("orientation")))
                {
                    if (equalsIgnoreCase(orientText, "vertical"))
                        orient = QW::Controls::ScrollOrientation::Vertical;
                    else if (equalsIgnoreCase(orientText, "horizontal"))
                        orient = QW::Controls::ScrollOrientation::Horizontal;
                }

                auto *slider = new QW::Controls::ScrollBar(m_desktopWindow, bounds, orient);

                if (const QC::JSON::Value *clickToMaxV = controlValue->find("clickToMax"))
                {
                    if (clickToMaxV->isBool())
                        slider->setClickToMax(clickToMaxV->asBool(false));
                }

                if (const QC::JSON::Value *minV = controlValue->find("min"))
                {
                    if (minV->isNumber())
                        slider->setMinimum(static_cast<QC::i32>(minV->asNumber(0.0)));
                }

                if (const QC::JSON::Value *maxV = controlValue->find("max"))
                {
                    if (maxV->isNumber())
                        slider->setMaximum(static_cast<QC::i32>(maxV->asNumber(100.0)));
                }

                if (const QC::JSON::Value *valueV = controlValue->find("value"))
                {
                    if (valueV->isNumber())
                        slider->setValue(static_cast<QC::i32>(valueV->asNumber(0.0)));
                }

                if (const QC::JSON::Value *pageV = controlValue->find("pageSize"))
                {
                    if (pageV->isNumber())
                        slider->setPageSize(static_cast<QC::u32>(pageV->asNumber(10.0)));
                }

                if (const QC::JSON::Value *smallV = controlValue->find("smallStep"))
                {
                    if (smallV->isNumber())
                        slider->setSmallStep(static_cast<QC::i32>(smallV->asNumber(1.0)));
                }

                if (const QC::JSON::Value *largeV = controlValue->find("largeStep"))
                {
                    if (largeV->isNumber())
                        slider->setLargeStep(static_cast<QC::i32>(largeV->asNumber(10.0)));
                }

                if (const char *track = stringOrNull(controlValue->find("track")))
                {
                    QW::Color c;
                    if (parseHexColor(track, &c))
                        slider->setTrackColor(c);
                }

                if (const char *thumb = stringOrNull(controlValue->find("thumb")))
                {
                    QW::Color c;
                    if (parseHexColor(thumb, &c))
                        slider->setThumbColor(c);
                }

                if (const char *bg = stringOrNull(controlValue->find("background")))
                {
                    QW::Color c;
                    if (parseHexColor(bg, &c))
                        slider->setBackgroundColor(c);
                }

                if (const char *action = stringOrNull(controlValue->find("action")))
                {
                    if (equalsIgnoreCase(action, "openLogin"))
                    {
                        slider->setScrollChangeHandler(&onJsonSliderOpenLogin, this);
                    }
                }

                created = slider;
            }

            if (!created)
                return;

            if (id && *id)
            {
                created->setId(hashControlId(id));
            }

            // Track ownership
            m_jsonControls.push_back(created);

            // Root vs child
            if (parentPanel)
            {
                parentPanel->addChild(created);
            }
            else
            {
                m_jsonRootControls.push_back(created);
                if (m_desktopWindow && m_desktopWindow->root())
                {
                    // Required for input routing: Window::onEvent dispatches into root control.
                    m_desktopWindow->root()->addChild(created);
                }
            }

            // Capture well-known pointers used by Desktop logic
            if (id)
            {
                if (!m_topBar && QC::String::strcmp(id, "headerBar") == 0)
                    m_topBar = created->asPanel();
                if (!m_sidebar && QC::String::strcmp(id, "sidebar") == 0)
                    m_sidebar = created->asPanel();
                if (!m_taskbar && QC::String::strcmp(id, "taskbar") == 0)
                    m_taskbar = created->asPanel();

                if (!m_titleLabel && QC::String::strcmp(id, "headerTitle") == 0)
                    m_titleLabel = static_cast<QW::Controls::Label *>(created);
                if (!m_clockLabel && QC::String::strcmp(id, "clockLabel") == 0)
                    m_clockLabel = static_cast<QW::Controls::Label *>(created);
                // (logoButton is optional; not present in current desktop.json)

                if (!m_jsonStartButton && QC::String::strcmp(id, "startButton") == 0)
                    m_jsonStartButton = created;
                if (!m_jsonShutdownButton && QC::String::strcmp(id, "shutDownButton") == 0)
                    m_jsonShutdownButton = created;
            }

            // Recurse children for panels
            if (created->asPanel())
            {
                const QC::JSON::Value *children = controlValue->find("children");
                if (children && children->isArray())
                {
                    const QC::JSON::Array *arr = children->asArray();
                    for (QC::usize i = 0; i < arr->size(); ++i)
                    {
                        self(self, (*arr)[i], created->asPanel(), static_cast<QC::i32>(bounds.width), static_cast<QC::i32>(bounds.height));
                    }
                }
            }
        };

        const QC::JSON::Array *arr = controls->asArray();
        for (QC::usize i = 0; i < arr->size(); ++i)
        {
            buildControl(buildControl, (*arr)[i], nullptr, static_cast<QC::i32>(m_screenWidth), static_cast<QC::i32>(m_screenHeight));
        }

        if (m_jsonRootControls.size() == 0)
        {
            QC_LOG_WARN(LOG_MODULE, "desktop.json produced no controls; using hardcoded desktop\n");
            clearJsonDesktopState();
            return false;
        }

        recomputeTaskbarWindowBase();

        // Optional runtime overrides (validated early by BootGate if present in production).
        // This lets production change banner/layout/palette without rebuilding the golden desktop.
        {
            QC::JSON::Value ovrRoot;
            const char *ovrOpenedPath = nullptr;
            if (tryLoadDesktopOverridesFromVfs(ovrRoot, ovrOpenedPath) && ovrOpenedPath)
            {
                QC_LOG_INFO(LOG_MODULE, "Applying desktop overrides from %s\n", ovrOpenedPath);
                applyDesktopOverridesObject(ovrRoot, backgroundApplied);
            }
        }

        if (!backgroundApplied)
        {
            parseBackground(desktop->find("background"));
            backgroundApplied = true;
        }

        // Apply final theme/background selection to the JSON-driven chrome.
        applyColors();

        QC_LOG_INFO(LOG_MODULE, "Desktop initialized from /desktop.json (%u controls)\n", static_cast<unsigned>(m_jsonControls.size()));
        return true;
    }

    void Desktop::createTopBar()
    {
        // TopBar: full width, at top
        QW::Rect topBarBounds = {0, 0, m_screenWidth, TOP_BAR_HEIGHT};
        m_topBar = new QW::Controls::Panel(m_desktopWindow, topBarBounds);
        m_topBar->setBorderStyle(QW::Controls::BorderStyle::None);
        m_topBar->setFrameVisible(false);

        if (m_desktopWindow && m_desktopWindow->root())
        {
            m_desktopWindow->root()->addChild(m_topBar);
        }

        // Logo button (left)
        QW::Rect logoBounds = {8, 6, 20, 20};
        m_logoButton = new QW::Controls::Button(m_desktopWindow, "C", logoBounds);
        m_logoButton->setContentMode(QW::ButtonContentMode::Text);
        m_logoButton->setRole(QW::ButtonRole::Accent);
        m_topBar->addChild(m_logoButton);

        // Title label (center-ish)
        QW::Rect titleBounds = {40, 8, 200, 16};
        m_titleLabel = new QW::Controls::Label(m_desktopWindow, "CITADEL Desktop", titleBounds);
        m_topBar->addChild(m_titleLabel);

        // Clock label (right)
        QW::Rect clockBounds = {static_cast<QC::i32>(m_screenWidth) - 80, 8, 60, 16};
        m_clockLabel = new QW::Controls::Label(m_desktopWindow, "10:32", clockBounds);
        m_topBar->addChild(m_clockLabel);
    }

    void Desktop::createSidebar()
    {
        // Sidebar: left side, below topbar, above taskbar
        QW::Rect sidebarBounds = {
            0,
            static_cast<QC::i32>(TOP_BAR_HEIGHT),
            SIDEBAR_WIDTH,
            m_screenHeight - TOP_BAR_HEIGHT - TASKBAR_HEIGHT};
        m_sidebar = new QW::Controls::Panel(m_desktopWindow, sidebarBounds);
        m_sidebar->setBorderStyle(QW::Controls::BorderStyle::None);
        m_sidebar->setFrameVisible(false);

        if (m_desktopWindow && m_desktopWindow->root())
        {
            m_desktopWindow->root()->addChild(m_sidebar);
        }

        // Create sidebar buttons
        QC::u32 buttonHeight = 48;
        QC::u32 buttonMargin = 4;

        for (QC::u8 i = 0; i < static_cast<QC::u8>(SidebarItem::Count); ++i)
        {
            QC::i32 y;
            if (static_cast<SidebarItem>(i) == SidebarItem::Power)
            {
                // Power button at bottom
                y = static_cast<QC::i32>(sidebarBounds.height) - static_cast<QC::i32>(buttonHeight) - static_cast<QC::i32>(buttonMargin);
            }
            else
            {
                y = static_cast<QC::i32>(buttonMargin + i * (buttonHeight + buttonMargin));
            }

            QW::Rect btnBounds = {
                static_cast<QC::i32>(buttonMargin),
                y,
                SIDEBAR_WIDTH - buttonMargin * 2,
                buttonHeight};

            m_sidebarButtons[i] = new QW::Controls::Button(m_desktopWindow, SIDEBAR_LABELS[i], btnBounds);
            m_sidebarButtons[i]->setContentMode(QW::ButtonContentMode::Text);
            m_sidebarButtons[i]->setId(i + 100); // Use ID to identify which button
            m_sidebarButtons[i]->setClickHandler(onSidebarClick, this);
            m_sidebarButtons[i]->setRole(QW::ButtonRole::Sidebar);
            m_sidebar->addChild(m_sidebarButtons[i]);
        }

        updateSidebarButtonRoles();
    }

    void Desktop::createTaskbar()
    {
        // Taskbar: bottom, after sidebar
        QW::Rect taskbarBounds = {
            static_cast<QC::i32>(SIDEBAR_WIDTH),
            static_cast<QC::i32>(m_screenHeight - TASKBAR_HEIGHT),
            m_screenWidth - SIDEBAR_WIDTH,
            TASKBAR_HEIGHT};
        m_taskbar = new QW::Controls::Panel(m_desktopWindow, taskbarBounds);
        m_taskbar->setBorderStyle(QW::Controls::BorderStyle::None);
        m_taskbar->setFrameVisible(false);

        if (m_desktopWindow && m_desktopWindow->root())
        {
            m_desktopWindow->root()->addChild(m_taskbar);
        }
    }

    void Desktop::updateLayout()
    {
        if (!m_initialized)
            return;

        // Update topbar
        if (m_topBar)
        {
            QW::Rect topBarBounds = {0, 0, m_screenWidth, TOP_BAR_HEIGHT};
            m_topBar->setBounds(topBarBounds);

            // Update clock position
            if (m_clockLabel)
            {
                QW::Rect clockBounds = {static_cast<QC::i32>(m_screenWidth) - 80, 8, 60, 16};
                m_clockLabel->setBounds(clockBounds);
            }
        }

        // Update sidebar
        if (m_sidebar)
        {
            QW::Rect sidebarBounds = {
                0,
                static_cast<QC::i32>(TOP_BAR_HEIGHT),
                SIDEBAR_WIDTH,
                m_screenHeight - TOP_BAR_HEIGHT - TASKBAR_HEIGHT};
            m_sidebar->setBounds(sidebarBounds);
        }

        // Update taskbar
        if (m_taskbar)
        {
            QW::Rect taskbarBounds = {
                static_cast<QC::i32>(SIDEBAR_WIDTH),
                static_cast<QC::i32>(m_screenHeight - TASKBAR_HEIGHT),
                m_screenWidth - SIDEBAR_WIDTH,
                TASKBAR_HEIGHT};
            m_taskbar->setBounds(taskbarBounds);
        }
    }

    void Desktop::applyColors()
    {
        DesktopColors colors = currentColors();
        applyAccent(colors);
        applyThemeToDesktopColors(colors);

        // Apply to panels
        if (m_topBar)
        {
            if (!m_topBar->hasBackgroundOverride())
                m_topBar->setBackgroundColor(colors.topBarBg);
        }

        if (m_sidebar)
        {
            if (!m_sidebar->hasBackgroundOverride())
                m_sidebar->setBackgroundColor(colors.sidebarBg);
        }

        if (m_taskbar)
        {
            if (!m_taskbar->hasBackgroundOverride())
                m_taskbar->setBackgroundColor(colors.taskbarBg);
        }

        // Apply to labels
        if (m_titleLabel)
        {
            m_titleLabel->setTextColor(colors.topBarText);
            m_titleLabel->setBackgroundColor(QW::Color(0, 0, 0, 0)); // Transparent
        }

        if (m_clockLabel)
        {
            m_clockLabel->setTextColor(colors.topBarText);
            m_clockLabel->setBackgroundColor(QW::Color(0, 0, 0, 0));
        }

        if (m_logoButton)
        {
            m_logoButton->setRole(QW::ButtonRole::Accent);
        }

        updateSidebarButtonRoles();

        publishStyleSnapshot(colors);
    }

    void Desktop::publishStyleSnapshot(const DesktopColors &colors)
    {
        QW::StyleSnapshot snapshot = m_themeService.buildStyleSnapshot(colors, m_themeOverrides, accent());

        const char *family = m_themeService.resolvedFontFamily(m_themeOverrides);
        const bool familyChanged = !m_lastAppliedFontFamilySet || (QC::String::strcmp(m_lastAppliedFontFamily, family) != 0);
        if (familyChanged)
        {
            m_lastAppliedFontFamilySet = true;
            QC::String::strncpy(m_lastAppliedFontFamily, family, sizeof(m_lastAppliedFontFamily) - 1);
            m_lastAppliedFontFamily[sizeof(m_lastAppliedFontFamily) - 1] = '\0';

            if (equalsIgnoreCase(family, "System"))
            {
                QG::FontManager::instance().clearDefaultFont();
            }
            else
            {
                QC::Vector<QC::u8> bytes;
                if (tryLoadFontFamilyFromVfs(family, bytes))
                {
                    if (QG::FontManager::instance().setDefaultFontFromBytes(bytes))
                    {
                        QC_LOG_INFO(LOG_MODULE, "Theme font loaded: %s (%u bytes)\n", family, static_cast<QC::u32>(bytes.size()));
                    }
                    else
                    {
                        QC_LOG_WARN(LOG_MODULE, "Theme font init failed: %s\n", family);
                        QG::FontManager::instance().clearDefaultFont();
                    }
                }
                else
                {
                    QC_LOG_WARN(LOG_MODULE, "Theme font not found in VFS: %s\n", family);
                    QG::FontManager::instance().clearDefaultFont();
                }
            }
        }

        QW::StyleSystem::instance().setStyle(snapshot);
    }

    void Desktop::updateSidebarButtonRoles()
    {
        for (QC::u8 i = 0; i < static_cast<QC::u8>(SidebarItem::Count); ++i)
        {
            auto *button = m_sidebarButtons[i];
            if (!button)
                continue;

            SidebarItem item = static_cast<SidebarItem>(i);
            QW::ButtonRole role = (item == SidebarItem::Power) ? QW::ButtonRole::Destructive
                                                               : QW::ButtonRole::Sidebar;
            if (item == m_selectedSidebarItem)
            {
                role = QW::ButtonRole::SidebarSelected;
            }
            button->setRole(role);
        }
    }

    QW::Rect Desktop::workArea() const
    {
        return {
            static_cast<QC::i32>(SIDEBAR_WIDTH),
            static_cast<QC::i32>(TOP_BAR_HEIGHT),
            m_screenWidth - SIDEBAR_WIDTH,
            m_screenHeight - TOP_BAR_HEIGHT - TASKBAR_HEIGHT};
    }

    void Desktop::setTime(QC::u32 hours, QC::u32 minutes)
    {
        m_hours = hours % 24;
        m_minutes = minutes % 60;

        if (m_clockLabel)
        {
            char timeStr[16];
            QC::u32 displayHour = m_hours;
            timeStr[0] = '0' + (displayHour / 10);
            timeStr[1] = '0' + (displayHour % 10);
            timeStr[2] = ':';
            timeStr[3] = '0' + (m_minutes / 10);
            timeStr[4] = '0' + (m_minutes % 10);
            timeStr[5] = '\0';

            m_clockLabel->setText(timeStr);
        }
    }

    void Desktop::setFocusedWindowTitle(const char *title)
    {
        if (m_titleLabel)
        {
            m_titleLabel->setText(title ? title : "CITADEL Desktop");
        }
    }
    void Desktop::addTaskbarWindow(QC::u32 windowId, const char *title)
    {
        if (!m_taskbar || windowId == 0)
            return;

        ensureWindowEventListener();

        // Defensive: avoid duplicate entries for the same window.
        removeTaskbarWindow(windowId);

        if (m_taskbarWindowCount >= MAX_TASKBAR_WINDOWS)
            return;

        static constexpr QC::u32 kTaskbarButtonWidth = 140;
        static constexpr QC::u32 kTaskbarEntryHeight = 32;
        static constexpr QC::u32 kTaskbarIconSize = 32;

        auto resolveTaskbarIconPath = [&](const char *windowTitle, char *outPath, QC::usize outCap) -> bool
        {
            if (!outPath || outCap == 0)
                return false;

            outPath[0] = '\0';
            if (!windowTitle || !*windowTitle)
                return false;

            const char *iconToken = nullptr;
            if (equalsIgnoreCase(windowTitle, "Terminal"))
                iconToken = "terminal";
            else if (equalsIgnoreCase(windowTitle, "Browser") || equalsIgnoreCase(windowTitle, "HTML Viewer"))
                iconToken = "file";
            else if (equalsIgnoreCase(windowTitle, "CUI-ML"))
                iconToken = "file";
            else if (equalsIgnoreCase(windowTitle, "Citadel Management Studio"))
                iconToken = "settings";

            if (!iconToken)
                return false;

            {
                const char *prefix = "/ICONS/svg/";
                const char *suffix = ".svg";
                const QC::usize preLen = QC::String::strlen(prefix);
                const QC::usize tokenLen = QC::String::strlen(iconToken);
                const QC::usize sufLen = QC::String::strlen(suffix);
                if (preLen + tokenLen + sufLen + 1 <= outCap)
                {
                    QC::String::memcpy(outPath, prefix, preLen);
                    QC::String::memcpy(outPath + preLen, iconToken, tokenLen);
                    QC::String::memcpy(outPath + preLen + tokenLen, suffix, sufLen);
                    outPath[preLen + tokenLen + sufLen] = '\0';

                    if (QFS::File *file = QFS::VFS::instance().open(outPath, QFS::OpenMode::Read))
                    {
                        QFS::VFS::instance().close(file);
                        return true;
                    }
                }
            }

            char upper[16];
            QC::String::memset(upper, 0, sizeof(upper));
            QC::usize upperLen = 0;
            for (const char *p = iconToken; *p && upperLen + 1 < sizeof(upper); ++p)
            {
                char c = *p;
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
                upper[upperLen++] = c;
            }
            upper[upperLen] = '\0';

            const char *prefix = "/ICONS/";
            const char *suffix = ".PNG";
            const QC::usize preLen = QC::String::strlen(prefix);
            const QC::usize sufLen = QC::String::strlen(suffix);
            if (preLen + upperLen + sufLen + 1 > outCap)
                return false;

            QC::String::memcpy(outPath, prefix, preLen);
            QC::String::memcpy(outPath + preLen, upper, upperLen);
            QC::String::memcpy(outPath + preLen + upperLen, suffix, sufLen);
            outPath[preLen + upperLen + sufLen] = '\0';
            return true;
        };

        char iconPath[192];
        QC::String::memset(iconPath, 0, sizeof(iconPath));
        ImageAsset *taskbarIcon = resolveTaskbarIconPath(title, iconPath, sizeof(iconPath)) ? loadImageAsset(iconPath) : nullptr;
        const bool useIconButton = (taskbarIcon != nullptr);

        m_taskbarEntries[m_taskbarWindowCount].windowId = windowId;
        m_taskbarEntries[m_taskbarWindowCount].button = nullptr;
        m_taskbarEntries[m_taskbarWindowCount].width = useIconButton ? kTaskbarIconSize : kTaskbarButtonWidth;
        m_taskbarEntries[m_taskbarWindowCount].height = kTaskbarEntryHeight;
        m_taskbarEntries[m_taskbarWindowCount].isActive = false;

        if (useIconButton)
        {
            QW::Rect btnBounds = {0, 0, kTaskbarIconSize, kTaskbarEntryHeight};
            auto *btn = new QW::Controls::Button(m_desktopWindow, nullptr, btnBounds);
            btn->setContentMode(QW::ButtonContentMode::Icon);
            btn->setVariant(QW::ButtonVariant::Icon);
            btn->setId(windowId);
            btn->setTooltipText(title);
            btn->setClickHandler(onTaskbarIconButtonClick, this);
            btn->setRole(QW::ButtonRole::Taskbar);

            btn->setIcon(&taskbarIcon->surface);

            m_taskbar->addChild(btn);
            m_taskbarEntries[m_taskbarWindowCount].button = btn;
        }
        else
        {
            QW::Rect btnBounds = {0, 0, kTaskbarButtonWidth, kTaskbarEntryHeight};
            auto *btn = new QW::Controls::Button(m_desktopWindow, title ? title : "Window", btnBounds);
            btn->setContentMode(QW::ButtonContentMode::Text);
            btn->setId(windowId);
            btn->setClickHandler(onTaskbarClick, this);
            btn->setRole(QW::ButtonRole::Taskbar);

            m_taskbar->addChild(btn);
            m_taskbarEntries[m_taskbarWindowCount].button = btn;
        }

        ++m_taskbarWindowCount;
        layoutTaskbarWindows();

        // Ensure the taskbar is repainted immediately (avoid stale pixels until next input event).
        m_desktopWindow->invalidateRect(m_taskbar->absoluteBounds());
        QW::WindowManager::instance().render();
    }

    void Desktop::removeTaskbarWindow(QC::u32 windowId)
    {
        for (QC::u32 i = 0; i < m_taskbarWindowCount; ++i)
        {
            if (m_taskbarEntries[i].windowId == windowId)
            {
                const QW::Rect dirty = m_taskbar ? m_taskbar->absoluteBounds() : QW::Rect{0, 0, 0, 0};

                // Remove from panel
                if (m_taskbar && m_taskbarEntries[i].button)
                {
                    m_taskbar->removeChild(m_taskbarEntries[i].button);
                    delete m_taskbarEntries[i].button;
                }

                // Shift remaining entries
                for (QC::u32 j = i; j < m_taskbarWindowCount - 1; ++j)
                {
                    m_taskbarEntries[j] = m_taskbarEntries[j + 1];
                }

                --m_taskbarWindowCount;
                m_taskbarEntries[m_taskbarWindowCount].windowId = 0;
                m_taskbarEntries[m_taskbarWindowCount].button = nullptr;
                m_taskbarEntries[m_taskbarWindowCount].width = 0;
                m_taskbarEntries[m_taskbarWindowCount].height = 0;
                m_taskbarEntries[m_taskbarWindowCount].isActive = false;

                layoutTaskbarWindows();

                // Taskbar pixels can otherwise remain on the framebuffer until a later repaint.
                if (dirty.width && dirty.height)
                {
                    m_desktopWindow->invalidateRect(dirty);
                    QW::WindowManager::instance().render();
                }

                return;
            }
        }
    }

    void Desktop::setActiveTaskbarWindow(QC::u32 windowId)
    {
        for (QC::u32 i = 0; i < m_taskbarWindowCount; ++i)
        {
            bool isActive = (m_taskbarEntries[i].windowId == windowId);
            m_taskbarEntries[i].isActive = isActive;

            if (m_taskbarEntries[i].button)
                m_taskbarEntries[i].button->setRole(isActive ? QW::ButtonRole::TaskbarActive
                                                             : QW::ButtonRole::Taskbar);
        }
    }

    void Desktop::layoutTaskbarWindows()
    {
        if (!m_taskbar)
            return;

        static constexpr QC::u32 kGap = 4;
        QC::i32 x = m_taskbarWindowBaseX;

        for (QC::u32 i = 0; i < m_taskbarWindowCount; ++i)
        {
            const QC::u32 w = (m_taskbarEntries[i].width != 0) ? m_taskbarEntries[i].width : 140;
            const QC::u32 h = (m_taskbarEntries[i].height != 0) ? m_taskbarEntries[i].height : 32;
            const QC::i32 y = static_cast<QC::i32>((TASKBAR_HEIGHT - h) / 2);
            const QW::Rect b = {x, y, w, h};

            if (m_taskbarEntries[i].button)
                m_taskbarEntries[i].button->setBounds(b);

            x += static_cast<QC::i32>(w + kGap);
        }

        // Ensure any moved/resized entries are repainted.
        m_taskbar->invalidate();
    }

    bool Desktop::onWindowEvent(const QK::Event::Event &event, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return false;

        if (event.type() == QK::Event::Type::WindowDestroy)
        {
            const auto &we = event.asWindow();
            desktop->removeTaskbarWindow(we.windowId);
        }

        return false;
    }

    void Desktop::ensureWindowEventListener()
    {
        if (m_windowListenerId != QK::Event::InvalidListenerId)
            return;

        auto &eventMgr = QK::Event::EventManager::instance();
        if (!eventMgr.isInitialized())
            return;

        m_windowListenerId = eventMgr.addListener(QK::Event::Category::Window, &Desktop::onWindowEvent, this);
    }

    bool Desktop::onInputEvent(const QK::Event::Event &event, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop || event.type() != QK::Event::Type::KeyDown)
            return false;

        const auto &key = event.asKey();
        if (QK::Event::hasModifier(key.modifiers, QK::Event::Modifiers::Ctrl) ||
            QK::Event::hasModifier(key.modifiers, QK::Event::Modifiers::Alt))
        {
            return false;
        }

        if (key.character == 't' || key.character == 'T')
        {
            desktop->toggleTerminal();
            return true;
        }

        return false;
    }

    void Desktop::ensureInputEventListener()
    {
        if (m_inputListenerId != QK::Event::InvalidListenerId)
            return;

        auto &eventMgr = QK::Event::EventManager::instance();
        if (!eventMgr.isInitialized())
            return;

        m_inputListenerId = eventMgr.addListener(QK::Event::Category::Input, &Desktop::onInputEvent, this);
    }

    // ==================== Rendering ====================

    void Desktop::paint()
    {
        if (!m_desktopWindow)
            return;

        // Paint background gradient
        paintBackground();

        QW::Controls::PaintContext paintContext{};
        paintContext.window = m_desktopWindow;
        paintContext.styleRenderer = m_desktopWindow ? m_desktopWindow->styleRenderer() : nullptr;
        paintContext.painter = m_desktopWindow ? m_desktopWindow->painter() : nullptr;

        if (m_jsonDriven)
        {
            for (QC::usize i = 0; i < m_jsonRootControls.size(); ++i)
            {
                if (m_jsonRootControls[i])
                    m_jsonRootControls[i]->paint(paintContext);
            }
            return;
        }

        // Paint panels (they paint their children)
        if (m_topBar)
            m_topBar->paint(paintContext);
        if (m_sidebar)
            m_sidebar->paint(paintContext);
        if (m_taskbar)
            m_taskbar->paint(paintContext);
    }

    void Desktop::paintBackground()
    {
        if (!m_desktopWindow)
            return;

        const auto &style = QW::StyleSystem::instance().currentStyle();
        QW::Color top = m_backgroundConfig.topOverride ? m_backgroundConfig.topColor : style.palette.desktopBackgroundTop;
        QW::Color bottom = m_backgroundConfig.bottomOverride ? m_backgroundConfig.bottomColor : style.palette.desktopBackgroundBottom;

        QW::Rect bounds = {0, 0, m_screenWidth, m_screenHeight};
        QG::IPainter *painter = m_desktopWindow->painter();
        if (!painter)
            return;

        if (top == bottom)
        {
            painter->fillRect(bounds, top);
        }
        else
        {
            painter->fillGradientV(bounds, top, bottom);
        }

        if (m_backgroundConfig.mode == BackgroundMode::Image && m_backgroundConfig.image && m_backgroundConfig.image->surface.isValid())
        {
            QG::blitImage(painter,
                          m_backgroundConfig.image->surface,
                          bounds,
                          m_backgroundConfig.scaleMode,
                          m_backgroundScratch);
        }
    }

    // ==================== Callbacks ====================

    void Desktop::onSidebarClick(QW::Controls::Button *button, void *userData)
    {
        if (!button || !userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        QC::u32 id = button->id();

        if (id >= 100 && id < 100 + static_cast<QC::u32>(SidebarItem::Count))
        {
            desktop->m_selectedSidebarItem = static_cast<SidebarItem>(id - 100);

            if (desktop->m_selectedSidebarItem == SidebarItem::Terminal)
            {
                desktop->toggleTerminal();
            }
            else if (desktop->m_selectedSidebarItem == SidebarItem::Settings)
            {
                desktop->cycleThemeFromSettings();
            }
            else if (desktop->m_selectedSidebarItem == SidebarItem::Power)
            {
                QK::Event::EventManager::instance().postShutdownEvent(
                    QK::Event::Type::ShutdownRequest,
                    static_cast<QC::u32>(QK::Shutdown::Reason::SidebarPowerButton));
            }

            desktop->updateSidebarButtonRoles();
        }
    }

    bool Desktop::onShutdownRequested(QK::Shutdown::Reason reason, void *userData)
    {
        Desktop *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return false;

        desktop->showShutdownPrompt(reason);
        return true;
    }

    void Desktop::onJsonTerminalClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        if (!userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        desktop->toggleTerminal();
    }

    void Desktop::onJsonShutdownClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        (void)userData;

        QK::Event::EventManager::instance().postShutdownEvent(
            QK::Event::Type::ShutdownRequest,
            static_cast<QC::u32>(QK::Shutdown::Reason::UserRequest));
    }

    void Desktop::onJsonSettingsClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        if (!userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        desktop->cycleThemeFromSettings();
    }

    void Desktop::onJsonCMMSClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        if (!userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        desktop->openCMMS();
    }

    void Desktop::onJsonTerminalButtonClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        if (!userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        desktop->toggleTerminal();
    }

    void Desktop::onJsonShutdownButtonClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        (void)userData;

        QK::Event::EventManager::instance().postShutdownEvent(
            QK::Event::Type::ShutdownRequest,
            static_cast<QC::u32>(QK::Shutdown::Reason::UserRequest));
    }

    void Desktop::onJsonSettingsButtonClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        if (!userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        desktop->cycleThemeFromSettings();
    }

    void Desktop::onJsonCMMSButtonClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        if (!userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        desktop->openCMMS();
    }

    void Desktop::onTaskbarClick(QW::Controls::Button *button, void *userData)
    {
        if (!button || !userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        QC::u32 windowId = button->id();

        // Activate this window
        desktop->setActiveTaskbarWindow(windowId);

        // Bring the selected window back to the front.
        QW::Window *window = QW::WindowManager::instance().windowById(windowId);
        if (window)
        {
            window->setVisible(true);
            QW::WindowManager::instance().bringToFront(window);
        }
    }

    void Desktop::onTaskbarIconButtonClick(QW::Controls::Button *button, void *userData)
    {
        if (!button || !userData)
            return;

        Desktop *desktop = static_cast<Desktop *>(userData);
        QC::u32 windowId = button->id();

        desktop->setActiveTaskbarWindow(windowId);

        QW::Window *window = QW::WindowManager::instance().windowById(windowId);
        if (window)
        {
            window->setVisible(true);
            QW::WindowManager::instance().bringToFront(window);
        }
    }

} // namespace QD
