V0.1 Specifications:

1. Goals and scope
Goals:
- Provide a JSON‑described function system that can:
- run in interpreter mode (no JIT required),
- optionally JIT to native code under strict policy,
- integrate cleanly with SecurityCenter (SC) and existing trust model.
- Make function execution:
- deterministic,
- auditable,
- authority‑bounded,
- compatible with SST/TAS/owner‑unlock gates.
Out of scope (for v1):
- Cross‑process sandboxing.
- Arbitrary native plugin ABI (beyond “DLL override” hooks you already envisioned).

2. Core concepts
- FunctionId: stable identity (e.g. {namespace}:{name} or GUID).
- FunctionKind: json_interpreted, json_jit, dll_override.
- FunctionState:
- Unvalidated
- Validated
- JitReady
- JitCompiled
- DllOverride
- FunctionSource:
- JSON file (.fn.json)
- DLL module
- AuthorityLevel:
- User
- Owner
- System (SC / kernel‑level)

3. Lifecycle state machine
For each FunctionId, QJF::Registry tracks:
- State: one of the FunctionState values.
- Source: JSON path, DLL path, or both.
- Metadata: hash of JSON, signature info, last validation time, authority requirements.
Allowed transitions:
- Unvalidated → Validated
- Preconditions:
- JSON parsed successfully.
- Schema validated.
- Signature/trust checks (if present) pass.
- Side effects:
- Store content hash.
- Emit audit event: fn_validated.
- Validated → JitReady
- Preconditions:
- Function marked as JIT‑eligible in metadata.
- Security policy allows JIT for this function (SC gate).
- Side effects:
- Emit audit event: fn_jit_ready.
- JitReady → JitCompiled
- Preconditions:
- JIT allocator available.
- RW→RX policy enforced (see JIT section).
- Side effects:
- Allocate RX pages.
- Emit audit event: fn_jit_compiled.
- * → DllOverride
- Preconditions:
- DLL present and validated (signature, trust store).
- SC approves override (owner‑unlock if required).
- Side effects:
- Mark JSON path as “shadowed by DLL”.
- Emit audit event: fn_dll_override.
- Any state → Unvalidated
- On:
- JSON content change (hash mismatch).
- DLL change.
- SST rotation / TAS rewrap requiring revalidation.
- Side effects:
- Invalidate JIT code.
- Emit audit event: fn_invalidated.

4. Trust and authority model
Each function has:
- RequiredAuthority: User, Owner, or System.
- Capabilities: optional list (e.g. ["fs.read", "net.http", "ui.modify"]).
Execution rules:
- User functions:
- Can be called from user context.
- Cannot escalate to Owner or System without SC‑mediated gate.
- Owner functions:
- Require owner‑unlock session or explicit SC approval.
- Logged with higher‑sensitivity audit events.
- System functions:
- Only callable from SC / kernel / trusted modules.
- Never exposed directly to user‑level callers.
SC enforces:
- Whether a function can be:
- validated,
- JIT‑enabled,
- overridden by DLL,
- executed in current session state (LOCKED/UNLOCKED/etc).

5. Execution semantics (interpreter mode)
Op registry:
- QJF::OpRegistry maps opcode names → handler functions.
- Each handler has:
- name
- minArgs, maxArgs
- requiresAuthority (optional)
- isPure (for future memoization)
Step execution:
- Function JSON defines steps[], each with:
- op
- args[]
- optional label, next, branch fields.
Interpreter loop:
- Initialize ExecutionContext:
- locals map
- inputs
- outputs (initially empty)
- call stack (for nested calls)
- Start at pc = 0.
- For each step:
- Lookup opcode in OpRegistry.
- Check authority (via SC if needed).
- Execute handler with current context.
- Update pc based on:
- sequential flow,
- explicit next,
- branch result (for if, switch, etc).
- Terminate on:
- return op,
- pc out of range,
- error (propagate to caller).
Determinism:
- No implicit global state mutation.
- All side‑effects must go through explicit ops (e.g. fs_write, net_request) that SC can gate and audit.

6. JIT security model
Allocator:
- QJF::JitAllocator:
- Allocates RW pages for codegen.
- After codegen, flips to RX (never RW+RX).
- Frees pages on invalidation or SST rotation.
Rules:
- JIT is optional:
- Interpreter mode must always be available.
- JIT allowed only if:
- Function is Validated.
- SC policy allows JIT for this function.
- Current device state is OPERATIONAL (not SAFE_MODE/RECOVERY).
- JIT code:
- Must not outlive its function’s validation (invalidate on JSON/DLL change).
- Must be tied to current SST/TAS context (invalidate on rotation if needed).
Execution:
- Call path:
- If JitCompiled and allowed → call native entrypoint.
- Else → fall back to interpreter.

