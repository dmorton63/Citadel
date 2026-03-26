# Citadel Brainstorm Roadmap — TODO (2026-03-08)

Source: `CitadelOS_Brainstorm2026-02-25.md` (items #10–#19 focus)

## Status snapshot (aligned to brainstorm roadmap numbers)

### Completed (MVP)
- [x] **#10 Recovery logic beyond selection**: stage/validate `/PROD` early modules and fall back to `/GOLDEN` without partial state.
  - Code: `kernel/Boot/QKBoot.cpp` (`SysConfigEarlyStageForRoot`, `SysConfigSelectActiveTier`), `kernel/Boot/Config/QKBootStagedConfig.cpp`
- [x] **#11 Runtime registries (MVP)**: kernel-owned Process/Service/Window/Resource/Security registries + `regdump`.
  - Code: `QEvent/Include/QKRuntimeRegistries.h`, `QEvent/Src/QKRuntimeRegistries.cpp`, `QKernel/Src/QKCommandCenter.cpp`
- [x] **#17 Desktop overrides merged at runtime**.
  - Code: (desktop JSON override path; already live)
- [x] **#18 Boot log capture + `bootlog`** (text ring buffer).

### Partial
- [~] **#14 Reduced-memory notices**: serial log line + structured event exist, but no unified `MemoryProfile` record, no splash integration, no persistence.
  - Code: `kernel/Boot/QKBoot.cpp` (`Reduced Memory Mode: ...`)
- [~] **#13 Structured measurement/event log**: kernel ring buffer + compact serial emission exists, with a few early boot events.
  - Missing: more event coverage (TPM/PCR summary, etc.), dump/inspection command, and any persistence.
  - Code: `QKernel/Include/QKBootEventLog.h`, `kernel/Boot/Config/QKBootEventLog.cpp`, `kernel/Boot/QKBoot.cpp`
- [~] **#19 Windowing UX rules / chrome**:
  - [x] Desktop is `AlwaysBottom|NoFocus` (clicking desktop won’t steal focus).
  - [x] Terminal has a dedicated title region + close button, plus sticky-input focus + better tail-follow scrolling.
  - [ ] Missing: minimize/maximize behavior + consistent window chrome conventions across apps.
  - Code: `QDesktop/Src/QDDesktop.cpp`, `QDesktop/Src/QDTerminal.cpp`, `QWindowing/*`

### Not started / not present yet
- [ ] **#12 TPM sealing for golden-config hashes** (needs persistent storage for the sealed blob, but we can compute/log digests now).
- [ ] **#15 Per-app registry/config system + JSON merge semantics** (generic layering engine; desktop overrides are currently a special-case).
- [ ] **#16 Unified sandbox profiles + app/service launch flow integration**.

## Execution order (practical next steps)

### P0 — Unblock observability + correctness
- [~] **#13** Implement `BootEventLog` (ring buffer) with:
  - event fields: `seq`, `t_ms`, `stage`, `type`, `details` (small key/value strings)
  - compact one-line serial emission
  - keep it kernel-only for now
- [ ] **#14 (MVP part A)** Define a single `MemoryProfile` decision struct early in boot and emit a structured boot event via #13 (reduced-memory mode now emits, but this isn’t a unified profile yet).
- [ ] Add 2–3 key events to prove value quickly:
  - config selection + fallback reason (ties into #10)
  - PCR extend summary (ties into existing TPM measurement work)
  - memory profile decision (#14)

### P1 — Security posture hardening
- [ ] **#12 (MVP part A)** Compute `golden_manifest_digest` deterministically and log/measure it (even before sealing exists).
- [ ] **#12 (MVP part B, BLOCKED on persistence)** Seal + store blob; implement unseal policy checks.

### P2 — Config & sandbox model
- [ ] **#15** Implement a generic JSON layering/merge engine (schema-aware) + validation hooks.
- [ ] **#16** Define sandbox profile JSON + selection rules, and plumb it into service/app launch.

### P3 — Window chrome maturity
- [ ] **#19** Add minimize/maximize plumbing (Terminal first), then standardize chrome widgets.

## Known UX glitches (observed; not today unless it blocks roadmap)
- [ ] Shutdown dialog: after opening it, clicking other buttons can confuse focus/capture and takes time to recover.
  - Suspected area: window focus/capture routing in `QWindowing` + modal dialog behavior in `QDesktop`.

## Bucketlist subsystems (future)
- [ ] **Embedded AI engine (system memory)**: monitor instructions/functions executed (inputs + return values/status where applicable) so the system can answer “I’ve seen this before; here’s the result.”
  - Sources: command processor/terminal, service calls, boot events.
  - Storage: append-only journal + hashed key (function ID + normalized inputs) with clear policy/limits.
  - Output: lookup API + optional suggestion mode (no automatic execution).
