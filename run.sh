#!/usr/bin/env bash
set -euo pipefail

# Citadel run-only helper.
# Runs the last-built ISO with the same QEMU defaults as build.sh.
#
# Examples:
#   ./run.sh --tpm --relmouse --prod
#   ./run.sh --headless
#

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
ISO_DIR="${BUILD_DIR}/iso"
ISO_FILE="${BUILD_DIR}/citadel-limine.iso"
SERIAL_LOG="${BUILD_DIR}/serial.log"
SHARED_DIR="${PROJECT_DIR}/shared"
SYSTEM_DISK="${BUILD_DIR}/system.qcow2"

TPM=false
USE_TABLET=true
HEADLESS=false
FULLSCREEN=false
AUTO_GRAB=true
PRODUCTION=false
RUN_FOR_SECONDS=0
SYSTEM_VOL=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--run) shift ;; # build.sh compatibility (run.sh always runs)
    -v|--verbose) shift ;; # build.sh compatibility
    -j*) shift ;; # build.sh compatibility
    --tpm) TPM=true; shift ;;
    --tablet) USE_TABLET=true; shift ;;
    --relmouse) USE_TABLET=false; shift ;;
    --headless) HEADLESS=true; shift ;;
    -f|--fullscreen) FULLSCREEN=true; shift ;;
    --no-grab) AUTO_GRAB=false; shift ;;
    --prod) PRODUCTION=true; shift ;;
    --system-vol) SYSTEM_VOL=true; shift ;;
    --run-for) RUN_FOR_SECONDS="${2:-0}"; shift 2 ;;
    -h|--help)
      cat <<EOF
usage: ./run.sh [options]

Options:
  --tpm            Enable TPM2 emulation (requires swtpm)
  --tablet         Use absolute USB tablet (default)
  --relmouse       Use relative USB mouse + keyboard
  --headless       Run QEMU without a GUI (serial still logs)
  -f, --fullscreen Start QEMU fullscreen
  --no-grab        Disable grab-on-hover helper
  --prod           Warn if running prod without --tpm
  --system-vol     Attach persistent system volume disk (build/system.qcow2)
  --run-for <s>    Auto-stop after <s> seconds (requires timeout)
EOF
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      echo "Try: ./run.sh --help" >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "${ISO_FILE}" ]]; then
  echo "Missing ISO: ${ISO_FILE}" >&2
  echo "Build first (example): ./build.sh" >&2
  exit 1
fi

: > "${SERIAL_LOG}"

if [[ "${PRODUCTION}" == true && "${TPM}" == false ]]; then
  echo "Warning: --prod without --tpm will likely refuse to boot (TPM required for production enforcement)." >&2
fi

SWTPM_PID=""
TPM_ARGS=()
if [[ "${TPM}" == true ]]; then
  if ! command -v swtpm >/dev/null 2>&1; then
    echo "swtpm not found. Install with: sudo apt install swtpm swtpm-tools" >&2
    exit 1
  fi

  SWTPM_DIR="${BUILD_DIR}/swtpm"
  SWTPM_SOCK="${SWTPM_DIR}/swtpm-sock"
  SWTPM_LOG="${SWTPM_DIR}/swtpm.log"
  mkdir -p "${SWTPM_DIR}" "${SWTPM_DIR}/state"
  rm -f "${SWTPM_SOCK}"

  swtpm socket --tpm2 \
    --tpmstate dir="${SWTPM_DIR}/state" \
    --ctrl type=unixio,path="${SWTPM_SOCK}" \
    --log level=20 \
    >"${SWTPM_LOG}" 2>&1 &
  SWTPM_PID=$!

  cleanup_swtpm() {
    if [[ -n "${SWTPM_PID}" ]] && kill -0 "${SWTPM_PID}" 2>/dev/null; then
      kill "${SWTPM_PID}" 2>/dev/null || true
      wait "${SWTPM_PID}" 2>/dev/null || true
    fi
    rm -f "${SWTPM_SOCK}" 2>/dev/null || true
  }
  trap cleanup_swtpm EXIT

  SWTPM_READY=false
  for _ in $(seq 1 100); do
    if [[ -S "${SWTPM_SOCK}" ]]; then
      SWTPM_READY=true
      break
    fi
    if ! kill -0 "${SWTPM_PID}" 2>/dev/null; then
      echo "swtpm exited before creating its socket. See log: ${SWTPM_LOG}" >&2
      tail -50 "${SWTPM_LOG}" 2>/dev/null || true
      exit 1
    fi
    sleep 0.05
  done
  if [[ "${SWTPM_READY}" != "true" ]]; then
    echo "swtpm socket did not appear in time: ${SWTPM_SOCK}" >&2
    echo "See log: ${SWTPM_LOG}" >&2
    tail -50 "${SWTPM_LOG}" 2>/dev/null || true
    exit 1
  fi

  TPM_ARGS=(
    -machine q35
    -device piix4-ide
    -chardev "socket,id=chrtpm,path=${SWTPM_SOCK}"
    -tpmdev "emulator,id=tpm0,chardev=chrtpm"
    -device "tpm-crb,tpmdev=tpm0"
  )
