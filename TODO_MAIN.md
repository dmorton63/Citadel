# TODO (Main)

Do not deviate from this list until it is done!

Generated: 2026-03-25
Sources scanned: 28 Markdown files (excluding build/, backups/, .git/; including backups/todo_archive_*/)

## Critical
- [x] Create a signing/hash check so only trusted modules load during secure boot. (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L43))
- [x] Allow Security Center to throttle or isolate flows (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L37))
- [x] Create project structure for Security Center (SC) subsystem (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L19))
- [x] Define **SST (System Security Token)** as a rotatable secret used to derive runtime keys (implemented in `QSecurityCenter/*` + wired via `QKernel/Src/QKSecurityCenter.cpp`; wrapped SST stored in SecureStore under `/system/sc`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L73))
- [x] Implement minimal Owner credential backend for elevation (no DB): persist salted verifier + KDF params in SC storage; add `SYS_USER_ENROLL` / `SYS_USER_UNLOCK` / `SYS_USER_LOCK` + attempt backoff; expose “unlocked session” state for `chmode admin/system` checks. (implemented in `QK::SecurityCenter` + wired into desktop terminal; commands: `sys_user_enroll/sys_user_unlock/sys_user_lock`)

  - Dev note (dev persistence): prefer running with `./build.sh -r --system-vol` so `/system` is backed by `build/system.qcow2` and SecureStore blobs (e.g., `/system/sc/OWNERCRD`) persist across reboots and rebuilds. Use `sysformat` once to initialize the volume, and `sysmount` to re-mount without formatting.
  - Fallback dev note: `./build.sh -r` regenerates the ramdisk each run; if you are not using `--system-vol`, you can seed the SecureStore blob in `ramdisk/system/sc/OWNERCRD`.
- [x] Expose Task_Flow metrics to Security Center (implemented as `QSC::SecurityCenter::taskFlowMetrics()` sourced from `QQ::Executor` counters; surfaced in `regdump` output). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L36))
- [x] Define non-TPM secure-bootstrapping path (still encrypted-at-rest, but weaker): recovery code → KDF → wraps the anchor secret (implemented as a boot-time recovery code prompt that derives a key (PBKDF2-HMAC-SHA256) to wrap the SecureStore anchor (`WRAPKEY.KDF`), migrating legacy `WRAPKEY.BIN`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L55))
- [x] Implement `seal_secret()` / `unseal_secret()` abstraction (TPM-backed; stubbed fallback) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L51)) — added SecureStore API + TPM-backed blob sealing; non-TPM returns NotSupported
- [x] Implement `tpm_present()` probe (implemented as `QK::SecureStore::tpm_present()` in `QKernel/*`; runtime state wired via `QKernel/Src/QKSecurityCenter.cpp`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L50))
- [x] Add TPM‑accelerated sealing for AI metadata (optional) (implemented as `QK::SecureStore::writeTpmSealedBlob()` / `readTpmSealedBlob()` which seal a per-blob content key via TPM policy when available, and fall back to `writeSealedBlob()`/`readSealedBlob()` otherwise). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L38))
- [x] Define “TPM Anchor Secret” (TAS): persistent secret sealed to the machine (optionally to measurements/PCRs later) (implemented as the existing SecureStore anchor wrap key; now exposed explicitly as `QK::SecureStore::readTas()` / `getOrCreateTas()` and used as the input to SRK derivation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L52))

## Needed Now
- [x] Add a `startx`-style console command that bootstraps the framebuffer, window manager, and desktop when running in TERMINAL startup mode. (implemented as kernel console built-in `startx` calling `QK::Boot::Desktop::InitializeWindowSystem()` + `InitializeDesktopAndRunLoop()`; console input handoff via `QK::Console::setInputEnabled(false)`) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L48))
- [x] Implement disguised SC initialization call inside `kernel_main()` (implemented as an early silent `QK::SecurityCenter::initialize(...)` hook in `kernel_main()` after startup config/driver bring-up, while non-TPM recovery-code unlock + SST provisioning remain in desktop boot). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L29))
- [x] Pipe boot logs into a ring buffer that both the console and desktop log viewer can tail. (implemented via shared `QK::Boot::Log` ring buffer fanout + `bootlog tail [lines]` command exposed through command registry, usable from kernel console and desktop terminal). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L51))
- [x] Refresh UI/boot logo artwork so the visual mark reads "CITADEL" while the system name/code remains QAIOSPLUSV1. (updated visible desktop/setup/runtime branding to CITADEL while preserving kernel/system references as QAIOSPLUSV1). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L4))
- [x] Update build scripts to emit module bundles separately from the core kernel image. (build artifacts now emit separately under `build/artifacts/kernel/` and `build/artifacts/modules/`, with ISO staging consuming those outputs while preserving legacy compatibility copies). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L41))

