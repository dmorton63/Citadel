#!/usr/bin/env bash
# Stage a Secure Boot copy of the ISO tree:
#   - pin citadel.elf and ramdisk.img by BLAKE2B in limine.conf,
#   - enroll the config's BLAKE2B into a copy of Limine's BOOTX64.EFI,
#   - sign that BOOTX64.EFI with the lab db key (Authenticode, sbsign),
#   - place it in both the ISO tree and the El Torito EFI FAT image.
# Usage: stage_signed_iso.sh <iso-source-dir> <stage-dir>
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${1:?iso source dir}"
STAGE_DIR="${2:?stage dir}"
LIMINE_DIR="${PROJECT_DIR}/limine"
KEY_DIR="${KEY_DIR:-${PROJECT_DIR}/build/secureboot/keys}"
LIMINE_TOOL="${PROJECT_DIR}/build/limine-host"

for tool in sbsign sbverify b2sum mcopy cc; do
  command -v "${tool}" >/dev/null || {
    echo "Missing ${tool} (sudo apt install sbsigntool mtools coreutils build-essential)" >&2
    exit 1
  }
done
for f in "${KEY_DIR}/db.key" "${KEY_DIR}/db.crt"; do
  [[ -f "${f}" ]] || { echo "Missing ${f}; run tools/secureboot/make_lab_keys.sh" >&2; exit 1; }
done

if [[ ! -x "${LIMINE_TOOL}" || "${LIMINE_DIR}/limine.c" -nt "${LIMINE_TOOL}" ]]; then
  mkdir -p "$(dirname "${LIMINE_TOOL}")"
  cc -O2 -std=c99 -I"${LIMINE_DIR}" "${LIMINE_DIR}/limine.c" -o "${LIMINE_TOOL}"
fi

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"
cp -a "${SRC_DIR}/." "${STAGE_DIR}/"

KERNEL_HASH="$(b2sum "${STAGE_DIR}/boot/citadel.elf" | cut -d' ' -f1)"
RAMDISK_HASH="$(b2sum "${STAGE_DIR}/modules/ramdisk.img" | cut -d' ' -f1)"

CONF="${STAGE_DIR}/boot/limine/limine.conf"
sed -i -E \
  -e "s|^(\s*kernel_path:\s*boot\(\):/boot/citadel\.elf)(#[0-9a-f]+)?\s*$|\1#${KERNEL_HASH}|" \
  -e "s|^(\s*module_path:\s*boot\(\):/modules/ramdisk\.img)(#[0-9a-f]+)?\s*$|\1#${RAMDISK_HASH}|" \
  "${CONF}"
if grep -E '^\s*(kernel|module)_path:' "${CONF}" | grep -qv '#'; then
  echo "limine.conf has a kernel/module path without a BLAKE2B pin" >&2
  exit 1
fi
# Limine loads whichever config it finds first; keep every copy identical so
# the enrolled hash matches regardless of search order.
for alt in "${STAGE_DIR}/limine.conf" "${STAGE_DIR}/boot/limine.conf" "${STAGE_DIR}/EFI/BOOT/limine.conf"; do
  [[ -f "${alt}" ]] && cp "${CONF}" "${alt}"
done
CONFIG_HASH="$(b2sum "${CONF}" | cut -d' ' -f1)"

UNSIGNED_EFI="$(mktemp)"
trap 'rm -f "${UNSIGNED_EFI}"' EXIT
cp "${LIMINE_DIR}/BOOTX64.EFI" "${UNSIGNED_EFI}"
"${LIMINE_TOOL}" enroll-config --quiet "${UNSIGNED_EFI}" "${CONFIG_HASH}"

SIGNED_EFI="${STAGE_DIR}/EFI/BOOT/BOOTX64.EFI"
mkdir -p "$(dirname "${SIGNED_EFI}")"
sbsign --key "${KEY_DIR}/db.key" --cert "${KEY_DIR}/db.crt" \
  --output "${SIGNED_EFI}" "${UNSIGNED_EFI}" 2>/dev/null
sbverify --cert "${KEY_DIR}/db.crt" "${SIGNED_EFI}" >/dev/null

EFI_IMG="${STAGE_DIR}/boot/limine/limine-uefi-cd.bin"
cp "${LIMINE_DIR}/limine-uefi-cd.bin" "${EFI_IMG}"
mcopy -o -i "${EFI_IMG}" "${SIGNED_EFI}" ::/EFI/BOOT/BOOTX64.EFI
# Only the signed x86-64 loader is trusted; drop the other architectures.
for other in BOOTAA64.EFI BOOTIA32.EFI BOOTRISCV64.EFI BOOTLOONGARCH64.EFI; do
  mdel -i "${EFI_IMG}" "::/EFI/BOOT/${other}" 2>/dev/null || true
done

echo "kernel blake2b:  ${KERNEL_HASH}"
echo "ramdisk blake2b: ${RAMDISK_HASH}"
echo "config blake2b:  ${CONFIG_HASH}"
echo "signed:          ${SIGNED_EFI}"