7. Persistence and VFS layout
Registry snapshot:
- File: /system/fn/FNREG.BIN or /system/fn/FNREG.JSON (pick one).
- Contains:
- FunctionId
- State
- Source paths
- Content hash
- RequiredAuthority
- Capabilities
- Flags (jit_allowed, dll_override_present, etc).
Function definitions:
- JSON files under /system/fn/*.fn.json.
- DLLs under /system/modules/*.dll (or your chosen extension).
VFS mount:
- /system/fn/ must be readable via QFileSystem VFS.
- Registry and editor enumerate functions via VFS, not raw SecureStore.

8. Boot behavior
After VFS is ready:
- Scan /system/fn/:
- For each .fn.json:
- Create/refresh registry entry in Unvalidated state.
- Scan /system/modules/:
- For each DLL:
- If it declares function overrides, mark corresponding entries as DllOverride (pending validation).
- Optionally pre‑validate:
- For core/system functions, SC may validate at boot.
- Emit boot event:
- fn_registry_initialized with counts:
- total functions
- validated
- jit_ready
- dll_overrides

9. Public APIs (high‑level sketch)
Registry:
- QJF::Registry::get(FunctionId) -> FunctionEntry*
- QJF::Registry::validate(FunctionId, SCContext&)
- QJF::Registry::markJitReady(FunctionId, SCContext&)
- QJF::Registry::markDllOverride(FunctionId, SCContext&)
- QJF::Registry::invalidate(FunctionId, Reason)
Execution:
- QJF::Executor::call(FunctionId, Inputs, SCContext&, Outputs&)
- Handles:
- state checks,
- authority checks,
- JIT vs interpreter selection,
- error propagation.
JIT:
- QJF::JitAllocator::allocate(size)
- QJF::JitAllocator::finalizeToRX(ptr, size)
- QJF::JitAllocator::free(ptr)



##  All of the items below must pass to mark this complete!

1. Concrete data structures
FunctionId
- Shape: struct FunctionId { QString ns; QString name; };
- Canonical form: ns + ":" + name for hashing/lookup.
FunctionState
- enum class FunctionState { Unvalidated, Validated, JitReady, JitCompiled, DllOverride };
AuthorityLevel
- enum class AuthorityLevel { User, Owner, System };
FunctionEntry
- Fields:
- FunctionId id;
- FunctionState state;
- FunctionKind kind; (JsonInterpreted, JsonJit, DllOverride)
- QString jsonPath;
- QString dllPath;
- AuthorityLevel requiredAuthority;
- QVector<QString> capabilities;
- QByteArray contentHash;
- bool jitAllowed;
- bool hasDllOverride;
- quint64 lastValidatedTick;

2. Registry API surface
QJF::Registry
- Core:
- FunctionEntry* find(const FunctionId&);
- FunctionEntry& ensure(const FunctionId&);
- bool validate(FunctionId, SCContext&);
- bool markJitReady(FunctionId, SCContext&);
- bool markDllOverride(FunctionId, SCContext&);
- void invalidate(FunctionId, InvalidateReason);
- Persistence:
- bool loadSnapshot(const QString& path);
- bool saveSnapshot(const QString& path) const;
- Boot:
- void scanJsonFunctions(QFS::IVfs& vfs);
- void scanDllModules(QFS::IVfs& vfs);

3. Execution API
QJF::Executor
- bool call(const FunctionId& id, const QJF::Inputs& in, SCContext& sc, QJF::Outputs& out);
Execution path:
- Lookup FunctionEntry.
- Check state:
- If Unvalidated → attempt validate(...) (SC‑gated).
- Check requiredAuthority vs SCContext.
- If state == JitCompiled and SC allows JIT → call native.
- Else → run interpreter over steps[].

4. Interpreter core
OpRegistry
- struct OpHandler { QString name; int minArgs; int maxArgs; AuthorityLevel requiredAuthority; bool isPure; bool (*fn)(ExecutionContext&, const Step&); };
- QMap<QString, OpHandler> g_ops;
ExecutionContext
- Locals map (string → value).
- Inputs, outputs.
- int pc;
- Call stack (vector of { returnPc, frameLocals }).
Loop:
- While pc in range:
- Fetch Step& s = steps[pc];
- Lookup handler in OpRegistry.
- SC authority check if requiredAuthority > User.
- Call handler.
- Handler sets:
- ctx.pc++ (default),
- or jumps (ctx.pc = labelIndex),
- or signals return.

5. JIT model (minimal but strict)
QJF::JitAllocator
- void* allocateRW(size_t size);
- bool finalizeToRX(void* ptr, size_t size);
- void free(void* ptr);
Rules:
- Only called from Registry::markJitReady/markJitCompiled paths.
- Never expose RW+RX simultaneously.
- On invalidate(...):
- Free JIT pages.
- Reset state to Unvalidated.

6. SC integration points
SCContext (already exists conceptually):
- Exposes:
- current device state (OPERATIONAL, SAFE_MODE, etc),
- current session authority (User, Owner, System),
- trust store access,
- audit logging.
Registry uses SC to:
- Approve validation (validate(...)):
- check signatures, hashes, trust store.
- Approve JIT:
- only in OPERATIONAL,
- only if policy allows JIT for this function.
- Approve DLL override:
- verify DLL signature,
- enforce owner‑unlock if required.
Executor uses SC to:
- Enforce requiredAuthority.
- Gate privileged ops (fs, net, etc).
- Emit audit events:
- fn_called, fn_denied, fn_error.

7. Boot sequence hooks
After VFS is mounted:
- registry.scanJsonFunctions(vfs);
- registry.scanDllModules(vfs);
- Optionally:
- Pre‑validate core/system functions.
- Emit boot event:
- fn_registry_initialized { total, validated, dllOverrides }.

8. Minimal v1 success criteria
You can call this “v1 done” when:
- You can drop a .fn.json into /system/fn/, boot, and:
- see it in the registry,
- validate it,
- execute it in interpreter mode.
- JIT allocator exists and can:
- allocate RW,
- flip to RX,
- free on invalidate (even if no real codegen yet).
- DLL override path:
- recognizes a DLL that claims to implement a function,
- marks the entry as DllOverride,
- routes calls to a stub native function.