## Needed (Non-Critical)
- [ ] Sketch and stage a unified button model so one base button implementation can support text, icon-only, and text+icon variants without a boolean-flag sprawl. (dev note: prefer shared button state/input handling with content/variant modes; `IconButton` can remain as a thin wrapper if that keeps call sites clearer.)
- [ ] Restore desktop icon actions to use `IconButton` controls instead of plain buttons once the current database work is complete. (dev note: rounded-button testing temporarily replaced icons with buttons; keep the icon path as the intended UI control model.)
- [ ] Hold off further Citadel-side CQL service-port work until indexes and relationships are finished in the standalone Visual Studio 2026 model, then resume the service integration against that shape.
- [ ] Add a database-driven theme runtime path so desktop theme resolution can come from QCQL/CQL tables at boot instead of external files. (dev note: do not start this until CQL is fully integrated into the system; target end state is trusted boot-time theme queries/materialization with builtin fallback.)
- [ ] After the database work is in a stable place, revisit the low-level video/rendering path (`QDrvSVGA`, compositor, present/update flow) for robustness and efficiency under current desktop usage.
- [ ] Audit the current video stack to determine whether shape/style limits (round, ellipse, square, rectangle, solid, transparent, glass) come from window/control design, style/render abstractions, or the low-level drawing/video path.
- [ ] Explore a future higher-end graphics path after the robustness pass: verts/frags/shader-like primitives or an equivalent Citadel-native rendering abstraction that can grow beyond the current immediate-mode path.
- [ ] Fix keyboard dual-state and key-combination handling, including incorrect grave/tilde mapping so the backtick/tilde key no longer renders as `?`.
- [x] AI integration: define stable function identity + canonical input representation (bytes + schema/version). (implemented in `QJFunctions` as validated `stableIdentity` generation plus `Engine::encodeCanonicalInputs(...)`, with registry dedupe on stable identity and a versioned canonical input byte format.)
- [x] AI integration: implement signature hash + input hash (e.g., SHA-256) and log per-call identity + timing. (implemented in `QJFunctions` as SHA-256 signature hashing over validated function schema, SHA-256 hashing of canonical input bytes, and per-call execution logging with stable identity, hashes, status, and cycle timing.)
- [x] AI integration: collect execution + build timing metrics, then use them to drive execution queues/scheduling decisions. (implemented in `QQExecutor` as retained build/exec timing metrics, per-signature timing history, adaptive priority selection, and ready-task pumping in priority order; surfaced via `taskFlowMetrics()`, `regdump`, and `taskls`.)
- [x] AI integration: implement in-memory `(signature, input_hash) -> result` cache with hard cap + eviction. (implemented in `QQExecutor` as fixed-cap ring cache keyed by signature/input hash with overwrite eviction and status telemetry for entries/cap/evictions via `memocache status`.)
- [x] AI integration: gate caching behind allowlist + safety rules (pure functions only; no I/O; no nondeterminism). (implemented in `QQExecutor` cached submit path: allowlist gate + strict safety rules requiring canonical inputs, stateless context, and side-effect domain denylist (`io/fs/net/time/rand/security/driver`), with `memocache status` telemetry for safety rejections.)
- [x] AI integration: define/implement a working AI runtime + persistence for the above (currently may not exist). (implemented as `QK::AIRuntime` persisted state in SecureStore (`AIRTIME.BIN`) for memoization runtime policy and allowlist, auto-loaded during early boot security runtime init, and surfaced via `airuntime status|load|save|clear`.)
- [x] Add a test harness that runs command handlers against captured console transcripts to avoid regressions. (implemented as `transcripttest <path> [unsafe]` in `QKCommandCenter`, replaying `> ...` transcript commands through `QC::Cmd::Registry` with per-command expected-output matching, role simulation (`admin/su/system/user`), and safety skips for destructive commands unless `unsafe` is passed.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L31))
- [x] Add debug visualizer for Task_Flow graphs (implemented as `taskflowviz [N]` in `QKCommandCenter`, emitting Mermaid graph text (`graph LR`) for recent task nodes + dependency edges from `QQ::Executor` task descriptors.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L42))
- [x] Document a style guide (colors, typography, animation beats) that desktop widgets and boot flows can reference. (implemented in `docs/UI_STYLE_GUIDE.md` with shared color roles, typography scale, animation timing beats, boot-flow visual rules, and widget consistency checklist.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L7))
- [x] Introduce a history buffer plus `history`/`!n` recall for faster debugging. (implemented in `QCommand/QCCommandRegistry` as a shared ring-buffer history (`history [N]`) with monotonic indices and `!n` replay support in the common execute path used by kernel console and desktop command processor.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L33))
- [x] Add kernel init hook for silent SC bring‑up (implemented via the early `kernel_main()` Security Center initialization hook; recovery-code prompts stay deferred to the desktop/session path). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L21))
- [x] Define a `.drv`/`.dll` packaging spec (header, metadata, relocation table) for loadable kernel modules. (defined in `docs/MODULE_PACKAGING_SPEC.md` as `CITM` container v1 with fixed header, section/relocation/import tables, string table, validation rules, and x86_64 MVP relocation types.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L38))
- [x] Define audit event taxonomy (provisioning, boot trust, update verify, exec approve/deny, SST rotation, user unlock/lock) (defined in `docs/SC_AUDIT_EVENT_TAXONOMY.md` with canonical event IDs/classes, severities, record shape, and correlation rules for SC audit logging.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L156))
- [x] Ensure naming standards stay consistent (volumes stay `QFS_*`, drivers stay `QDRV_*`) when new devices are surfaced. (enforced by existing `QKStorageRegistry` `QFS_` gate plus new `QDRV_*` stable driver IDs on surfaced drivers (`driverId()` in `QKDrv::DriverBase` implementations) and surfacing-time validation/logging in `QKDrvManager`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L59))
- [x] Expand `QFileSystem` device discovery so it can enumerate block devices beyond the ramdisk (e.g., SATA/NVMe exposed by QDrivers). (implemented IDE discovery expansion via `QKDrv::IDE::probeAndRegisterDataVolumes()` to enumerate additional FAT-capable block devices and register them as `QFS_DISK*` under `/mnt/disk*`, wired into `QKDrvManager::probeStorage()` alongside existing `/system` and `/shared` probes.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L54))
- [x] For v1: use TAS unseal success + SC integrity checks as the boot trust gate (implemented via `QK::SecurityCenter::checkBootTrustGate()` (`ensureSst` + SST availability/generation integrity) and enforced in startup flow under `Mode::Enforce` before desktop bring-up, falling back to terminal-only safe path on failure.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L93))
- [x] Implement a minimal DHCPv4 client and run it at boot to auto-configure IPv4 (IP/mask/gateway/DNS); fall back to manual `ip set` if DHCP fails. (already implemented in `kernel/Boot/Desktop/QKBootDesktopSession.cpp`: bounded boot-time `QNet::DHCPv4Client` DORA with IP/mask/gateway/DNS apply and explicit timeout/skip fallback logs, plus manual override via `ip set`/`ip dhcp` in `QKCommandCenter`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L69))
- [x] Implement kernel-side dispatchers (implemented in `QK::SecurityCenter::dispatch(...)` with `DispatchOp`/`DispatchRequest`/`DispatchResult` bridging trust/update/rotation calls and returning structured status for pending handlers). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L178))
- [x] Keep SST out of normal filesystem storage; persist only **wrapped SST** (encrypted and integrity-protected) in SC storage (wrapped SST persistence remains via `QSC::SstStorageProvider` (`SSTWRAP` sealed blob), and legacy plaintext `SST.BIN` is now proactively removed during SC init). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L74))
- [x] Keep “PCR-based measured boot attestation” as a later enhancement (documented as deferred in `docs/BUILD_SIGNING.md` while v1 trust gate remains TAS/SST + SC integrity-based). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L92))
- [x] Persist a registry/alias map so commands can be extended or overridden without editing the kernel image. (implemented in `QCommand::Registry` alias map + expansion and persisted loader/saver in `QKCommandCenter` at `/system/config/CMDALIAS.CFG`, surfaced via `alias`/`unalias`/`aliasreload`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L30))
- [x] Provide a VFS-backed `stat` API so tooling can query file metadata without touching filesystem internals. (implemented as `QFS::statPath()` in `QFileSystem` plus `stat <path>` command integration in command center.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L62))
- [x] Provide lifecycle hooks (`init`, `start`, `stop`) so modules can register/unregister drivers at runtime. (implemented in `QKDrv::Manager` via lifecycle hook registration/unregistration and ordered `init/start/stop` execution around driver manager startup/shutdown.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L40))
- [x] Replace Limine's basic terminal view with an in-kernel terminal that mirrors serial.log output so we get on-screen feedback without relying on external tailing. (boot now prefers in-kernel framebuffer terminal mirroring via `QK::Debug::FramebufferText` with Limine terminal only as fallback.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L47))
- [x] Support command scripts (`source file.cmd`) so repetitive bootstraps can be automated. (implemented as `source <file.cmd>` command in `QKCommandCenter` with bounded nesting, comment/blank skipping, and per-run command/failure summary.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L34))
- [x] Add a unit-test corpus of tiny icons and malformed headers so the decoder path is robust before UI hooks it up. (implemented in `QGraphics` as `runPngDecoderCorpus(...)` with tiny valid PNG + malformed header/signature/truncation cases, executed once during desktop init with logged pass/fail counts.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L15))
- [x] After feature design is complete: position **User Registration / Login** UI *before* the Desktop is created (pre-desktop gate), so enrollment/unlock can happen prior to any desktop initialization. (implemented as pre-desktop owner enrollment/unlock gate in boot desktop session before any desktop initialization path proceeds.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L108))
- [x] Audit only the visible logo placements (splash, desktop, installer) for the CITADEL art update—no internal renames. (audited and updated desktop top bar, setup wizard title, terminal banner, and shipped seasonal desktop assets only). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L5))
- [x] Build a loader that can fetch those binaries from disk/ramdisk on demand and resolve their dependencies. (implemented as `QK::Module::Loader` (`QKernel/Src/QKModuleLoader.cpp`) with catalog parsing, recursive dependency resolution, and on-demand module binary fetch via VFS; surfaced by `modfetch <module_id>`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L39))
- [x] Build a preview CLI command that dumps decoded surfaces to verify the pipeline without launching the desktop. (implemented as `imgpreview <path>` in `QKCommandCenter`, printing decoded format, dimensions, pixel count, and first-pixel value.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L16))
- [x] Build a structure to hold the commands (implemented as shared `QK::CmdCenter::CommandPacket` envelope + packet execution path used by command frontends.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L24))
- [x] Expose an IPC hook so future GUI shells can reuse the parser without reimplementing every command. (implemented as `QK::CmdCenter::setIpcHook(...)` + `executePacket(...)`, and desktop command processor now routes through the shared packet path.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L35))
- [x] Implement an Image Reader service capable of loading bitmap/PNG assets so the desktop can render icons and other artwork. (implemented as `QK::ImageReader` (`QKernel/Src/QKImageReader.cpp`) with PNG + uncompressed BMP loading and format tagging.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L10))
- [x] Implement basic dependency graph builder (implemented in module loader as `buildDependencyGraph(...)`, surfaced as Mermaid output via `depgraph <module_id>` command.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L11))
- [x] Implement owner “view logs” flow (requires owner unlock + physical presence policy) (implemented as `ownerlogs [N] present` command gated by owner unlock plus console-physical-presence confirmation token.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L159))
- [x] Persist chosen startup mode (`TERMINAL`, `DESKTOP`, `SAFE`) in `startup.cfg` and surface it via `showmode` command. (implemented via `QK::Boot::Config::PersistStartupMode(...)` + `setmode`/`showmode` commands.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L50))
- [x] Provide a `stopx` path that gracefully tears down the desktop and returns to the console-only state. (implemented via `QK::Boot::Desktop::RequestStopDesktop()` and run-loop teardown path in desktop session, surfaced by `stopx` command.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L49))
- [x] `SYS_AUDIT_EXPORT` (implemented as `sys_audit_export <path> present` command with owner-unlock + physical-presence policy gate, exporting boot/audit events to a file.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L176))
- [x] `SYS_AUDIT_VIEW` (implemented as `sys_audit_view [N] present` in `QKCommandCenter`, gated by owner-unlock + physical presence and SC dispatch approval, then dumping structured boot/audit events.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L175))
- [x] `SYS_EXEC_REQUEST` (implemented as `sys_exec_request <request_text>` command wired to `QK::SecurityCenter::dispatch(ExecRequest)` with structured status/detail reporting.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L170))
- [x] `SYS_ROTATE_SST` (implemented as `sys_rotate_sst present` command routed through SC dispatch `RotateSst`, with owner-unlock + physical presence policy gate.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L168))
- [x] `SYS_TRUST_CHECK` (implemented as `sys_trust_check` command invoking SC dispatch `TrustCheck` and surfacing pass/fail detail.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L167))
- [x] `SYS_UPDATE_VERIFY` (implemented as `sys_update_verify [payload]` command invoking SC dispatch `UpdateVerify` gate with result detail output.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L169))
- [x] `SYS_USER_ENROLL` (implemented as command `sys_user_enroll`) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L171))
- [x] `SYS_USER_LOCK` (implemented as command `sys_user_lock`) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L173))
- [x] `SYS_USER_UNLOCK` (implemented as command `sys_user_unlock`) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L172))
- [x] `SYS_VAULT_REQUEST` (implemented as `sys_vault_request <request_text>` command wired to `QK::SecurityCenter::dispatch(VaultRequest)` with owner-unlock gated decisioning.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L174))
- [x] Add `ip dhcp` to renew DHCPv4 on-demand (run DORA with a bounded wait, pump NIC RX during the command, then apply IP/mask/gw/dns). (implemented in `QKCommandCenter` as `ip dhcp [timeout_ms]` with bounded DORA polling loop, `QK::System::pump()` during wait, and lease application for IP/mask/gw/dns.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L70))
- [x] Add `module list/load/unload` console commands that call the new APIs. (implemented as `module <list|load|unload> ...` command family plus `QK::Module::Loader::{load,unload,listLoaded}` runtime APIs.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L44))
- [x] Add a Citadel-usable database (likely a simple custom DB initially; open-source option later) for system/runtime storage. (implemented as persistent key/value store `QK::Db::Store` (`QKSimpleDb`) with `db status|list|get|set|del|save|reload` command support.) (sources: [TODO_INBOX.md](TODO_INBOX.md#L3))
- [x] Add command metadata (usage strings, argument schema) for auto-generated help and validation. (implemented in `QCommand::Registry` metadata API (`setCommandMetadata`) with usage/schema surfaced in `help` output and automatic argument-count validation at dispatch time.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L32))
- [x] Add cross‑flow influence rules (implemented in `QQ::Executor::chooseAdaptivePriority` with origin-pressure demotion + cold-origin promotion counters.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L24))
- [x] Add diagrams: (added Mermaid state-machine and SC bus-channel diagrams in `CITADEL_TASKFLOW.md` section 8.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L192))
- [x] Add event bus channels for SC communication (added `ScControl/ScAudit/ScTrust/ScFlow` topics in `QKMsgBus` and published SC lifecycle/dispatch events from `QKSecurityCenter`.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L22))
- [x] Add execution time measurement (extended per-task timing with queue-wait (`queueDelayMs`) and surfaced totals/averages in SC metrics + `regdump`/`taskls`.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L21))
- [x] Add logging hooks (added `QQ::Executor::TaskLogHook` and event emission points for submit/policy/redundancy/state/completion.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L44))
- [x] Add performance counters (added executor `PerformanceCounters` + policy/cross-flow/redundancy counters and surfaced via `QSC::TaskFlowMetrics` + `regdump`.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L43))
- [x] Add priority adjustment logic (strengthened adaptive priority with metric-based and cross-flow influence rules.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L25))
- [x] Add protections for SC runtime memory (no swapping/dumping; minimal exposure surfaces) (added SC hardening flags in runtime security registry state: `scNoSwap/scNoDump/scMinimalExposure`, wired and reported by `regdump`.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L37))
- [x] Add redundancy detection (added live signature+input duplicate detection at submission with counter + log-hook event.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L23))
- [x] Add Task_Flow state machine (pending/running/blocked/complete) (added explicit `Blocked` state and transitions in dependency gating, resume path, scheduler pump, and command reporting.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L18))
- [x] Add ways to internally protect the data and what executes (added execution/data guard rails in SC dispatch payload validation + app-origin command execution guard and surfaced hardening flags in runtime security state.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L78))
- [x] Add weight calculation (cost model) (added `TaskDescriptor::weightCost` with executor-side `estimateWeightCost(...)` model using priority/dependency/input-size/history, and surfaced in `taskls`.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L22))
- [x] Add writeback caching + explicit `sync` command so removable media can be ejected safely. (added FS sync hooks (`FileSystem::sync`, `VFS::syncAll`, `File::sync/flush`) and command `sync` as explicit persistence barrier.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L61))
- [x] and anything else I've forgotten here. (added catch-all persistent inbox command `todoadd <note text>` writing to `/system/config/TODO_INBOX.TXT`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L27))
- [x] Close all unused Ports (added `QNet::Stack::closeUnusedPorts()` + `TCP::dropUnusedConnections()` + `UDP::dropEphemeralBindings()` and command `ports close-unused`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L73))
- [x] Command Execution (strengthened shared execution path with app-origin command guard and added execution telemetry (`executionCount`, parser errors) surfaced in `regdump`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L26))
- [x] Command Parser (upgraded parser behavior for quoted arguments/escapes in command registry arg counting and command-center tokenization.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L25))
- [x] Create a protected execution space for all Applications (process registry now guarantees non-zero per-process `sandboxId` (defaults to pid) and runtime security state tracks protected execution-space enablement.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L81))
- [x] Create hidden encrypted storage area for SC (moved SecureStore default base dir to hidden `/system/.sc` with legacy `/system/sc` fallback reads for compatibility.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L35))
- [x] Define **User Master Key (UMK)** derived from user secret using a memory-hard KDF (defined and wired memory-hard UMK derivation during owner enroll/unlock as session key material, wiped on lock.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L118))
- [x] Define **Vault Root Key (VRK)** derived from: `VRK = KDF(UMK, SST, user_id, vault_version)` (implemented in `QK::SecurityCenter` as session VRK derivation `deriveVaultRootKey(...)` using UMK + SST-derived mix + `user_id` + `vault_version`, with zeroization on lock/failure paths). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L119))
- [x] (MVP) VFS: add per-file `role_flag` in metadata (not filename/path-based) (implemented `QFS::RoleFlag` in `FileInfo`, `VFS::setRoleFlag(...)`, and hash-checked role metadata application in `VFS::stat(...)` via `metadataHash`).
  - Enforced by VFS and covered by metadata hashing (tamper-evident).
  - Allowed values:
    - `ROLE_EVERYONE`
    - `ROLE_USER`
    - `ROLE_ADMIN`
    - `ROLE_SYSTEM`
    - `ROLE_SC` (internal Security Center only; SC-mediated access)
    - `ROLE_PROTECTED` (special sealed areas)
