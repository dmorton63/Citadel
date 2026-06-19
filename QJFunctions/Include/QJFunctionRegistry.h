#pragma once

// QJFunctionRegistry - lifecycle-aware JSON function registry
// Namespace: QC::JFunc
//
// The Registry is the single source of truth for all registered functions.
// It owns FunctionEntry objects and drives the lifecycle state machine
// (Unvalidated → Validated → JitReady → JitCompiled / DllOverride).

#include "QJFTypes.h"

namespace QFS { class VFS; }

namespace QC
{
    namespace JFunc
    {
        class Registry
        {
        public:
            static Registry& instance();

            // ------------------------------------------------------------------
            // Legacy MVP API (preserved for existing callers)
            // ------------------------------------------------------------------

            // Register a pre-parsed Function.  Fails on duplicate stableIdentity.
            bool registerFunction(Function&& fn, const char* jsonPath);

            // Lookup by name + version (linear scan).
            const Function* find(const char* name, QC::u32 version) const;

            // Lookup by stable identity string (linear scan).
            const Function* findByStableIdentity(const char* stableIdentity) const;

            // Total number of registered entries.
            QC::usize count() const { return m_entries.size(); }

            // ------------------------------------------------------------------
            // Lifecycle API
            // ------------------------------------------------------------------

            // Mutable lookup by stable identity.  Returns nullptr if not found.
            FunctionEntry* findEntry(const char* stableIdentity);
            const FunctionEntry* findEntry(const char* stableIdentity) const;

            // Validate an entry: compute content hash, run schema/signature checks.
            // Transitions Unvalidated → Validated on success.
            // sc may be null (signature verification is skipped when sc is null).
            bool validate(const char* stableIdentity, SCContext* sc);

            // Enable or disable extra JIT lifecycle logging.
            void setJitDebugMode(bool enabled) { m_jitDebugMode = enabled; }
            bool jitDebugMode() const { return m_jitDebugMode; }

            // Mark JIT-eligible: transitions Validated → JitReady.
            // Requires: state == Validated, entry->jitAllowed == true,
            //           sc != null, sc->deviceState() == Operational.
            bool markJitReady(const char* stableIdentity, SCContext* sc);

            // Register a DLL override: transitions any state → DllOverride.
            // sc must not be null; DLL signature is verified via sc->verifySignature.
            // dllPath       — path recorded in the entry.
            // callFn        — pointer to the resolved native function; may be null
            //                 for v1 (DLL loader not yet implemented; path recorded).
            bool markDllOverride(const char*  stableIdentity,
                                 SCContext*   sc,
                                 const char*  dllPath,
                                 DllCallFn    callFn);

            // Reset an entry to Unvalidated and free any JIT pages.
            // reason is emitted to the audit log if sc != null.
            void invalidate(const char* stableIdentity,
                            InvalidateReason reason,
                            SCContext*  sc);

            // Invalidate all entries (e.g. on SST rotation).
            void invalidateAll(InvalidateReason reason, SCContext* sc);

            // ------------------------------------------------------------------
            // VFS boot scanner
            // ------------------------------------------------------------------

            // Scan /system/fn/ for *.fn.json files and create/refresh entries.
            // Existing entries whose content hash changed are reset to Unvalidated.
            void scanJsonFunctions(QFS::VFS& vfs);

            // Scan /system/modules/ for *.dll files and note DLL candidates.
            // v1: records dllPath on existing entries whose name matches the file
            // stem; actual DLL loading deferred until DLL loader is implemented.
            void scanDllModules(QFS::VFS& vfs);

            // ------------------------------------------------------------------
            // Snapshot persistence  (/system/fn/FNREG.BIN)
            // ------------------------------------------------------------------

            // Load lifecycle metadata from a snapshot file.
            // Function definitions themselves are always re-read from .fn.json at boot.
            bool loadSnapshot(QFS::VFS& vfs, const char* path);

            // Save lifecycle metadata to a snapshot file.
            bool saveSnapshot(QFS::VFS& vfs, const char* path) const;

        private:
            Registry() = default;
            Registry(const Registry&) = delete;
            Registry& operator=(const Registry&) = delete;

            QC::Vector<FunctionEntry> m_entries;
            bool m_jitDebugMode = false;
        };

    } // namespace JFunc
} // namespace QC
