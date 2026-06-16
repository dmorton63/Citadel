# Citadel Secure Boot — Refusal Reason Taxonomy & Operator Messages

**Status:** Reference (Batch 2, Items 4–5)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Security Team  
**References:** [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)

---

## 1. Taxonomy Overview

Every Secure Boot refusal must identify:
1. **Which stage** refused (UEFI, Limine, BootGate, Kernel)
2. **Which artifact** failed
3. **Why it failed** (one of the six root causes below)
4. **What the operator must do** (precise recovery action)

---

## 2. Root Causes (Six Categories)

| Code Prefix | Category | Description |
|-------------|----------|-------------|
| `SB-1xxx` | **Unsigned** | Artifact has no signature file at all |
| `SB-2xxx` | **Invalid Signature** | Signature file present but cryptographically invalid |
| `SB-3xxx` | **Revoked Key** | Key that signed the artifact is in `dbx` (blacklisted) |
| `SB-4xxx` | **Wrong Signer** | Artifact is signed, but by a key not listed in `db` for this stage |
| `SB-5xxx` | **Corrupted Artifact** | Hash of artifact does not match the signed hash |
| `SB-6xxx` | **Policy / Config** | Signature is structurally valid but violates a boot policy rule |

---

## 3. Full Error Code Table

### UEFI Stage (0x1xxx / SB-1000–1999)

| Code | Hex | Message | Recovery |
|------|-----|---------|----------|
| `SB-1001` | `0x1001` | `Unsigned: Limine.efi has no signature` | Sign Limine with CITADEL_BOOT_*_v1; redeploy |
| `SB-2001` | `0x2001` | `Bad signature: Limine.efi signature is cryptographically invalid` | Re-sign Limine; verify key matches enrolled db |
| `SB-3001` | `0x3001` | `Revoked key: Limine.efi signer is in dbx` | Deploy new Limine signed with non-revoked key |
| `SB-4001` | `0x4001` | `Wrong signer: Limine.efi signer not in db` | Enroll correct public key in db via KEK update |
| `SB-5001` | `0x5001` | `Corrupted: Limine.efi hash mismatch after signature` | Redownload/rebuild Limine; re-sign |

### Limine Stage (0x2xxx / SB-2000–2999)

| Code | Hex | Message | Recovery |
|------|-----|---------|----------|
| `SB-1002` | `0x1002` | `Unsigned: BootGate has no signature` | Sign BootGate with LSK; redeploy to /boot/BootGate |
| `SB-2002` | `0x2002` | `Bad signature: BootGate signature invalid (LSK)` | Re-sign BootGate; verify LSK cert is embedded in Limine |
| `SB-3002` | `0x3002` | `Revoked key: BootGate LSK is revoked` | Rotate LSK; rebuild Limine with new LSK public key; re-sign BootGate |
| `SB-4002` | `0x4002` | `Wrong signer: BootGate not signed with expected LSK` | Ensure BootGate is signed with the LSK matching Limine's embedded cert |
| `SB-5002` | `0x5002` | `Corrupted: BootGate binary corrupt` | Rebuild BootGate; re-sign |
| `SB-6002` | `0x6002` | `Policy: BootGate signature format not PKCS#7-DER` | Re-sign using correct format (openssl cms -outform DER) |

### BootGate Stage (0x3xxx / SB-3000–3999)

| Code | Hex | Message | Recovery |
|------|-----|---------|----------|
| `SB-1003` | `0x1003` | `Unsigned: kernel has no signature` | Sign kernel with KSK; redeploy to /boot/kernel |
| `SB-2003` | `0x2003` | `Bad signature: kernel signature invalid (KSK)` | Re-sign kernel; verify KSK cert is embedded in BootGate |
| `SB-3003` | `0x3003` | `Revoked key: kernel KSK is revoked` | Rotate KSK; rebuild BootGate with new KSK public key; re-sign kernel |
| `SB-4003` | `0x4003` | `Wrong signer: kernel not signed with expected KSK` | Ensure kernel is signed with the KSK matching BootGate's embedded cert |
| `SB-5003` | `0x5003` | `Corrupted: kernel binary corrupt` | Rebuild kernel; re-sign |

### Kernel Stage — boot.json (0x4xxx / SB-4000–4499)

| Code | Hex | Message | Recovery |
|------|-----|---------|----------|
| `SB-1004` | `0x1004` | `Unsigned: boot.json has no signature` | Sign boot.json with MSK; redeploy |
| `SB-2004` | `0x2004` | `Bad signature: boot.json signature invalid (MSK)` | Re-sign boot.json |
| `SB-3004` | `0x3004` | `Revoked key: boot.json MSK is revoked` | Rotate MSK; re-sign boot.json |
| `SB-5004` | `0x5004` | `Corrupted: boot.json hash mismatch` | Rebuild and re-sign boot.json |
| `SB-6004` | `0x6004` | `Policy: boot.json schema version unsupported` | Update boot.json to supported schema version |

