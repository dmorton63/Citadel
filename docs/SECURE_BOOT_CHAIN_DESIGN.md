# Citadel Secure Boot Chain Design

**Status:** Design specification (Batch 1, Item 1)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Security Team  
**References:** [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md), [SECURE_BOOT_ARTIFACT_INVENTORY.md](SECURE_BOOT_ARTIFACT_INVENTORY.md)

---

## 1. Overview

This document specifies the boot chain verification flow for Citadel Secure Boot, defining:
- How each stage verifies and hands off to the next stage
- Which keys are used at each verification point
- Fallback and recovery procedures on verification failure
- Error codes and logging at each stage

**Boot Chain Order:**
```
UEFI Firmware
    ↓ (verifies using PK/KEK/db)
Limine Bootloader (Limine.efi)
    ↓ (verifies using LSK)
BootGate (optional boot manager/loader)
    ↓ (verifies using KSK)
Kernel Image
    ↓ (verifies at runtime using MSK)
boot.json (policy) + Modules + Ramdisk
```

---

## 2. Stage 1: UEFI Firmware → Limine Bootloader

### 2.1 Verification Flow

**Artifact:** `Limine.efi` (signed with Limine Boot Key per environment)

**Verification Method:**
- UEFI Secure Boot firmware consults PK/KEK/db (UEFI Standard)
- `Limine.efi` must have a valid signature from a key listed in `db` (allowed-signatures database)
- KEK is used to update db; PK controls KEK updates

**Key Used:**
- Environment-specific Limine Boot Key:
  - Lab: `CITADEL_BOOT_LAB_v1`
  - Staging: `CITADEL_BOOT_STAGING_v1`
  - Production: `CITADEL_BOOT_PROD_v1`

**Success Case:**
- UEFI accepts `Limine.efi` signature and transfers control to Limine bootloader
- Limine begins execution (stage 2)

**Failure Case (Signature Invalid or Missing):**
1. UEFI Secure Boot rejects `Limine.efi`
2. Firmware displays error: "Secure Boot Verification Failed" or manufacturer-specific message
3. Boot halts; no fallback to unsigned bootloader
4. **Recovery:** Manual BIOS intervention required:
   - Enter BIOS setup mode
   - Update db with correct signing key (requires KEK private key)
   - Alternatively: Disable Secure Boot (if policy allows) and boot unsigned media (degraded mode)
   - Alternatively: Enroll new PK/KEK/db (requires platform-specific mechanism, e.g., physical presence + button press)

**Failure Case (dbx Blacklist Match):**
- If `Limine.efi` is signed with a key in `dbx` (revoked-signatures database), UEFI rejects it
- Behavior same as above; customer must update firmware with new KEK/db
- **27.1 dbx Override Procedure:**
  - Security team publishes signed dbx update (signed with KEK)
  - Customer deploys via fwupd or manual BIOS firmware update
  - Firmware on next boot applies new dbx and re-evaluates Limine signature

---

## 3. Stage 2: Limine Bootloader → BootGate

### 3.1 Verification Flow

**Artifact:** `BootGate` (second-stage loader; optional but recommended for modularity)

**Verification Method:**
- Limine reads BootGate binary from disk or embedded payload
- Limine verifies BootGate signature using **Limine Signing Key (LSK)** public key
- Signature format: PKCS#7 or ed25519 detached signature (TBD: finalize format)
- LSK public key is embedded in Limine binary at build time or loaded from a trusted configuration

**Key Used:**
- Environment-specific LSK:
  - Lab: `CITADEL_LSK_LAB_v1`
  - Staging: `CITADEL_LSK_STAGING_v1`
  - Production: `CITADEL_LSK_PROD_v1`

**Error Codes:**
- `0x2001`: BootGate not found (if BootGate is mandatory)
- `0x2002`: BootGate signature invalid
- `0x2003`: BootGate signature verification failed (key mismatch)
- `0x2004`: BootGate corrupted (hash mismatch)

**Success Case:**
- Limine verifies BootGate signature successfully
- Limine transfers control to BootGate
- BootGate begins execution (stage 3)

**Failure Case (Signature Invalid or Missing):**
1. Limine detects signature verification failure
2. Log error code to serial/TPM (if available)
3. **Fallback Option A (if BootGate is optional):**
   - Limine skips BootGate and directly loads Kernel
   - Proceed to Stage 3 (Limine → Kernel), bypassing BootGate verification
   - Logged as "BootGate skipped; direct kernel load"
4. **Fallback Option B (if BootGate is mandatory):**
   - Halt boot with error message to UART/display
   - Boot fails; operator must investigate

**Recovery Procedure:**
- Limine prompts user (if interactive console available):
  - "Press SPACE to skip BootGate and boot kernel directly (degraded mode)"
  - "Press ESC to halt and investigate"
