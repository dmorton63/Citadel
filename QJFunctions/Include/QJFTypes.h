#pragma once

// QJFunctions - Lifecycle types for the JSON function registry.
// These types extend the base Function/Engine definitions with state-machine,
// authority, and SC-integration concepts described in JSON_Function_Registry_SPECS.md.
// Namespace: QC::JFunc

#include "QCTypes.h"
#include "QCString.h"
#include "QCVector.h"
#include "QJFunction.h"

namespace QC
{
    namespace JFunc
    {
        // -------------------------------------------------------------------------
        // Lifecycle state machine
        // -------------------------------------------------------------------------

        // State of a single registered function.
        enum class FunctionState : QC::u8
        {
            Unvalidated,  // Newly discovered; schema/auth not yet checked.
            Validated,    // Schema + trust checks passed; content hash stored.
            JitReady,     // SC policy permits JIT compilation for this function.
            JitCompiled,  // Native code pages allocated (RX); ready to call native.
            DllOverride,  // A validated DLL claims this function's stable identity.
        };

        // Execution backend kind.
        enum class FunctionKind : QC::u8
        {
            JsonInterpreted, // Run through Engine::execute().
            JsonJit,         // JIT-compiled native code (requires JitCompiled state).
            DllOverride,     // Routed to a native DLL function pointer.
        };

        // Minimum authority required to call or validate a function.
        enum class AuthorityLevel : QC::u8
        {
            User   = 0,
            Owner  = 1,
            System = 2,
        };

        // Reason a function entry is reset to Unvalidated.
        enum class InvalidateReason : QC::u8
        {
            ContentChanged, // JSON content hash mismatch detected.
            DllChanged,     // DLL binary changed or signature invalidated.
            SSTRotation,    // SST/TAS rotation requires re-validation.
            PolicyRevoked,  // SC policy explicitly revoked this function.
            ManualReset,    // Explicit invalidation request.
        };

        // Device operational state (mirrors QK::SecurityCenter state; kept here
        // as a minimal enum so QJFunctions does not link against QKernel).
        enum class DeviceState : QC::u8
        {
            Unknown,
            Operational,
            SafeMode,
            Recovery,
            Locked,
        };

        // -------------------------------------------------------------------------
        // FunctionEntry — wraps Function + adds lifecycle fields.
        // Owned by QC::JFunc::Registry.
        // -------------------------------------------------------------------------

        // Native call signature for DLL override functions.
        // Must match the calling convention produced by the DLL metadata generator.
        typedef bool (*DllCallFn)(const TypedValue* inputs,
                                  QC::usize          inputCount,
                                  TypedValue*        outputs,
                                  QC::usize          outputCount,
                                  Error&             outErr);

        struct FunctionEntry
        {
            Function       fn;
            FunctionState  state             = FunctionState::Unvalidated;
            FunctionKind   kind              = FunctionKind::JsonInterpreted;
            QC::String     jsonPath;
            QC::String     dllPath;
            AuthorityLevel requiredAuthority = AuthorityLevel::User;
            QC::u8         contentHash[Engine::HASH_BYTES] = {};
            bool           jitAllowed        = false;
            bool           hasDllOverride    = false;
            QC::u64        lastValidatedTick = 0;

            // Non-null only when state == DllOverride and the DLL is loaded.
            DllCallFn      dllCallFn         = nullptr;
        };

        // -------------------------------------------------------------------------
        // SCContext — abstract SC integration interface.
        // Implemented by QKernel (QKSecurityCenter) and injected at boot.
        // QJFunctions itself does not link against QKernel; this interface is the
        // boundary.  All methods are optional-use: callers must null-check sc first.
        // -------------------------------------------------------------------------
        class SCContext
        {
        public:
            virtual ~SCContext() = default;

            // Current device operational state.
            virtual DeviceState    deviceState()      const = 0;

            // Authority level of the active session (User/Owner/System).
            virtual AuthorityLevel sessionAuthority() const = 0;

            // Verify a hex-encoded signature over 'data'.
            // Returns true if the signature is valid per the trust store.
            virtual bool verifySignature(const QC::u8* data,
                                         QC::usize     len,
                                         const char*   sigHex) const = 0;

            // Emit a structured audit log entry.
            virtual void auditLog(const char* event,
                                  const char* detail) const = 0;

            // Monotonic tick counter (milliseconds or rdtsc-derived).
            virtual QC::u64 currentTick() const = 0;
        };

        // -------------------------------------------------------------------------
        // Call inputs/outputs — thin wrappers so Executor callers do not pass
        // raw arrays directly.
        // -------------------------------------------------------------------------

        struct CallInputs
        {
            const TypedValue* values = nullptr;
            QC::usize         count  = 0;
        };

        struct CallOutputs
        {
            TypedValue* values = nullptr;
            QC::usize   count  = 0;
        };

    } // namespace JFunc
} // namespace QC
