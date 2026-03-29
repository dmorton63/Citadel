#!/usr/bin/env bash
set -euo pipefail

# Host-side formatter for build/system.qcow2.
# Creates a FAT32 filesystem in the qcow2 so Citadel can mount it as /system.
#
# Requires: qemu-img, qemu-nbd, mkfs.fat (dosfstools)
# Linux only.

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="${PROJECT_DIR}/build/system.qcow2"
SIZE_MB="${1:-1024}"

if [[ $EUID -ne 0 ]]; then
  echo "Run as root (needs qemu-nbd). Example: sudo $0" >&2
  exit 1
fi

command -v qemu-img >/dev/null 2>&1 || { echo "Missing qemu-img (qemu-utils)" >&2; exit 1; }
command -v qemu-nbd >/dev/null 2>&1 || { echo "Missing qemu-nbd (qemu-utils)" >&2; exit 1; }
command -v mkfs.fat >/dev/null 2>&1 || { echo "Missing mkfs.fat (dosfstools)" >&2; exit 1; }

mkdir -p "$(dirname "${DISK}")"

if [[ ! -f "${DISK}" ]]; then
  qemu-img create -f qcow2 "${DISK}" "${SIZE_MB}M" >/dev/null
  echo "Created ${DISK} (${SIZE_MB}MB)"
fi

modprobe nbd max_part=8 || true

NBD="/dev/nbd0"

cleanup() {
  qemu-nbd --disconnect "${NBD}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

qemu-nbd --connect "${NBD}" "${DISK}"

# Wipe first few MiB to remove any old signatures/partitions.
dd if=/dev/zero of="${NBD}" bs=1M count=4 conv=fsync status=none

# Create a single MBR partition of type W95 FAT32 (0x0c).
# sfdisk uses sectors; start at 2048 for alignment.
TOTAL_SECTORS=$(blockdev --getsz "${NBD}")
START=2048
SIZE=$((TOTAL_SECTORS - START))
printf "${START},${SIZE},c,*\n" | sfdisk --quiet "${NBD}"

if command -v partprobe >/dev/null 2>&1; then
  partprobe "${NBD}" || true
else
  # partprobe isn't always available on minimal distros.
  blockdev --rereadpt "${NBD}" >/dev/null 2>&1 || true
  if command -v udevadm >/dev/null 2>&1; then
    udevadm settle >/dev/null 2>&1 || true
  fi
fi
sleep 0.2

PART="${NBD}p1"
if [[ ! -b "${PART}" ]]; then
  # Some setups expose partitions without 'p' (rare); fallback.
  PART="${NBD}1"
fi

# Use larger clusters (4KB) to reduce FAT traversal overhead for large assets.
mkfs.fat -F 32 -s 8 -n CITADEL_SYS "${PART}" >/dev/null

echo "Formatted ${DISK} with FAT32 partition label CITADEL_SYS"
