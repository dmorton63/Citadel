#!/usr/bin/env bash
set -euo pipefail

# Convert scdumpownercred hex dump into a binary ramdisk seed file.
#
# Usage:
#   tools/seed_ownercred_from_hex.sh < hex.txt
#   tools/seed_ownercred_from_hex.sh shared/citadel.txt
#
# Input may include the BEGIN/END markers and other lines; we only use hex lines.

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${PROJECT_DIR}/ramdisk/system/sc"
OUT_FILE="${OUT_DIR}/OWNERCRD"

mkdir -p "${OUT_DIR}"

tmp_hex="$(mktemp)"
trap 'rm -f "${tmp_hex}"' EXIT

if [[ $# -gt 0 ]]; then
  cat "$1" >"${tmp_hex}"
else
  cat >"${tmp_hex}"
fi

# Extract hex payload strictly between markers, then concatenate hex-only lines.
# This avoids accidentally capturing unrelated hex-looking fields elsewhere.
hex_payload="$(
  tr -d '\r' <"${tmp_hex}" \
    | awk '
        /^-----BEGIN CITADEL OWNERCRD HEX-----$/ { inside=1; next }
        /^-----END CITADEL OWNERCRD HEX-----$/   { inside=0; next }
        inside { print }
      ' \
    | grep -E '^[0-9a-fA-F]+$' \
    | tr -d ' \t\n'
)"

if [[ -z "${hex_payload}" ]]; then
  echo "No hex payload found in input." >&2
  exit 1
fi

# Validate even length.
if (( ${#hex_payload} % 2 != 0 )); then
  echo "Hex payload length is odd (${#hex_payload})." >&2
  exit 1
fi

# Write binary.
# xxd is commonly available; fall back to python if needed.
if command -v xxd >/dev/null 2>&1; then
  echo -n "${hex_payload}" | xxd -r -p >"${OUT_FILE}"
else
  python3 - <<'PY' "${OUT_FILE}" "${hex_payload}"
import sys, binascii
out_path = sys.argv[1]
hex_payload = sys.argv[2]
with open(out_path, 'wb') as f:
    f.write(binascii.unhexlify(hex_payload))
PY
fi

size=$(wc -c <"${OUT_FILE}" | tr -d ' ')
echo "Wrote ${OUT_FILE} (${size} bytes)"
