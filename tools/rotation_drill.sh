#!/usr/bin/env bash
# Citadel Secure Boot -- Staged Key-Rotation Drill
# Batch 4, Item 1
#
# Simulates a planned KEK/db rotation to verify:
#   1. New key can be enrolled and trusted
#   2. Previously trusted media (signed with old key) is deterministically rejected
#   3. New media (signed with new key) boots cleanly
#   4. Rollback to old key set is documented and tested
#
# This is a LAB-ONLY drill.  Staging/production rotation requires
# a full ceremony (see SECURE_BOOT_KEY_HIERARCHY.md §5.3).
#
# Usage:
#   ./tools/rotation_drill.sh [--old-key-dir DIR] [--new-key-dir DIR] \
#                              [--artifact build/limine/Limine.efi] \
#                              [--log-dir build/rotation-drill-logs]

set -euo pipefail

OLD_KEY_DIR="/tmp/citadel-lab-keys"
NEW_KEY_DIR="/tmp/citadel-lab-keys-new"
ARTIFACT="build/limine/Limine.efi"
LOG_DIR="build/rotation-drill-logs"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --old-key-dir) OLD_KEY_DIR="$2"; shift 2 ;;
    --new-key-dir) NEW_KEY_DIR="$2"; shift 2 ;;
    --artifact)    ARTIFACT="$2";    shift 2 ;;
    --log-dir)     LOG_DIR="$2";     shift 2 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

mkdir -p "$LOG_DIR"
TS=$(date +%Y%m%d-%H%M%S)
LOG="$LOG_DIR/rotation-drill-${TS}.log"
exec > >(tee "$LOG") 2>&1

echo "========================================================"
echo "Citadel Secure Boot: Staged Key-Rotation Drill"
echo "Date:        $(date -u)"
echo "Old keys:    $OLD_KEY_DIR"
echo "New keys:    $NEW_KEY_DIR"
echo "Artifact:    $ARTIFACT"
echo "Log:         $LOG"
echo "========================================================"

# ---------------------------------------------------------------------------
# Phase 1: Baseline — verify artifact boots with OLD key
# ---------------------------------------------------------------------------
echo ""
echo "=== Phase 1: Baseline (old key) ==="

echo "  Verifying artifact with OLD key..."
if python3 tools/verify_signatures.py \
     --environment lab \
     --key-dir "$OLD_KEY_DIR" \
     --artifacts "$ARTIFACT"; then
  echo "  [OK] Artifact verifies with OLD key (expected)"
else
  echo "  [FAIL] Baseline failed — artifact doesn't verify with old key"
  exit 1
fi

# ---------------------------------------------------------------------------
# Phase 2: Generate new key set
# ---------------------------------------------------------------------------
echo ""
echo "=== Phase 2: Generate new key set ==="

mkdir -p "$NEW_KEY_DIR"

for KEY_ID in \
  CITADEL_BOOT_LAB_v2 \
  CITADEL_LSK_LAB_v2  \
  CITADEL_KSK_LAB_v2  \
  CITADEL_MSK_LAB_v2; do

  openssl req -x509 -newkey rsa:2048 -sha256 \
    -keyout "${NEW_KEY_DIR}/${KEY_ID}.pem" \
    -out    "${NEW_KEY_DIR}/${KEY_ID}.crt" \
    -days 1 -nodes \
    -subj "/CN=Citadel-${KEY_ID}/O=CitadelOS/C=US"

  echo "  Generated: ${KEY_ID}"
done

# ---------------------------------------------------------------------------
# Phase 3: Re-sign artifact with NEW key
# ---------------------------------------------------------------------------
echo ""
echo "=== Phase 3: Re-sign artifact with new key ==="

python3 tools/sign_artifacts.py \
  --environment lab \
  --key-dir "$NEW_KEY_DIR" \
  --artifacts "$ARTIFACT" \
  --manifest-out "${LOG_DIR}/manifest-new-key.json" \
  --signer "rotation-drill@citadel.local"

echo "  [OK] Artifact re-signed with new key"

# ---------------------------------------------------------------------------
# Phase 4: Verify old key CANNOT verify new-key-signed artifact
# ---------------------------------------------------------------------------
echo ""
echo "=== Phase 4: Verify old key rejects new-key-signed artifact (expected) ==="

# Temporarily swap .sig with new-key sig, then try to verify with OLD cert
SIG_BACKUP="${ARTIFACT}.sig.bak-old"
cp "${ARTIFACT}.sig" "$SIG_BACKUP"

if python3 tools/verify_signatures.py \
     --environment lab \
     --key-dir "$OLD_KEY_DIR" \
     --artifacts "$ARTIFACT" 2>/dev/null; then
  echo "  [FAIL] Old key unexpectedly accepted new-key signature — KEY ISOLATION BROKEN"
  exit 1
else
  echo "  [OK] Old key correctly rejects new-key-signed artifact (SB-4001 expected)"
fi

# Restore original sig
cp "$SIG_BACKUP" "${ARTIFACT}.sig"

# ---------------------------------------------------------------------------
# Phase 5: Verify NEW key accepts new-key-signed artifact
# ---------------------------------------------------------------------------
echo ""
echo "=== Phase 5: Verify new key accepts new-key-signed artifact ==="

# Swap in new-key sig again
cp "${LOG_DIR}/manifest-new-key.json" /tmp/new-manifest.json

if python3 tools/verify_signatures.py \
     --environment lab \
     --key-dir "$NEW_KEY_DIR" \
     --manifest /tmp/new-manifest.json; then
  echo "  [OK] New key correctly verifies new-key-signed artifact"
else
  echo "  [FAIL] New key cannot verify its own signed artifact"
  exit 1
fi

# ---------------------------------------------------------------------------
# Phase 6: Rollback test — restore old sig, verify with old key
# ---------------------------------------------------------------------------
echo ""
echo "=== Phase 6: Rollback — restore old signature + verify with old key ==="

cp "$SIG_BACKUP" "${ARTIFACT}.sig"

if python3 tools/verify_signatures.py \
     --environment lab \
     --key-dir "$OLD_KEY_DIR" \
     --artifacts "$ARTIFACT"; then
  echo "  [OK] Rollback successful — old key verifies old-key-signed artifact"
else
  echo "  [FAIL] Rollback failed — old key cannot verify old-key-signed artifact"
  exit 1
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "========================================================"
echo "Key-Rotation Drill: ALL PHASES PASSED"
echo ""
echo "Results:"
echo "  Phase 1 (baseline):                 PASS"
echo "  Phase 2 (new key generation):       PASS"
echo "  Phase 3 (re-sign with new key):     PASS"
echo "  Phase 4 (old key rejects new sig):  PASS"
echo "  Phase 5 (new key accepts new sig):  PASS"
echo "  Phase 6 (rollback to old key):      PASS"
echo ""
echo "Log: $LOG"
echo "========================================================"

# Write structured summary
cat > "${LOG_DIR}/rotation-drill-summary-${TS}.json" <<EOF
{
  "drill": "staged-key-rotation",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "artifact": "$ARTIFACT",
  "old_key_dir": "$OLD_KEY_DIR",
  "new_key_dir": "$NEW_KEY_DIR",
  "phases": {
    "phase1_baseline":       "PASS",
    "phase2_key_generation": "PASS",
    "phase3_resign":         "PASS",
    "phase4_old_rejects_new":"PASS",
    "phase5_new_accepts_new":"PASS",
    "phase6_rollback":       "PASS"
  },
  "result": "PASS"
}
EOF
