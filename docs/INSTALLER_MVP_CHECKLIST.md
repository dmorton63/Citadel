# Installer MVP Checklist

Date: 2026-06-06

## Purpose

This checklist turns the storage and installer MVP note into a concrete execution list.

The intent is to produce one reliable real-hardware installer path for Citadel.
The installer MVP is scoped to the storage buses and filesystems Citadel already supports.

## Scope

- Target supported disks that Citadel already detects through AHCI or legacy IDE.
- Target FAT32 as the installer filesystem.
- Treat `/system` as the persistent OS data target.
- Do not require broad filesystem or bus support before the first installer works.

## Phase 1: Confirm Current Boot And Storage Model

- [x] Confirm what device backs `/system` on the test machine.
- [x] Confirm whether `/system` is mounted through AHCI or IDE on the test machine.
- [x] Confirm whether the machine is still booting Citadel kernel/modules from Limine-supplied media.
- [ ] Confirm whether any boot artifacts already live on local disk.
- [x] Confirm whether `/shared` is expected to be part of the installer story or remains a separate development convenience.
- [ ] Capture one reference boot log from the test machine showing `/`, `/system`, and `/shared` mount behavior.

## Phase 2: Define Installer MVP Contract

- [x] Declare the supported installer target for v1: AHCI and IDE disks only.
- [x] Declare the supported installer filesystem for v1: FAT32 only.
- [x] Declare the supported install layout for v1.
- [x] Decide whether installer v1 provisions only `/system` or also installs boot artifacts.
- [x] Decide whether installer v1 is destructive-only or supports refresh-in-place.
- [x] Decide what data, if any, must be preserved across reinstall.

Contract decisions for installer v1 (2026-06-13):

- Install layout: one selected target disk with one installer-created FAT32 system partition used for `/system`.
- Boot scope: provision `/system` only; boot-artifact installation remains out of scope for v1.
- Install mode: destructive provisioning only for the selected target; no refresh-in-place mode in v1.
- Preservation policy: no in-place data preservation guarantees in v1; operator must treat reinstall as wipe-and-redeploy.

## Phase 3: Target Discovery And Selection

- [x] Expose a single installer-oriented disk inventory flow based on current `sysdisks` behavior.
- [x] Clearly label controller type, disk index, size, model, and current layout state.
- [x] Show whether a disk already appears partitioned or FAT-formatted.
- [x] Show whether a disk is already bound to `QFS_SYSTEM` or `QFS_SHARED`.
- [ ] Require explicit operator selection of the install target.
- [ ] Prevent ambiguous default-target behavior once multiple disks are present.

## Phase 4: Safe Format And Mount Flow

- [x] Wrap format, reprobe, and mount into one installer operation.
- [ ] Require explicit confirmation before destructive formatting.
- [ ] Refuse format when the target selection is invalid or ambiguous.
- [ ] Emit clear failure states for probe failure, partition failure, format failure, and mount failure.
- [ ] Verify that `/system` is mounted before payload deployment begins.

## Phase 5: Payload Deployment

- [x] Define the exact payload that must be copied to `/system`.
- [x] Define what directories and files must exist on a newly provisioned `/system`.
- [x] Define whether the installer copies from ramdisk content, packaged modules, or a dedicated installer payload.
- [x] Define whether existing `/system` content is wiped, overwritten selectively, or preserved.
- [x] Add post-copy verification so the installer confirms required files exist on `/system`.

Payload decisions for installer v1 (2026-06-13):

- Payload source model: dedicated installer payload derived from the build-time `ramdisk/system/` content (not ad-hoc copy from currently mounted runtime paths).
- Payload copy rule: copy the complete `ramdisk/system/` tree into `/system` while preserving relative paths.
- Required directory set after provisioning:
	- `/system/ui`
	- `/system/wall`
	- `/system/icons`
	- `/system/icons/svg`
	- `/system/fonts`
	- `/system/fonts/static`
	- `/system/.sc`
	- `/system/config/apps`
