# Citadel Secure Boot — Batch 3 Operational Checklists

**Status:** Operational procedures (Batch 3, Items 8–10)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Operations / Security Team

---

## Item 8: Key-Custody Handoff Checklist (Lab → Staging)

### Overview

When promoting from lab to staging, signing responsibility transfers from an
individual developer to a controlled CI process backed by a YubiHSM2.  
This checklist ensures that transition is traceable and audited.

### Roles

| Role | Responsibility |
|------|---------------|
| **Engineer (Signer)** | Generates staging key set; signs staging artifacts |
| **Engineer (Verifier)** | Independently verifies all signatures before any artifact is deployed |
| **Engineering Lead** | Reviews evidence bundle; approves staging promotion |

### Pre-Handoff Checklist

- [ ] All Batch 2 promotion gate items checked (see `SECURE_BOOT_REFUSAL_TAXONOMY.md` §8)
- [ ] Lab key set archived (encrypted, stored on secure USB labeled with date)
- [ ] Lab key private keys confirmed NOT present in git history (`git log --all -S "BEGIN PRIVATE KEY"`)
- [ ] YubiHSM2 unit physically present, firmware updated, health check passed:
  ```bash
  yubihsm-connector --version
  yubihsm-shell -p password -C connect -c get-device-info
  ```
- [ ] Staging key backup USB formatted and ready for escrow storage

### Key Generation (Signer)

```bash
# Generate staging keys INTO YubiHSM2 (keys are non-exportable)
yubihsm-shell << 'EOF'
connect
session open 1 password
generate asymmetric 1 CITADEL_BOOT_STAGING_v1  rsa2048 sign-pkcs1v15-sha256
generate asymmetric 1 CITADEL_LSK_STAGING_v1   rsa2048 sign-pkcs1v15-sha256
generate asymmetric 1 CITADEL_KSK_STAGING_v1   rsa2048 sign-pkcs1v15-sha256
generate asymmetric 1 CITADEL_MSK_STAGING_v1   rsa2048 sign-pkcs1v15-sha256
session close 1
EOF

# Export public certificates (NOT private keys)
for KEY_ID in CITADEL_BOOT_STAGING_v1 CITADEL_LSK_STAGING_v1 \
              CITADEL_KSK_STAGING_v1 CITADEL_MSK_STAGING_v1; do
  yubihsm-shell -c get-public-key -i $KEY_ID -out /tmp/staging-certs/${KEY_ID}.pub
  # Convert to self-signed cert for OpenSSL verification
  openssl req -new -x509 -key /tmp/staging-certs/${KEY_ID}.pub \
    -out /tmp/staging-certs/${KEY_ID}.crt \
    -subj "/CN=Citadel-${KEY_ID}/O=CitadelOS"
done
```

### Staging Artifact Signing (Signer)

```bash
# Sign all artifacts with staging keys via YubiHSM2
python3 tools/sign_artifacts.py \
  --environment staging \
  --artifacts build/limine/Limine.efi build/BootGate/BootGate \
              build/kernel/vmlinuz build/boot.json \
  --hsm-slot 0 \
  --hsm-pin-env YHSM_PIN \
  --manifest-out build/secure-boot-manifest-staging.json \
  --signer "engineer@citadel.local"

# Tag identity
python3 tools/tag_artifact_identity.py \
  --manifest build/secure-boot-manifest-staging.json \
  --update-manifest
```

### Independent Verification (Verifier)

```bash
# Verifier fetches artifacts + manifest from shared location
# (does NOT use the same machine as signer)

python3 tools/verify_signatures.py \
  --environment staging \
  --key-dir /tmp/staging-certs \
  --manifest build/secure-boot-manifest-staging.json \
  --strict

python3 tools/check_provenance.py \
  --manifest build/secure-boot-manifest-staging.json \
  --build-dir build/ \
  --source-dir /home/dmort/citadel \
  --strict
```

### Handoff Sign-Off

- [ ] Signer confirms: staging keys generated in YubiHSM2 (non-exportable)
- [ ] Signer confirms: private keys not written to disk or git
- [ ] Verifier confirms: all staging signatures independently verified
- [ ] Verifier confirms: all provenance checks pass
- [ ] Backup USB created and placed in secure storage
- [ ] Manifest committed to repo (or uploaded to artifact server)
- [ ] Engineering lead approves staging promotion in tracking ticket

