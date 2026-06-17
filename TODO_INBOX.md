# TODO (Inbox)

Use this file for the active stabilization queue.
Use `TODO_MAIN.md` for the broader product backlog.
Use `CITADEL_CURRENT_STATE.md` for what is already true in the codebase.

Maintenance rule:
- keep this file short and execution-focused
- keep only the items currently being worked or about to be worked
- move completed durable outcomes back into `CITADEL_CURRENT_STATE.md`
- move deferred or broader roadmap work back into `TODO_MAIN.md`

## Expand Now

These are the areas where the current system works, but the implementation surface is still too shallow for smooth real-hardware use.

- [x] Expand mount/source provenance so Citadel reports which paths are ramdisk-backed, which are persistent, and which device/driver provided each mounted volume.
- [x] Expand durable boot-log export defaults so Citadel prefers `/system` when mounted, and falls back to ramdisk `/shared` only when no persistent target exists.
- [x] Expand the shell/file tooling with a simple file-copy path (`cp` or equivalent) so logs saved to ramdisk can be copied to `/system` during the same session.
- [x] Expand USB mass-storage support so the boot USB stick can be surfaced as a writable mounted volume from inside Citadel. (initial pass: xHCI + USB BOT/SCSI + FAT volume surfacing under `/mnt/usb*`)
- [x] Expand real-hardware input/device tuning with a user-visible mouse speed/sensitivity adjustment path.

## Tighten Now

These are the areas where the behavior exists, but the flow, diagnostics, or ownership still need to be cleaned up and hardened.

- [x] Tighten the boot/session path by reviewing real-hardware `BOOTMETRIC` output and deciding which temporary diagnostics should remain permanently.
- [x] Tighten the SecureStore/owner path by removing temporary owner-boot debug output once repeated real-hardware boots stay stable.
- [x] Tighten mount/export behavior so boot-log save paths are explicit, predictable, and not mistaken for persistent storage when they are only ramdisk fallbacks.

## Execution Order

- [x] Step 1: finish mount/source provenance and durable log-export defaults together.
- [x] Step 2: add `cp` so the logging workflow is usable before USB mass-storage lands.
- [x] Step 3: trim and lock the permanent `BOOTMETRIC` set after another real-hardware review pass.
- [x] Step 4: add USB mass-storage support.
- [x] Step 5: finish input/device tuning polish.

## Execution Batch 11 to 20 (Active)

Reference: `TODO_MAIN.md` Phase 1 and Phase 2 sequence.

### Expand Now

- [x] Item 11: Surface per-mount persistence class in mount listings (`persistent`, `ephemeral`, `removable`) and include backing driver/device id.
- [x] Item 12: Add a single command to print storage provenance for `/system`, `/shared`, and each discovered block device in one report.
- [x] Item 13: Add explicit log-export target selection (`auto`, `system`, `shared`, `usb`) with clear refusal messaging when target is unavailable.
- [x] Item 14: Add pre-export writability check and free-space check so failed exports explain the exact blocking condition.
- [x] Item 15: Add boot/audit export metadata file (timestamp, source, target, persistence class, artifact hash).

### Tighten Now

- [x] Item 16: Remove temporary SecureStore owner-debug logs once stable on repeated real-hardware boots; keep only permanent diagnostics.
- [x] Item 17: Normalize console/export status messages so persistence intent is always explicit (never implies ramdisk is durable).
- [x] Item 18: Add command-level guardrails for writing to ephemeral targets unless user passes explicit override intent.
- [x] Item 19: Add one end-to-end real-hardware validation checklist for export paths (`/system`, `/shared`, USB) with expected outcomes. (Reference: `docs/EXPORT_PATH_VALIDATION_CHECKLIST.md`)
- [x] Item 20: Update current-state documentation after landing Items 11 to 19 and archive evidence locations for future regressions. (Reference: `CITADEL_CURRENT_STATE.md` section 11)

### Batch 11 to 20 Order