- Required file entry points after provisioning:
	- `/system/ui/DESKTOP.CML` (installer should normalize from `desktop.cml` source naming when needed)
	- `/system/ui/SPRING.CXS`
	- `/system/ui/common.cui`
- Required asset roots after provisioning:
	- `/system/wall/*` from `ramdisk/system/wall/`
	- `/system/icons/*` and `/system/icons/svg/*` from `ramdisk/system/icons/`
	- `/system/fonts/*` and `/system/fonts/static/*` from `ramdisk/system/fonts/`
- Non-required pre-seed artifact: `/system/CMMS.QDB` (runtime creates and initializes if absent).

Post-copy verification contract for installer v1 (2026-06-13):

- Verification gate must run immediately after payload copy and before final success is reported.
- Required directory checks (must exist):
	- `/system/ui`
	- `/system/wall`
	- `/system/icons`
	- `/system/icons/svg`
	- `/system/fonts`
	- `/system/fonts/static`
	- `/system/.sc`
	- `/system/config/apps`
- Required file checks (must exist and be readable):
	- `/system/ui/DESKTOP.CML`
	- `/system/ui/SPRING.CXS`
	- `/system/ui/common.cui`
- Required non-empty asset roots (must contain at least one file):
	- `/system/wall`
	- `/system/icons`
	- `/system/icons/svg`
	- `/system/fonts`
	- `/system/fonts/static`
- Failure policy:
	- If any required path is missing or unreadable, installer reports `payload verification failed` and does not report overall success.
	- Failure output must include the first missing/unreadable path and a final count of failed checks.

## Phase 6: Boot Path Handling

- [x] Document the current boot path separately from the desired installed boot path.
- [x] Decide whether installer v1 stops at provisioning `/system` only.
- [x] If boot artifacts are included in v1, define exactly what is written and where.
- [x] Define how production signatures interact with installed assets.
- [ ] Verify that the installed target can boot using the intended method on the test machine.

Boot-path decisions for installer v1 (2026-06-13):

- Boot-artifact write plan in v1: not applicable by design; v1 does not install boot artifacts.
- Production-signature interaction in v1: unchanged from current behavior; boot-time signature enforcement remains tied to Limine-supplied boot assets, while installer-provisioned `/system` payload is outside boot-artifact signature installation scope for v1.

## Phase 7: Operator Experience

- [x] Make installer progress visible in both console UI and serial log.
- [x] Report the chosen disk, filesystem, mount point, and copy status.
- [x] Report a final success state that tells the operator what to do next.
- [x] Report a final failure state that points to the failed stage.

Current installer command surface (implemented in [kernel/QKSystemVolumeCommands.cpp](../kernel/QKSystemVolumeCommands.cpp)):

- `sysdisks` prints the install-relevant disk inventory and stage markers for discovery.
- `sysformat [diskN|N]` wraps select, format, probe/mount, payload verification, and completion reporting.
- `sysmount` reprobes and mounts `/system` without reformatting.
- `sysverify` checks the installer-required payload set under `/system`.

Operator note:

- The current real-hardware path is centered on `/system` persistence plus explicit operator review of the disk inventory before destructive format operations.
- The boot path already falls back to `INSTALLER` mode when `/system` is unavailable and security enforcement is active.

Operator output contract for installer v1 (2026-06-13):

- Every stage message must be emitted to both console UI and serial log.
- Required stage markers (ordered):
	- `stage=discover`
	- `stage=select`
	- `stage=format`
	- `stage=probe_mount`
	- `stage=copy_payload`
	- `stage=verify_payload`
	- `stage=complete`
- Selection summary line must include:
	- selected disk index
	- controller type (`ahci` or `ide`)
	- target filesystem (`fat32`)
	- mount target (`/system`)
- Copy progress must include at minimum:
	- source payload root
	- destination root (`/system`)
	- copied file count
	- copied byte total