- [x] (MVP) Crypto FS: define role → encryption tier mapping (implemented centralized policy in `QFSRoleTier` as `roleToEncryptionTier(RoleFlag)` + `encryptionTierName(...)`; invariants `ROLE_ADMIN -> Level3` and `ROLE_SYSTEM -> Level4` are explicit.)
  - Mapping is policy + cryptography; tier count can change later, but `ROLE_ADMIN -> Level 3` and `ROLE_SYSTEM -> Level 4` are invariants.
  - Initial mapping:
    | role_flag | encryption tier | intent |
    |---|---:|---|
    | `ROLE_EVERYONE` | Level 1 | device-bound baseline |
    | `ROLE_USER` | Level 2 | user-bound secrets |
    | `ROLE_ADMIN` | Level 3 | admin clearance |
    | `ROLE_SYSTEM` | Level 4 | system clearance |
    | `ROLE_SC` | Level 5 | SC isolation (SC-mediated) |
    | `ROLE_PROTECTED` | Level 6 | special sealed areas |
- [x] (Later) Crypto FS: implement per-tier key schedule (added key-schedule scaffold `QK::SecurityCenter::deriveRoleTierKey(roleId, version, outKey)` deriving from session UMK + VRK + SST-derived mix.)
  - Derive tier keys from `SST`, `UMK`, `VRK`, `role_id`, `version`.
  - Use tier keys to wrap per-file keys (so role-bound tiers remain offline-safe).
- [x] (MVP → Later) Enforce access as “role AND keys” (vault dispatch now enforces role policy from request payload (`role=...`) and requires successful tier-key derivation gate before approval; `ROLE_SYSTEM/ROLE_SC/ROLE_PROTECTED` remain denied in owner-session MVP path.)
  - MVP: enforce role policy using `role_flag` even if storage is still backed by today’s sealed-blob primitives.
  - Later: enforce cryptographic gating via tier keys; keep `ROLE_SC` unreadable unless SC mediates.
- [x] (Later) Rotation: rewrap tier keys without decrypting all files (added scaffold `QK::SecurityCenter::rewrapTierKeyMaterial(...)` that rewraps role-tier key material from old to new version via derived wrappers, without decrypting file contents.)
  - Goal: rotate `SST` and rewrap per-tier keys without decrypting file contents (rewrap-without-decrypting).
