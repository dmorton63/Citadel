# Storage And Installer MVP

Date: 2026-06-06

## Purpose

This note captures the current state of Citadel's storage and filesystem stack, what is missing for a first real-hardware installer, and what should explicitly wait until after the first installer works.

The goal is to keep scope disciplined.
Citadel does not need a fully general-purpose storage stack before it can ship an installer MVP.
It needs one storage path that is reliable end-to-end on the target hardware.

## 1. What Citadel Already Supports Today

### Filesystem layer

- A VFS exists and already supports mount, unmount, open, directory operations, stat, remove, rename, and path resolution.
- A volume manager exists and already supports volume registration, auto-mount, explicit mount, unmount, and volume enumeration.
- Mounted volumes can be tagged with source metadata such as `ramdisk`, `ide`, `ahci`, or `module`.

### On-disk filesystem support

- FAT16 support exists.
- FAT32 support exists.
- FAT auto-detection exists through FAT boot-sector probing.
- The current FAT probe explicitly assumes 512-byte sectors.

### Storage device support

- AHCI SATA probing exists for persistent system-volume discovery.
- Legacy IDE/ATA probing exists for system, shared, and additional data volumes.
- Limine modules can also be exposed as block devices and mounted through the same volume pipeline.

### Mount and runtime behavior

- Drivers register block devices through the storage registry.
- The driver manager triggers pending auto-mounts after device probing.
- Citadel already uses this path in practice for `/`, `/system`, and `/shared`.
- Current real-hardware and QEMU runs demonstrate that `/system` can be mounted as a persistent FAT volume.

### Operator and recovery commands

- `sysdisks` lists detected AHCI and IDE disks and reports layout hints such as FAT boot sector, partition table, and MBR signature.
- `sysformat` can partition and format an eligible detected disk as FAT32 and then attempt to mount it as `/system`.
- `sysmount` can reprobe and mount `/system` without a reboot.

### Practical summary

Citadel already has a real storage stack.
It is not yet broad, but it is sufficient to support an installer MVP if the installer is scoped to the hardware and formats Citadel already understands.

## 2. What Is Missing For An Installer MVP

The first installer does not need every possible bus, partition scheme, or filesystem.
It does need a clean, reliable installation workflow.

### Device and topology clarity

- A single installer-oriented disk inventory flow is needed.
- The installer must clearly distinguish controller, disk, partition, mounted volume, and chosen install target.
- The install target selection must be explicit and hard to confuse.

### Safer install workflow

- A dedicated installer flow should wrap `sysdisks`, `sysformat`, probe, mount, payload copy, and boot-asset installation into one guided path.
- The installer should refuse destructive actions unless the target is clearly selected and confirmed.
- The installer should detect and report whether the target already contains a partition table, FAT boot sector, or existing Citadel payload.

### Payload deployment

- The installer needs a defined payload copy step for the Citadel system image, runtime assets, and configuration files.
- The installer needs a defined rule for what must be placed on `/system` and what remains bootloader-supplied.
- The installer needs a deterministic way to refresh or preserve existing `/system` content.

### Boot-path definition

- The real-hardware boot path must be documented precisely before the installer is finalized.
- It must be explicit whether the machine boots from Limine media, from a local disk-installed boot path, or from a mixed path.
- The installer must know whether it is only provisioning `/system` or also laying down boot artifacts.

### Filesystem/format support needed for MVP

- FAT32 formatting and mounting are already present and should remain the installer MVP target.
- The installer should standardize on one expected disk layout instead of supporting multiple installation layouts on day one.
- If partition creation is currently implicit or minimal, the installer should formalize the expected partition scheme and validation rules.

### Better observability

- The installer should log probe, format, mount, copy, and verification outcomes in a way that is readable in both serial output and console UI.
- Failures should tell the operator whether the problem is controller detection, partitioning, FAT formatting, mount failure, or payload copy.

### Minimum recommendation for MVP

The first installer MVP should target:

- AHCI SATA disks and legacy IDE disks that Citadel already detects.
- FAT32 as the supported install filesystem.
- `/system` as the primary persistent install target.
- A conservative, operator-confirmed install flow with clear status output.

## 3. What Can Wait Until After The First Real Hardware Installer Works

These items are reasonable future goals, but they should not block the first installer.

### Additional filesystem formats

- exFAT
- NTFS
- ext2/ext4
- Btrfs
- ISO9660/UDF
- encrypted filesystem formats

### Additional storage transports

- NVMe
- USB mass storage
- RAID support
- SAS and enterprise storage paths
- hot-plug and removable-media workflows beyond the installer's immediate needs

### Broader sector and partition support

- non-512-byte sector handling
- GPT-first installation support if current flows are still MBR-oriented
- more advanced partition editors and partition-resize workflows
- multiple install layouts and dual-boot-aware policies

### Rich desktop-grade storage UX

- a full storage settings app
- drive health dashboards
- graphical partition editors
- per-volume policy management
- user-facing removable-media workflows

### Advanced lifecycle features

- in-place OS upgrade orchestration
- snapshot/rollback support
- online filesystem migration between formats
- multi-disk installation policies
- installer automation profiles and unattended install modes

## Recommendation

Citadel should not try to become a full general-purpose storage platform before the first installer.

The correct next target is narrower:

1. Confirm the real-hardware boot path and how `/system` is actually backed.
2. Standardize one installer disk layout and one supported filesystem for installation.
3. Build one safe installer flow around the storage and format support Citadel already has.
4. Expand bus and filesystem coverage only after that path works reliably on the target machine.

## Working Rule

For the first installer, breadth is less important than determinism.
One reliable install path on known hardware is more valuable than partial support for many devices and formats.