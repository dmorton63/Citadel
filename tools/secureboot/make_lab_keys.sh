#!/usr/bin/env bash
# Generate Citadel lab Secure Boot keys (PK/KEK/db) and an OVMF variable store
# with them enrolled and Secure Boot enabled. For QEMU testing only: the keys
# live under build/secureboot/keys and must never be used for real hardware.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${PROJECT_DIR}/build/secureboot"
KEY_DIR="${OUT_DIR}/keys"
OVMF_DIR="${OVMF_DIR:-/usr/share/OVMF}"
VARS_TEMPLATE="${OVMF_DIR}/OVMF_VARS_4M.fd"
VARS_OUT="${OUT_DIR}/OVMF_VARS_citadel.fd"
# Fixed owner GUID so regenerated stores stay comparable.
OWNER_GUID="c17ade1a-5ec0-4b00-8e7a-000000000001"
FORCE=false

usage() {
  cat <<EOF
Usage: $0 [--force]

Creates ${KEY_DIR}/{PK,KEK,db}.{key,crt} and ${VARS_OUT}.
Existing keys are reused unless --force is given.
Requires: openssl, virt-fw-vars (python3-virt-firmware), OVMF.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) FORCE=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

for tool in openssl virt-fw-vars; do
  command -v "${tool}" >/dev/null || { echo "Missing ${tool}" >&2; exit 1; }
done
[[ -f "${VARS_TEMPLATE}" ]] || { echo "Missing ${VARS_TEMPLATE} (sudo apt install ovmf)" >&2; exit 1; }

mkdir -p "${KEY_DIR}"
chmod 700 "${KEY_DIR}"

for name in PK KEK db; do
  if [[ "${FORCE}" == true || ! -f "${KEY_DIR}/${name}.key" || ! -f "${KEY_DIR}/${name}.crt" ]]; then
    openssl req -new -x509 -newkey rsa:2048 -nodes -sha256 -days 3650 \
      -subj "/CN=Citadel Lab ${name}/" \
      -keyout "${KEY_DIR}/${name}.key" -out "${KEY_DIR}/${name}.crt" 2>/dev/null
    chmod 600 "${KEY_DIR}/${name}.key"
    echo "Generated ${name} key"
  fi
done

virt-fw-vars --input "${VARS_TEMPLATE}" --output "${VARS_OUT}" \
  --set-pk "${OWNER_GUID}" "${KEY_DIR}/PK.crt" \
  --add-kek "${OWNER_GUID}" "${KEY_DIR}/KEK.crt" \
  --add-db "${OWNER_GUID}" "${KEY_DIR}/db.crt" \
  --secure-boot >"${OUT_DIR}/virt-fw-vars.log" 2>&1 || {
    cat "${OUT_DIR}/virt-fw-vars.log" >&2
    exit 1
  }

echo "Enrolled lab keys: ${VARS_OUT}"
