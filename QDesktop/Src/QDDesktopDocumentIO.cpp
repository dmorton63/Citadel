#include "QDDesktopDocumentIO.h"

#include "QDDesktopDocumentValidation.h"

#include "QCJson.h"
#include "QCString.h"
#include "QDTheme.h"

namespace QD
{
    namespace
    {
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
        constexpr const char *CMMS_DESKTOP_LAYOUT_ASSET_TABLE = "DesktopLayoutAssets";
        constexpr const char *CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE = "DesktopControlHierarchy";
        constexpr const char *CMMS_DESKTOP_LAYOUT_PRODUCTION = "production";
        constexpr const char *CMMS_DESKTOP_LAYOUT_GOLDEN = "golden";
        constexpr QC::u32 CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES = 1024;
        constexpr QC::u32 MAX_DESKTOP_EXPORT_TREE_DEPTH = 32;

        struct ParentFrame
        {
            char id[64]{};
        };

        inline const char *stringOrNull(const QC::JSON::Value *value)
        {
            return (value && value->isString()) ? value->asString(nullptr) : nullptr;
        }

        void copyText(char *dst, QC::usize dstSize, const char *src)
        {
            if (!dst || dstSize == 0)
                return;
            if (!src)
            {
                dst[0] = '\0';
                return;
            }
            QC::String::strncpy(dst, src, dstSize - 1);
            dst[dstSize - 1] = '\0';
        }

        char toLowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        }