- If unattended boot:
  - Behavior configurable in `boot.json` (once kernel loads):
    - `fail_hard`: halt on BootGate verification failure
    - `skip_bootgate`: bypass BootGate, load kernel directly
  - Default: `skip_bootgate` (permissive mode for production deployment)

---

## 4. Stage 3: BootGate → Kernel Image

### 4.1 Verification Flow

**Artifact:** `kernel` (kernel image)

**Verification Method:**
- BootGate reads kernel image from disk
- BootGate verifies kernel signature using **Kernel Signing Key (KSK)** public key
- Signature format: PKCS#7 or ed25519 detached signature (TBD: finalize format)
- KSK public key is embedded in BootGate binary or loaded from BootGate configuration

**Key Used:**
- Environment-specific KSK:
  - Lab: `CITADEL_KSK_LAB_v1`
  - Staging: `CITADEL_KSK_STAGING_v1`
  - Production: `CITADEL_KSK_PROD_v1`

**Error Codes:**
- `0x3001`: Kernel image not found
- `0x3002`: Kernel signature invalid
- `0x3003`: Kernel signature verification failed (key mismatch)
- `0x3004`: Kernel corrupted (hash mismatch)

**Success Case:**
- BootGate verifies kernel signature successfully
- BootGate decompresses/loads kernel into memory
- BootGate transfers control to kernel entry point (stage 4)

**Failure Case (Signature Invalid or Missing):**
1. BootGate detects signature verification failure
2. Log error code and kernel hash to TPM (if available)
3. Attempt fallback kernel load:
   - Look for alternative/backup kernel image (if available on disk)
   - If backup kernel found, verify its signature with KSK
   - If backup kernel valid, load and boot from backup
   - Log: "Booted from backup kernel after primary verification failure"
4. If no backup kernel:
   - Halt boot with error message
   - Display: "Kernel signature verification failed; no fallback available"
   - Boot stops; operator must investigate

**Recovery Procedure:**
- **Immediate (on-device):**
  - No operator interaction during boot verification (BootGate runs early)
  - BootGate logs detailed error to TPM + UART + disk (if mounted)
  - Boot halts; operator must re-image device or restore kernel from backup
- **Remote (fleet-wide):**
  - Monitoring system detects boot failures (via logs or telemetry)
  - Team publishes corrected kernel with valid KSK signature
  - Customer pulls OTA update and reboots
  - New kernel passes verification and boots normally

---

## 5. Stage 4: Kernel → Runtime Components (boot.json, Modules, Ramdisk)

### 5.1 Verification Flow

**Artifacts:**
- `boot.json` (boot-time policy and configuration)
- `*.ko` files (kernel modules)
- `ramdisk` or `initramfs` (optional; required for future stages)

**Verification Method:**
- Kernel performs verification **at runtime** (after kernel is running)
- Kernel reads boot.json from disk and verifies signature using **Module Signing Key (MSK)**
- boot.json defines which modules are allowed to load
- Kernel verifies each module signature with MSK before loading
- Ramdisk (if present) verified with MSK before use

**Key Used:**
- Environment-specific MSK:
  - Lab: `CITADEL_MSK_LAB_v1`
  - Staging: `CITADEL_MSK_STAGING_v1`
  - Production: `CITADEL_MSK_PROD_v1`

**Error Codes:**
- `0x4001`: boot.json not found
- `0x4002`: boot.json signature invalid
- `0x4003`: boot.json corrupted (hash mismatch)
- `0x4004`: Module X signature invalid
- `0x4005`: Module X not listed in boot.json (unauthorized)
- `0x4006`: Ramdisk signature invalid

**Success Case:**
- Kernel verifies boot.json signature
- Kernel loads and executes modules in order defined by boot.json
- All modules pass signature verification
- Ramdisk (if needed) passes verification
- Boot proceeds to userland (init, systemd, etc.)

**Failure Case (boot.json Signature Invalid):**
1. Kernel detects signature verification failure
2. **Action:** Refuse to load any modules
3. **Fallback:**
   - Kernel boots with minimal drivers (built-in drivers only)
   - Logs error: "boot.json signature verification failed; modules disabled"
   - System runs degraded but functional
   - Operator investigates and updates boot.json + re-signs with MSK

**Failure Case (Module Signature Invalid):**
1. Kernel detects signature verification failure for module X
2. **Action:** Refuse to load module X
3. **Fallback:**
   - Kernel continues loading remaining modules in boot.json
   - System logs: "Module X signature verification failed; skipping"
   - If module is marked as required in boot.json: panic or halt
   - If module is marked as optional: continue boot
