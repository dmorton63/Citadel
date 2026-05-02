#pragma once

// QJFunctions - Executor: lifecycle-aware call dispatch.
// Namespace: QC::JFunc
//
// Executor::call() is the single public entry point for invoking a registered
// function.  It:
//   1. Looks up the FunctionEntry by stable identity.
//   2. Optionally attempts validation if the entry is still Unvalidated.
//   3. Enforces requiredAuthority against the current SC session.
//   4. Routes the call to the correct backend:
//        DllOverride  → dllCallFn()
//        JitCompiled  → native entrypoint (v1: falls through to interpreter)
//        Validated    → Engine::execute()
//   5. Emits SC audit events for fn_called / fn_denied / fn_error.

#include "QJFTypes.h"

namespace QC
{
    namespace JFunc
    {
        class Registry;

        class Executor
        {
        public:
            // Call a function by stable identity.
            //
            //   registry — must not be null; owns the FunctionEntry.
            //   sc       — may be null for boot/debug use; authority checks
            //              and audit logging are skipped when null.
            //
            // Returns false on any failure; outErr is populated with the reason.
            static bool call(Registry&         registry,
                             const char*       stableIdentity,
                             const CallInputs& in,
                             SCContext*        sc,
                             CallOutputs&      out,
                             Error&            outErr);

        private:
            Executor() = delete;
        };

    } // namespace JFunc
} // namespace QC
