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
- [ ] Tighten the SecureStore/owner path by removing temporary owner-boot debug output once repeated real-hardware boots stay stable.
- [ ] Tighten mount/export behavior so boot-log save paths are explicit, predictable, and not mistaken for persistent storage when they are only ramdisk fallbacks.

## Execution Order

- [x] Step 1: finish mount/source provenance and durable log-export defaults together.
- [x] Step 2: add `cp` so the logging workflow is usable before USB mass-storage lands.
- [x] Step 3: trim and lock the permanent `BOOTMETRIC` set after another real-hardware review pass.
- [x] Step 4: add USB mass-storage support.
- [x] Step 5: finish input/device tuning polish.
