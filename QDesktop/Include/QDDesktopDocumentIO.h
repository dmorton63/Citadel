#pragma once

#include "QDDesktopDocument.h"
#include "QCQLEngine.h"

namespace QD
{
    struct DesktopDocumentImportResult
    {
        bool loaded = false;
        DesktopDocument document{};
        char sourcePath[192]{};
        char error[192]{};
    };

    struct DesktopDocumentExportResult
    {
        bool generated = false;
        QC::Vector<char> text;
        char error[192]{};
    };

    struct DesktopDocumentSaveResult
    {
        bool saved = false;
        char sourcePath[192]{};
        QC::u32 chunkCount = 0;
        char error[192]{};
    };

    class DesktopDocumentIO
    {
    public:
        static bool importCuimlText(const char *documentId,
                                    const char *sourcePath,
                                    const char *cuimlText,
                                    DesktopDocumentImportResult &outResult);

        static bool importJsonText(const char *documentId,
                                   const char *sourcePath,
                                   const char *jsonText,
                                   DesktopDocumentImportResult &outResult);

        static bool importCmmsCuiml(const QCQL::Database &database,
                                    const char *documentId,
                                    DesktopDocumentImportResult &outResult);

        static bool importCmmsJson(const QCQL::Database &database,
                                   const char *documentId,
                                   DesktopDocumentImportResult &outResult);

        static bool importCmmsRuntime(const QCQL::Database &database,
                          const char *documentId,
                          DesktopDocumentImportResult &outResult);

        static bool exportCuimlText(const DesktopDocument &document,
                                    DesktopDocumentExportResult &outResult);

        static bool exportJsonText(const DesktopDocument &document,
                                   DesktopDocumentExportResult &outResult);

        static bool saveCmmsCuiml(QCQL::Database &database,
                                  const DesktopDocument &document,
                                  DesktopDocumentSaveResult &outResult);

        static bool saveCmmsJson(QCQL::Database &database,
                                 const DesktopDocument &document,
                                 DesktopDocumentSaveResult &outResult);
    };
}