### Kernel Stage — Modules (0x4xxx / SB-4500–4999)

| Code | Hex | Message | Recovery |
|------|-----|---------|----------|
| `SB-1005` | `0x1005` | `Unsigned: module {name} has no signature` | Sign module with MSK; redeploy to /lib/modules/ |
| `SB-2005` | `0x2005` | `Bad signature: module {name} signature invalid` | Re-sign module with MSK |
| `SB-3005` | `0x3005` | `Revoked key: module {name} MSK is revoked` | Rotate MSK; re-sign all modules |
| `SB-4005` | `0x4005` | `Unauthorized: module {name} not listed in boot.json` | Add module to boot.json; re-sign boot.json |
| `SB-5005` | `0x5005` | `Corrupted: module {name} hash mismatch` | Rebuild module; re-sign |
| `SB-6005` | `0x6005` | `Policy: required module {name} missing from filesystem` | Redeploy module to /lib/modules/ |

### Ramdisk (0x4xxx / SB-4900–4999)

| Code | Hex | Message | Recovery |
|------|-----|---------|----------|
| `SB-1006` | `0x1006` | `Unsigned: ramdisk has no signature` | Sign ramdisk with MSK |
| `SB-2006` | `0x2006` | `Bad signature: ramdisk signature invalid` | Re-sign ramdisk |
| `SB-5006` | `0x5006` | `Corrupted: ramdisk hash mismatch` | Rebuild ramdisk; re-sign |

---

## 4. Standardised Operator-Visible Messages

### 4.1 Console/Serial Format

All Secure Boot diagnostics must use this exact format:

```
[SB][<STAGE>] <CODE>: <ONE-LINE-MESSAGE>
[SB][<STAGE>] Artifact: <path-or-name>
[SB][<STAGE>] Key ID:   <key-id-or-UNKNOWN>
[SB][<STAGE>] Action:   <what-the-system-will-do-next>
```

**Examples — Successful:**
```
[SB][UEFI] Limine.efi: signature VALID (key: CITADEL_BOOT_LAB_v1)
[SB][UEFI] Secure Boot: ENABLED — transferring to bootloader
[SB][Limine] BootGate: signature VALID (key: CITADEL_LSK_LAB_v1)
[SB][BootGate] kernel: signature VALID (key: CITADEL_KSK_LAB_v1)
[SB][Kernel] boot.json: signature VALID (key: CITADEL_MSK_LAB_v1)
[SB][Kernel] All 3 modules loaded and verified
```

**Examples — Failure:**
```
[SB][UEFI] SB-2001: Bad signature: Limine.efi signature is cryptographically invalid
[SB][UEFI] Artifact: /boot/efi/BOOT/BOOTX64.EFI
[SB][UEFI] Key ID:   CITADEL_BOOT_LAB_v1
[SB][UEFI] Action:   Boot HALTED (no fallback at UEFI stage)
```

```
[SB][Limine] SB-2002: Bad signature: BootGate signature invalid (LSK)
[SB][Limine] Artifact: /boot/BootGate
[SB][Limine] Key ID:   CITADEL_LSK_LAB_v1
[SB][Limine] Action:   Fallback — loading kernel directly (BootGate skipped)
```

```
[SB][Kernel] SB-4005: Unauthorized: module qfs_extra.ko not listed in boot.json
[SB][Kernel] Artifact: /lib/modules/qfs_extra.ko
[SB][Kernel] Key ID:   CITADEL_MSK_LAB_v1
[SB][Kernel] Action:   Module load REJECTED; continuing with remaining modules
```

### 4.2 Rules for Implementors

1. **Always emit Code + Message on the same line** — parsers rely on `SB-NNNN:` prefix.
2. **Always emit Artifact path** — even if it is `(unknown)`, not empty.
3. **Always emit Action** — operator must not have to guess what the system did.
4. **Stage tag must match** — use exactly `UEFI`, `Limine`, `BootGate`, `Kernel`.
5. **Do not suppress messages on success** — positive confirmation is required for `parse_boot_log.py` marker detection.

---

## 5. dbx Refusal Cases (Item 7)

### 5.1 dbx Test Procedure

```bash
# 1. Identify key fingerprint of currently-enrolled db signer
openssl x509 -in /tmp/citadel-lab-keys/CITADEL_BOOT_LAB_v1.crt \
  -noout -fingerprint -sha256

# 2. Create dbx update that blacklists this key
python3 tools/update_dbx.py \
  --add-revoked CITADEL_BOOT_LAB_v1.crt \
  --sign-with CITADEL_KEK_LAB_v1.pem \
  --output /tmp/citadel-lab-dbx.esl

# 3. Enroll dbx update in UEFI
sudo efibootmgr --update-var --var dbx --file /tmp/citadel-lab-dbx.esl

# 4. Attempt boot (should fail with SB-3001)
sudo reboot
```