**Signer:** _________________  Date: ________  
**Verifier:** _______________  Date: ________  
**Engineering Lead:** ________  Date: ________

---

## Item 9: Secure Media-Handling Checklist

### Scope

All physical and digital media containing signed Secure Boot outputs:
signing keys, signed artifacts, manifests, enrollment bundles, and audit logs.

### Physical Media (USB Drives, DVDs, HSMs)

**On Creation:**
- [ ] Media labeled with: artifact name, environment, date, SHA-256 hash (first 12 chars)
- [ ] Media tested for readability immediately after writing
- [ ] Media encrypted (for keys/private material) or hash-verified (for artifacts)
- [ ] Media stored in locked drawer or fireproof safe (not on open desk)
- [ ] Chain-of-custody log entry created

**Before Use:**
- [ ] Verify media is the correct labeled item (not a substitute)
- [ ] Re-verify hash/checksum against stored value before loading
- [ ] Check media for signs of physical tampering (seal broken, case scratched)
- [ ] Log access: who accessed, when, why

**After Use:**
- [ ] Return media to secure storage immediately (do not leave unattended)
- [ ] Update chain-of-custody log entry (returned, condition OK/NOK)

**Revocation / Destruction:**
- [ ] If media is known or suspected compromised: immediately notify security lead
- [ ] Initiate emergency revocation (dbx update) if keys on media were active
- [ ] Destroy media: degauss + physical shredding; confirm with witness
- [ ] Log destruction in chain-of-custody: who destroyed, method, date, witness

### Digital Artifacts (Manifests, Logs, Signed Binaries)

**Storage Requirements:**
- [ ] Manifests stored in git repo under `releases/` (immutable after tagging)
- [ ] Signed binaries stored in artifact server with access logging
- [ ] Audit logs stored in append-only storage (no delete/overwrite)
- [ ] Canonical boot logs committed to `logs/` directory in git repo

**Integrity Checks:**
- [ ] All digital artifacts have SHA-256 recorded in manifest
- [ ] Manifests themselves have a recorded hash (manifest_id field)
- [ ] Re-verify hashes before use in any deployment pipeline
- [ ] CI pipeline fails if artifact hash does not match manifest

**Revocation Process (if signed artifact is compromised):**
1. Remove artifact from artifact server immediately
2. Publish dbx update blacklisting the signing key (24-hour SLA)
3. Notify downstream consumers via security advisory
4. Archive compromised artifact under `build/quarantine/` with incident tag
5. Do NOT delete compromised artifact (evidence preservation)

### Chain-of-Custody Log Template

```
Date:         _______________
Media:        _______________  (label/name/SHA-prefix)
Contents:     _______________  (e.g. "CITADEL_BOOT_PROD_v1 private key")
Action:       CREATED / ACCESSED / RETURNED / DESTROYED
Operator:     _______________  (name + employee ID)
Witness:      _______________  (name + employee ID, if required)
Reason:       _______________
Condition:    OK / NOK  (NOK → immediate incident report)
Notes:        _______________
```

---

## Item 10: Release-Readiness Evidence Bundle

### Purpose

A release-readiness evidence bundle is the complete set of artifacts that
security review needs to approve a Secure Boot release (lab → staging or
staging → production).

### Bundle Contents

For each release candidate, produce and archive the following:

| # | Artifact | Source | Notes |
|---|---------|--------|-------|
| 1 | `secure-boot-manifest-{env}.json` | `build/` | Signed artifact hashes + key IDs + identity |
| 2 | `secure-boot-manifest-{env}.json.sig` | `build/` | Manifest is itself signed |
| 3 | All `.sig` files for boot artifacts | `build/` | PKCS#7-DER detached sigs |
| 4 | All `.identity.json` sidecars | `build/` | Build ID + git SHA + manifest ID |
| 5 | `boot-log-report.json` | `build/` | parse_boot_log.py output (positive case) |
| 6 | `SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt` | `logs/` | Reference boot log baseline |
| 7 | Negative test logs (one per test case 8.1–8.5) | `logs/` | Serial logs showing refusal |
| 8 | `pcr-snapshot-{date}.txt` | `build/` | TPM PCR values after successful boot |
| 9 | `stress-run-summary-{date}.json` | `build/stress-logs/` | 10+ cycle stress run results |
| 10 | Recovery bundle receipt | Physical/email | Confirms bundle was created and stored |
| 11 | Rollback rehearsal log | `logs/` | Refusal + recovery serial logs |
| 12 | `diff-report.json` (vs previous release) | `build/` | No regressions vs prior golden log |