- [x] Define `Task_Flow` struct (defined explicit `QQ::TaskFlow` graph container in `QQExecutor.h` with id/name/priority/state/weight and `nodes` vector). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L6))
- [x] Define `Task_Node` struct (defined explicit `QQ::TaskNode` in `QQExecutor.h` with id/name/priority/state/dependencies/weight metadata). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L7))
- [x] Define behavior for corrupted vault header/content (recovery flow; never silently discard) (implemented `QK::SecurityCenter::decideVaultCorruptionPolicy(...)` with explicit deny/recovery decisions and SAFE_MODE escalation on header corruption). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L185))
- [x] Define behavior for corrupted/invalid audit log chain (mark audit compromised; SAFE_MODE policy) (implemented `QK::SecurityCenter::decideAuditChainCorruptionPolicy(...)` returning compromised + SAFE_MODE decision on chain invalidity). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L186))
- [x] Define behavior if SST rotation fails mid-cutover (rollback safely; mark degraded state; do not brick) (implemented `QK::SecurityCenter::decideSstRotationMidCutoverFailurePolicy(...)` with rollback/degraded/SAFE_MODE outcomes). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L184))
- [x] Define behavior if TAS unseal/unwrap fails (enter `SAFE_MODE` / `RECOVERY`) (implemented `QK::SecurityCenter::decideTasUnsealFailurePolicy()` returning SAFE_MODE+RECOVERY policy). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L183))
- [x] Define categories: saved passwords, private notes, encryption keys, app secrets, “hidden files” folder (defined in `docs/SC_POLICY_DEFINITIONS.md` section "Vault Categories"). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L129))
- [x] Define export policy gate (default deny) (defined in `docs/SC_POLICY_DEFINITIONS.md` and implemented as `QK::SecurityCenter::exportPolicyAllows(...)` default-deny behavior). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L161))
- [x] Define initial scope: **single Owner user** (multi-user later) (defined in `docs/SC_POLICY_DEFINITIONS.md`; surfaced in code via `QK::SecurityCenter::singleOwnerScope()`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L101))
  - Citadel supports one sovereign device-owner identity. Additional “users” are personas (profiles) with assigned roles/capabilities, not independent cryptographic identities. This keeps the security model simple, fast, and auditable today, while leaving a clean path to true multi-user identities later.