### 5.2 Expected Refusal Output

```
[SB][UEFI] SB-3001: Revoked key: Limine.efi signer is in dbx
[SB][UEFI] Artifact: /boot/efi/BOOT/BOOTX64.EFI
[SB][UEFI] Key ID:   CITADEL_BOOT_LAB_v1  [REVOKED]
[SB][UEFI] Action:   Boot HALTED (key blacklisted by dbx)
```

### 5.3 Recovery After dbx Test

```bash
# 1. Generate new lab boot key
openssl req -x509 -newkey rsa:2048 -sha256 \
  -keyout /tmp/citadel-lab-keys/CITADEL_BOOT_LAB_v2.pem \
  -out    /tmp/citadel-lab-keys/CITADEL_BOOT_LAB_v2.crt \
  -days 1 -nodes -subj "/CN=CITADEL_BOOT_LAB_v2"

# 2. Update db to add new key
sudo efibootmgr --update-var --var db \
  --file /tmp/citadel-lab-keys/CITADEL_BOOT_LAB_v2.crt

# 3. Re-sign Limine with new key
python3 tools/sign_artifacts.py \
  --environment lab \
  --artifacts build/limine/Limine.efi \
  --key-dir /tmp/citadel-lab-keys \
  --manifest-out build/secure-boot-manifest-lab.json

# 4. Redeploy
sudo cp build/limine/Limine.efi /boot/efi/BOOT/BOOTX64.EFI
sudo cp build/limine/Limine.efi.sig /boot/efi/BOOT/BOOTX64.EFI.sig

# 5. Reboot — should succeed with new key
```

---

## 6. TPM Continuity Validation Checklist (Item 8)

### 6.1 Prerequisites

```bash
# Verify TPM 2.0 present and active
tpm2_getcap properties-fixed | grep TPMFamilyIndicator
# Expected: value: 0x322E3000 (TPM 2.0)

# Verify TPM is not in lockout
tpm2_getcap properties-variable | grep lockoutCounter
# Expected: lockoutCounter = 0

# Verify TPM auth paths are set
tpm2_getcap handles-persistent
```

### 6.2 Pre-Boot Checks

- [ ] TPM 2.0 detected by firmware (PCR[0] extended before Limine loads)
- [ ] No unexpected PCR values (baseline PCRs match golden snapshot)
- [ ] Lockout counter = 0 (no failed authorization attempts)
- [ ] TPM endorsement key (EK) matches expected certificate

### 6.3 In-Boot Checks (Kernel Side)

- [ ] PCR[4] extended with Limine bootloader hash (Stage 1 measure)
- [ ] PCR[8] extended with BootGate hash (Stage 2 measure)
- [ ] PCR[9] extended with kernel image hash (Stage 3 measure)
- [ ] PCR[11] extended with boot.json hash (policy measure)
- [ ] SecureStore anchor path reports `TPM-backed` (not recovery/software fallback)

```bash
# Verify TPM PCR values after boot
tpm2_pcrread sha256:0,4,8,9,11 | tee build/pcr-snapshot-$(date +%Y%m%d).txt

# Diff against golden snapshot
diff build/pcr-golden.txt build/pcr-snapshot-$(date +%Y%m%d).txt
# No output = PCRs match expected values
```

### 6.4 Verify No Unintended Recovery Fallback

```bash
# Check SecureStore is using TPM anchor, not recovery path
dmesg | grep -i "securestore"
# Expected:  "SecureStore: TPM anchor active, anchor path: TPM-2.0"
# Unexpected: "SecureStore: TPM unavailable, falling back to software"

# Check kernel Secure Boot status
cat /sys/firmware/efi/efivars/SecureBoot-*/  2>/dev/null | xxd
# Expected: last byte = 01 (Secure Boot ON)
```

### 6.5 Post-Boot TPM Verification

- [ ] All PCR values extended as expected
- [ ] No "TPM error" lines in dmesg
- [ ] SecureStore reports `TPM-backed` (not `software-fallback`)
- [ ] No unintended SecureBoot disable in `/sys/firmware/efi/efivars/`
- [ ] Baseline PCR snapshot saved to `build/pcr-snapshot-{date}.txt`

---

## 7. Performance Timing Baseline (Item 9)

### 7.1 Measurement Method