- [x] Step A: implement Items 11 to 13 (provenance + explicit targeting).
- [x] Step B: implement Items 14 to 15 (preflight + export metadata).
- [x] Step C: implement Items 16 to 18 (tightening + guardrails).
- [x] Step D: complete Items 19 to 20 (validation + state/evidence update).

## Execution Batch 21 to 30 (Active)

Reference: `TODO_MAIN.md` Phase 3 sequence.

### Tighten Now

- [x] Item 21: Tighten the pre-desktop boot/session flow so DHCP, SecureStore, owner unlock, console ownership, and desktop handoff have clearer boundaries.
- [x] Item 22: Tighten console ownership so boot logs, hidden input, prompt rendering, and terminal-only mode each have a single explicit owner.
- [x] Item 23: Tighten ACPI/power behavior around validation, diagnostics, and fallback policy so shutdown remains robust across more firmware variants.
- [x] Item 24: Tighten the owner/session gate so boot-time enrollment, unlock prompts, and recovery prompts avoid ambiguous restart loops.

### Expand Now

- [x] Item 25: Expand SecureStore/TPM parity so certified TPM-backed systems consistently use the TPM anchor path without falling back unexpectedly.
- [x] Item 26: Expand TPM-backed owner/session reporting so the console can state whether the active anchor is TPM-backed or recovery-backed.
- [x] Item 27: Expand device-configuration surfaces for keyboard tuning so bring-up settings become user-manageable runtime controls.
- [x] Item 28: Expand device-configuration surfaces for mouse and pointing behavior so sensitivity and related tuning remain user-adjustable at runtime.
- [x] Item 29: Expand hardware-tuning visibility so current boot/session device settings are easy to inspect from the command layer.
- [x] Item 30: Expand fallback-path reporting so recovery and bypass modes are explicit, documented, and easy to verify during boot.

### Batch 21 to 30 Order

- [x] Step A: tighten boot/session and ownership boundaries first.
- [x] Step B: harden ACPI/power fallback behavior.
- [x] Step C: expand SecureStore/TPM parity and anchor reporting.
- [x] Step D: expand device-configuration and fallback visibility controls.

## Execution Batch 31 to 40 (Active)

Reference: `TODO_MAIN.md` next platform-depth sequence.

### Expand Now

- [ ] Item 31: Expand QCQL schema support so desktop data can use normalized relational tables (layout, controls, bindings, themes, capabilities) instead of chunk-style payload storage.
- [ ] Item 32: Expand QCQL integrity behavior with explicit relationship/foreign-key checks and deterministic rejection diagnostics for invalid references.
- [ ] Item 33: Expand desktop boot loading so QCQL-backed layout/theme models are the primary runtime source, with file import as provisioning-only fallback.
- [ ] Item 34: Expand command-layer QCQL inspection so operators can view desktop-model readiness and key table health from terminal tools.
- [ ] Item 35: Expand migration tooling that converts existing desktop JSON/CML assets into QCQL relational rows with reproducible output.

### Tighten Now

- [ ] Item 36: Tighten boot-time desktop data validation so malformed/incomplete QCQL desktop models fail closed to terminal with explicit recovery guidance.
- [ ] Item 37: Tighten desktop theme/layout apply-path diagnostics so stage failures identify table/row/key root cause in both boot logs and command output.
- [ ] Item 38: Tighten fallback policy between QCQL data and file import so fallback reason is explicit and emits structured boot events.
- [ ] Item 39: Tighten evidence hygiene by adding a concrete validation checklist for QCQL desktop-model bring-up across reboot cycles.
- [ ] Item 40: Tighten current-state and roadmap documentation after Items 31 to 39 land, including durable evidence locations for regressions.

### Batch 31 to 40 Order

- [ ] Step A: implement QCQL relational model depth first.
- [ ] Step B: wire desktop runtime to database-first sources.
- [ ] Step C: harden validation, fallback reporting, and diagnostics.
- [ ] Step D: finish evidence capture and current-state documentation updates.
