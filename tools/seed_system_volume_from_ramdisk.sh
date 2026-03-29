#!/usr/bin/env bash
set -euo pipefail

# Seed the persistent system volume (build/system.qcow2) with the same
# /system assets that exist in the ramdisk tree.
#
# This eliminates missing /system/* warnings when --system-vol shadows the ramdisk.
#
# Usage:
#   sudo tools/seed_system_volume_from_ramdisk.sh
#
# Requires: qemu-nbd (qemu-utils), mkfs.fat (dosfstools), sfdisk (util-linux)

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="${PROJECT_DIR}/build/system.qcow2"
RAM_SYSTEM="${PROJECT_DIR}/ramdisk/system"
MNT="/mnt/citadel_sys"
NBD="/dev/nbd0"

REFORMAT=false
if [[ ${1:-} == "--reformat" ]]; then
  REFORMAT=true
fi

if [[ $EUID -ne 0 ]]; then
  echo "Run as root. Example: sudo $0" >&2
  exit 1
fi

command -v qemu-img >/dev/null 2>&1 || { echo "Missing qemu-img (qemu-utils)" >&2; exit 1; }
command -v qemu-nbd >/dev/null 2>&1 || { echo "Missing qemu-nbd (qemu-utils)" >&2; exit 1; }
command -v mkfs.fat >/dev/null 2>&1 || { echo "Missing mkfs.fat (dosfstools)" >&2; exit 1; }
command -v sfdisk >/dev/null 2>&1 || { echo "Missing sfdisk (util-linux)" >&2; exit 1; }

if [[ ! -d "${RAM_SYSTEM}" ]]; then
  echo "Missing ramdisk system tree: ${RAM_SYSTEM}" >&2
  exit 1
fi

mkdir -p "$(dirname "${DISK}")"
if [[ ! -f "${DISK}" ]]; then
  qemu-img create -f qcow2 "${DISK}" 1G >/dev/null
  echo "Created ${DISK} (1G)"
fi

modprobe nbd max_part=8 >/dev/null 2>&1 || true

cleanup() {
  umount "${MNT}" >/dev/null 2>&1 || true
  qemu-nbd --disconnect "${NBD}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

qemu-nbd --disconnect "${NBD}" >/dev/null 2>&1 || true
qemu-nbd --connect "${NBD}" "${DISK}"

# Ensure a partition exists; if none, create and format.
# Detect partition node (p1 vs 1).
probe_part() {
  if [[ -b "${NBD}p1" ]]; then echo "${NBD}p1"; return; fi
  if [[ -b "${NBD}1" ]]; then echo "${NBD}1"; return; fi
  echo ""
}

PART="$(probe_part)"
if [[ "${REFORMAT}" == true ]]; then
  PART=""
fi

if [[ -z "${PART}" ]]; then
  # New/blank image: create MBR + single FAT32 partition and format it.
  dd if=/dev/zero of="${NBD}" bs=1M count=4 conv=fsync status=none
  TOTAL_SECTORS=$(blockdev --getsz "${NBD}")
  START=2048
  SIZE=$((TOTAL_SECTORS - START))
  printf "${START},${SIZE},c,*\n" | sfdisk --quiet "${NBD}"

  if command -v partprobe >/dev/null 2>&1; then
    partprobe "${NBD}" || true
  else
    blockdev --rereadpt "${NBD}" >/dev/null 2>&1 || true
    command -v udevadm >/dev/null 2>&1 && udevadm settle >/dev/null 2>&1 || true
  fi
  sleep 0.2

  PART="$(probe_part)"
  if [[ -z "${PART}" ]]; then
    echo "Failed to find partition node for ${NBD} (expected ${NBD}p1)." >&2
    exit 1
  fi

  # Use larger clusters (4KB) to reduce FAT traversal overhead for large assets.
  mkfs.fat -F 32 -s 8 -n CITADEL_SYS "${PART}" >/dev/null
  echo "Formatted ${DISK} with FAT32 partition label CITADEL_SYS"
fi

mkdir -p "${MNT}"
mount "${PART}" "${MNT}"

# Seed system subtree.
mkdir -p "${MNT}"
cp -a --no-preserve=ownership "${RAM_SYSTEM}/." "${MNT}/"

# Add a stable alias for theme lookups.
mkdir -p "${MNT}/fonts"
if [[ -f "${RAM_SYSTEM}/fonts/opensans-regular.ttf" && ! -f "${MNT}/fonts/OpenSans.ttf" ]]; then
  cp -a --no-preserve=ownership "${RAM_SYSTEM}/fonts/opensans-regular.ttf" "${MNT}/fonts/OpenSans.ttf"
fi

sync

echo "Seeded ${DISK} from ${RAM_SYSTEM}"
