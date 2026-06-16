# Current Boot And Storage Mapping

Date: 2026-06-06

## Purpose

This note captures the current observed Citadel boot and storage model so installer design can start from facts rather than assumptions.

## Current Model

Citadel currently appears to use a mixed boot and storage arrangement.

- `/` is provided from the Limine ramdisk module.
- `/system` is a separately probed persistent FAT volume.
- `/shared` is a separate additional mounted volume when present.

This means Citadel is not yet operating as a fully disk-installed system in the common Linux or Windows sense.
It is currently closer to a bootloader-supplied runtime plus persistent mounted data volumes.

## What `/` Is Today

The boot ramdisk is mounted as the root volume.

- The ramdisk module is found through Limine module discovery.
- That module is registered as `QFS_RAMDISK0`.
- It is mounted at `/`.
- Startup configuration and boot-time assets are then read from that mounted ramdisk-backed root.

Practical implication:

- Core boot-provided assets currently come from the ramdisk path, not from `/system`.

## What `/system` Is Today

`/system` is a persistent block-backed volume discovered after driver bring-up.

- Storage probing prefers AHCI first for the system volume.
- If AHCI does not provide a usable system volume, Citadel falls back to IDE probing.
- The resulting persistent volume is registered as `QFS_SYSTEM` and mounted at `/system`.

Practical implication:

- `/system` is already the correct conceptual target for installer persistence.
- Citadel already treats `/system` as a separate persistent volume, not as the bootloader-supplied root.

## What `/shared` Is Today

`/shared` is a separate optional mounted volume.

- Current probing uses IDE shared-volume discovery for this path.
- It should be treated as separate from the installer MVP unless explicitly required.

## What The Current Logs Show

Based on current observed boot logs:

- Citadel mounts the ramdisk at `/`.
- Citadel then probes storage controllers after driver initialization.
- On the observed machine and QEMU paths, AHCI may be present but not always provide a mountable FAT system volume.
- IDE probing can then register and mount `QFS_SYSTEM` at `/system`.
- The desktop and CMMS persistence behavior demonstrate that `/system` is being used as persistent storage.

Practical implication:

- Current persistence and current boot origin are not the same thing.
- The system can persist state on `/system` while still booting kernel and module payloads from Limine-supplied media.

## What Boot Artifacts Appear To Be Today

Current production boot behavior indicates that major boot artifacts are still supplied through the bootloader/module path.

- `BOOT.JSN` and `SYSCFG.JSN` are loaded through the Limine module path.
- Desktop signature verification also occurs before runtime filesystem behavior takes over.
- The root filesystem seen early in boot is ramdisk-backed.

Practical implication:

- The current system should be treated as bootloader-seeded plus persistent `/system`, not yet as a fully disk-installed standalone boot.

## Current Installer-Relevant Conclusion

Today, the safest installer framing is:

- provision and validate `/system` first
- decide separately whether installer v1 also lays down boot artifacts

That keeps the first installer grounded in the part of the storage model that already exists and is already working.

## Open Questions To Resolve Before Final Installer Design

- Does the test machine already have Limine or equivalent boot support installed locally?
- Is the intended first installer supposed to install only the persistent Citadel system volume?
- Or is the intended first installer supposed to create a self-booting local-disk installation?
- If self-booting is required, what exact boot artifacts must be written to disk and to which location?
- Should `/shared` remain outside installer scope for v1?

## Working Rule

Until proven otherwise by direct test-machine mapping, assume the current real-hardware setup is:

- boot origin: Limine-supplied ramdisk/modules
- persistent OS data: `/system`
- optional additional data: `/shared`

That assumption is strong enough to drive installer MVP planning, but it should still be verified explicitly on the target machine.