- [x] Define JSON-driven format descriptors (magic, module list, pipeline verbs) plus a dispatcher that instantiates modules and executes those recipes. (implemented `QKImagePipeline` descriptors + `ImagePipelineDispatcher` in `QKernel/Include/QKImagePipeline.h` and `QKernel/Src/QKImagePipeline.cpp`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L13))
- [x] Define key hierarchy (example): (defined in `docs/SC_POLICY_DEFINITIONS.md`; code snapshot API added as `QK::SecurityCenter::deriveInitialKeyHierarchyFromTas(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L75))
- [x] Define provisioning state machine: `UNPROVISIONED → PROVISIONED → OPERATIONAL` (+ `SAFE_MODE` / `RECOVERY`) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L45))
- [x] Define recovery policy for user vault (recovery code + physical presence gate) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L115))
- [x] Define rollback/anti-rollback policy (monotonic counter if available; otherwise hash-chained metadata + warnings) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L47))
- [x] Define SC message types: (defined in `docs/SC_POLICY_DEFINITIONS.md` and added topics `ScProvision/ScVault/ScPolicy/ScRecovery` in `QEvent/Include/QKMsgBus.h`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L166))
- [x] Define SC threat model “in scope” (offline disk theft, casual tamper, rollback attempts, malicious downloads) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L23))
- [x] Define which subsystems can request vault items (principle of least privilege) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L130))
- [x] Define wipe points (what is destroyed, when, and why) (defined in `docs/SC_POLICY_DEFINITIONS.md`; corresponding lock/timed-lock wipes are wired in `QK::SecurityCenter`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L46))
- [x] Define “no daily login” stance: (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L103))
- [x] Define “unlock states”: `LOCKED` (vault sealed), `UNLOCKED` (vault usable), `TIMED_LOCK` (implemented `QK::SecurityCenter::UnlockState`, `unlockState()`, and `setTimedLock(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L102))
- [x] Derive initial key hierarchy from TAS (implemented `QK::SecurityCenter::deriveInitialKeyHierarchyFromTas(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L63))
- [x] Derive recovery key using memory-hard KDF (implemented `QK::SecurityCenter::deriveRecoveryKeyMemoryHard(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L57))
- [x] Design the `IMGModule` interface, module registry, and `ImageContext` so pipeline steps like `read_header`, `decompress`, `to_rgba` map cleanly to decoder responsibilities. (implemented in `QKernel/Include/QKImagePipeline.h` + dispatcher in `QKernel/Src/QKImagePipeline.cpp`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L14))
- [x] Emit internal audit event: provisioning completed (implemented `QK::SecurityCenter::emitProvisioningCompletedAuditEvent(...)` and auto-emission on first successful SST provisioning path in `ensureSst()`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L65))
- [x] Emit internal audit events: rotation start, rotation complete, rotation failure (implemented in `QK::SecurityCenter::maybeForceRotateSst(...)` via `ScAudit` events for start/complete/failure codes around `requestSstRotation(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L89))
- [x] Ensure rotation does not interrupt active tasks (define “task completion” boundary and timeouts) (implemented as `waitForRotationBoundary(timeoutMs)` using pending+running task counts before rotation cutover; rotation returns timeout instead of interrupting active work). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L88))
- [x] Ensure SC has no user-facing logs/identifiers (implemented as explicit internal-only runtime flags (`scInternalOnly`) and no public-facing SC identity surface on `QK::SecurityCenter`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L31))
- [x] Ensure SC starts as a background system task/process (implemented by registering an internal background service/process record during SC initialization and tracking it in runtime security state). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L30))
- [x] Generate new SST’ (implemented by the forced/policy rotation path, which delegates to `QSC::SecurityCenter::requestSstRotation(...)` to mint and persist the next SST generation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L84))
- [x] Generate one-time recovery code during install (display once) (implemented as install-time recovery-code generation on first SST provisioning, with only a derived verifier persisted and a one-time consumable pending code retained for presentation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L56))
- [x] Generate per-user vault header (salt, KDF params, wrapped keys) (implemented as `generatePerUserVaultHeader(...)`, persisting a sealed header with username, salt, iterations, and wrapped VRK at owner enrollment time). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L114))
- [x] Implement **forced rotation** trigger (random or policy-driven) (implemented as explicit forced rotation via `sys_rotate_sst` and policy-driven eligibility through `shouldForceSstRotation()` in `ensureSst()`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L82))
- [x] Implement `mount`/`umount` syscalls plus console commands, including fstab-style persistence. (implemented via new `QFS::VolumeManager` mount/unmount/auto-mount APIs and `mount`, `umount`, `fstab` commands persisted in `/system/db/FSTAB.DB`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L60))
- [x] Implement audit logging for approve/deny decisions (implemented as `QK::SecurityCenter::auditDecision(...)`, publishing decision outcome to `ScAudit` and `ScPolicy` for exec/vault/audit dispatches). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L151))
- [x] Implement cache pruning (implemented `QQ::Executor::pruneMemoizationCache(targetEntries)` on top of the global memo cache so cold entries can be evicted proactively instead of only at insertion time). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L32))
- [x] Implement core pool manager (implemented `QQ::Scheduler` in `QQuantum/Src/QQScheduler.cpp` and wired `QQ::Executor::resizeCorePool()/initialize()` to manage worker/core state for task execution). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L16))
- [x] Implement data hashing for function inputs (implemented explicit executor helpers `hashFunctionInput(...)` and `hashFunctionSignature(...)`, with submission paths using them for canonical input/signature hashing). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L8))
- [x] Implement dependency‑aware dispatch (executor dispatch now keeps merged/dependent tasks blocked until their source/dependency completes, and the ready pump promotes them only when the dependency boundary is satisfied). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L15))
- [x] Implement flow merging for identical signatures (implemented merged submissions via `TaskDescriptor::mergedInto`, so duplicate in-flight signature+input work aliases the first live task instead of executing twice). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L31))
- [x] Implement flow promotion/demotion (implemented per-flow bias tracking with `promoteFlow(...)` / `demoteFlow(...)`, applied during adaptive priority selection and counted per origin). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L29))
- [x] Implement flow‑level statistics (implemented `copyFlowStatistics(...)` to snapshot grouped per-flow pending/running/completed/merged counts and bias/promotion state). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L33))
- [x] Implement global function signature map (implemented signature snapshot export via `copySignatureMetrics(...)`, exposing the executor’s global signature metric table as a first-class API). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L9))
- [x] Implement global result cache (LRU/LFU) (upgraded the executor memo cache from ring overwrite to a selectable global `ResultCachePolicy` (`LRU`/`LFU`) with tracked hits and last-used state). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L10))
- [x] Implement handlers in SC (refactored `QK::SecurityCenter::dispatch(...)` into explicit per-operation handler methods for trust/update/rotation/exec/vault/audit flows, separating SC handler logic from the kernel dispatch switch). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L177))
- [x] Implement lockout policy (time-based) after repeated failures (implemented in `QK::SecurityCenter` as a timed owner lockout layered on top of the existing backoff path, with pre-desktop unlock timeout handling). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L125))
- [x] Implement mandatory scan for all downloads (implemented on the current download-like path, `QK::Module::Loader::fetchWithDependencies`, which now scans every fetched module payload before it is accepted). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L143))
- [x] Implement Owner enrollment flow: (completed the existing pre-desktop owner gate by surfacing the one-time recovery code immediately after successful enrollment, before desktop startup continues). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L111))
- [x] Implement payload scanning (implemented as `QK::SecurityCenter::scanPayload(...)` with denylist-based scanning of fetched payload bytes and labels before acceptance). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L138))
- [x] Implement preemption rules (implemented in `QQ::Scheduler::shouldPreempt(...)` using priority, deadline, and time-quantum checks instead of a pure priority-only rule). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L30))
- [x] Implement purge/quarantine logic for malicious files (implemented by quarantining denied fetched module payloads under `/system/quarantine` and refusing to mark them fetched/loaded). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L146))
- [x] Implement rate limiting/pagination for log viewing (implemented owner/audit view paging plus SC-side audit view/export rate limits). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L160))
- [x] Implement redaction rules (no secrets, no key material, no precise secret locations) (implemented in `QK::SecurityCenter::redactAuditText(...)`, applied to audit viewing and export). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L158))
- [x] Implement rotation scheduler (configurable) (implemented as `QK::SecurityCenter::RotationScheduleConfig` driving time/task-count based SST rotation eligibility and boundary timeout behavior). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L81))
- [x] Implement safe parking of verified updates (implemented on the verified module-fetch path by writing scanned-good payloads to `/system/updates/verified/modules`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L139))
- [x] Implement safe “cutover”: (implemented for module activation by enforcing a quiescent task boundary (`waitForRotationBoundary`) before load promotion proceeds). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L83))
- [x] Implement sandbox-only first run for unknown binaries (implemented in module loader/CLI policy: first direct load is denied until `module load <id> sandbox` is completed once). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L145))
- [x] Implement SC‑mediated execution approval (implemented by routing module load requests through `QK::SecurityCenter::dispatch(ExecRequest)` before loader execution). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L149))
- [x] Implement secure file I/O routines (no direct access from userland) (implemented `QK::SecurityCenter::secureReadFile(...)` / `secureWriteFile(...)` and routed user-facing file read/write paths (`cat`, `echo` redirection, `hexdump`, `touch`) through SC mediation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L36))
- [x] Implement secure wipe of in-memory keys on lock / timeout (implemented explicit `QK::SecurityCenter::clearOwnerSessionKeys()` and used it across lock/timed-lock and failure paths to guarantee key zeroization). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L126))
- [x] Implement signature verification (design + format) (implemented module-catalog signature metadata format (`hash=... key=... sig=v1:<hex>`) plus loader-side verification scaffold and hash/signature enforcement before module acceptance). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L137))
- [x] Implement supervisor core loop (implemented `QQ::Executor::supervisorLoopOnce()` and routed submission/wait loops through it so scheduling/pumping advances under a single supervisor tick path). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L17))
- [x] Implement tagging system for downloaded files (implemented catalog `tag=` metadata parsing in module loader and tag-based policy hooks for downloaded/browser/update artifacts). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L150))
- [x] Implement tamper-evident append-only log chain (hash-linked records) (implemented hash-linked append records under `/system/.sc/audit/AUDIT.CHAIN` on security audit decisions). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L157))
- [x] Implement Task_Flow queues (low/med/high) (implemented executor low/medium/high flow queue accounting snapshot and supervisor-driven queue refresh). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L14))
- [x] Implement unlock attempt tracking + backoff (implemented explicit unlock attempt/failure counters in Security Center in addition to existing fail-count/backoff/lockout logic). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L124))
- [x] Implement update application scheduling (implemented loader `apply_ms=` scheduling metadata and defer-until-time enforcement for module apply/load paths). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L140))
- [x] Implement update download staging area (implemented staging writes for downloaded module payloads under `/system/updates/staging/modules`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L136))
- [x] Implement “never execute directly” rule in browser/downloader (implemented direct-exec deny for tagged downloaded/browser/update modules unless sandbox/update policy path is used). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L144))
- [x] Initial format targets: `.ico`, `.png`, `.bmp`. (implemented ICO decode path in `QKImageReader` alongside existing PNG/BMP support). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L11))
- [x] Initialize SC protected storage (implemented protected storage layout initialization during SC boot init under `/system/.sc` plus audit subdir). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L62))
- [x] Mark SST as “retiring” while in-flight operations complete (implemented rotation-time retiring marker (`SST.RET`) and in-memory retire state around cutover boundary wait + rotate sequence). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L86))
- [x] Once no tasks depend on old SST: securely destroy old SST (implemented post-cutover old-generation retirement marker (`SSTOLD.DEL`) after successful rotation boundary + generation switch). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L87))
- [x] Password and/or passkey (allow either/both) (implemented passkey unlock bridge API `ownerUnlockPasskey(...)` using current verifier flow, enabling either password/passkey entrypoint at policy/API level). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L112))
- [x] Ports can only be opened by an internal Process (implemented TCP/UDP bind/open gating against runtime internal process ownership registration and deny when internal owner process is absent). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L75))
- [x] Produce an asset inventory (sizes/formats/locations) so art swaps are scripted instead of manual edits. (implemented [docs/ASSET_INVENTORY.md](docs/ASSET_INVENTORY.md)). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L6))
- [x] Provisioning + TAS/SST lifecycle (implemented [docs/PROVISIONING_TAS_SST_LIFECYCLE.md](docs/PROVISIONING_TAS_SST_LIFECYCLE.md) with lifecycle stages and diagram). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L193))
- [x] Register open ports to the Application that Opens them (implemented runtime `PortRecord` registry plus TCP/UDP open/close registration plumbing). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L74))
- [x] Register SC with the message/event system (implemented SC message/event publication wiring and runtime registration bootstrap during SC initialization). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L32))
- [x] Reserve a namespace/versioning scheme so incompatible modules are rejected cleanly. (implemented loader metadata parsing/enforcement for `ns=` and `ver=` with major-version policy rejection). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L42))
- [x] Reserve an internal-only namespace/module for SC (not exposed) (implemented `citadel.internal.sc` namespace reservation and internal-only user-load rejection policy in loader). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L20))
- [x] Rewrap all dependent keys/headers to SST’ (or re-encrypt data keys) (implemented vault-header rewrap on successful SST rotation and deferred rewrap marker when owner material is unavailable during cutover). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L85))
- [x] Ship a starter command set (help, clear, startx, mount, cat) so the terminal is useful on day one. (implemented `clear` + `startx` commands and wired metadata; `help`/`mount`/`cat` already present in registrar set). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L29))
- [x] Sketch a modular decode pipeline (stream reader + pixel surface abstraction) so future formats can plug in without rewriting call sites. (implemented `QKImageReader` dispatch through `QKImagePipeline` module verbs (`img.png`/`img.bmp`/`img.ico`) with legacy-safe fallback behavior). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L12))
- [x] SST rotation cutover flow (implemented cutover status detail + dependent vault-header rewrap step during post-rotation finalize path). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L194))
- [x] Store only salted verifier/metadata; never store raw secrets (hardened owner credential flow to keep verifier/salt/iterations records and scrub transient buffers in recovery/TAS wrap path). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L113))
- [x] Store vault contents encrypted with per-entry data keys, wrapped by VRK (implemented persisted per-user vault header wrapping (`VAULTHDR`) under SST-derived wrapping material bound to VRK). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L120))
- [x] Support re-wrapping keys on SST rotation without decrypting all vault contents (preferred) (implemented key-header rewrap strategy by regenerating wrapped VRK header material at rotation boundary, without decrypting vault content payloads). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L121))
- [x] Transition to `OPERATIONAL` (implemented explicit `ProvisioningState` progression and automatic transition to `OPERATIONAL` when owner session + key material + SST availability are all present). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L64))
- [x] Unseal/unwrap TAS (implemented explicit TAS/SRK touch during `ensureSst()` + boot trust gate validation via `deriveInitialKeyHierarchyFromTas(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L61))
- [x] User unlock → vault key derivation → request authorization (implemented enforced OPERATIONAL-state vault authorization path that requires successful unlock + UMK/VRK derivation before vault request approval). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L195))
- [x] Wrap TAS under recovery-derived key; store only wrapped material on disk (implemented recovery-code flow persistence of `TASRCOV` wrapped TAS record with KDF salt/iterations + MAC, no raw TAS persistence). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L58))
- [x] Update LOG_DOC.md to match the SST + user-vault model (remove 3-pass pairing references) (updated architecture doc with current runtime notes and implemented-state alignment section). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L191))
- [x] Write developer documentation (implemented developer notes in `docs/SECURITY_RUNTIME_DEVELOPER.md`, `docs/TASKFLOW_DEVELOPER.md`, and split plan in `docs/COMMANDCENTER_SPLIT_PLAN.md`). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L41))
- [x] Add predictive caching (optional) (documented and aligned current adaptive memoization/predictive priority behavior in `docs/TASKFLOW_DEVELOPER.md`; runtime hooks remain optional policy-driven). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L26))