4. **Recovery:**
   - Kernel boots to degraded state (missing functionality)
   - Administrator investigates and re-signs module with MSK

**Failure Case (Ramdisk Signature Invalid):**
1. Kernel detects signature verification failure for ramdisk
2. **Action:** Refuse to mount ramdisk
3. **Fallback:**
   - Kernel continues boot from other rootfs (if available)
   - Or: Boot halts with error message
   - Depends on boot.json configuration (`ramdisk_required: true/false`)
4. **Recovery:**
   - Re-sign ramdisk with MSK and redeploy

### 5.2 boot.json Schema (Example)

```json
{
  "schema_version": "1.0",
  "kernel_name": "citadel-kernel-6.1.0",
  "kernel_hash": "sha256:abcd...",
  "ramdisk_required": false,
  "modules": [
    {
      "name": "qfs_core.ko",
      "path": "/lib/modules/qfs_core.ko",
      "hash": "sha256:xyz...",
      "required": true,
      "signature": "..."
    },
    {
      "name": "qnet_driver.ko",
      "path": "/lib/modules/qnet_driver.ko",
      "hash": "sha256:...",
      "required": false,
      "signature": "..."
    }
  ],
  "policy": {
    "verify_modules": true,
    "verify_ramdisk": true,
    "enforce_signatures": true,
    "fallback_mode": "degraded"
  }
}
```

---

## 6. End-to-End Boot Sequence

### 6.1 Normal Boot (All Stages Succeed)

```
1. Power on → UEFI firmware starts
2. UEFI Secure Boot verifies Limine.efi (against db)
   ✓ Signature valid → Load Limine
3. Limine starts, verifies BootGate (against LSK)
   ✓ Signature valid → Load BootGate
4. BootGate starts, verifies kernel (against KSK)
   ✓ Signature valid → Load kernel
5. Kernel boots, verifies boot.json (against MSK)
   ✓ Signature valid → Parse policy
6. Kernel verifies + loads modules (against MSK)
   ✓ All modules valid → Load all required modules
7. Kernel ready, transfers to userland
   ✓ Boot complete
```

### 6.2 Degraded Boot (BootGate Fails)

```
1–2. UEFI → Limine (succeed)
3. Limine verifies BootGate
   ✗ Signature invalid → Skip BootGate (fallback)
4. Limine directly loads kernel (no BootGate)
   ✓ Kernel loads
5–7. Kernel boot proceeds (normal)
   ✓ Boot complete (degraded: BootGate skipped)
```

### 6.3 Degraded Boot (Module Verification Fails)

```
1–4. UEFI → Limine → BootGate → Kernel (succeed)
5. Kernel verifies boot.json
   ✓ Signature valid
6. Kernel loads modules
   ✗ Module X signature invalid → Skip module X (logged)
   ✓ Other modules loaded
7. Kernel ready, transfers to userland
   ✓ Boot complete (degraded: Module X missing)
```

### 6.4 Boot Failure (Kernel Verification Fails)

```
1–3. UEFI → Limine → BootGate (succeed)
4. BootGate verifies kernel
   ✗ Signature invalid → Check for backup kernel
   ✓ Backup kernel found + valid → Load backup
   ✓ Kernel boots from backup
5–7. Kernel boot proceeds
   ✓ Boot complete (from backup kernel)
   
   OR (no backup available)
   
   ✗ No backup kernel → Halt boot
   ✗ Boot failed; operator intervention needed
```

---

## 7. Error Reporting & Logging

### 7.1 Where Errors Are Logged

| Stage | Log Location | Persistence |
|-------|--------------|-------------|
| UEFI → Limine | UEFI firmware logs (NVRAM or TPM) | Persistent |
| Limine → BootGate | Limine serial output + TPM | Persistent (TPM) |
| BootGate → Kernel | BootGate serial output + TPM | Persistent (TPM) |
| Kernel → Modules | Kernel ring buffer (`dmesg`) + `/system/.sc/audit/` | Persistent (disk after mount) |

### 7.2 Log Format

**Error Entry (Example):**
```
[BOOT] 2026-06-16T14:23:45Z Stage=Limine→BootGate ErrorCode=0x2002 
       Artifact=BootGate Hash=sha256:abc123... KeyID=CITADEL_LSK_PROD_v1 
       Status=SignatureInvalid Recovery=Fallback
```

### 7.3 Customer Visibility

- **Lab:** Errors logged to UART; visible on serial console
- **Staging:** Errors logged to UART + forwarded to monitoring system (Prometheus/Grafana)
- **Production:** Errors sent to secure syslog + forwarded to Citadel telemetry service (encrypted TLS)

---

## 8. Fallback & Recovery Strategy

### 8.1 Fallback Priority (Production Mode)