        bool equalsIgnoreCaseAscii(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                if (toLowerAscii(*a) != toLowerAscii(*b))
                    return false;
                ++a;
                ++b;
            }
            return *a == '\0' && *b == '\0';
        }

        bool startsWithIgnoreCaseAscii(const char *text, const char *prefix)
        {
            if (!text || !prefix)
                return false;
            while (*prefix)
            {
                if (!*text || toLowerAscii(*text) != toLowerAscii(*prefix))
                    return false;
                ++text;
                ++prefix;
            }
            return true;
        }

        bool containsIgnoreCaseAscii(const char *text, const char *needle)
        {
            if (!text || !needle || !*needle)
                return false;
            for (const char *cursor = text; *cursor; ++cursor)
            {
                const char *lhs = cursor;
                const char *rhs = needle;
                while (*lhs && *rhs && toLowerAscii(*lhs) == toLowerAscii(*rhs))
                {
                    ++lhs;
                    ++rhs;
                }
                if (*rhs == '\0')
                    return true;
            }
            return false;
        }

        const char *skipWs(const char *text)
        {
            while (text && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n'))
                ++text;
            return text;
        }

        const char *findStr(const char *haystack, const char *needle)
        {
            if (!haystack || !needle || !*needle)
                return nullptr;
            for (const char *cursor = haystack; *cursor; ++cursor)
            {
                const char *lhs = cursor;
                const char *rhs = needle;
                while (*lhs && *rhs && *lhs == *rhs)
                {
                    ++lhs;
                    ++rhs;
                }
                if (*rhs == '\0')
                    return cursor;
            }
            return nullptr;
        }

        bool tryParseI32(const char *text, QC::i32 &outValue)
        {
            outValue = 0;
            if (!text || !*text)
                return false;

            bool negative = false;
            QC::usize i = 0;
            if (text[0] == '-')
            {
                negative = true;
                i = 1;
            }
            if (!text[i])
                return false;

            QC::i32 value = 0;
            for (; text[i]; ++i)
            {
                if (text[i] < '0' || text[i] > '9')
                    return false;
                value = static_cast<QC::i32>(value * 10 + (text[i] - '0'));
            }
            outValue = negative ? -value : value;
            return true;
        }

        DesktopControlKind controlKindFromText(const char *type)
        {
            if (!type || !*type)
                return DesktopControlKind::Unknown;
            if (equalsIgnoreCaseAscii(type, "panel") || containsIgnoreCaseAscii(type, "panel"))
                return DesktopControlKind::Panel;
            if (equalsIgnoreCaseAscii(type, "button") || containsIgnoreCaseAscii(type, "button"))
                return DesktopControlKind::Button;
            if (equalsIgnoreCaseAscii(type, "label") || containsIgnoreCaseAscii(type, "label"))
                return DesktopControlKind::Label;
            if (equalsIgnoreCaseAscii(type, "image") || containsIgnoreCaseAscii(type, "image"))
                return DesktopControlKind::Image;
            if (equalsIgnoreCaseAscii(type, "slider") || containsIgnoreCaseAscii(type, "slider") || containsIgnoreCaseAscii(type, "scrollbar"))
                return DesktopControlKind::Slider;
            if (equalsIgnoreCaseAscii(type, "container") || containsIgnoreCaseAscii(type, "container"))
                return DesktopControlKind::Container;
            if (containsIgnoreCaseAscii(type, "clock"))
                return DesktopControlKind::Clock;
            return DesktopControlKind::Custom;
        }

        bool isPanelLikeKind(DesktopControlKind kind)
        {
            return kind == DesktopControlKind::Panel || kind == DesktopControlKind::Container;
        }

        void setImportError(DesktopDocumentImportResult &outResult, const char *error)
        {
            copyText(outResult.error, sizeof(outResult.error), error);
            outResult.loaded = false;
        }

        void initializeDocument(DesktopDocumentImportResult &outResult,
                                const char *documentId,
                                const char *sourcePath,
                                DesktopDocumentFormat format)
        {
            outResult.loaded = false;
            outResult.document = DesktopDocument{};
            outResult.error[0] = '\0';
            copyText(outResult.sourcePath, sizeof(outResult.sourcePath), sourcePath);
            copyText(outResult.document.documentId, sizeof(outResult.document.documentId), documentId ? documentId : "desktop");
            copyText(outResult.document.displayName, sizeof(outResult.document.displayName), documentId ? documentId : "desktop");
            copyText(outResult.document.metadata.sourcePath, sizeof(outResult.document.metadata.sourcePath), sourcePath);
            outResult.document.format = format;
        }

        void addProperty(DesktopControlModel &control, const char *key, const char *value)
        {
            if (!key || !*key || !value || !*value)
                return;
            DesktopProperty property{};
            copyText(property.key, sizeof(property.key), key);
            copyText(property.value, sizeof(property.value), value);
            control.properties.push_back(property);
        }

        void addBinding(DesktopControlModel &control, const char *eventName, const char *action, const char *argument)
        {
            if ((!eventName || !*eventName) && (!action || !*action))
                return;
            DesktopBinding binding{};
            copyText(binding.event, sizeof(binding.event), eventName ? eventName : "click");
            copyText(binding.action, sizeof(binding.action), action ? action : "");
            copyText(binding.argument, sizeof(binding.argument), argument ? argument : "");
            control.bindings.push_back(binding);
        }

        void assignExprOrNumber(const char *raw, const char *propertyKey, QC::i32 &target, DesktopControlModel &control)
        {
            if (!raw || !*raw)
                return;

            QC::i32 parsed = 0;
            if (tryParseI32(raw, parsed))
            {
                target = parsed;
            }
            else
            {
                addProperty(control, propertyKey, raw);
            }
        }

        void assignExprOrUnsigned(const char *raw, const char *propertyKey, QC::u32 &target, DesktopControlModel &control)
        {
            if (!raw || !*raw)
                return;

            QC::i32 parsed = 0;
            if (tryParseI32(raw, parsed) && parsed >= 0)
            {
                target = static_cast<QC::u32>(parsed);
            }
            else
            {
                addProperty(control, propertyKey, raw);
            }
        }

        bool parseAttrValue(const char *tagRaw, const char *key, char *out, QC::usize outSize)
        {
            if (!tagRaw || !key || !out || outSize == 0)
                return false;
            out[0] = '\0';

            const char *cursor = tagRaw;
            const QC::usize keyLen = QC::String::strlen(key);
            while (*cursor)
            {
                if ((*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') && keyLen > 0)
                {
                    bool match = true;
                    for (QC::usize i = 0; i < keyLen; ++i)
                    {
                        if (cursor[1 + i] != key[i])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (!match)
                    {
                        ++cursor;
                        continue;
                    }

                    const char *valueStart = cursor + 1 + keyLen;
                    valueStart = skipWs(valueStart);
                    if (*valueStart != '=')
                    {
                        ++cursor;
                        continue;
                    }

                    ++valueStart;
                    valueStart = skipWs(valueStart);
                    if (*valueStart != '"' && *valueStart != '\'')
                    {
                        ++cursor;
                        continue;
                    }

                    const char quote = *valueStart++;
                    QC::usize length = 0;
                    while (valueStart[length] && valueStart[length] != quote && length + 1 < outSize)
                    {
                        out[length] = valueStart[length];
                        ++length;
                    }
                    out[length] = '\0';
                    return length > 0;
                }
                ++cursor;
            }
            return false;
        }

        void inferThemeIdFromStylesheetHref(const char *href, DesktopDocument &document)
        {
            if (!href || !*href || document.themeRef.themeId[0])
                return;

            const char *fileName = href;
            for (const char *cursor = href; *cursor; ++cursor)
            {
                if (*cursor == '/' || *cursor == '\\')
                    fileName = cursor + 1;
            }

            char stem[64]{};
            QC::usize length = 0;
            while (fileName[length] && fileName[length] != '.' && length + 1 < sizeof(stem))
            {
                char c = fileName[length];
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
                stem[length] = c;
                ++length;
            }
            stem[length] = '\0';

            if (!stem[0])
                return;

            ThemeID themeId{};
            if (themeIdFromString(stem, &themeId))
                copyText(document.themeRef.themeId, sizeof(document.themeRef.themeId), themeIdToString(themeId));
        }

        void applyCommonControlFields(DesktopControlModel &control,
                                      const char *id,
                                      const char *parentId,
                                      const char *text,
                                      const char *styleClass,
                                      const char *role,
                                      const char *icon,
                                      const char *action)
        {
            copyText(control.id, sizeof(control.id), id);
            copyText(control.parentId, sizeof(control.parentId), parentId);
            copyText(control.text, sizeof(control.text), text);
            copyText(control.styleClass, sizeof(control.styleClass), styleClass);
            copyText(control.name, sizeof(control.name), id && *id ? id : desktopControlKindName(control.kind));

            if (role && *role)
                addProperty(control, "role", role);

            if (icon && *icon)
            {
                if (icon[0] == '/')
                {
                    copyText(control.iconRef.path, sizeof(control.iconRef.path), icon);
                    control.iconRef.kind = DesktopAssetKind::Icon;
                }
                else
                {
                    addProperty(control, "iconAlias", icon);
                }
            }

            if (action && *action)
                addBinding(control, "click", action, nullptr);
        }

        void importJsonControlRecursive(const QC::JSON::Value *controlValue,
                                        const char *parentId,
                                        DesktopDocument &document)
        {
            if (!controlValue || !controlValue->isObject())
                return;

            const char *type = stringOrNull(controlValue->find("type"));
            if (!type || !*type)
                return;

            DesktopControlModel control{};
            control.kind = controlKindFromText(type);

            const char *id = stringOrNull(controlValue->find("id"));
            const char *text = stringOrNull(controlValue->find("text"));
            const char *styleClass = stringOrNull(controlValue->find("class"));
            const char *role = stringOrNull(controlValue->find("role"));
            const char *icon = stringOrNull(controlValue->find("icon"));
            const char *action = stringOrNull(controlValue->find("action"));

            applyCommonControlFields(control, id, parentId, text, styleClass, role, icon, action);
            addProperty(control, "type", type);

            if (const QC::JSON::Value *x = controlValue->find("x"))
            {
                if (x->isNumber())
                    control.layout.x = static_cast<QC::i32>(x->asNumber(0.0));
                else if (x->isString())
                    assignExprOrNumber(x->asString(nullptr), "xExpr", control.layout.x, control);
            }

            if (const QC::JSON::Value *y = controlValue->find("y"))
            {
                if (y->isNumber())
                    control.layout.y = static_cast<QC::i32>(y->asNumber(0.0));
                else if (y->isString())
                    assignExprOrNumber(y->asString(nullptr), "yExpr", control.layout.y, control);
            }

            if (const QC::JSON::Value *width = controlValue->find("width"))
            {
                if (width->isNumber())
                    control.layout.width = static_cast<QC::u32>(width->asNumber(0.0));
                else if (width->isString())
                    assignExprOrUnsigned(width->asString(nullptr), "widthExpr", control.layout.width, control);
            }

            if (const QC::JSON::Value *height = controlValue->find("height"))
            {
                if (height->isNumber())
                    control.layout.height = static_cast<QC::u32>(height->asNumber(0.0));
                else if (height->isString())
                    assignExprOrUnsigned(height->asString(nullptr), "heightExpr", control.layout.height, control);
            }

            if (const QC::JSON::Value *visible = controlValue->find("visible"); visible && visible->isBool())
                control.visible = visible->asBool(true);
            if (const QC::JSON::Value *enabled = controlValue->find("enabled"); enabled && enabled->isBool())
                control.enabled = enabled->asBool(true);

            const char *font = stringOrNull(controlValue->find("font"));
            if (font && *font)
                addProperty(control, "font", font);

            const char *background = stringOrNull(controlValue->find("background"));
            if (background && *background)
                addProperty(control, "background", background);

            const char *border = stringOrNull(controlValue->find("border"));
            if (border && *border)
                addProperty(control, "border", border);

            document.controls.push_back(control);

            const QC::JSON::Value *children = controlValue->find("children");
            if (children && children->isArray())
            {
                const QC::JSON::Array *array = children->asArray();
                for (QC::usize i = 0; i < array->size(); ++i)
                    importJsonControlRecursive((*array)[i], control.id, document);
            }
        }

        QCQL::Cell makeTextCell(const char *text)
        {
            QCQL::Cell cell{};
            cell.type = QCQL::ColumnType::Text;
            if (!text)
                return cell;

            const QC::usize length = QC::String::strlen(text);
            for (QC::usize i = 0; i < length; ++i)
                cell.bytes.push_back(static_cast<QC::u8>(text[i]));
            return cell;
        }

        QCQL::Cell makeUnsignedTextCell(QC::u32 value)
        {
            char reversed[16]{};
            QC::usize count = 0;
            do
            {
                reversed[count++] = static_cast<char>('0' + (value % 10u));
                value /= 10u;
            } while (value != 0 && count + 1 < sizeof(reversed));

            char text[16]{};
            for (QC::usize i = 0; i < count; ++i)
                text[i] = reversed[count - 1 - i];
            text[count] = '\0';
            return makeTextCell(text);
        }

        bool copyCellText(const QCQL::Cell &cell, char *dst, QC::usize dstSize)
        {
            if (!dst || dstSize == 0 || cell.type != QCQL::ColumnType::Text)
                return false;

            const QC::usize copyLen = (cell.bytes.size() < (dstSize - 1)) ? cell.bytes.size() : (dstSize - 1);
            for (QC::usize i = 0; i < copyLen; ++i)
                dst[i] = static_cast<char>(cell.bytes[i]);
            dst[copyLen] = '\0';
            return copyLen > 0;
        }

        bool cellMatchesText(const QCQL::Cell &cell, const char *text)
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

        bool parseUnsignedTextCell(const QCQL::Cell &cell, QC::u32 &outValue)
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

        const QCQL::Table *findTableByName(const QCQL::Database &database, const char *tableName)
        {
            if (!tableName || !*tableName)
                return nullptr;

            QC::u32 tableId = 0;
            if (QCQL::Engine::instance().lookupTableId(database, tableName, tableId) != QCQL::Status::Success)
                return nullptr;

            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (database.tables[i].tableId == tableId)
                    return &database.tables[i];
            }
            return nullptr;
        }

        bool parseBoolTextCell(const QCQL::Cell &cell, bool &outValue)
        {
            char text[16]{};
            if (!copyCellText(cell, text, sizeof(text)))
                return false;
            if (equalsIgnoreCaseAscii(text, "true") || equalsIgnoreCaseAscii(text, "1") || equalsIgnoreCaseAscii(text, "yes"))
            {
                outValue = true;
                return true;
            }
            if (equalsIgnoreCaseAscii(text, "false") || equalsIgnoreCaseAscii(text, "0") || equalsIgnoreCaseAscii(text, "no"))
            {
                outValue = false;
                return true;
            }
            return false;
        }

        DesktopBackgroundMode backgroundModeFromText(const char *text)
        {
            if (!text || !*text)
                return DesktopBackgroundMode::None;
            if (equalsIgnoreCaseAscii(text, "solid"))
                return DesktopBackgroundMode::Solid;
            if (equalsIgnoreCaseAscii(text, "gradient"))
                return DesktopBackgroundMode::Gradient;
            if (equalsIgnoreCaseAscii(text, "image"))
                return DesktopBackgroundMode::Image;
            return DesktopBackgroundMode::None;
        }

        DesktopAssetKind assetKindFromText(const char *text)
        {
            if (!text || !*text)
                return DesktopAssetKind::Unknown;
            if (equalsIgnoreCaseAscii(text, "wallpaper"))
                return DesktopAssetKind::Wallpaper;
            if (equalsIgnoreCaseAscii(text, "icon"))
                return DesktopAssetKind::Icon;
            if (equalsIgnoreCaseAscii(text, "illustration"))
                return DesktopAssetKind::Illustration;
            if (equalsIgnoreCaseAscii(text, "font"))
                return DesktopAssetKind::Font;
            if (equalsIgnoreCaseAscii(text, "import"))
                return DesktopAssetKind::Import;
            return DesktopAssetKind::Unknown;
        }

        struct RuntimeControlRecord
        {
            DesktopControlModel control{};
            char rowId[128]{};
            char parentRowId[128]{};
            QC::u32 childOrder = 0;
        };

        bool loadChunkedDocumentPayload(const QCQL::Database &database,
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

        bool loadCmmsDocumentText(const QCQL::Database &database,
                                  const char *tableName,
                                  const char *chunkTableName,
                                  const char *documentId,
                                  char *outSourcePath,
                                  QC::usize outSourcePathSize,
                                  QC::Vector<char> &outText)
        {
            outText.clear();
            if (outSourcePath && outSourcePathSize > 0)
                outSourcePath[0] = '\0';

            const QCQL::Cell keyCell = makeTextCell(documentId);
            QCQL::Row row{};
            const QCQL::Status rowStatus = QCQL::Engine::instance().selectRowByPrimaryKeyByName(database, tableName, keyCell.bytes, row);
            if (rowStatus != QCQL::Status::Success || row.tombstone || row.cells.size() < 3)
                return false;

            QC::u32 chunkCount = 0;
            if (!parseUnsignedTextCell(row.cells[2], chunkCount) || chunkCount == 0)
                return false;

            QC::Vector<QC::u8> payloadBytes;
            if (!loadChunkedDocumentPayload(database, chunkTableName, documentId, chunkCount, payloadBytes))
                return false;

            if (outSourcePath && outSourcePathSize > 0)
                (void)copyCellText(row.cells[1], outSourcePath, outSourcePathSize);

            outText.resize(payloadBytes.size() + 1);
            for (QC::usize i = 0; i < payloadBytes.size(); ++i)
                outText[i] = static_cast<char>(payloadBytes[i]);
            outText[payloadBytes.size()] = '\0';
            return payloadBytes.size() > 0;
        }

        void initializeExportResult(DesktopDocumentExportResult &outResult)
        {
            outResult.generated = false;
            outResult.text.clear();
            outResult.error[0] = '\0';
        }

        void initializeSaveResult(DesktopDocumentSaveResult &outResult)
        {
            outResult.saved = false;
            outResult.sourcePath[0] = '\0';
            outResult.chunkCount = 0;
            outResult.error[0] = '\0';
        }

        void setExportError(DesktopDocumentExportResult &outResult, const char *error)
        {
            copyText(outResult.error, sizeof(outResult.error), error);
            outResult.generated = false;
        }

        void setSaveError(DesktopDocumentSaveResult &outResult, const char *error)
        {
            copyText(outResult.error, sizeof(outResult.error), error);
            outResult.saved = false;
        }

        void appendChar(QC::Vector<char> &out, char c)
        {
            out.push_back(c);
        }

        void appendText(QC::Vector<char> &out, const char *text)
        {
            if (!text)
                return;
            for (QC::usize i = 0; text[i]; ++i)
                out.push_back(text[i]);
        }

        void appendIndent(QC::Vector<char> &out, QC::u32 depth)
        {
            for (QC::u32 i = 0; i < depth; ++i)
                appendText(out, "    ");
        }

        void appendUnsigned(QC::Vector<char> &out, QC::u32 value)
        {
            char digits[16]{};
            QC::usize length = 0;
            do
            {
                digits[length++] = static_cast<char>('0' + (value % 10u));
                value /= 10u;
            } while (value > 0 && length < sizeof(digits));

            while (length > 0)
                out.push_back(digits[--length]);
        }

        void appendSigned(QC::Vector<char> &out, QC::i32 value)
        {
            if (value < 0)
            {
                appendChar(out, '-');
                appendUnsigned(out, static_cast<QC::u32>(-value));
                return;
            }
            appendUnsigned(out, static_cast<QC::u32>(value));
        }

        void appendJsonEscaped(QC::Vector<char> &out, const char *text)
        {
            appendChar(out, '"');
            if (text)
            {
                for (QC::usize i = 0; text[i]; ++i)
                {
                    const char c = text[i];
                    if (c == '"' || c == '\\')
                    {
                        appendChar(out, '\\');
                        appendChar(out, c);
                    }
                    else if (c == '\n')
                    {
                        appendText(out, "\\n");
                    }
                    else if (c == '\r')
                    {
                        appendText(out, "\\r");
                    }
                    else if (c == '\t')
                    {
                        appendText(out, "\\t");
                    }
                    else
                    {
                        appendChar(out, c);
                    }
                }
            }
            appendChar(out, '"');
        }

        const char *findPropertyValue(const DesktopControlModel &control, const char *key)
        {
            if (!key || !*key)
                return nullptr;
            for (QC::usize i = 0; i < control.properties.size(); ++i)
            {
                if (equalsIgnoreCaseAscii(control.properties[i].key, key))
                    return control.properties[i].value;
            }
            return nullptr;
        }

        const char *findDocumentBindingAction(const DesktopControlModel &control, const char *eventName)
        {
            for (QC::usize i = 0; i < control.bindings.size(); ++i)
            {
                if (!eventName || !*eventName || equalsIgnoreCaseAscii(control.bindings[i].event, eventName))
                    return control.bindings[i].action;
            }
            return nullptr;
        }

        bool controlHasParent(const DesktopControlModel &control, const char *parentId)
        {
            if (!parentId || !*parentId)
                return control.parentId[0] == '\0';
            return equalsIgnoreCaseAscii(control.parentId, parentId);
        }

        const char *jsonTypeNameForControl(const DesktopControlModel &control)
        {
            const char *type = findPropertyValue(control, "type");
            return (type && *type) ? type : desktopControlKindName(control.kind);
        }

        const char *cuimlTagNameForControl(const DesktopControlModel &control)
        {
            const char *tagName = findPropertyValue(control, "tagName");
            if (tagName && *tagName)
                return tagName;

            switch (control.kind)
            {
            case DesktopControlKind::Panel:
                return "Panel";
            case DesktopControlKind::Button:
                return "Button";
            case DesktopControlKind::Label:
                return "Label";
            case DesktopControlKind::Image:
                return "Image";
            case DesktopControlKind::Slider:
                return "Slider";
            case DesktopControlKind::Container:
                return "Container";
            case DesktopControlKind::Clock:
                return "Label";
            case DesktopControlKind::LauncherTile:
                return "Button";
            default:
                return "Control";
            }
        }

        const char *exprOrNull(const DesktopControlModel &control, const char *key)
        {
            const char *value = findPropertyValue(control, key);
            return (value && *value) ? value : nullptr;
        }

        const char *defaultSourcePath(const DesktopDocument &document, DesktopDocumentFormat format)
        {
            if (document.metadata.sourcePath[0])
                return document.metadata.sourcePath;

            const bool isGolden = equalsIgnoreCaseAscii(document.documentId, CMMS_DESKTOP_LAYOUT_GOLDEN);
            if (format == DesktopDocumentFormat::CuiML)
                return isGolden ? "/GOLDEN/DESKTOP.CML" : "/PROD/DESKTOP.CML";
            if (format == DesktopDocumentFormat::Json)
                return isGolden ? "/GOLDEN/DESKTOP.JSN" : "/PROD/DESKTOP.JSN";
            return isGolden ? "/GOLDEN/DESKTOP.DOC" : "/PROD/DESKTOP.DOC";
        }

        bool ensureMetadataTable(QCQL::Database &database, const char *tableName)
        {
            if (!tableName || !*tableName)
                return false;

            QC::u32 tableId = 0;
            QCQL::Engine &engine = QCQL::Engine::instance();
            const QCQL::Status lookupSt = engine.lookupTableId(database, tableName, tableId);
            if (lookupSt == QCQL::Status::Success)
                return true;
            if (lookupSt != QCQL::Status::NotFound)
                return false;

            QCQL::TableSchema schema{};
            copyText(schema.tableName, sizeof(schema.tableName), tableName);

            QCQL::Column idCol{};
            copyText(idCol.name, sizeof(idCol.name), "id");
            idCol.type = QCQL::ColumnType::Text;
            idCol.isPrimaryKey = true;
            schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

            QCQL::Column sourcePathCol{};
            copyText(sourcePathCol.name, sizeof(sourcePathCol.name), "sourcePath");
            sourcePathCol.type = QCQL::ColumnType::Text;
            schema.columns.push_back(static_cast<QCQL::Column &&>(sourcePathCol));

            QCQL::Column chunkCountCol{};
            copyText(chunkCountCol.name, sizeof(chunkCountCol.name), "chunkCount");
            chunkCountCol.type = QCQL::ColumnType::Text;
            schema.columns.push_back(static_cast<QCQL::Column &&>(chunkCountCol));

            schema.primaryKeyIndex = 0;
            const QCQL::Status createSt = engine.createTable(database, schema);
            return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
        }

        bool ensureChunkTable(QCQL::Database &database, const char *tableName)
        {
            if (!tableName || !*tableName)
                return false;

            QC::u32 tableId = 0;
            QCQL::Engine &engine = QCQL::Engine::instance();
            const QCQL::Status lookupSt = engine.lookupTableId(database, tableName, tableId);
            if (lookupSt == QCQL::Status::Success)
                return true;
            if (lookupSt != QCQL::Status::NotFound)
                return false;

            QCQL::TableSchema schema{};
            copyText(schema.tableName, sizeof(schema.tableName), tableName);

            QCQL::Column idCol{};
            copyText(idCol.name, sizeof(idCol.name), "id");
            idCol.type = QCQL::ColumnType::Text;
            idCol.isPrimaryKey = true;
            schema.columns.push_back(static_cast<QCQL::Column &&>(idCol));

            QCQL::Column documentIdCol{};
            copyText(documentIdCol.name, sizeof(documentIdCol.name), "documentId");
            documentIdCol.type = QCQL::ColumnType::Text;
            schema.columns.push_back(static_cast<QCQL::Column &&>(documentIdCol));

            QCQL::Column chunkIndexCol{};
            copyText(chunkIndexCol.name, sizeof(chunkIndexCol.name), "chunkIndex");
            chunkIndexCol.type = QCQL::ColumnType::Text;
            schema.columns.push_back(static_cast<QCQL::Column &&>(chunkIndexCol));

            QCQL::Column payloadCol{};
            copyText(payloadCol.name, sizeof(payloadCol.name), "payload");
            payloadCol.type = QCQL::ColumnType::Text;
            schema.columns.push_back(static_cast<QCQL::Column &&>(payloadCol));

            schema.primaryKeyIndex = 0;
            const QCQL::Status createSt = engine.createTable(database, schema);
            return createSt == QCQL::Status::Success || createSt == QCQL::Status::AlreadyExists;
        }

        bool makeChunkRowId(const char *documentId, QC::u32 chunkIndex, char *out, QC::usize outCap)
        {
            if (!documentId || !*documentId || !out || outCap == 0)
                return false;

            char indexText[16]{};
            QC::Vector<char> digits;
            do
            {
                digits.push_back(static_cast<char>('0' + (chunkIndex % 10u)));
                chunkIndex /= 10u;
            } while (chunkIndex > 0);

            QC::usize count = 0;
            while (!digits.empty() && count + 1 < sizeof(indexText))
            {
                indexText[count++] = digits.back();
                digits.pop_back();
            }
            indexText[count] = '\0';

            const QC::usize docLen = QC::String::strlen(documentId);
            if (docLen + 1 + count + 1 > outCap)
                return false;
            QC::String::memcpy(out, documentId, docLen);
            out[docLen] = ':';
            QC::String::memcpy(out + docLen + 1, indexText, count);
            out[docLen + 1 + count] = '\0';
            return true;
        }

        bool writeChunkedDocumentText(QCQL::Database &database,
                                      const char *tableName,
                                      const char *chunkTableName,
                                      const char *documentId,
                                      const char *sourcePath,
                                      const char *payloadText,
                                      DesktopDocumentSaveResult &outResult)
        {
            initializeSaveResult(outResult);
            if (!tableName || !*tableName || !chunkTableName || !*chunkTableName || !documentId || !*documentId || !sourcePath || !*sourcePath || !payloadText || !*payloadText)
            {
                setSaveError(outResult, "Missing CMMS save inputs");
                return false;
            }

            if (!ensureMetadataTable(database, tableName) || !ensureChunkTable(database, chunkTableName))
            {
                setSaveError(outResult, "Failed to ensure CMMS desktop tables");
                return false;
            }

            QCQL::Engine &engine = QCQL::Engine::instance();
            const QCQL::Cell keyCell = makeTextCell(documentId);
            QCQL::Row existing{};
            const QCQL::Status existingSt = engine.selectRowByPrimaryKeyByName(database, tableName, keyCell.bytes, existing);

            QC::u32 existingChunkCount = 0;
            if (existingSt == QCQL::Status::Success && !existing.tombstone && existing.cells.size() >= 3)
                (void)parseUnsignedTextCell(existing.cells[2], existingChunkCount);

            const QC::usize payloadLen = QC::String::strlen(payloadText);
            const QC::u32 chunkCount = static_cast<QC::u32>((payloadLen + CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES - 1) / CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES);
            if (chunkCount == 0)
            {
                setSaveError(outResult, "Refusing to save empty CMMS desktop payload");
                return false;
            }

            QCQL::Row row{};
            row.cells.push_back(keyCell);
            row.cells.push_back(makeTextCell(sourcePath));
            row.cells.push_back(makeUnsignedTextCell(chunkCount));

            bool metadataOk = false;
            if (existingSt == QCQL::Status::Success && !existing.tombstone)
                metadataOk = (engine.updateRowByPrimaryKeyByName(database, tableName, keyCell.bytes, row) == QCQL::Status::Success);
            else
            {
                QC::u32 pageId = 0;
                metadataOk = (engine.insertRowByName(database, tableName, row, &pageId) == QCQL::Status::Success);
            }
            if (!metadataOk)
            {
                setSaveError(outResult, "Failed to write CMMS desktop metadata row");
                return false;
            }

            for (QC::u32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
            {
                const QC::usize start = static_cast<QC::usize>(chunkIndex) * CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES;
                QC::usize length = payloadLen - start;
                if (length > CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES)
                    length = CMMS_DESKTOP_DOCUMENT_CHUNK_BYTES;

                char chunkId[48]{};
                if (!makeChunkRowId(documentId, chunkIndex, chunkId, sizeof(chunkId)))
                {
                    setSaveError(outResult, "Failed to build CMMS chunk row id");
                    return false;
                }

                QCQL::Row chunkRow{};
                chunkRow.cells.push_back(makeTextCell(chunkId));
                chunkRow.cells.push_back(makeTextCell(documentId));
                chunkRow.cells.push_back(makeUnsignedTextCell(chunkIndex));

                QCQL::Cell payloadCell{};
                payloadCell.type = QCQL::ColumnType::Text;
                for (QC::usize i = 0; i < length; ++i)
                    payloadCell.bytes.push_back(static_cast<QC::u8>(payloadText[start + i]));
                chunkRow.cells.push_back(static_cast<QCQL::Cell &&>(payloadCell));

                const QCQL::Cell chunkKeyCell = makeTextCell(chunkId);
                QCQL::Row existingChunk{};
                const QCQL::Status existingChunkSt = engine.selectRowByPrimaryKeyByName(database, chunkTableName, chunkKeyCell.bytes, existingChunk);
                bool chunkOk = false;
                if (existingChunkSt == QCQL::Status::Success && !existingChunk.tombstone)
                    chunkOk = (engine.updateRowByPrimaryKeyByName(database, chunkTableName, chunkKeyCell.bytes, chunkRow) == QCQL::Status::Success);
                else
                {
                    QC::u32 pageId = 0;
                    chunkOk = (engine.insertRowByName(database, chunkTableName, chunkRow, &pageId) == QCQL::Status::Success);
                }
                if (!chunkOk)
                {
                    setSaveError(outResult, "Failed to write CMMS desktop chunk row");
                    return false;
                }
            }

            for (QC::u32 chunkIndex = chunkCount; chunkIndex < existingChunkCount; ++chunkIndex)
            {
                char chunkId[48]{};
                if (!makeChunkRowId(documentId, chunkIndex, chunkId, sizeof(chunkId)))
                    continue;
                const QCQL::Cell chunkKeyCell = makeTextCell(chunkId);
                (void)engine.removeRowByPrimaryKeyByName(database, chunkTableName, chunkKeyCell.bytes);
            }

            copyText(outResult.sourcePath, sizeof(outResult.sourcePath), sourcePath);
            outResult.chunkCount = chunkCount;
            outResult.saved = true;
            return true;
        }

        void appendJsonStringField(QC::Vector<char> &out, QC::u32 depth, const char *key, const char *value, bool trailingComma)
        {
            appendIndent(out, depth);
            appendJsonEscaped(out, key);
            appendText(out, ": ");
            appendJsonEscaped(out, value ? value : "");
            if (trailingComma)
                appendChar(out, ',');
            appendChar(out, '\n');
        }

        void appendJsonBoolField(QC::Vector<char> &out, QC::u32 depth, const char *key, bool value, bool trailingComma)
        {
            appendIndent(out, depth);
            appendJsonEscaped(out, key);
            appendText(out, ": ");
            appendText(out, value ? "true" : "false");
            if (trailingComma)
                appendChar(out, ',');
            appendChar(out, '\n');
        }

        void appendJsonNumericOrExprField(QC::Vector<char> &out,
                                          QC::u32 depth,
                                          const char *key,
                                          bool hasExpression,
                                          const char *expression,
                                          bool signedValue,
                                          QC::i32 i32Value,
                                          QC::u32 u32Value,
                                          bool trailingComma)
        {
            appendIndent(out, depth);
            appendJsonEscaped(out, key);
            appendText(out, ": ");
            if (hasExpression && expression)
            {
                appendJsonEscaped(out, expression);
            }
            else if (signedValue)
            {
                appendSigned(out, i32Value);
            }
            else
            {
                appendUnsigned(out, u32Value);
            }
            if (trailingComma)
                appendChar(out, ',');
            appendChar(out, '\n');
        }

        void appendControlJsonRecursive(const DesktopDocument &document,
                                        const char *parentId,
                                        QC::u32 depth,
                                        QC::Vector<char> &out)
        {
            if (depth > MAX_DESKTOP_EXPORT_TREE_DEPTH)
                return;

            bool first = true;
            for (QC::usize i = 0; i < document.controls.size(); ++i)
            {
                const DesktopControlModel &control = document.controls[i];
                if (!controlHasParent(control, parentId))
                    continue;

                if (!first)
                    appendText(out, ",\n");
                first = false;

                appendIndent(out, depth);
                appendText(out, "{\n");
                appendJsonStringField(out, depth + 1, "id", control.id, true);
                appendJsonStringField(out, depth + 1, "type", jsonTypeNameForControl(control), true);
                if (control.text[0])
                    appendJsonStringField(out, depth + 1, "text", control.text, true);

                const char *xExpr = exprOrNull(control, "xExpr");
                const char *yExpr = exprOrNull(control, "yExpr");
                const char *widthExpr = exprOrNull(control, "widthExpr");
                const char *heightExpr = exprOrNull(control, "heightExpr");
                appendJsonNumericOrExprField(out, depth + 1, "x", xExpr != nullptr, xExpr, true, control.layout.x, 0, true);
                appendJsonNumericOrExprField(out, depth + 1, "y", yExpr != nullptr, yExpr, true, control.layout.y, 0, true);
                appendJsonNumericOrExprField(out, depth + 1, "width", widthExpr != nullptr, widthExpr, false, 0, control.layout.width, true);
                appendJsonNumericOrExprField(out, depth + 1, "height", heightExpr != nullptr, heightExpr, false, 0, control.layout.height, true);

                const char *role = findPropertyValue(control, "role");
                const char *background = findPropertyValue(control, "background");
                const char *border = findPropertyValue(control, "border");
                const char *font = findPropertyValue(control, "font");
                const char *iconAlias = findPropertyValue(control, "iconAlias");
                const char *action = findDocumentBindingAction(control, "click");
                const char *iconPath = control.iconRef.path[0] ? control.iconRef.path : iconAlias;

                if (role && *role)
                    appendJsonStringField(out, depth + 1, "role", role, true);
                if (background && *background)
                    appendJsonStringField(out, depth + 1, "background", background, true);
                if (border && *border)
                    appendJsonStringField(out, depth + 1, "border", border, true);
                if (font && *font)
                    appendJsonStringField(out, depth + 1, "font", font, true);
                if (iconPath && *iconPath)
                    appendJsonStringField(out, depth + 1, "icon", iconPath, true);
                if (action && *action)
                    appendJsonStringField(out, depth + 1, "action", action, true);
                if (!control.visible)
                    appendJsonBoolField(out, depth + 1, "visible", false, true);
                if (!control.enabled)
                    appendJsonBoolField(out, depth + 1, "enabled", false, true);

                appendIndent(out, depth + 1);
                appendText(out, "\"children\": [");
                bool hasChildren = false;
                if (control.id[0])
                {
                    for (QC::usize j = 0; j < document.controls.size(); ++j)
                    {
                        if (controlHasParent(document.controls[j], control.id))
                        {
                            hasChildren = true;
                            break;
                        }
                    }
                }
                if (hasChildren)
                {
                    appendChar(out, '\n');
                    appendControlJsonRecursive(document, control.id, depth + 2, out);
                    appendChar(out, '\n');
                    appendIndent(out, depth + 1);
                }
                appendText(out, "]\n");
                appendIndent(out, depth);
                appendChar(out, '}');
            }
        }

        void appendCuimlAttribute(QC::Vector<char> &out, const char *key, const char *value)
        {
            if (!key || !*key || !value || !*value)
                return;
            appendChar(out, ' ');
            appendText(out, key);
            appendText(out, "=\"");
            appendText(out, value);
            appendChar(out, '"');
        }

        void appendCuimlNumericAttribute(QC::Vector<char> &out, const char *key, QC::i32 value)
        {
            appendChar(out, ' ');
            appendText(out, key);
            appendText(out, "=\"");
            appendSigned(out, value);
            appendChar(out, '"');
        }

        void appendCuimlUnsignedAttribute(QC::Vector<char> &out, const char *key, QC::u32 value)
        {
            appendChar(out, ' ');
            appendText(out, key);
            appendText(out, "=\"");
            appendUnsigned(out, value);
            appendChar(out, '"');
        }

        const char *preferredExportIconPath(const DesktopControlModel &control,
                                            char *buffer,
                                            QC::usize bufferSize)
        {
            const char *iconPath = control.iconRef.path[0] ? control.iconRef.path : findPropertyValue(control, "iconAlias");
            if (!iconPath || !*iconPath || !buffer || bufferSize == 0)
                return iconPath;

            buffer[0] = '\0';
            if (startsWithIgnoreCaseAscii(iconPath, "/ICONS/svg/") || startsWithIgnoreCaseAscii(iconPath, "/system/icons/svg/"))
            {
                copyText(buffer, bufferSize, iconPath);
                return buffer;
            }

            const char *systemIconsPrefix = "/system/icons/";
            const char *iconsPrefix = "/ICONS/";
            const char *tail = nullptr;
            if (startsWithIgnoreCaseAscii(iconPath, systemIconsPrefix))
                tail = iconPath + QC::String::strlen(systemIconsPrefix);
            else if (startsWithIgnoreCaseAscii(iconPath, iconsPrefix))
                tail = iconPath + QC::String::strlen(iconsPrefix);
            else
                return iconPath;

            if (!tail || !*tail)
                return iconPath;

            const char *dot = nullptr;
            for (const char *cursor = tail; *cursor; ++cursor)
            {
                if (*cursor == '.')
                    dot = cursor;
            }
            if (!dot || dot == tail)
                return iconPath;

            char stemLower[64]{};
            QC::usize stemLen = static_cast<QC::usize>(dot - tail);
            if (stemLen + 1 > sizeof(stemLower))
                return iconPath;
            for (QC::usize i = 0; i < stemLen; ++i)
                stemLower[i] = toLowerAscii(tail[i]);
            stemLower[stemLen] = '\0';

            if (equalsIgnoreCaseAscii(stemLower, "shutdown"))
            {
                copyText(buffer, bufferSize, "/ICONS/svg/power.svg");
                return buffer;
            }

            const char *svgPrefix = "/ICONS/svg/";
            const char *svgSuffix = ".svg";
            const QC::usize prefixLen = QC::String::strlen(svgPrefix);
            const QC::usize suffixLen = QC::String::strlen(svgSuffix);
            if (prefixLen + stemLen + suffixLen + 1 > bufferSize)
                return iconPath;

            QC::String::memcpy(buffer, svgPrefix, prefixLen);
            QC::String::memcpy(buffer + prefixLen, stemLower, stemLen);
            QC::String::memcpy(buffer + prefixLen + stemLen, svgSuffix, suffixLen);
            buffer[prefixLen + stemLen + suffixLen] = '\0';
            return buffer;
        }

        void appendControlCuimlRecursive(const DesktopDocument &document,
                                         const char *parentId,
                                         QC::u32 depth,
                                         QC::Vector<char> &out)
        {
            if (depth > MAX_DESKTOP_EXPORT_TREE_DEPTH)
                return;

            for (QC::usize i = 0; i < document.controls.size(); ++i)
            {
                const DesktopControlModel &control = document.controls[i];
                if (!controlHasParent(control, parentId))
                    continue;

                const char *tagName = cuimlTagNameForControl(control);
                appendIndent(out, depth);
                appendChar(out, '<');
                appendText(out, tagName);

                appendCuimlAttribute(out, "id", control.id);
                appendCuimlAttribute(out, "text", control.text);

                const char *xExpr = exprOrNull(control, "xExpr");
                const char *yExpr = exprOrNull(control, "yExpr");
                const char *widthExpr = exprOrNull(control, "widthExpr");
                const char *heightExpr = exprOrNull(control, "heightExpr");
                if (xExpr)
                    appendCuimlAttribute(out, "x", xExpr);
                else
                    appendCuimlNumericAttribute(out, "x", control.layout.x);
                if (yExpr)
                    appendCuimlAttribute(out, "y", yExpr);
                else
                    appendCuimlNumericAttribute(out, "y", control.layout.y);
                if (widthExpr)
                    appendCuimlAttribute(out, "width", widthExpr);
                else
                    appendCuimlUnsignedAttribute(out, "width", control.layout.width);
                if (heightExpr)
                    appendCuimlAttribute(out, "height", heightExpr);
                else
                    appendCuimlUnsignedAttribute(out, "height", control.layout.height);

                appendCuimlAttribute(out, "class", control.styleClass);
                appendCuimlAttribute(out, "role", findPropertyValue(control, "role"));
                appendCuimlAttribute(out, "background", findPropertyValue(control, "background"));
                appendCuimlAttribute(out, "border", findPropertyValue(control, "border"));
                appendCuimlAttribute(out, "font", findPropertyValue(control, "font"));
                char preferredIconPath[192]{};
                appendCuimlAttribute(out, "icon", preferredExportIconPath(control, preferredIconPath, sizeof(preferredIconPath)));
                appendCuimlAttribute(out, "action", findDocumentBindingAction(control, "click"));

                bool hasChildren = false;
                if (control.id[0])
                {
                    for (QC::usize j = 0; j < document.controls.size(); ++j)
                    {
                        if (controlHasParent(document.controls[j], control.id))
                        {
                            hasChildren = true;
                            break;
                        }
                    }
                }

                if (hasChildren)
                {
                    appendText(out, ">\n");
                    appendControlCuimlRecursive(document, control.id, depth + 1, out);
                    appendIndent(out, depth);
                    appendText(out, "</");
                    appendText(out, tagName);
                    appendText(out, ">\n");
                }
                else
                {
                    appendText(out, " />\n");
                }
            }
        }
    }

    bool DesktopDocumentIO::importCuimlText(const char *documentId,
                                            const char *sourcePath,
                                            const char *cuimlText,
                                            DesktopDocumentImportResult &outResult)
    {
        initializeDocument(outResult, documentId, sourcePath, DesktopDocumentFormat::CuiML);
        if (!cuimlText || !*cuimlText)
        {
            setImportError(outResult, "Empty CUI-ML document");
            return false;
        }

        outResult.document.sourceKind = sourcePath ? DesktopDocumentSourceKind::CuiMLImport : DesktopDocumentSourceKind::Unknown;

        QC::Vector<ParentFrame> stack;
        ParentFrame root{};
        stack.push_back(root);

        const char *cursor = cuimlText;
        while (*cursor)
        {
            if (*cursor != '<')
            {
                ++cursor;
                continue;
            }

            if (startsWithIgnoreCaseAscii(cursor, "<!--"))
            {
                const char *end = findStr(cursor, "-->");
                if (!end)
                    break;
                cursor = end + 3;
                continue;
            }

            if (startsWithIgnoreCaseAscii(cursor, "<![CDATA["))
            {
                const char *end = findStr(cursor, "]]>");
                if (!end)
                    break;
                cursor = end + 3;
                continue;
            }

            const char *gt = findStr(cursor, ">");
            if (!gt)
                break;

            const QC::usize tagLen = static_cast<QC::usize>(gt - cursor + 1);
            if (tagLen > 1023)
            {
                cursor = gt + 1;
                continue;
            }

            char tagRaw[1024]{};
            for (QC::usize i = 0; i < tagLen; ++i)
                tagRaw[i] = cursor[i];
            tagRaw[tagLen] = '\0';

            bool isClose = false;
            bool selfClose = false;
            const char *nameStart = skipWs(cursor + 1);
            if (*nameStart == '/')
            {
                isClose = true;
                ++nameStart;
                nameStart = skipWs(nameStart);
            }

            const char *nameEnd = nameStart;
            while (*nameEnd && *nameEnd != ' ' && *nameEnd != '\t' && *nameEnd != '\r' && *nameEnd != '\n' && *nameEnd != '>' && *nameEnd != '/')
                ++nameEnd;

            char name[64]{};
            QC::usize nameLen = static_cast<QC::usize>(nameEnd - nameStart);
            if (nameLen >= sizeof(name))
                nameLen = sizeof(name) - 1;
            for (QC::usize i = 0; i < nameLen; ++i)
                name[i] = nameStart[i];
            name[nameLen] = '\0';

            const char *scan = gt;
            while (scan > cursor && (*scan == '>' || *scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n'))
                --scan;
            if (*scan == '/')
                selfClose = true;

            cursor = gt + 1;
            if (!name[0])
                continue;

            if (isClose)
            {
                if (stack.size() > 1 && (containsIgnoreCaseAscii(name, "panel") || containsIgnoreCaseAscii(name, "container")))
                    stack.pop_back();
                continue;
            }

            if (startsWithIgnoreCaseAscii(name, "html") || startsWithIgnoreCaseAscii(name, "head") || startsWithIgnoreCaseAscii(name, "body") ||
                startsWithIgnoreCaseAscii(name, "meta") || startsWithIgnoreCaseAscii(name, "Desktop") || startsWithIgnoreCaseAscii(name, "Layout") ||
                startsWithIgnoreCaseAscii(name, "Import") || startsWithIgnoreCaseAscii(name, "link") || startsWithIgnoreCaseAscii(name, "help-window") ||
                startsWithIgnoreCaseAscii(name, "HelpWindow") || startsWithIgnoreCaseAscii(name, "cui-ml"))
            {
                if (startsWithIgnoreCaseAscii(name, "link"))
                {
                    char rel[64]{};
                    char type[64]{};
                    char href[256]{};
                    (void)parseAttrValue(tagRaw, "rel", rel, sizeof(rel));
                    (void)parseAttrValue(tagRaw, "type", type, sizeof(type));
                    (void)parseAttrValue(tagRaw, "href", href, sizeof(href));
                    if (rel[0] && type[0] && href[0] &&
                        equalsIgnoreCaseAscii(rel, "stylesheet") &&
                        equalsIgnoreCaseAscii(type, "text/cuimlss"))
                    {
                        inferThemeIdFromStylesheetHref(href, outResult.document);
                    }
                }
                continue;
            }

            if (startsWithIgnoreCaseAscii(name, "Background"))
            {
                char path[192]{};
                char type[32]{};
                char mode[32]{};
                (void)parseAttrValue(tagRaw, "path", path, sizeof(path));
                (void)parseAttrValue(tagRaw, "type", type, sizeof(type));
                (void)parseAttrValue(tagRaw, "mode", mode, sizeof(mode));
                if (path[0])
                {
                    copyText(outResult.document.backgroundAsset.path, sizeof(outResult.document.backgroundAsset.path), path);
                    outResult.document.backgroundAsset.kind = DesktopAssetKind::Wallpaper;
                    outResult.document.backgroundMode = DesktopBackgroundMode::Image;
                    outResult.document.canvas.backgroundMode = DesktopBackgroundMode::Image;
                }
                if (type[0] && equalsIgnoreCaseAscii(type, "gradient"))
                {
                    outResult.document.backgroundMode = DesktopBackgroundMode::Gradient;
                    outResult.document.canvas.backgroundMode = DesktopBackgroundMode::Gradient;
                }
                if (mode[0])
                    copyText(outResult.document.metadata.notes, sizeof(outResult.document.metadata.notes), mode);
                continue;
            }

            if (startsWithIgnoreCaseAscii(name, "Theme"))
            {
                char base[48]{};
                char id[48]{};
                (void)parseAttrValue(tagRaw, "base", base, sizeof(base));
                (void)parseAttrValue(tagRaw, "id", id, sizeof(id));
                if (id[0])
                    copyText(outResult.document.themeRef.themeId, sizeof(outResult.document.themeRef.themeId), id);
                else if (base[0])
                    copyText(outResult.document.themeRef.themeId, sizeof(outResult.document.themeRef.themeId), base);
                continue;
            }

            DesktopControlModel control{};
            control.kind = controlKindFromText(name);

            char id[64]{};
            char classAttr[128]{};
            char text[160]{};
            char role[64]{};
            char icon[128]{};
            char action[64]{};
            char x[64]{};
            char y[64]{};
            char width[64]{};
            char height[64]{};
            char background[128]{};
            char border[128]{};
            char font[96]{};

            (void)parseAttrValue(tagRaw, "id", id, sizeof(id));
            (void)parseAttrValue(tagRaw, "class", classAttr, sizeof(classAttr));
            (void)parseAttrValue(tagRaw, "text", text, sizeof(text));
            (void)parseAttrValue(tagRaw, "role", role, sizeof(role));
            (void)parseAttrValue(tagRaw, "icon", icon, sizeof(icon));
            (void)parseAttrValue(tagRaw, "action", action, sizeof(action));
            (void)parseAttrValue(tagRaw, "x", x, sizeof(x));
            (void)parseAttrValue(tagRaw, "y", y, sizeof(y));
            (void)parseAttrValue(tagRaw, "width", width, sizeof(width));
            (void)parseAttrValue(tagRaw, "height", height, sizeof(height));
            (void)parseAttrValue(tagRaw, "background", background, sizeof(background));
            (void)parseAttrValue(tagRaw, "border", border, sizeof(border));
            (void)parseAttrValue(tagRaw, "font", font, sizeof(font));

            const char *parentId = (stack.size() > 0 && stack.back().id[0]) ? stack.back().id : nullptr;
            applyCommonControlFields(control, id, parentId, text, classAttr, role, icon, action);
            addProperty(control, "tagName", name);
            assignExprOrNumber(x, "xExpr", control.layout.x, control);
            assignExprOrNumber(y, "yExpr", control.layout.y, control);
            assignExprOrUnsigned(width, "widthExpr", control.layout.width, control);
            assignExprOrUnsigned(height, "heightExpr", control.layout.height, control);

            if (background[0])
                addProperty(control, "background", background);
            if (border[0])
                addProperty(control, "border", border);
            if (font[0])
                addProperty(control, "font", font);

            outResult.document.controls.push_back(control);

            if (!selfClose && isPanelLikeKind(control.kind))
            {
                ParentFrame frame{};
                copyText(frame.id, sizeof(frame.id), control.id);
                stack.push_back(frame);
            }
        }

        outResult.loaded = outResult.document.controls.size() > 0;
        if (!outResult.loaded)
            setImportError(outResult, "No controls parsed from CUI-ML document");
        return outResult.loaded;
    }

    bool DesktopDocumentIO::importJsonText(const char *documentId,
                                           const char *sourcePath,
                                           const char *jsonText,
                                           DesktopDocumentImportResult &outResult)
    {
        initializeDocument(outResult, documentId, sourcePath, DesktopDocumentFormat::Json);
        if (!jsonText || !*jsonText)
        {
            setImportError(outResult, "Empty JSON document");
            return false;
        }

        outResult.document.sourceKind = sourcePath ? DesktopDocumentSourceKind::JsonImport : DesktopDocumentSourceKind::Unknown;

        QC::JSON::Value root;
        if (!QC::JSON::parse(jsonText, root) || !root.isObject())
        {
            setImportError(outResult, "Failed to parse desktop JSON");
            return false;
        }

        const QC::JSON::Value *desktop = root.find("desktop");
        if (!desktop || !desktop->isObject())
        {
            setImportError(outResult, "desktop JSON missing 'desktop' object");
            return false;
        }

        const QC::JSON::Value *theme = desktop->find("theme");
        if (theme)
        {
            if (theme->isString())
            {
                copyText(outResult.document.themeRef.themeId, sizeof(outResult.document.themeRef.themeId), theme->asString(nullptr));
            }
            else if (theme->isObject())
            {
                const char *themeId = stringOrNull(theme->find("id"));
                const char *base = stringOrNull(theme->find("base"));
                const char *selected = themeId ? themeId : base;
                if (selected)
                    copyText(outResult.document.themeRef.themeId, sizeof(outResult.document.themeRef.themeId), selected);
            }
        }

        const QC::JSON::Value *background = desktop->find("background");
        if (background)
        {
            if (background->isString())
            {
                copyText(outResult.document.backgroundAsset.path, sizeof(outResult.document.backgroundAsset.path), background->asString(nullptr));
                outResult.document.backgroundAsset.kind = DesktopAssetKind::Wallpaper;
                outResult.document.backgroundMode = DesktopBackgroundMode::Image;
                outResult.document.canvas.backgroundMode = DesktopBackgroundMode::Image;
            }
            else if (background->isObject())
            {
                const char *path = stringOrNull(background->find("path"));
                const char *mode = stringOrNull(background->find("mode"));
                if (path)
                {
                    copyText(outResult.document.backgroundAsset.path, sizeof(outResult.document.backgroundAsset.path), path);
                    outResult.document.backgroundAsset.kind = DesktopAssetKind::Wallpaper;
                    outResult.document.backgroundMode = DesktopBackgroundMode::Image;
                    outResult.document.canvas.backgroundMode = DesktopBackgroundMode::Image;
                }
                else if (mode && equalsIgnoreCaseAscii(mode, "gradient"))
                {
                    outResult.document.backgroundMode = DesktopBackgroundMode::Gradient;
                    outResult.document.canvas.backgroundMode = DesktopBackgroundMode::Gradient;
                }
            }
        }

        const QC::JSON::Value *layout = desktop->find("layout");
        const QC::JSON::Value *controls = layout ? layout->find("controls") : nullptr;
        if (!controls || !controls->isArray())
        {
            setImportError(outResult, "desktop JSON missing layout.controls array");
            return false;
        }

        const QC::JSON::Array *array = controls->asArray();
        for (QC::usize i = 0; i < array->size(); ++i)
            importJsonControlRecursive((*array)[i], nullptr, outResult.document);

        outResult.loaded = outResult.document.controls.size() > 0;
        if (!outResult.loaded)
            setImportError(outResult, "No controls parsed from desktop JSON");
        return outResult.loaded;
    }

    bool DesktopDocumentIO::importCmmsCuiml(const QCQL::Database &database,
                                            const char *documentId,
                                            DesktopDocumentImportResult &outResult)
    {
        char sourcePath[192]{};
        QC::Vector<char> text;
        if (!loadCmmsDocumentText(database, CMMS_DESKTOP_CUIML_TABLE, CMMS_DESKTOP_CUIML_CHUNK_TABLE, documentId, sourcePath, sizeof(sourcePath), text))
        {
            setImportError(outResult, "Failed to load CUI-ML document from CMMS");
            return false;
        }

        if (!importCuimlText(documentId, sourcePath, text.data(), outResult))
            return false;

        outResult.document.sourceKind = DesktopDocumentSourceKind::Cmms;
        return true;
    }

    bool DesktopDocumentIO::importCmmsRuntime(const QCQL::Database &database,
                                              const char *documentId,
                                              DesktopDocumentImportResult &outResult)
    {
        initializeDocument(outResult, documentId, "CMMS runtime rows", DesktopDocumentFormat::Unknown);

        struct RuntimeControlState
        {
            char rowId[128]{};
            QC::i32 x = 0;
            QC::i32 y = 0;
            QC::u32 width = 0;
            QC::u32 height = 0;
            QC::i32 zIndex = 0;
            bool visible = true;
            bool enabled = true;
            char styleClass[96]{};
            char text[192]{};
            char iconPath[192]{};
        };

        const QCQL::Cell layoutKeyCell = makeTextCell(documentId);
        QCQL::Row layoutRow{};
        const QCQL::Status layoutSt = QCQL::Engine::instance().selectRowByPrimaryKeyByName(database,
                                                                                             CMMS_DESKTOP_LAYOUT_TABLE,
                                                                                             layoutKeyCell.bytes,
                                                                                             layoutRow);
        if (layoutSt != QCQL::Status::Success || layoutRow.tombstone || layoutRow.cells.size() < 3)
        {
            setImportError(outResult, "Failed to load runtime layout metadata from CMMS");
            return false;
        }

        if (copyCellText(layoutRow.cells[1], outResult.sourcePath, sizeof(outResult.sourcePath)) && outResult.sourcePath[0])
            copyText(outResult.document.metadata.sourcePath, sizeof(outResult.document.metadata.sourcePath), outResult.sourcePath);

        const QCQL::Table *controlTable = findTableByName(database, CMMS_DESKTOP_CONTROL_TABLE);
        const QCQL::Table *runtimeTable = findTableByName(database, CMMS_DESKTOP_CONTROL_RUNTIME_TABLE);
        if (!controlTable || !runtimeTable)
        {
            setImportError(outResult, "Normalized CMMS runtime tables are unavailable");
            return false;
        }

        QC::Vector<RuntimeControlRecord> runtimeControls;
        QC::Vector<RuntimeControlState> runtimeStates;

        for (QC::usize pageIndex = 0; pageIndex < runtimeTable->pages.size(); ++pageIndex)
        {
            QCQL::Page page{};
            if (QCQL::Engine::instance().loadPage(database, runtimeTable->pages[pageIndex], page) != QCQL::Status::Success)
                continue;

            for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
            {
                QCQL::Row row{};
                if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                    continue;
                if (row.tombstone || row.cells.size() < 13)
                    continue;

                RuntimeControlState state{};
                if (!copyCellText(row.cells[0], state.rowId, sizeof(state.rowId)) || !state.rowId[0])
                    continue;

                char textValue[192]{};
                QC::i32 signedValue = 0;
                QC::u32 unsignedValue = 0;
                bool boolValue = true;

                if (copyCellText(row.cells[3], textValue, sizeof(textValue)) && tryParseI32(textValue, signedValue))
                    state.x = signedValue;
                if (copyCellText(row.cells[4], textValue, sizeof(textValue)) && tryParseI32(textValue, signedValue))
                    state.y = signedValue;
                if (parseUnsignedTextCell(row.cells[5], unsignedValue))
                    state.width = unsignedValue;
                if (parseUnsignedTextCell(row.cells[6], unsignedValue))
                    state.height = unsignedValue;
                if (copyCellText(row.cells[7], textValue, sizeof(textValue)) && tryParseI32(textValue, signedValue))
                    state.zIndex = signedValue;
                if (parseBoolTextCell(row.cells[8], boolValue))
                    state.visible = boolValue;
                if (parseBoolTextCell(row.cells[9], boolValue))
                    state.enabled = boolValue;
                (void)copyCellText(row.cells[10], state.styleClass, sizeof(state.styleClass));
                (void)copyCellText(row.cells[11], state.text, sizeof(state.text));
                (void)copyCellText(row.cells[12], state.iconPath, sizeof(state.iconPath));

                runtimeStates.push_back(state);
            }
        }

        auto findRuntimeState = [&](const char *rowId) -> const RuntimeControlState *
        {
            if (!rowId || !*rowId)
                return nullptr;
            for (QC::usize i = 0; i < runtimeStates.size(); ++i)
            {
                if (equalsIgnoreCaseAscii(runtimeStates[i].rowId, rowId))
                    return &runtimeStates[i];
            }
            return nullptr;
        };

        for (QC::usize pageIndex = 0; pageIndex < controlTable->pages.size(); ++pageIndex)
        {
            QCQL::Page page{};
            if (QCQL::Engine::instance().loadPage(database, controlTable->pages[pageIndex], page) != QCQL::Status::Success)
                continue;

            for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
            {
                QCQL::Row row{};
                if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                    continue;
                if (row.tombstone || row.cells.size() < 5 || !cellMatchesText(row.cells[1], documentId))
                    continue;

                RuntimeControlRecord record{};
                if (!copyCellText(row.cells[0], record.rowId, sizeof(record.rowId)))
                    continue;

                char controlType[48]{};
                char controlKey[96]{};
                (void)copyCellText(row.cells[3], controlType, sizeof(controlType));
                (void)copyCellText(row.cells[4], controlKey, sizeof(controlKey));

                record.control.kind = controlKindFromText(controlType);
                copyText(record.control.id, sizeof(record.control.id), controlKey[0] ? controlKey : record.rowId);
                copyText(record.control.name, sizeof(record.control.name), controlKey[0] ? controlKey : controlType);

                const RuntimeControlState *runtimeState = findRuntimeState(record.rowId);
                if (!runtimeState)
                    continue;

                record.control.layout.x = runtimeState->x;
                record.control.layout.y = runtimeState->y;
                record.control.layout.width = runtimeState->width;
                record.control.layout.height = runtimeState->height;
                record.control.zIndex = runtimeState->zIndex;
                record.control.visible = runtimeState->visible;
                record.control.enabled = runtimeState->enabled;
                if (runtimeState->styleClass[0])
                    copyText(record.control.styleClass, sizeof(record.control.styleClass), runtimeState->styleClass);
                if (runtimeState->text[0])
                    copyText(record.control.text, sizeof(record.control.text), runtimeState->text);
                if (runtimeState->iconPath[0])
                {
                    copyText(record.control.iconRef.path, sizeof(record.control.iconRef.path), runtimeState->iconPath);
                    record.control.iconRef.kind = DesktopAssetKind::Icon;
                }

                runtimeControls.push_back(record);
            }
        }

        if (runtimeControls.empty())
        {
            setImportError(outResult, "Normalized CMMS runtime rows are not populated");
            return false;
        }

        if (const QCQL::Table *themeTable = findTableByName(database, CMMS_DESKTOP_LAYOUT_THEME_TABLE))
        {
            for (QC::usize pageIndex = 0; pageIndex < themeTable->pages.size(); ++pageIndex)
            {
                QCQL::Page page{};
                if (QCQL::Engine::instance().loadPage(database, themeTable->pages[pageIndex], page) != QCQL::Status::Success)
                    continue;

                for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                {
                    QCQL::Row row{};
                    if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                        continue;
                    if (row.tombstone || row.cells.size() < 4 || !cellMatchesText(row.cells[1], documentId))
                        continue;

                    (void)copyCellText(row.cells[2], outResult.document.themeRef.themeId, sizeof(outResult.document.themeRef.themeId));
                    (void)copyCellText(row.cells[3], outResult.document.themeRef.variant, sizeof(outResult.document.themeRef.variant));
                    break;
                }
                if (outResult.document.themeRef.themeId[0])
                    break;
            }
        }

        if (const QCQL::Table *assetTable = findTableByName(database, CMMS_DESKTOP_LAYOUT_ASSET_TABLE))
        {
            for (QC::usize pageIndex = 0; pageIndex < assetTable->pages.size(); ++pageIndex)
            {
                QCQL::Page page{};
                if (QCQL::Engine::instance().loadPage(database, assetTable->pages[pageIndex], page) != QCQL::Status::Success)
                    continue;

                for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                {
                    QCQL::Row row{};
                    if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                        continue;
                    if (row.tombstone || row.cells.size() < 6 || !cellMatchesText(row.cells[1], documentId))
                        continue;

                    char assetRole[48]{};
                    char assetKind[48]{};
                    char assetPath[192]{};
                    char backgroundMode[48]{};
                    (void)copyCellText(row.cells[2], assetRole, sizeof(assetRole));
                    (void)copyCellText(row.cells[3], assetKind, sizeof(assetKind));
                    (void)copyCellText(row.cells[4], assetPath, sizeof(assetPath));
                    (void)copyCellText(row.cells[5], backgroundMode, sizeof(backgroundMode));

                    if (equalsIgnoreCaseAscii(assetRole, "background"))
                    {
                        outResult.document.backgroundMode = backgroundModeFromText(backgroundMode);
                        outResult.document.backgroundAsset.kind = assetKindFromText(assetKind);
                        copyText(outResult.document.backgroundAsset.path, sizeof(outResult.document.backgroundAsset.path), assetPath);
                        break;
                    }
                }
            }
        }

        if (const QCQL::Table *propertyTable = findTableByName(database, CMMS_DESKTOP_CONTROL_PROPERTIES_TABLE))
        {
            for (QC::usize pageIndex = 0; pageIndex < propertyTable->pages.size(); ++pageIndex)
            {
                QCQL::Page page{};
                if (QCQL::Engine::instance().loadPage(database, propertyTable->pages[pageIndex], page) != QCQL::Status::Success)
                    continue;

                for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                {
                    QCQL::Row row{};
                    if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                        continue;
                    if (row.tombstone || row.cells.size() < 5 || !cellMatchesText(row.cells[1], documentId))
                        continue;

                    char controlId[128]{};
                    char propertyKey[64]{};
                    char propertyValue[192]{};
                    (void)copyCellText(row.cells[2], controlId, sizeof(controlId));
                    (void)copyCellText(row.cells[3], propertyKey, sizeof(propertyKey));
                    (void)copyCellText(row.cells[4], propertyValue, sizeof(propertyValue));

                    for (QC::usize i = 0; i < runtimeControls.size(); ++i)
                    {
                        if (!equalsIgnoreCaseAscii(runtimeControls[i].rowId, controlId))
                            continue;
                        addProperty(runtimeControls[i].control, propertyKey, propertyValue);
                        break;
                    }
                }
            }
        }

        if (const QCQL::Table *bindingTable = findTableByName(database, CMMS_DESKTOP_CONTROL_BINDINGS_TABLE))
        {
            for (QC::usize pageIndex = 0; pageIndex < bindingTable->pages.size(); ++pageIndex)
            {
                QCQL::Page page{};
                if (QCQL::Engine::instance().loadPage(database, bindingTable->pages[pageIndex], page) != QCQL::Status::Success)
                    continue;

                for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                {
                    QCQL::Row row{};
                    if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                        continue;
                    if (row.tombstone || row.cells.size() < 6 || !cellMatchesText(row.cells[1], documentId))
                        continue;

                    char controlId[128]{};
                    char eventName[64]{};
                    char actionName[64]{};
                    char argument[192]{};
                    (void)copyCellText(row.cells[2], controlId, sizeof(controlId));
                    (void)copyCellText(row.cells[3], eventName, sizeof(eventName));
                    (void)copyCellText(row.cells[4], actionName, sizeof(actionName));
                    (void)copyCellText(row.cells[5], argument, sizeof(argument));

                    for (QC::usize i = 0; i < runtimeControls.size(); ++i)
                    {
                        if (!equalsIgnoreCaseAscii(runtimeControls[i].rowId, controlId))
                            continue;
                        addBinding(runtimeControls[i].control, eventName, actionName, argument);
                        break;
                    }
                }
            }
        }

        if (const QCQL::Table *hierarchyTable = findTableByName(database, CMMS_DESKTOP_CONTROL_HIERARCHY_TABLE))
        {
            for (QC::usize pageIndex = 0; pageIndex < hierarchyTable->pages.size(); ++pageIndex)
            {
                QCQL::Page page{};
                if (QCQL::Engine::instance().loadPage(database, hierarchyTable->pages[pageIndex], page) != QCQL::Status::Success)
                    continue;

                for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                {
                    QCQL::Row row{};
                    if (QCQL::Engine::instance().readRow(database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success)
                        continue;
                    if (row.tombstone || row.cells.size() < 5 || !cellMatchesText(row.cells[1], documentId))
                        continue;

                    char parentControlId[128]{};
                    char childControlId[128]{};
                    QC::u32 childOrder = 0;
                    (void)copyCellText(row.cells[2], parentControlId, sizeof(parentControlId));
                    (void)copyCellText(row.cells[3], childControlId, sizeof(childControlId));
                    (void)parseUnsignedTextCell(row.cells[4], childOrder);

                    for (QC::usize i = 0; i < runtimeControls.size(); ++i)
                    {
                        if (!equalsIgnoreCaseAscii(runtimeControls[i].rowId, childControlId))
                            continue;
                        copyText(runtimeControls[i].parentRowId, sizeof(runtimeControls[i].parentRowId), parentControlId);
                        runtimeControls[i].childOrder = childOrder;
                        break;
                    }
                }
            }
        }

        for (QC::usize i = 0; i < runtimeControls.size(); ++i)
        {
            if (!runtimeControls[i].parentRowId[0])
                continue;

            for (QC::usize j = 0; j < runtimeControls.size(); ++j)
            {
                if (!equalsIgnoreCaseAscii(runtimeControls[j].rowId, runtimeControls[i].parentRowId))
                    continue;
                copyText(runtimeControls[i].control.parentId,
                         sizeof(runtimeControls[i].control.parentId),
                         runtimeControls[j].control.id[0] ? runtimeControls[j].control.id : runtimeControls[j].rowId);
                break;
            }
        }

        for (QC::usize i = 0; i < runtimeControls.size(); ++i)
            outResult.document.controls.push_back(runtimeControls[i].control);

        outResult.document.sourceKind = DesktopDocumentSourceKind::Cmms;
        outResult.loaded = !outResult.document.controls.empty();
        if (!outResult.loaded)
            setImportError(outResult, "Normalized CMMS runtime document is empty");
        return outResult.loaded;
    }

    bool DesktopDocumentIO::importCmmsJson(const QCQL::Database &database,
                                           const char *documentId,
                                           DesktopDocumentImportResult &outResult)
    {
        char sourcePath[192]{};
        QC::Vector<char> text;
        if (!loadCmmsDocumentText(database, CMMS_DESKTOP_LAYOUT_TABLE, CMMS_DESKTOP_LAYOUT_CHUNK_TABLE, documentId, sourcePath, sizeof(sourcePath), text))
        {
            setImportError(outResult, "Failed to load JSON document from CMMS");
            return false;
        }

        if (!importJsonText(documentId, sourcePath, text.data(), outResult))
            return false;

        outResult.document.sourceKind = DesktopDocumentSourceKind::Cmms;
        return true;
    }

    bool DesktopDocumentIO::exportCuimlText(const DesktopDocument &document,
                                            DesktopDocumentExportResult &outResult)
    {
        initializeExportResult(outResult);
        if (document.controls.empty())
        {
            setExportError(outResult, "DesktopDocument has no controls to export");
            return false;
        }

        appendText(outResult.text, "<html lang=\"cuiml\">\n");
        appendText(outResult.text, "    <head>\n");
        appendText(outResult.text, "        <meta charset=\"utf-8\" />\n");
        appendText(outResult.text, "        <meta name=\"cuiml-version\" content=\"1.0\" />\n");
        appendText(outResult.text, "        <Import src=\"/UI/COMMON.CUI\" />\n");
        appendText(outResult.text, "        <link rel=\"stylesheet\" type=\"text/cuimlss\" href=\"/UI/SPRING.CXS\" />\n");
        appendText(outResult.text, "    </head>\n");
        appendText(outResult.text, "    <body>\n");
        appendText(outResult.text, "        <Desktop>\n");

        if (document.themeRef.themeId[0])
        {
            appendText(outResult.text, "            <Theme");
            appendCuimlAttribute(outResult.text, "base", document.themeRef.themeId);
            appendCuimlAttribute(outResult.text, "id", document.themeRef.themeId);
            appendText(outResult.text, " />\n");
        }

        if (document.backgroundAsset.path[0] || document.backgroundMode != DesktopBackgroundMode::None)
        {
            appendText(outResult.text, "            <Background");
            if (document.backgroundMode == DesktopBackgroundMode::Gradient)
                appendCuimlAttribute(outResult.text, "type", "gradient");
            else if (document.backgroundAsset.path[0])
                appendCuimlAttribute(outResult.text, "type", "image");
            if (document.backgroundAsset.path[0])
                appendCuimlAttribute(outResult.text, "path", document.backgroundAsset.path);
            if (document.metadata.notes[0])
                appendCuimlAttribute(outResult.text, "mode", document.metadata.notes);
            appendText(outResult.text, " />\n");
        }

        appendText(outResult.text, "            <Layout type=\"absolute\">\n");
        appendControlCuimlRecursive(document, nullptr, 4, outResult.text);
        appendText(outResult.text, "            </Layout>\n");
        appendText(outResult.text, "        </Desktop>\n");
        appendText(outResult.text, "    </body>\n");
        appendText(outResult.text, "</html>\n");
        appendChar(outResult.text, '\0');

        outResult.generated = true;
        return true;
    }

    bool DesktopDocumentIO::exportJsonText(const DesktopDocument &document,
                                           DesktopDocumentExportResult &outResult)
    {
        initializeExportResult(outResult);
        if (document.controls.empty())
        {
            setExportError(outResult, "DesktopDocument has no controls to export");
            return false;
        }

        appendText(outResult.text, "{\n");
        appendText(outResult.text, "    \"desktop\": {\n");

        if (document.themeRef.themeId[0])
        {
            appendText(outResult.text, "        \"theme\": {\n");
            appendJsonStringField(outResult.text, 3, "base", document.themeRef.themeId, false);
            appendText(outResult.text, "        },\n");
        }

        if (document.backgroundAsset.path[0] || document.backgroundMode != DesktopBackgroundMode::None)
        {
            appendText(outResult.text, "        \"background\": {\n");
            if (document.backgroundAsset.path[0])
                appendJsonStringField(outResult.text, 3, "path", document.backgroundAsset.path, true);
            const char *mode = (document.backgroundMode == DesktopBackgroundMode::Gradient) ? "gradient" :
                               (document.backgroundMode == DesktopBackgroundMode::Image) ? "image" : "solid";
            appendJsonStringField(outResult.text, 3, "mode", mode, false);
            appendText(outResult.text, "        },\n");
        }

        appendText(outResult.text, "        \"layout\": {\n");
        appendText(outResult.text, "            \"type\": \"absolute\",\n");
        appendText(outResult.text, "            \"controls\": [\n");
        appendControlJsonRecursive(document, nullptr, 4, outResult.text);
        appendText(outResult.text, "\n            ]\n");
        appendText(outResult.text, "        }\n");
        appendText(outResult.text, "    }\n");
        appendText(outResult.text, "}\n");
        appendChar(outResult.text, '\0');

        outResult.generated = true;
        return true;
    }

    bool DesktopDocumentIO::saveCmmsCuiml(QCQL::Database &database,
                                          const DesktopDocument &document,
                                          DesktopDocumentSaveResult &outResult)
    {
        DesktopDocumentValidationResult validation{};
        if (!DesktopDocumentValidation::validateForSave(document, validation))
        {
            initializeSaveResult(outResult);
            setSaveError(outResult, validation.issues.empty() ? "DesktopDocument failed save validation" : validation.issues[0].message);
            return false;
        }

        DesktopDocumentExportResult exportResult{};
        if (!exportCuimlText(document, exportResult))
        {
            initializeSaveResult(outResult);
            setSaveError(outResult, exportResult.error);
            return false;
        }

        const char *documentId = document.documentId[0] ? document.documentId : CMMS_DESKTOP_LAYOUT_PRODUCTION;
        const char *sourcePath = defaultSourcePath(document, DesktopDocumentFormat::CuiML);
        return writeChunkedDocumentText(database,
                                        CMMS_DESKTOP_CUIML_TABLE,
                                        CMMS_DESKTOP_CUIML_CHUNK_TABLE,
                                        documentId,
                                        sourcePath,
                                        exportResult.text.data(),
                                        outResult);
    }

    bool DesktopDocumentIO::saveCmmsJson(QCQL::Database &database,
                                         const DesktopDocument &document,
                                         DesktopDocumentSaveResult &outResult)
    {
        DesktopDocumentValidationResult validation{};
        if (!DesktopDocumentValidation::validateForSave(document, validation))
        {
            initializeSaveResult(outResult);
            setSaveError(outResult, validation.issues.empty() ? "DesktopDocument failed save validation" : validation.issues[0].message);
            return false;
        }

        DesktopDocumentExportResult exportResult{};
        if (!exportJsonText(document, exportResult))
        {
            initializeSaveResult(outResult);
            setSaveError(outResult, exportResult.error);
            return false;
        }

        const char *documentId = document.documentId[0] ? document.documentId : CMMS_DESKTOP_LAYOUT_PRODUCTION;
        const char *sourcePath = defaultSourcePath(document, DesktopDocumentFormat::Json);
        return writeChunkedDocumentText(database,
                                        CMMS_DESKTOP_LAYOUT_TABLE,
                                        CMMS_DESKTOP_LAYOUT_CHUNK_TABLE,
                                        documentId,
                                        sourcePath,
                                        exportResult.text.data(),
                                        outResult);
    }
}