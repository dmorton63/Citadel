#include "QJFExecutor.h"
#include "QJFunctionRegistry.h"
#include "QCLogger.h"

namespace QC
{
    namespace JFunc
    {
        namespace
        {
            static constexpr const char* LOG_MODULE = "QJFExecutor";

            static void setErr(Error& e, ErrorCode code, const char* msg)
            {
                e.code      = code;
                e.message   = msg;
                e.stepIndex = 0xFFFFFFFFu;
            }

            // Returns true if 'session' meets or exceeds 'required'.
            static bool authorityMet(AuthorityLevel required, AuthorityLevel session)
            {
                return static_cast<QC::u8>(session) >= static_cast<QC::u8>(required);
            }
        }

        bool Executor::call(Registry&         registry,
                            const char*       stableIdentity,
                            const CallInputs& in,
                            SCContext*        sc,
                            CallOutputs&      out,
                            Error&            outErr)
        {
            if (!stableIdentity || !*stableIdentity)
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "stableIdentity is null/empty");
                return false;
            }

            // ----------------------------------------------------------------
            // 1. Look up entry.
            // ----------------------------------------------------------------
            FunctionEntry* entry = registry.findEntry(stableIdentity);
            if (!entry)
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "function not found in registry");
                if (sc)
                    sc->auditLog("fn_denied", stableIdentity);
                return false;
            }

            // ----------------------------------------------------------------
            // 2. If Unvalidated, attempt validation now (best-effort at call
            //    time; caller should pre-validate during boot for performance).
            // ----------------------------------------------------------------
            if (entry->state == FunctionState::Unvalidated)
            {
                if (!registry.validate(stableIdentity, sc))
                {
                    setErr(outErr, ErrorCode::E_AUTH, "function failed validation");
                    if (sc)
                        sc->auditLog("fn_denied", stableIdentity);
                    return false;
                }
            }

            // After validation attempt, recheck state.
            if (entry->state == FunctionState::Unvalidated)
            {
                setErr(outErr, ErrorCode::E_AUTH, "function could not be validated");
                if (sc)
                    sc->auditLog("fn_denied", stableIdentity);
                return false;
            }

            // ----------------------------------------------------------------
            // 3. Authority check.
            // ----------------------------------------------------------------
            if (sc)
            {
                const AuthorityLevel session = sc->sessionAuthority();
                if (!authorityMet(entry->requiredAuthority, session))
                {
                    setErr(outErr, ErrorCode::E_AUTH, "insufficient authority");
                    sc->auditLog("fn_denied", stableIdentity);
                    QC_LOG_WARN(LOG_MODULE, "fn_denied authority identity=%s required=%u session=%u",
                                stableIdentity,
                                static_cast<QC::u32>(entry->requiredAuthority),
                                static_cast<QC::u32>(session));
                    return false;
                }
            }

            // ----------------------------------------------------------------
            // 4. Route to correct backend.
            // ----------------------------------------------------------------
            bool ok = false;

            switch (entry->state)
            {
            case FunctionState::DllOverride:
                if (entry->dllCallFn)
                {
                    ok = entry->dllCallFn(in.values, in.count, out.values, out.count, outErr);
                }
                else
                {
                    setErr(outErr, ErrorCode::E_OP, "DllOverride entry has no call function");
                    ok = false;
                }
                break;

            case FunctionState::JitCompiled:
                // v1: JIT codegen not yet wired; fall through to interpreter.
                // When a real codegen backend exists, call the native entrypoint here.
                QC_LOG_INFO(LOG_MODULE, "JitCompiled fallback to interpreter identity=%s", stableIdentity);
                // fall through
            case FunctionState::JitReady:
            case FunctionState::Validated:
                ok = Engine::execute(entry->fn,
                                     in.values,
                                     in.count,
                                     out.values,
                                     out.count,
                                     outErr);
                break;

            case FunctionState::Unvalidated:
                // Already handled above; should not reach here.
                setErr(outErr, ErrorCode::E_AUTH, "unvalidated function reached execute path");
                ok = false;
                break;
            }

            // ----------------------------------------------------------------
            // 5. Audit.
            // ----------------------------------------------------------------
            if (sc)
            {
                if (ok)
                    sc->auditLog("fn_called", stableIdentity);
                else
                    sc->auditLog("fn_error", stableIdentity);
            }

            if (!ok)
            {
                QC_LOG_WARN(LOG_MODULE, "fn_error identity=%s code=%u",
                            stableIdentity,
                            static_cast<QC::u32>(outErr.code));
            }

            return ok;
        }

    } // namespace JFunc
} // namespace QC
