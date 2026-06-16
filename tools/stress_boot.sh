#!/usr/bin/env bash
# Citadel Secure Boot -- Stress Run Script
# Batch 3, Item 7
#
# Performs multiple consecutive cold-boot cycles to detect:
#   - Intermittent TPM PCR extension failures
#   - Anchor-path drift (TPM → software fallback)
#   - Non-deterministic signature refusal (flaky hardware)
#   - Serial log format changes between boots
#
# Usage:
#   ./tools/stress_boot.sh [--cycles N] [--serial /dev/ttyUSB0] [--log-dir build/stress-logs]
#
# Requirements:
#   - Serial console accessible at /dev/ttyUSB0 (or override with --serial)
#   - Reboot command accessible (sudo reboot)
#   - parse_boot_log.py available in tools/
#   - diff_boot_log.py available in tools/
#   - Golden log at logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt

set -euo pipefail

# ----------------------------- defaults -------------------------------------
CYCLES=10
SERIAL_DEV="/dev/ttyUSB0"
BAUD=115200
LOG_DIR="build/stress-logs"
GOLDEN="logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt"
REBOOT_DELAY=30          # seconds to wait after reboot before reading serial
SERIAL_CAPTURE_DURATION=60   # seconds to capture serial after reboot

# ----------------------------- arg parsing ----------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cycles)   CYCLES="$2";     shift 2 ;;
    --serial)   SERIAL_DEV="$2"; shift 2 ;;
    --log-dir)  LOG_DIR="$2";    shift 2 ;;
    --golden)   GOLDEN="$2";     shift 2 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

mkdir -p "$LOG_DIR"

PASS=0
FAIL=0
REGRESSION=0

echo "========================================================"
echo "Citadel Secure Boot Stress Run"
echo "Cycles:      $CYCLES"
echo "Serial:      $SERIAL_DEV @ $BAUD"
echo "Log dir:     $LOG_DIR"
echo "Golden log:  $GOLDEN"
echo "========================================================"
echo ""

for i in $(seq 1 "$CYCLES"); do
  CYCLE_LOG="$LOG_DIR/cycle-$(printf '%03d' $i)-$(date +%Y%m%d-%H%M%S).log"
  echo "[Cycle $i/$CYCLES] Starting..."

  # 1. Trigger reboot
  echo "  Rebooting..."
  sudo reboot &
  sleep 2

  # 2. Capture serial output for the boot sequence
  echo "  Capturing serial for ${SERIAL_CAPTURE_DURATION}s..."
  timeout "$SERIAL_CAPTURE_DURATION" \
    stty -F "$SERIAL_DEV" "$BAUD" raw -echo &&
    timeout "$SERIAL_CAPTURE_DURATION" \
      cat "$SERIAL_DEV" > "$CYCLE_LOG" 2>/dev/null || true

  echo "  Log: $CYCLE_LOG ($(wc -l < "$CYCLE_LOG") lines)"

  # 3. Parse log for required markers
  REPORT_JSON="$LOG_DIR/cycle-$(printf '%03d' $i)-report.json"
  if python3 tools/parse_boot_log.py \
       --log "$CYCLE_LOG" \
       --json-out "$REPORT_JSON" > /dev/null 2>&1; then
    MARKER_OK=true
  else
    MARKER_OK=false
  fi

  # 4. Diff against golden (regression check)
  DIFF_JSON="$LOG_DIR/cycle-$(printf '%03d' $i)-diff.json"
  if [[ -f "$GOLDEN" ]]; then
    if python3 tools/diff_boot_log.py \
         --golden "$GOLDEN" \
         --current "$CYCLE_LOG" \
         --json-out "$DIFF_JSON" > /dev/null 2>&1; then
      DIFF_OK=true
    else
      DIFF_OK=false
      REGRESSION=$((REGRESSION + 1))
    fi
  else
    DIFF_OK=true  # no golden yet; skip
  fi

  # 5. Check for SecureStore TPM fallback (anchor-path drift)
  if grep -qi "TPM unavailable\|software fallback\|recovery path" "$CYCLE_LOG"; then
    TPM_OK=false
  else
    TPM_OK=true
  fi

  # 6. Summarise cycle result
  if $MARKER_OK && $DIFF_OK && $TPM_OK; then
    echo "  [PASS] Cycle $i — all checks OK"
    PASS=$((PASS + 1))
  else
    echo "  [FAIL] Cycle $i:"
    $MARKER_OK || echo "         ✗ Required boot-log markers missing"
    $DIFF_OK   || echo "         ✗ Regression detected vs golden log"
    $TPM_OK    || echo "         ✗ TPM anchor-path drift detected"
    FAIL=$((FAIL + 1))
  fi

  # Wait between cycles (allow hardware to fully power off/on)
  if [[ $i -lt $CYCLES ]]; then
    echo "  Waiting ${REBOOT_DELAY}s before next cycle..."
    sleep "$REBOOT_DELAY"
  fi
done

# ----------------------------- summary --------------------------------------
echo ""
echo "========================================================"
echo "Stress Run Summary"
echo "Total cycles:  $CYCLES"
echo "Pass:          $PASS"
echo "Fail:          $FAIL"
echo "Regressions:   $REGRESSION"
echo "========================================================"

# Write summary JSON
SUMMARY="$LOG_DIR/stress-run-summary-$(date +%Y%m%d-%H%M%S).json"
cat > "$SUMMARY" <<EOF
{
  "cycles":      $CYCLES,
  "pass":        $PASS,
  "fail":        $FAIL,
  "regressions": $REGRESSION,
  "log_dir":     "$LOG_DIR",
  "golden":      "$GOLDEN",
  "timestamp":   "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
echo "Summary written → $SUMMARY"

[[ $FAIL -eq 0 ]] && exit 0 || exit 1
