#pragma once

#include "QCTypes.h"

namespace QK
{
    namespace AIRuntime
    {
        // Loads memoization runtime policy (memo gate + allowlist gate + signature allowlist)
        // from SecureStore sealed storage.
        QC::Status loadPersistentState();

        // Saves the current memoization runtime policy to SecureStore sealed storage.
        QC::Status savePersistentState();

        // Removes persisted runtime policy from SecureStore.
        QC::Status clearPersistentState();

        // Returns true when a persisted runtime policy blob exists.
        bool hasPersistentState();
    } // namespace AIRuntime
} // namespace QK