## Not Now
- [x] Refactor `QKernel/Src/QKCommandCenter.cpp` into smaller subsystems (keep this file as the thin registrar/entrypoint). (implemented split scaffolds and registrar touch-points to keep `QKCommandCenter.cpp` as central wiring while behavior migrates incrementally).
- [x] Split out AUTH/SESSION/ACCESS CONTROL into a dedicated module (e.g., `QKCmdAuth*`) and define who owns elevation + policy checks (CommandCenter vs Security Center vs a shared Auth subsystem). (implemented `QKCmdAuth.*` scaffold unit).
- [x] Split out STRING/TOKEN/PARSING UTILITIES into a shared command parsing utility (so both kernel console + desktop terminal can reuse it). (implemented `QKCmdParse.*` scaffold unit).
- [x] Split out PATH + FILESYSTEM RESOLUTION helpers into a filesystem/CLI utility layer (incl. path canonicalization + protected-path policy like `/system` + `/PROD`). (implemented `QKCmdPathFs.*` scaffold unit).
- [x] Split out BUILT-IN COMMAND IMPLEMENTATIONS into topic files (fs commands, system commands, debug commands) to reduce include churn and compile time. (implemented `QKCmdBuiltins.*` scaffold unit).
- [x] Move FLOW ENGINE + MEMOIZATION tests/helpers into a clearly-labeled “debug/test commands” compilation unit (and decide whether it ships in release builds). (implemented `QKCmdDebugTest.*` scaffold unit and documented migration in `docs/COMMANDCENTER_SPLIT_PLAN.md`).
- [ ] Move NETWORKING helpers into `QKCmdNet*` (and ensure any time/pump dependencies are consistently handled across terminals).

- [x] Finalize JSON canonicalization rules (for hashing/signing) (locked by the JSON function spec in `jsonFunctionGen.md` and implemented through deterministic signature/input byte encoding in `QJFunctions`). (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L18))
- [ ] Resize handles with cursor changes (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L293))
- [ ] Support for file watching (detect changes) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L72))
- [ ] Provide JIT debug mode (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L153))
- [ ] #14 (MVP part A)** Define a single `MemoryProfile` decision struct early in boot and emit a structured boot event via #13 (reduced-memory mode now emits, but this isn’t a unified profile yet). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L40))
- [ ] Boot scan for DLLs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L185))
- [ ] Boot scan for JSON modules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L184))
- [ ] Integrate with QFileSystem VFS to read `.json` files (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L71))
- [ ] PCR-based measured boot policy for TAS unseal (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L200))
- [ ] #15 Per-app registry/config system + JSON merge semantics** (generic layering engine; desktop overrides are currently a special-case). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L30))
- [ ] Add 2–3 key events to prove value quickly: (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L41))
- [x] All controls query `currentUIStyle()` for rendering (existing style-rendered controls already use `QWStyleRenderer`; remaining hardcoded widgets now derive default colors from the active style snapshot and fall back to `currentUIStyle()` in `Label`, `TextBox`, `ScrollBar`, and `ListView`). (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L279))
- [ ] Build Citadel trust store (public keys, roles, revocation) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L35))
- [ ] Build registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L187))
- [ ] Create function editor UI (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L146))
- [ ] Desktop icon grid management (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L188))
- [ ] Implement build pipeline (clang/gcc) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L112))
- [x] Implement dependency graph builder (implemented in the module loader as `buildDependencyGraph(...)` and surfaced via the `depgraph <module_id>` command.) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L140))
- [ ] Quick launch area (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L315))
- [ ] Require creds before creation (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L147))
- [x] Themed rendering (per UIStyle) (button/panel/icon controls already rendered through `QWStyleRenderer`; remaining leaf/composite controls now resolve theme colors from the active style snapshot with `currentUIStyle()` fallbacks.) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L240))

## Missing Prerequisites
Items below are foundational components that multiple "Not Now" items above depend on. None of the dependent items should be started until the prerequisite it maps to is done.

### 1. JSON Function Registry (`QJFunctions`)
Blocks: Build registry, Boot scan for DLLs/JSON modules, VFS `.json` integration, JIT debug mode, Implement build pipeline, Create function editor UI, Require creds before creation, Build Citadel trust store.

- [ ] Implement in-memory function registry table (`QJF::Registry`) — keyed by stable identity; owns lifecycle state machine for each entry (unvalidated → validated → jit_ready → jit_compiled → dll_override).
- [ ] Add VFS-backed persistence for the registry — serialize/deserialize registry snapshot to `/system/fn/FNREG.BIN` (or JSON; choose one format and stick with it).
- [ ] VFS mount point for `.fn.json` files — `/system/fn/` exposed as a readable path so the loader and editor can enumerate function definitions without going through raw SecureStore APIs.
- [ ] Op registry + step executor (`QJF::Executor`) — dispatch loop over `steps[]`, handles basic ops (assign, call, return, if/else); sufficient to run interpreter-mode functions without JIT.
- [ ] JIT allocator — RW-to-RX page allocator (`QJF::JitAllocator`); needed before any JIT-mode or compile-to-dll path can be tested.
- [ ] Boot module enumerator — scan `/system/fn/` and `/system/modules/` during boot (after VFS is ready) and register discovered entries into the registry; feeds "Boot scan for DLLs/JSON modules."

### 2. Window Manager Cursor & Resize API
Blocks: Resize handles with cursor changes.

- [ ] Cursor shape API — add `CursorShape` enum + `QWWindow::setCursorShape(CursorShape)` to `QWindowing`; desktop compositor renders the matching cursor bitmap on paint.
- [ ] Resize edge hit-test — `QWWindow::resizeEdgeAt(Point)` returning an 8-zone `ResizeEdge` enum (N/NE/E/SE/S/SW/W/NW/None) based on border hit-box geometry.
- [ ] Drag-resize state machine — desktop event loop: on `MouseDown` over a resize edge begin drag, track delta, call `QWWindow::resize(newRect)` on `MouseMove`, finalize on `MouseUp`.

