#!/usr/bin/env bash
# Secure Boot negative tests under OVMF with the Citadel lab keys enrolled.
# Requires a prior `./build.sh --secure-boot` (uses build/secureboot/iso).
#   signed    : the signed tree must reach the kernel (control case)
#   unsigned  : stock Limine BOOTX64.EFI must be refused by firmware
#   kernel    : a modified citadel.elf must be refused by Limine (BLAKE2B pin)
#   config    : a modified limine.conf must be refused by Limine (enrolled hash)
set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SB_DIR="${PROJECT_DIR}/build/secureboot"
STAGE="${SB_DIR}/iso"
WORK="${SB_DIR}/negtest"
OVMF_DIR="${OVMF_DIR:-/usr/share/OVMF}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-90}"
KERNEL_MARKER="EFI runtime:"

[[ -d "${STAGE}" ]] || { echo "Missing ${STAGE}; run ./build.sh --secure-boot" >&2; exit 1; }
[[ -f "${SB_DIR}/OVMF_VARS_citadel.fd" ]] || { echo "Run tools/secureboot/make_lab_keys.sh" >&2; exit 1; }

rm -rf "${WORK}"
mkdir -p "${WORK}"

make_iso() {
  local tree="$1" iso="$2"
  xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
    -boot-info-table -eltorito-alt-boot -e boot/limine/limine-uefi-cd.bin -no-emul-boot \
    --protective-msdos-label -isohybrid-gpt-basdat "${tree}" -o "${iso}" >/dev/null 2>&1
}

# Boots an ISO until the kernel marker appears or the timeout passes; prints the serial log path.
boot_iso() {
  local name="$1" iso="$2"
  local serial="${WORK}/${name}.serial.log"
  cp "${SB_DIR}/OVMF_VARS_citadel.fd" "${WORK}/${name}.vars.fd"
  qemu-system-x86_64 -machine q35,smm=on \
    -global driver=cfi.pflash01,property=secure,value=on \
    -drive "if=pflash,format=raw,unit=0,readonly=on,file=${OVMF_DIR}/OVMF_CODE_4M.secboot.fd" \
    -drive "if=pflash,format=raw,unit=1,file=${WORK}/${name}.vars.fd" \
    -cdrom "${iso}" -m 3G -smp 2 -vga vmware -display none -net none \
    -serial "file:${serial}" >/dev/null 2>&1 &
  local pid=$!
  local deadline=$((SECONDS + BOOT_TIMEOUT))
  while (( SECONDS < deadline )) && kill -0 "${pid}" 2>/dev/null; do
    grep -q "${KERNEL_MARKER}" "${serial}" 2>/dev/null && break
    sleep 1
  done
  kill "${pid}" 2>/dev/null
  wait "${pid}" 2>/dev/null
  echo "${serial}"
}

FAILED=0
check() {
  local name="$1" expect="$2" serial="$3" booted=no
  grep -q "${KERNEL_MARKER}" "${serial}" && booted=yes
  local detail=""
  grep -q "Access Denied" "${serial}" && detail=" (firmware: Access Denied)"
  if [[ "${booted}" == "${expect}" ]]; then
    echo "PASS ${name}: kernel reached=${booted}${detail}"
  else
    echo "FAIL ${name}: kernel reached=${booted}, expected ${expect}${detail}"
    FAILED=1
  fi
}

# Control: the signed tree boots.
make_iso "${STAGE}" "${WORK}/signed.iso"
check signed yes "$(boot_iso signed "${WORK}/signed.iso")"

# Unsigned Limine: firmware must refuse it.
cp -a "${STAGE}" "${WORK}/unsigned"
cp "${PROJECT_DIR}/limine/limine-uefi-cd.bin" "${WORK}/unsigned/boot/limine/limine-uefi-cd.bin"
cp "${PROJECT_DIR}/limine/BOOTX64.EFI" "${WORK}/unsigned/EFI/BOOT/BOOTX64.EFI"
make_iso "${WORK}/unsigned" "${WORK}/unsigned.iso"
serial="$(boot_iso unsigned "${WORK}/unsigned.iso")"
check unsigned no "${serial}"
grep -q "Access Denied" "${serial}" || { echo "FAIL unsigned: firmware did not report Access Denied"; FAILED=1; }

# Tampered kernel: append a byte so the BLAKE2B pin no longer matches.
cp -a "${STAGE}" "${WORK}/kernel"
printf '\0' >> "${WORK}/kernel/boot/citadel.elf"
make_iso "${WORK}/kernel" "${WORK}/kernel.iso"
check kernel no "$(boot_iso kernel "${WORK}/kernel.iso")"

# Tampered config: change the kernel command line without re-enrolling.
cp -a "${STAGE}" "${WORK}/config"
for conf in "${WORK}/config/boot/limine/limine.conf" "${WORK}/config/limine.conf"; do
  [[ -f "${conf}" ]] && sed -i 's/cmdline: citadel/cmdline: citadel citadel.tampered=1/' "${conf}"
done
make_iso "${WORK}/config" "${WORK}/config.iso"
check config no "$(boot_iso config "${WORK}/config.iso")"

if (( FAILED )); then
  echo "Secure Boot negative tests: FAILED (logs in ${WORK})"
  exit 1
fi
rm -rf "${WORK}"
echo "Secure Boot negative tests: all passed"
