# 2026-02-25 — JSON Function MVP Checkpoint

Checkpoint created after landing the first working "JSON Function module(s)" MVP plumbing (validator + interpreter + registry) and the strict-number JSON parsing option needed by the spec.

## Snapshot artifact

- Workspace: `/home/dmort/citadel`
- Git HEAD: `028a39b`
- Working tree: DIRTY (5 changes when captured)

- Archive: `backups/citadel_backup_20260225_162928_028a39b_dirty5.tgz`
- SHA-256: `e743af3d6442a20b2ecbd3f42ed5dcba3852f92d9fecba13b999ea5877a65214`
- Checksum file: `backups/citadel_backup_20260225_162928_028a39b_dirty5.tgz.sha256`

### Excludes (to keep size reasonable)
- `./.git`
- `./build` (CMake outputs)
- `./backups` (avoid recursive inclusion)
- `./iso/modules/ramdisk.img` (generated)
- `./tmp`
- `./shared`

## What changed (high level)

- Added `QC::JSON::parseEx()` + `QC::JSON::Parser::Options` so consumers can require canonical numeric literals and forbid exponent notation.
- Added new `QJFunctions` library implementing:
  - strict schema validation for `jsonFunctionGen.md` MVP
  - typed frame + interpreter backend for the allowed ops
  - simple `(name, version)` function registry
- Wired `QJFunctions` into CMake and linked it into the kernel build.

## Verify / restore

- Verify checksum:
  - `cd /home/dmort/citadel && sha256sum -c backups/citadel_backup_20260225_162928_028a39b_dirty5.tgz.sha256`
- Restore into a new folder:
  - `mkdir -p /tmp/citadel_restore && tar -xzf backups/citadel_backup_20260225_162928_028a39b_dirty5.tgz -C /tmp/citadel_restore`
