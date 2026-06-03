#include "QDDesktopDocumentValidation.h"

#include "QDDesktopDocumentIO.h"
#include "QCString.h"

namespace QD
{
    namespace
    {
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

        void initializeValidationResult(DesktopDocumentValidationResult &outResult)
        {
            outResult.valid = false;
            outResult.errorCount = 0;
            outResult.warningCount = 0;
            outResult.issues.clear();
        }

        void addIssue(DesktopDocumentValidationResult &outResult,
                      DesktopValidationSeverity severity,
                      DesktopValidationCode code,
                      const char *controlId,
                      const char *message)
        {
            DesktopDocumentValidationIssue issue{};
            issue.severity = severity;
            issue.code = code;
            copyText(issue.controlId, sizeof(issue.controlId), controlId);
            copyText(issue.message, sizeof(issue.message), message);
            outResult.issues.push_back(issue);
            if (severity == DesktopValidationSeverity::Error)
                ++outResult.errorCount;
            else if (severity == DesktopValidationSeverity::Warning)
                ++outResult.warningCount;
        }

        const DesktopControlModel *findControlById(const DesktopDocument &document, const char *id)
        {
            if (!id || !*id)
                return nullptr;
            for (QC::usize i = 0; i < document.controls.size(); ++i)
            {
                if (equalsIgnoreCaseAscii(document.controls[i].id, id))
                    return &document.controls[i];
            }
            return nullptr;
        }

        const char *findPropertyValue(const DesktopControlModel &control, const char *key)
        {
            for (QC::usize i = 0; i < control.properties.size(); ++i)
            {
                if (equalsIgnoreCaseAscii(control.properties[i].key, key))
                    return control.properties[i].value;
            }
            return nullptr;
        }

        bool hasBindingGap(const DesktopControlModel &control)
        {
            for (QC::usize i = 0; i < control.bindings.size(); ++i)
            {
                const bool hasEvent = control.bindings[i].event[0] != '\0';
                const bool hasAction = control.bindings[i].action[0] != '\0';
                if (hasEvent != hasAction)
                    return true;
            }
            return false;
        }

        bool hasGeometryExpression(const DesktopControlModel &control, const char *key)
        {
            const char *value = findPropertyValue(control, key);
            return value && *value;
        }

        bool allowsImplicitContentSizing(const DesktopControlModel &control)
        {
            switch (control.kind)
            {
            case DesktopControlKind::Label:
                return control.text[0] != '\0';
            default:
                break;
            }

            const char *tagName = findPropertyValue(control, "tagName");
            return tagName &&
                   (equalsIgnoreCaseAscii(tagName, "TitleLabel") ||
                    equalsIgnoreCaseAscii(tagName, "BodyLabel"));
        }

        bool isRuntimeSafePath(const char *path)
        {
            if (!path || !*path)
                return false;
            return startsWithIgnoreCaseAscii(path, "/WALL/") ||
                   startsWithIgnoreCaseAscii(path, "/ICONS/") ||
                   startsWithIgnoreCaseAscii(path, "/UI/") ||
                   startsWithIgnoreCaseAscii(path, "/FONTS/") ||
                   startsWithIgnoreCaseAscii(path, "/PROD/") ||
                   startsWithIgnoreCaseAscii(path, "/GOLDEN/") ||
                   startsWithIgnoreCaseAscii(path, "/SYSTEM/") ||
                   startsWithIgnoreCaseAscii(path, "/system/");
        }