### Bundle Assembly Script

```bash
#!/usr/bin/env bash
# Assemble and hash the release-readiness evidence bundle
set -euo pipefail

VERSION="${1:-v1.0}"
ENV="${2:-lab}"
BUNDLE_DIR="build/release-evidence-${VERSION}-${ENV}"
mkdir -p "$BUNDLE_DIR"

# Manifests + signatures
cp build/secure-boot-manifest-${ENV}.json     "$BUNDLE_DIR/"
cp build/secure-boot-manifest-${ENV}.json.sig "$BUNDLE_DIR/" 2>/dev/null || echo "[WARN] No manifest sig yet"

# Artifact .sig and .identity.json sidecars
find build/ -name "*.sig" -o -name "*.identity.json" | while read f; do
  cp "$f" "$BUNDLE_DIR/"
done

# Boot logs
cp logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt "$BUNDLE_DIR/" 2>/dev/null || true
find logs/ -name "SECURE_BOOT_CANONICAL_LOG_NEGATIVE_*.txt" | \
  xargs -I{} cp {} "$BUNDLE_DIR/" 2>/dev/null || true

# Reports
cp build/boot-log-report.json      "$BUNDLE_DIR/" 2>/dev/null || true
cp build/pcr-snapshot-*.txt        "$BUNDLE_DIR/" 2>/dev/null || true
cp build/diff-report.json          "$BUNDLE_DIR/" 2>/dev/null || true

# Stress run
cp build/stress-logs/stress-run-summary-*.json "$BUNDLE_DIR/" 2>/dev/null || true

# Generate bundle manifest
find "$BUNDLE_DIR" -type f | sort | while read f; do
  sha256sum "$f"
done > "$BUNDLE_DIR/BUNDLE_HASHES.txt"

echo "Evidence bundle assembled: $BUNDLE_DIR"
echo "$(wc -l < "$BUNDLE_DIR/BUNDLE_HASHES.txt") files"

# Tar for archival
tar czf "${BUNDLE_DIR}.tar.gz" -C build "$(basename "$BUNDLE_DIR")"
echo "Archive: ${BUNDLE_DIR}.tar.gz"
```

### Security Review Checklist

Reviewer completes this before approving any promotion:

**Artifact Integrity:**
- [ ] All artifacts in bundle have valid signatures (run `verify_signatures.py --strict`)
- [ ] All artifact hashes match manifest entries
- [ ] Manifest itself is signed (`manifest.sig` valid)

**Test Evidence:**
- [ ] All P0 test matrix cases show PASS (see `SECURE_BOOT_TEST_MATRIX.md`)
- [ ] Negative tests show correct refusal codes (SB-1xxx/2xxx/3xxx as expected)
- [ ] TPM continuity checklist complete (all PCR values match golden)
- [ ] Stress run: 10+ cycles, zero failures, zero regressions

**Operational Readiness:**
- [ ] Recovery bundle created, encrypted, and confirmed accessible
- [ ] Enrollment runbook walkthrough-tested on target hardware
- [ ] Key-custody handoff checklist complete and signed
- [ ] Secure media-handling checklist walkthrough-completed

**No Regressions:**
- [ ] `diff-report.json` shows zero regressions vs previous golden log (or "first release")
- [ ] No new SB-xxxx codes in current logs vs baseline

**Sign-Off:**

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Engineering Lead | | | |
| Security Lead | | | |
| Operations Lead | | | |

---

## References

- [SECURE_BOOT_TEST_MATRIX.md](SECURE_BOOT_TEST_MATRIX.md)
- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md)
- [SECURE_BOOT_REFUSAL_TAXONOMY.md](SECURE_BOOT_REFUSAL_TAXONOMY.md)
- [SECURE_BOOT_RECOVERY_BUNDLE.md](SECURE_BOOT_RECOVERY_BUNDLE.md)
- [tools/tamper_pack.py](../../tools/tamper_pack.py)
- [tools/diff_boot_log.py](../../tools/diff_boot_log.py)
- [tools/stress_boot.sh](../../tools/stress_boot.sh)
