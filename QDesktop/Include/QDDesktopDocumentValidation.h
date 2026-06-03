#pragma once

#include "QDDesktopDocument.h"

namespace QD
{
    enum class DesktopValidationSeverity : QC::u8
    {
        Info = 0,
        Warning,
        Error
    };

    enum class DesktopValidationCode : QC::u16
    {
        None = 0,
        EmptyDocument,
        MissingDocumentId,
        MissingControlId,
        DuplicateControlId,
        InvalidParent,
        UnsupportedControlKind,
        MissingGeometry,
        MissingTextOrAsset,
        MissingAssetPath,
        InvalidBinding,
        MissingThemeId,
        NonRuntimeSafeAssetPath,
        RoundTripFailure
    };

    struct DesktopDocumentValidationIssue
    {
        DesktopValidationSeverity severity = DesktopValidationSeverity::Info;
        DesktopValidationCode code = DesktopValidationCode::None;
        char controlId[64]{};
        char message[192]{};
    };

    struct DesktopDocumentValidationResult
    {
        bool valid = false;
        QC::u32 errorCount = 0;
        QC::u32 warningCount = 0;
        QC::Vector<DesktopDocumentValidationIssue> issues;
    };

    class DesktopDocumentValidation
    {
    public:
        static bool validateForSave(const DesktopDocument &document,
                                    DesktopDocumentValidationResult &outResult);

        static bool validateForPublish(const DesktopDocument &document,
                                       DesktopDocumentValidationResult &outResult);
    };
}