fi

SHARED_ARGS=()
if [[ -d "${SHARED_DIR}" ]]; then
  SHARED_ARGS=(-drive "file=fat:rw:${SHARED_DIR},format=raw,if=ide,index=1")
fi

SYSTEM_ARGS=()
if [[ "${SYSTEM_VOL}" == true ]]; then
  if ! command -v qemu-img >/dev/null 2>&1; then
    echo "qemu-img not found (needed to create ${SYSTEM_DISK}). Install qemu-utils." >&2
    exit 1
  fi
  if [[ ! -f "${SYSTEM_DISK}" ]]; then
    # Default to 1G so FAT32 cluster counts stay comfortably in FAT32 territory.
    qemu-img create -f qcow2 "${SYSTEM_DISK}" 1G >/dev/null
    echo "Created system volume: ${SYSTEM_DISK} (1G)"
    echo "Note: format it as FAT in-guest before it can mount as /system."
  fi
  # Attach as primary master so IDE probe finds it for /system.
  SYSTEM_ARGS=(-drive "file=${SYSTEM_DISK},if=ide,index=0,media=disk,format=qcow2")
fi

INPUT_DEVICE=( -device usb-tablet,bus=xhci.0 )
if [[ "${USE_TABLET}" == false ]]; then
  INPUT_DEVICE=( -device usb-mouse,bus=xhci.0 -device usb-kbd,bus=xhci.0 )
fi

QEMU_ARGS=(
  "${SYSTEM_ARGS[@]}"
  -cdrom "${ISO_FILE}"
  -boot order=d
  -m 3G
  -vga vmware
  -netdev user,id=net0
  -device e1000,netdev=net0
  -device isa-debug-exit,iobase=0xf4,iosize=0x04
  -device qemu-xhci,id=xhci
  "${INPUT_DEVICE[@]}"
  -serial file:"${SERIAL_LOG}"
)

if [[ "${HEADLESS}" == true ]]; then
  QEMU_ARGS+=( -display none )
fi
if [[ "${FULLSCREEN}" == true ]]; then
  QEMU_ARGS+=( -full-screen )
fi

QEMU_BIN="qemu-system-x86_64"
if [[ -x "/usr/bin/qemu-system-x86_64" ]]; then
  QEMU_BIN="/usr/bin/qemu-system-x86_64"
elif [[ -x "/usr/local/bin/qemu-system-x86_64" ]]; then
  QEMU_BIN="/usr/local/bin/qemu-system-x86_64"
fi

if [[ "${HEADLESS}" == false && "${AUTO_GRAB}" == true ]]; then
  DISPLAY_HELP="$(${QEMU_BIN} -display help 2>/dev/null || true)"
  HELP_TEXT="$(${QEMU_BIN} -help 2>/dev/null || true)"

  if echo "${DISPLAY_HELP}" | grep -q "gtk"; then
    if echo "${HELP_TEXT}" | grep -i "^-display gtk" | grep -q "grab-on-hover"; then
      QEMU_ARGS+=( -display gtk,grab-on-hover=on )
    fi
  elif echo "${DISPLAY_HELP}" | grep -q "sdl"; then
    if echo "${HELP_TEXT}" | grep -i "^-display sdl" | grep -q "grab-on-hover"; then
      QEMU_ARGS+=( -display sdl,grab-on-hover=on )
    fi
  fi
fi

echo "${QEMU_BIN} ${QEMU_ARGS[*]} ${TPM_ARGS[*]} ${SHARED_ARGS[*]}"

if [[ "${RUN_FOR_SECONDS}" -gt 0 ]]; then
  if command -v timeout >/dev/null 2>&1; then
    timeout --preserve-status "${RUN_FOR_SECONDS}" "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${SHARED_ARGS[@]}" || true
  else
    "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${SHARED_ARGS[@]}"
  fi
else
  "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${SHARED_ARGS[@]}"
fi

echo "=== Serial output (last 30 lines) ==="
tail -30 "${SERIAL_LOG}" || true