| Failure Point | Fallback 1 | Fallback 2 | Fallback 3 |
|---------------|-----------|-----------|-----------|
| UEFI → Limine | None (halt) | Manual BIOS update | Disable SB (degraded) |
| Limine → BootGate | Skip BootGate, load Kernel | None | N/A |
| BootGate → Kernel | Backup Kernel (if available) | Halt | N/A |
| Kernel → Modules | Skip module (log error) | Boot degraded | N/A |

### 8.2 Backup Kernel Strategy

**For Production:**
- Maintain two kernel images on-disk: `kernel` (primary) and `kernel.backup`
- Both signed with KSK
- BootGate attempts to boot primary kernel first
- If primary kernel verification fails, try backup kernel
- Log which kernel booted for analysis

**Implementation:**
- Boot partition layout:
  ```
  /boot/efi/EFI/BOOT/BOOTX64.EFI  (Limine)
  /boot/BootGate                  (Second-stage loader)
  /boot/kernel                    (Primary kernel)
  /boot/kernel.backup             (Backup kernel)
  /boot/boot.json                 (Policy)
  ```

### 8.3 Recovery Procedure (Operator)

**If boot halts with verification failure:**

1. **Check serial logs / TPM logs** for error code
2. **Identify failure point** (UEFI, Limine, BootGate, Kernel, Modules)
3. **Take action:**
   - **UEFI → Limine failure:** Update firmware (new KEK/db via BIOS)
   - **Limine → BootGate failure:** Re-sign BootGate with LSK; redeploy
   - **BootGate → Kernel failure:** Re-sign kernel with KSK; system attempts backup kernel automatically
   - **Kernel → Module failure:** Re-sign module with MSK; redeploy OTA update
4. **Redeploy artifact** (firmware update capsule, or OTA for kernel/modules)
5. **Reboot and verify** new signature accepted

---

## 9. dbx Emergency Revocation Integration

**When a key is compromised:**

1. Security team generates new KEK and updates dbx with compromised key ID
2. dbx is signed with new KEK and deployed via firmware update capsule
3. On next boot, UEFI firmware checks dbx against Limine signature
4. If Limine is signed with compromised key, UEFI rejects it (fails at stage 1)
5. Customer must deploy new Limine image (signed with new key) via firmware update
6. New Limine boots successfully with updated dbx and KEK

**Timeline:** dbx update must be deployed within 24 hours of compromise (per Secure Boot Key Hierarchy policy)

---

## 10. Testing & Validation

### 10.1 Test Cases (Batch 2–4)

- [ ] **Test 1:** Normal boot (all signatures valid) → verify full chain executes
- [ ] **Test 2:** Invalid UEFI signature → verify UEFI rejects Limine
- [ ] **Test 3:** Invalid LSK signature → verify Limine skips BootGate, loads Kernel directly
- [ ] **Test 4:** Invalid KSK signature → verify BootGate attempts backup kernel
- [ ] **Test 5:** Invalid MSK signature (boot.json) → verify Kernel boots degraded (modules disabled)
- [ ] **Test 6:** Invalid MSK signature (module) → verify Kernel skips module, continues boot
- [ ] **Test 7:** dbx blacklist hit → verify UEFI rejects old Limine, accepts new Limine
- [ ] **Test 8:** Backup kernel fallback → verify BootGate switches to backup on primary failure
- [ ] **Test 9:** Emergency recovery → verify air-gapped signing machine can re-sign artifacts
- [ ] **Test 10:** Monitoring alerts → verify boot failure events trigger incident ticket

### 10.2 Validation Criteria

- Boot chain completes in < 5 seconds (UEFI to kernel ready)
- All error codes logged to TPM + UART
- Fallback mechanisms trigger correctly on verification failure
- No hang or infinite loops on signature mismatch
- Recovery procedures documented and tested

---

## 11. Future Enhancements

- **Measured Boot (TPM PCR):** Extend PCRs with artifact hashes for attestation
- **Runtime Module Loading:** Dynamic module loading with on-demand signature verification
- **Rollback Protection:** Use TPM or RPMB to prevent downgrades to old (vulnerable) artifacts
- **Secure Configuration:** Encrypt boot.json to prevent tampering between signature verification and use
- **Hardware Security Tokens:** Support for USB security keys (Titan, YubiKey) for mobile/remote signing

---

## References

- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md) — Key management policy
- [SECURE_BOOT_ARTIFACT_INVENTORY.md](SECURE_BOOT_ARTIFACT_INVENTORY.md) — Build artifacts and signing steps
- [CITADEL_CURRENT_STATE.md](../CITADEL_CURRENT_STATE.md) — Project status
- UEFI Secure Boot Specification (external)
- EDK2 (UEFI firmware reference implementation)