- Success terminal message must include:
	- `status=success`
	- selected disk index and controller type
	- `/system` mount confirmation
	- operator next step text: reboot and run normal startup path
- Failure terminal message must include:
	- `status=failure`
	- failed stage marker
	- status/error code (when available)
	- first failing path or operation
	- operator recovery hint (`run sysdisks`, `sysmount`, or `sysformat` as appropriate)

## Phase 8: Verification

- [ ] Verify the installer flow in QEMU using the current supported storage path.
- [ ] Verify the installer flow on the real test machine.
- [ ] Verify that `/system` contents persist across reboot.
- [ ] Verify that a production-mode boot still accepts the installed assets and configuration.
- [ ] Verify that desktop startup still loads correctly from the installed persistent state.

Verification order for real-hardware bring-up:

1. Run `sysdisks` and confirm the expected controller, disk index, and layout state.
2. Run `sysformat` against the intended target and confirm `installer: stage=format` and `installer: stage=probe_mount` succeed.
3. Run `sysverify` and confirm the payload checklist passes for the mounted `/system`.
4. Reboot once and confirm `/system` is still present and the installed payload remains readable.
5. Confirm normal production-mode boot continues into the desktop using the installed persistent state.

Verification procedure for installer v1 (2026-06-13):

- QEMU flow validation:
	- Boot with persistent system disk attached.
	- Run installer path end-to-end (discover -> select -> format -> mount -> copy -> verify).
	- Capture console + serial logs and confirm required stage/status markers.
- Real-machine flow validation:
	- Run same installer path on the target hardware.
	- Capture one reference boot/install log artifact for project records.
- Persistence validation:
	- After successful install, reboot at least once.
	- Confirm `/system` remains mounted and payload verification paths remain present.
- Production-mode validation:
	- Boot in production mode with current trusted boot media.
	- Confirm no signature refusal and confirm desktop session enters normal path.
- Desktop runtime validation:
	- Confirm desktop loads expected layout/theme.
	- Confirm CMMS database auto-creates if missing and remains usable after reboot.

## Out Of Scope For MVP

- [x] Do not add NVMe support just to complete the first installer.
- [x] Do not add USB mass storage support just to complete the first installer.
- [x] Do not add ext4, NTFS, exFAT, or Btrfs support just to complete the first installer.
- [x] Do not build a full graphical partition editor before the first installer works.
- [x] Do not expand to multiple install layouts before the first layout is stable.

## Exit Criteria

The installer MVP is complete when all of the following are true:

- [ ] Citadel can identify a supported target disk on the test machine.
- [ ] Citadel can safely format and mount that target as `/system`.
- [ ] Citadel can deploy the required payload to `/system`.
- [ ] Citadel can boot or run using the intended installed path on the test machine.
- [ ] The installation flow is understandable from console and serial output.

## Post-MVP: Secure Boot Enablement Track

Current lab posture (2026-06-13): test machine runs with Secure Boot disabled and TPM enabled.

Reference implementation note: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`.

- [ ] Define target Secure Boot model for Citadel boot media (direct signed bootloader vs shim-based chain).
- [ ] Define key ownership and enrollment policy (PK, KEK, db, dbx) for dev, staging, and production devices.
- [ ] Define signing pipeline outputs for boot artifacts and installer-delivered assets that participate in trust decisions.
- [ ] Add deterministic build verification that fails when required Secure Boot signatures/artifacts are missing.
- [ ] Document firmware enrollment and rollback/recovery procedure for key updates.
- [ ] Validate boot success on Secure Boot enabled hardware with TPM still enabled.
- [ ] Validate refusal behavior and operator diagnostics for unsigned/tampered boot artifacts under Secure Boot.

Secure Boot readiness gate:

- Secure Boot enabled boot path is reproducible on the primary test machine.
- Signed-boot failure modes are deterministic and documented for operators.
- TPM-backed policy behavior remains intact when Secure Boot is enabled.