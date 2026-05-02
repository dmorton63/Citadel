#pragma once

// QDThemeImporter - Populates the QCQL system database with builtin theme rows.
// Call importBuiltinThemes() once after initializeSystemTables() at boot.
// Namespace: QD

#include "QCQLEngine.h"

namespace QD
{

    class ThemeImporter
    {
    public:
        // Inserts one Themes row and 13 ThemeTokens rows per builtin theme.
        // Skips rows that already exist (idempotent). Returns the first
        // non-Success, non-AlreadyExists status, or Success.
        static QCQL::Status importBuiltinThemes(QCQL::Database &database);
    };

} // namespace QD
