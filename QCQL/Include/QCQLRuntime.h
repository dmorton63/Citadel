#pragma once

// QCQL Runtime - Canonical handle-based execution surface
// Namespace: QCQL::Runtime

#include "QCQLEngine.h"

namespace QCQL
{
    namespace Runtime
    {
        struct HandleOpenOptions
        {
            QC::u32 callerProcessId = 0;
            const TablePermission *tablePermissions = nullptr;
            QC::usize tablePermissionCount = 0;
            bool enforceProcessBinding = false;
            bool enforceTablePermissions = false;
        };

        struct QueryResult
        {
            Status status = Status::Error;
            QC::u32 rowsAffected = 0;
            char output[8192] = {};
            char diagnostic[256] = {};
        };

        Status openHandle(const char *path,
                          DbHandle &outHandle,
                          bool createIfMissing = false,
                          const HandleOpenOptions *options = nullptr);
        Status closeHandle(DbHandle &ioHandle);
        Status execute(DbHandle &handle, const char *query, QueryResult &outResult);
        Status insertRowByName(DbHandle &handle,
                       const char *tableName,
                       const Row &row,
                       QC::u32 *outPageId = nullptr,
                       QC::u16 *outRowOffset = nullptr);

        // Transitional bridge for existing callers that still need direct Database access.
        Status borrowDatabase(DbHandle &handle, Database *&outDatabase);

        // Returns the SchemaIntegrityReport captured when the handle was opened.
        // Returns NotFound if the handle is not open.
        Status getIntegrityReport(DbHandle &handle, SchemaIntegrityReport &outReport);

        const char *statusName(Status st);
    }
}