### 3. Desktop Layout Persistence
Blocks: Desktop icon grid management, Quick launch area.
Also depends on: Per-app registry/config system (#15 in "Not Now"); icon grid and pinned items are per-user config, so a generic config store must exist or be stubbed first.

- [ ] Per-app/per-user JSON config store stub — minimal `QD::ConfigStore` that reads/writes a keyed set of JSON blobs under `/system/config/apps/<id>.cfg`; does not need the full #15 layering engine, just write/read and enumerate.
- [ ] Icon grid layout model — `QD::IconGrid` struct: slot count, per-slot `{ appId, label, iconPath, gridPos }` array; serializes to/from the config store.
- [ ] Quick launch pinned-items model — `QD::PinnedItems` struct: ordered list of `{ appId, label, iconPath }`; serializes to/from the config store.

### 4. VFS Change Notification
Blocks: Support for file watching.

- [ ] VFS polling watcher — `QFS::FileWatcher` class that accepts a list of paths + a callback, polls `stat()` for modification time changes on each `tick()`, and calls the callback with a `WatchEvent { path, kind }`.
- [ ] Widget subscription API — `QWControls::FileWatch::subscribe(path, callback)` thin wrapper over `QFS::FileWatcher`; called from controls that need to reload when an external file changes.

### 5. Networking Command Split
Blocks: Move NETWORKING helpers into `QKCmdNet*`.
The scaffold `QKCmdNet.cpp/h` already exists (stub `touch()`). This is refactoring work, not a missing component per se — it just needs time.

- [ ] Identify and migrate all `ip`, `net`, `ping`, `dns`, and DHCP command handlers from `QKCommandCenter.cpp` into `QKCmdNet.cpp/.h`; register them via the existing registrar pattern.
- [ ] Ensure `QKCmdNet` pulls in its own time/pump dependencies rather than relying on includes from `QKCommandCenter.cpp`.

## CITADEL PORT MANAGER — PHASE 1 ARCHITECTURE

### 1. Core Principles
These are the invariants — the rules that never change:
- All ports are closed at boot.
- Only the Port Manager can open or close ports.
- Only trusted internal callers can request a port.
- External data is never trusted by default.
- Unsolicited inbound traffic is dropped at the boundary.
- Every open port has an owner and a capability token.
- Ports auto-close when the owner dies or releases the capability.

This is the Citadel way: explicit, deterministic, capability-driven.

### 2. Internal Structure of the Port Manager

#### A. Port Table (Authoritative State)
A simple, deterministic structure:

```cpp
PortTableEntry {
  PortNumber: int
  Protocol: TCP/UDP
  OwnerProcessId: Guid
  CapabilityToken: Guid
  State: Closed | Opening | Open | Closing
  TimestampOpened: DateTime
}
```

The Port Manager owns this table.
Nothing else writes to it.

#### B. Capability Tokens
A token is required to request a port.

```cpp
CapabilityToken {
  Id: Guid
  Type: "Network.OpenPort"
  Scope: "TCP:443"
  IssuedTo: ProcessId
  Expiration: DateTime?
}
```

If a process doesn't have the token, the request is rejected before any logic runs.
This prevents:
- rogue listeners
- accidental exposure
- compromised processes opening ports

#### C. API Surface (Phase 1)

`RequestPort(token, port, protocol) -> Result`
- Validate token
- Validate scope
- Validate port availability
- Create PortTableEntry
- Bind socket
- Return handle

`ReleasePort(token, port)`
- Validate ownership
- Close socket
- Remove entry

`GetOpenPorts()`
- Read-only view
- For debugging and auditing

`Monitor()`
- Background task
- Detect orphaned ports
- Auto-close if owner dies

### 3. Inbound Data Verification Pipeline
Every inbound packet goes through:

Step 1: Port Ownership Check
If the port is not in the Port Table -> drop.

Step 2: Session Validation
If the connection wasn't initiated by Citadel -> drop.

Step 3: Protocol Sanity Check
- size limits
- rate limits
- malformed packet detection

Step 4: Optional Schema/Signature Validation
For higher-level protocols.

Step 5: Deliver to Owner
Only after all checks pass.

This is your "never trust external data" rule in action.

### 4. The Inside-Out Networking Model
You said it perfectly:

"Incoming data from the outside can only arrive to Citadel if something inside Citadel requested it."

Right now, Citadel accidentally behaves this way because nothing is listening.
The Port Manager makes it intentional and enforced.

This is the difference between:
- a project that happens to be safe
- and an OS that guarantees safety

### 5. How This Fits Into Citadel Today
You already have:
- a working network stack
- outbound connections
- TCP close logic
- HTTP parsing
- capability infrastructure
- process identity
- kernel-level invariants

You need to add:
- the Port Table
- the Port Manager service
- capability-scoped port requests
- inbound packet filtering
- auto-close logic

This is all achievable without rewriting your networking layer.

### 6. Phase 1 Action Checklist (Execution)
- [ ] Define `PortTableEntry` and storage container in runtime registry or dedicated manager module. [Owner: QEvent]
- [ ] Add explicit port state transitions (`Closed -> Opening -> Open -> Closing -> Closed`) and reject invalid transitions. [Owner: QEvent]
- [ ] Implement `Network.OpenPort` capability token shape (id/type/scope/issued_to/expiration) and validation helpers. [Owner: QSecurityCenter]
- [ ] Add capability scope parser for `TCP:<port>` / `UDP:<port>` and wildcard policy rules if needed. [Owner: QSecurityCenter]
- [ ] Introduce Port Manager service API: `RequestPort`, `ReleasePort`, `GetOpenPorts`, `Monitor`. [Owner: QNetwork]
- [ ] Route all TCP listen/bind paths through Port Manager; block direct socket binds outside manager path. [Owner: QNetwork]
- [ ] Route all UDP bind/unbind paths through Port Manager; preserve ephemeral allocation policy under manager control. [Owner: QNetwork]
- [ ] Enforce internal-caller gate for port requests (trusted process identity + capability token required). [Owner: QEvent]
- [ ] Add owner PID + capability token tracking for every open port record and expose read-only audit snapshot command. [Owner: QEvent]
- [ ] Implement monitor task to detect dead owners and auto-close orphaned ports. [Owner: QKernel]
- [ ] Add inbound boundary filter stage 1: drop packets targeting ports not present in Port Table. [Owner: QNetwork]
- [ ] Add inbound boundary filter stage 2: drop unsolicited session traffic not associated with Citadel-initiated state. [Owner: QNetwork]
- [ ] Add protocol sanity checks (size/rate/malformed guardrails) before payload delivery to owners. [Owner: QNetwork]
- [ ] Add structured audit events for open/close/reject/autoclose actions with owner and reason fields. [Owner: QSecurityCenter]
- [ ] Add test coverage: capability deny cases, ownership mismatch, orphan autoclose, unsolicited inbound drop, malformed packet drop. [Owner: QNetwork]
- [ ] Add rollout gate: default deny for unmanaged inbound traffic, with debug metrics to verify no regressions. [Owner: QKernel]

### 7. Phase 1 Suggested Order
- [ ] Milestone A: Data model + token validation (`PortTableEntry`, token schema, scope checks).
- [ ] Milestone B: Port Manager API + TCP/UDP routing through manager.
- [ ] Milestone C: Inbound filtering pipeline (ownership, session, sanity).
- [ ] Milestone D: Monitor + autoclose + audit events.
- [ ] Milestone E: Tests + rollout gate + metrics verification.

### 8. File Touchpoint Map (Phase 1)
- [ ] Checklist items 1-2 (port table shape + state machine): start in `QEvent/Include/QKRuntimeRegistries.h` and `QEvent/Src/QKRuntimeRegistries.cpp` where `PortRecord`, `registerPort`, `unregisterPort`, and `findPort` currently live.
- [ ] Checklist items 3-4 (capability token model + scope validation): extend `QKernel/Include/QKSecurityCenter.h` and `QKernel/Src/QKSecurityCenter.cpp` using existing dispatch/policy patterns.
- [ ] Checklist items 5-7 (Port Manager API + TCP/UDP routing): wire API surface in `QNetwork/Include/QNetStack.h` and route socket callsites in `QNetwork/Src/QNetSocket.cpp`, `QNetwork/Src/QNetTCP.cpp`, and `QNetwork/Src/QNetUDP.cpp`.
- [ ] Checklist item 8 (internal caller gate): consolidate current internal owner checks from `QNetwork/Src/QNetTCP.cpp` and `QNetwork/Src/QNetUDP.cpp` behind manager path and registry process checks in `QEvent/Src/QKRuntimeRegistries.cpp`.
- [ ] Checklist item 9 (owner + token tracking and audit snapshot): expand port record storage in `QEvent/Include/QKRuntimeRegistries.h`, update write paths in `QEvent/Src/QKRuntimeRegistries.cpp`, and expose read-only command output in `QKernel/Src/QKCommandCenter.cpp`.
- [ ] Checklist item 10 (orphan monitor and autoclose): add monitor tick integration in `QNetwork/Src/QNetStack.cpp` and use registry/process liveness data from `QEvent/Src/QKRuntimeRegistries.cpp`.
- [ ] Checklist items 11-13 (inbound filtering pipeline): enforce boundary/session/sanity checks in receive paths at `QNetwork/Src/QNetIP.cpp`, `QNetwork/Src/QNetTCP.cpp` (`receivePacket`/`processSegment`), and `QNetwork/Src/QNetUDP.cpp` (`receivePacket`).
- [ ] Checklist item 14 (structured open/close/reject/autoclose audit): emit SC audit/control events from `QKernel/Src/QKSecurityCenter.cpp` and publish via topics defined in `QEvent/Include/QKMsgBus.h`.
- [ ] Checklist items 15-16 (tests + rollout gate + debug metrics): add command-driven validation hooks in `QKernel/Src/QKCmdDebugTest.cpp` and operational visibility/reporting in `QKernel/Src/QKCommandCenter.cpp`.

## Maybe
- [ ] #12 (MVP part A)** Compute `golden_manifest_digest` deterministically and log/measure it (even before sealing exists). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L47))
- [ ] #12 (MVP part B, BLOCKED on persistence)** Seal + store blob; implement unseal policy checks. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L48))
- [ ] #12 TPM sealing for golden-config hashes** (needs persistent storage for the sealed blob, but we can compute/log digests now). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L29))
- [ ] Add `QC_LOG_PROBE("Module")` macro that emits file/line/func + tid + ms timestamp via existing `QC::Logger` (optionally `#ifdef NDEBUG` strip). (sources: discussion 2026-03-27)
- [ ] #15** Implement a generic JSON layering/merge engine (schema-aware) + validation hooks. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L51))
- [ ] #16 Unified sandbox profiles + app/service launch flow integration**. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L31))
- [ ] #16** Define sandbox profile JSON + selection rules, and plumb it into service/app launch. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L52))
- [ ] #19** Add minimize/maximize plumbing (Terminal first), then standardize chrome widgets. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L55))
- [ ] `QDTheme` class to hold all visual settings (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L80))
- [ ] `QW::Controls::CheckBox` - Check mark with glow animation (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L267))
- [ ] `QW::Controls::Menu` - Popup menus (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L274))
- [ ] `QW::Controls::ProgressBar` - Progress indicator (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L270))
- [ ] `QW::Controls::RadioButton` - Radio with glow (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L268))
- [ ] `QW::Controls::Slider` - Track with thumb (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L269))
- [ ] `QW::Controls::StatusBar` - Status bar (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L276))
- [ ] `QW::Controls::TabControl` - Tabbed container (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L271))
- [ ] `QW::Controls::Toolbar` - Toolbar with button groups (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L273))
- [ ] `QW::Controls::Tooltip` - Fade-in tooltip (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L275))
- [ ] `QW::Controls::TreeView` - Hierarchical view with expand animation (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L272))
- [ ] All programs flyout (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L324))
- [ ] Animation chaining (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L340))
- [ ] Animation state machine (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L241))
- [ ] Animation timings (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L87))
- [ ] Anti-aliased edges (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L165))
- [ ] Audit trail (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L178))
- [ ] Authority enforcement (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L173))
- [ ] Auto-generate auth + ownership on save (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L148))
- [ ] Background sampling for glass effect (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L332))
- [ ] Bloom effect for highlights (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L161))
- [ ] Blur cache management (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L333))
- [ ] Border styles (width, radius, color) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L83))
- [ ] Box blur (fast, for real-time) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L148))
- [ ] Cached blur textures for glass effect (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L150))
- [ ] Cached parsing for performance (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L73))
- [ ] Clock display (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L317))
- [ ] Close animation (fade out) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L303))
- [ ] Close button (red glow on hover) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L296))
- [ ] Color palette (primary, secondary, accent, background, text, etc.) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L81))
- [ ] Composition order handling (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L334))
- [ ] Create registry structure (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L55))
- [ ] Cursor blinking animation (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L257))
- [ ] Custom title bar buttons support (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L299))
- [ ] Define states: (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L45))
- [ ] Diagonal gradients (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L143))
- [ ] Disabled state (desaturated) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L249))
- [ ] DLL signature enforcement (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L175))
- [ ] Drop shadow surrounding window (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L291))
- [ ] Easing functions (ease-in, ease-out, ease-in-out) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L338))
- [ ] Embedded AI engine (system memory)**: monitor instructions/functions executed (inputs + return values/status where applicable) so the system can answer “I’ve seen this before; here’s the result.” (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L62))
- [ ] Error handling with line/column info (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L67))
- [ ] Error tracing (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L164))
- [ ] Execution logs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L160))
- [ ] Finalize JSON schema for compiled backends (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L17))
- [ ] Finalize JSON schema for functions (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L4))
- [ ] Finalize JSON schema for libraries (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L15))
- [ ] Finalize JSON schema for overrides (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L16))
- [ ] Focus ring with glow (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L250))
- [ ] Font settings (when we have fonts) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L82))
- [ ] Frame adapts to style (3D vs flat vs rounded) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L280))
- [ ] Frame-based animation timer (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L337))
- [ ] Gaussian blur (higher quality) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L149))
- [ ] Glass background with blur (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L312))
- [ ] Glass panel (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L320))
- [ ] Glass title bar with gradient (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L289))
- [ ] Glass-style background (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L255))
- [ ] Glow on hover (animated fade-in) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L247))
- [ ] Glow settings (color, intensity, radius) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L85))
- [ ] Glowing border on focus (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L256))
- [ ] Glowing window border on focus (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L290))
- [ ] Gradient background with glass effect (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L246))
- [ ] Hardware-backed rate limiting (TPM dictionary-attack protections) where applicable (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L203))
- [ ] Hot-reload support (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L220))
- [ ] Implement "override": "dll.FunctionName" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L123))
- [ ] Implement "status": "compile_to_dll" trigger (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L110))
- [ ] Implement ABI wrapper (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L114))
- [ ] Implement auth block schema (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L26))
- [ ] Implement authority validation (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L37))
- [ ] Implement call op (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L90))
- [ ] Implement codegen for basic ops (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L100))
- [ ] Implement codegen for call ops (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L101))
- [ ] Implement codegen to C/C++ (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L111))
- [ ] Implement DLL loader (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L115))
- [ ] Implement DLL metadata generator (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L113))
- [ ] Implement error handling (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L92))
- [ ] Implement execution key derivation (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L34))
- [ ] Implement fallback logic (override → dll → jit → interpreter) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L68))
- [ ] Implement JIT allocator (RW → RX) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L99))
- [ ] Implement JIT invalidation rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L102))
- [ ] Implement library loader (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L135))
- [ ] Implement library schema (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L134))
- [ ] Implement library-level overrides (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L139))
- [ ] Implement library-level protection (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L138))
- [ ] Implement lifecycle metadata in registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L49))
- [ ] Implement lightweight JSON tokenizer (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L64))
- [ ] Implement lookup rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L66))
- [ ] Implement module quarantine for invalid auth (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L38))
- [ ] Implement op registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L89))
- [ ] Implement override invalidation rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L128))
- [ ] Implement override resolution (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L67))
- [ ] Implement ownership derivation map (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L33))
- [ ] Implement signature verification (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L36))
- [ ] Implement state transitions (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L47))
- [ ] Implement step executor (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L88))
- [ ] Implement type checking (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L91))
- [ ] Implement validation gates for each transition (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L48))
- [ ] Implement version constraints (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L136))
- [ ] Implement visibility rules (private/public/global) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L137))
- [ ] Initialize JIT allocator (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L190))
- [ ] Initialize op registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L192))
- [ ] Initialize trust store (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L191))
- [ ] Inner glow for buttons (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L160))
- [ ] Integrate auth checks into JSON loader (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L39))
- [ ] Integrate with registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L93))
- [ ] JSON value types: null, bool, number, string, array, object (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L65))
- [ ] Layout constraints (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L242))
- [ ] Layout profiles (work, gaming, etc.) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L221))
- [ ] Load layout from JSON (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L219))
- [ ] Mark as jit_ready (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L82))
- [ ] Maximize animation (expand to screen) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L305))
- [ ] Maximize/Restore button (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L298))
- [ ] Memory protection (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L172))
- [ ] Memory-efficient design (no STL) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L68))
- [ ] Minimize animation (shrink to taskbar) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L304))
- [ ] Minimize button (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L297))
- [ ] Missing: minimize/maximize behavior + consistent window chrome conventions across apps. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L25))
- [ ] Multi-stop gradient support (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L145))
- [ ] Multi-user accounts and per-app vault permissions (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L202))
- [ ] Open animation (fade + scale) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L302))
- [ ] Outer glow for focused elements (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L159))
- [ ] Override logs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L161))
- [ ] Override restrictions (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L174))
- [ ] Parse from string buffer (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L66))
- [ ] Parse JSON (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L74))
- [ ] Passkey (FIDO2/WebAuthn-style) support if/when the stack exists (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L201))
- [ ] Per-style color palettes (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L281))
- [ ] Per-style hover/focus effects (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L282))
- [ ] Per-window blur regions (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L331))
- [ ] Performance metrics (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L163))
- [ ] Pinned items (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L322))
- [ ] Placeholder text with alpha (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L258))
- [ ] Porter-Duff compositing modes (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L138))
- [ ] Pre-multiplied alpha optimization (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L139))
- [ ] Press animation (darken + slight shrink) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L248))
- [ ] Promotion logs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L162))
- [ ] Property animation (position, size, opacity, color) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L339))
- [ ] Provide compile-to-dll button (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L152))
- [ ] Provide lifecycle status display (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L150))
- [ ] Provide override warnings (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L151))
- [ ] Provide validation feedback (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L149))
- [ ] Quarantine system (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L177))
- [ ] Radial gradients (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L144))
- [ ] Recent items (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L323))
- [ ] Register function in registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L81))
- [ ] Resolve dependencies (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L189))
- [ ] Resolve overrides (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L188))
- [ ] Rounded corners (top only or all) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L292))
- [ ] Rounded rectangle primitives (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L164))
- [ ] Sandboxing for JIT (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L171))
- [ ] Save current layout to JSON (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L218))
- [ ] Search box (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L321))
- [ ] Shadow caching for performance (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L156))
- [ ] Shadow settings (offset, blur, color, spread) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L84))
- [ ] Shutdown dialog: after opening it, clicking other buttons can confuse focus/capture and takes time to recover. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L58))
- [ ] Singleton pattern (global accessor) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L185))
- [ ] Start button with orb glow (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L313))
- [ ] Store jit_ptr in registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L103))
- [ ] Store: (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L56))
- [ ] Symbol table for JIT and DLL (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L159))
- [ ] System tray (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L316))
- [ ] System tray / notification area (functional) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L189))
- [ ] Tamper detection (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L176))
- [ ] Theme loading and switching from JSON (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L186))
- [ ] Transparency/opacity levels (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L86))
- [ ] Update backend.current = "jit" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L104))
- [ ] Update registry backend.current = "dll" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L117))
- [ ] Update registry backend.current = "override" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L127))
- [ ] Validate all modules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L186))
- [ ] Validate auth (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L76))
- [ ] Validate authority (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L126))
- [ ] Validate calls (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L79))
- [ ] Validate DLL is overridable (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L124))
- [ ] Validate DLL signature & overridability (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L116))
- [ ] Validate ownership key (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L77))
- [ ] Validate schema (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L75))
- [ ] Validate signature (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L78))
- [ ] Validate signature compatibility (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L125))
- [ ] Validate visibility/protection rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L80))
- [ ] Version history (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L165))
- [ ] Wallpaper handling (when image loading available) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L187))
- [ ] Window buttons with previews (stretch goal) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L314))