        void validateDocumentStructure(const DesktopDocument &document,
                                       bool publishMode,
                                       DesktopDocumentValidationResult &outResult)
        {
            if (document.documentId[0] == '\0')
            {
                addIssue(outResult,
                         publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                         DesktopValidationCode::MissingDocumentId,
                         nullptr,
                         "DesktopDocument is missing a documentId");
            }

            if (document.controls.empty())
            {
                addIssue(outResult,
                         DesktopValidationSeverity::Error,
                         DesktopValidationCode::EmptyDocument,
                         nullptr,
                         "DesktopDocument has no controls");
                return;
            }

            if (document.themeRef.themeId[0] == '\0')
            {
                addIssue(outResult,
                         publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                         DesktopValidationCode::MissingThemeId,
                         nullptr,
                         "DesktopDocument has no themeRef.themeId");
            }

            if (document.backgroundAsset.path[0] && !isRuntimeSafePath(document.backgroundAsset.path))
            {
                addIssue(outResult,
                         publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                         DesktopValidationCode::NonRuntimeSafeAssetPath,
                         nullptr,
                         "Background asset path is not runtime-safe");
            }

            for (QC::usize i = 0; i < document.controls.size(); ++i)
            {
                const DesktopControlModel &control = document.controls[i];
                if (control.id[0] == '\0')
                {
                    addIssue(outResult,
                             DesktopValidationSeverity::Error,
                             DesktopValidationCode::MissingControlId,
                             nullptr,
                             "A control is missing its id");
                    continue;
                }

                for (QC::usize j = i + 1; j < document.controls.size(); ++j)
                {
                    if (equalsIgnoreCaseAscii(control.id, document.controls[j].id))
                    {
                        addIssue(outResult,
                                 DesktopValidationSeverity::Error,
                                 DesktopValidationCode::DuplicateControlId,
                                 control.id,
                                 "Duplicate control id detected");
                        break;
                    }
                }

                if (control.parentId[0])
                {
                    const DesktopControlModel *parent = findControlById(document, control.parentId);
                    if (!parent || equalsIgnoreCaseAscii(control.parentId, control.id))
                    {
                        addIssue(outResult,
                                 DesktopValidationSeverity::Error,
                                 DesktopValidationCode::InvalidParent,
                                 control.id,
                                 "Control has an invalid parent relationship");
                    }
                }

                if (control.kind == DesktopControlKind::Unknown)
                {
                    addIssue(outResult,
                             DesktopValidationSeverity::Error,
                             DesktopValidationCode::UnsupportedControlKind,
                             control.id,
                             "Control kind is unknown");
                }
                else if (publishMode && control.kind == DesktopControlKind::Custom)
                {
                    addIssue(outResult,
                             DesktopValidationSeverity::Error,
                             DesktopValidationCode::UnsupportedControlKind,
                             control.id,
                             "Custom controls are not publish-safe yet");
                }

                const bool hasWidth = control.layout.width > 0 || hasGeometryExpression(control, "widthExpr");
                const bool hasHeight = control.layout.height > 0 || hasGeometryExpression(control, "heightExpr");
                if ((!hasWidth || !hasHeight) && !allowsImplicitContentSizing(control))
                {
                    addIssue(outResult,
                             publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                             DesktopValidationCode::MissingGeometry,
                             control.id,
                             "Control is missing width or height");
                }

                if ((control.kind == DesktopControlKind::Button || control.kind == DesktopControlKind::Label) &&
                    control.text[0] == '\0' && control.iconRef.path[0] == '\0' && !findPropertyValue(control, "iconAlias"))
                {
                    addIssue(outResult,
                             DesktopValidationSeverity::Warning,
                             DesktopValidationCode::MissingTextOrAsset,
                             control.id,
                             "Interactive/text control has neither text nor icon");
                }

                if (control.kind == DesktopControlKind::Image && control.iconRef.path[0] == '\0' && !findPropertyValue(control, "iconAlias"))
                {
                    addIssue(outResult,
                             publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                             DesktopValidationCode::MissingAssetPath,
                             control.id,
                             "Image control is missing its asset reference");
                }

                if (control.iconRef.path[0] && !isRuntimeSafePath(control.iconRef.path))
                {
                    addIssue(outResult,
                             publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                             DesktopValidationCode::NonRuntimeSafeAssetPath,
                             control.id,
                             "Control asset path is not runtime-safe");
                }

                if (hasBindingGap(control))
                {
                    addIssue(outResult,
                             publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                             DesktopValidationCode::InvalidBinding,
                             control.id,
                             "Control has an incomplete binding");
                }
            }
        }

        void validateRoundTrip(const DesktopDocument &document,
                               bool publishMode,
                               DesktopDocumentValidationResult &outResult)
        {
            DesktopDocumentExportResult jsonExport{};
            if (!DesktopDocumentIO::exportJsonText(document, jsonExport))
            {
                addIssue(outResult,
                         DesktopValidationSeverity::Error,
                         DesktopValidationCode::RoundTripFailure,
                         nullptr,
                         "JSON export failed during validation");
            }
            else
            {
                DesktopDocumentImportResult jsonImport{};
                if (!DesktopDocumentIO::importJsonText(document.documentId, document.metadata.sourcePath, jsonExport.text.data(), jsonImport) ||
                    jsonImport.document.controls.size() != document.controls.size())
                {
                    addIssue(outResult,
                             publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                             DesktopValidationCode::RoundTripFailure,
                             nullptr,
                             "JSON round-trip validation failed");
                }
            }

            DesktopDocumentExportResult cuimlExport{};
            if (!DesktopDocumentIO::exportCuimlText(document, cuimlExport))
            {
                addIssue(outResult,
                         DesktopValidationSeverity::Error,
                         DesktopValidationCode::RoundTripFailure,
                         nullptr,
                         "CUI-ML export failed during validation");
            }
            else
            {
                DesktopDocumentImportResult cuimlImport{};
                if (!DesktopDocumentIO::importCuimlText(document.documentId, document.metadata.sourcePath, cuimlExport.text.data(), cuimlImport) ||
                    cuimlImport.document.controls.size() != document.controls.size())
                {
                    addIssue(outResult,
                             publishMode ? DesktopValidationSeverity::Error : DesktopValidationSeverity::Warning,
                             DesktopValidationCode::RoundTripFailure,
                             nullptr,
                             "CUI-ML round-trip validation failed");
                }
            }
        }
    }

    bool DesktopDocumentValidation::validateForSave(const DesktopDocument &document,
                                                    DesktopDocumentValidationResult &outResult)
    {
        initializeValidationResult(outResult);
        validateDocumentStructure(document, false, outResult);
        if (outResult.errorCount == 0)
            validateRoundTrip(document, false, outResult);
        outResult.valid = (outResult.errorCount == 0);
        return outResult.valid;
    }

    bool DesktopDocumentValidation::validateForPublish(const DesktopDocument &document,
                                                       DesktopDocumentValidationResult &outResult)
    {
        initializeValidationResult(outResult);
        validateDocumentStructure(document, true, outResult);
        if (outResult.errorCount == 0)
            validateRoundTrip(document, true, outResult);
        outResult.valid = (outResult.errorCount == 0);
        return outResult.valid;
    }
}