```bash
# Capture timestamps from serial log
# Boot with serial console; timestamps are prepended by firmware/bootloader

python3 - <<'EOF'
import re

log = open("build/serial-boot.log").read()

# Extract timing markers
markers = {
    "uefi_start":          r"\[  0\.(\d+)\] UEFI Firmware Starting",
    "limine_start":        r"\[  (\d+\.\d+)\] Limine.*Starting",
    "bootgate_start":      r"\[  (\d+\.\d+)\] BootGate.*Starting",
    "kernel_start":        r"\[  (\d+\.\d+)\] Kernel.*Booting",
    "modules_loaded":      r"\[  (\d+\.\d+)\] Kernel.*All modules loaded",
    "boot_complete":       r"\[  (\d+\.\d+)\] Kernel.*Boot complete",
    "desktop_ready":       r"\[  (\d+\.\d+)\] Desktop.*Ready",
}

print("=== Secure Boot Timing Breakdown ===")
for label, pattern in markers.items():
    m = re.search(pattern, log)
    if m:
        print(f"  {label:30} {m.group(1):>8}s")
    else:
        print(f"  {label:30}  (not found)")
EOF
```

### 7.2 Baseline Targets

| Segment | Secure Boot OFF | Secure Boot ON | Overhead Budget |
|---------|----------------|---------------|-----------------|
| UEFI → Limine | 0.1s | 0.2s | +0.1s |
| Limine → BootGate | 0.1s | 0.5s | +0.4s |
| BootGate → Kernel | 0.1s | 0.2s | +0.1s |
| Kernel → Modules | 0.5s | 1.3s | +0.8s |
| Modules → Desktop | 8.0s | 10.1s | +2.1s |
| **Total** | **9.0s** | **12.3s** | **+3.3s** |

**Acceptance Criteria:**
- Secure Boot overhead ≤ 5 seconds over non-SB baseline
- No individual stage adds > 2 seconds of overhead
- Any regression > 10% of baseline triggers investigation

### 7.3 Regression Check Script

```bash
# Compare two timing reports
python3 tools/parse_boot_log.py \
  --log build/serial-boot-current.log \
  --json-out build/timing-current.json

python3 tools/compare_boot_timing.py \
  --baseline build/timing-baseline.json \
  --current  build/timing-current.json \
  --threshold 0.10    # 10% regression tolerance
```

---

## 8. Lab → Staging Promotion Criteria (Item 10)

### 8.1 Gate: All Items Must Pass

**Documentation Gate:**
- [x] Chain design document complete and reviewed
- [x] Artifact inventory complete and reviewed
- [x] Key hierarchy and rotation policy documented
- [x] Enrollment runbook documented and walkthrough-tested
- [x] Recovery bundle created and rehearsal drill completed

**Code Gate:**
- [x] `sign_artifacts.py` deterministic signing verified (two identical runs produce identical manifests, excluding timestamps)
- [x] `verify_signatures.py` rejects corrupted artifacts and missing signatures
- [x] CI job (`secure-boot.yml`) passes on main branch

**Test Gate:**
- [ ] Negative Test 8.1 (tampered Limine) → PASS in lab
- [ ] Negative Test 8.2 (tampered BootGate) → PASS in lab
- [ ] Negative Test 8.3 (tampered Kernel) → PASS in lab
- [ ] Negative Test 8.4 (tampered boot.json) → PASS in lab
- [ ] Negative Test 8.5 (tampered module) → PASS in lab
- [ ] Positive Test 9.1 (full chain + TPM + desktop) → PASS in lab
- [ ] dbx/revocation test → PASS in lab
- [ ] TPM continuity checklist (Section 6) → all items checked
- [ ] Canonical log captured and archived

**Evidence Bundle:**
- [ ] `build/secure-boot-manifest-lab.json` attached to release tag
- [ ] `build/pcr-golden.txt` (TPM PCR snapshot) committed to repo
- [ ] Recovery bundle created, encrypted, and stored
- [ ] Signed artifact set archived in `build/archives/`

**Sign-Off:**
- [ ] Engineering lead: _________________  Date: ________
- [ ] Security lead: _________________    Date: ________

### 8.2 Rollback Rehearsal (Required Before Staging Promotion)

**Procedure:**
1. Deliberately corrupt one boot artifact
2. Verify refusal occurs at correct stage
3. Restore from recovery bundle
4. Verify clean boot

**Evidence Required:**
- Serial log showing refusal at correct stage
- Serial log showing clean boot after recovery
- Timing of recovery (target: < 30 minutes from failure detection)

---

## References

- [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)
- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md)
- [SECURE_BOOT_TEST_CASES_AND_LOGS.md](SECURE_BOOT_TEST_CASES_AND_LOGS.md)
- [tools/sign_artifacts.py](../../tools/sign_artifacts.py)
- [tools/verify_signatures.py](../../tools/verify_signatures.py)
- [tools/parse_boot_log.py](../../tools/parse_boot_log.py)
