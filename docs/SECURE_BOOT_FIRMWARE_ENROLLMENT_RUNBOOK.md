# Citadel Secure Boot Firmware Enrollment Runbook

**Status:** Operational runbook (Batch 1, Item 6)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Lab Operations  
**Audience:** Lab technicians, developers, QA engineers

---

## 1. Overview

This runbook documents the procedure for enrolling Secure Boot keys into lab hardware, updating keys, rolling back, and recovering from enrollment failures.

**Scope:** Lab hardware only (single developer or local test machine)  
**Trust Model:** Team-internal; ceremony-free  
**Key Holders:** Lab developer (keys stored on disk)  
**Recovery:** Air-gapped backup USB + manual BIOS recovery

---

## 2. Prerequisites

- Lab hardware with UEFI firmware (x86-64 BIOS)
- Physical access to lab machine (USB drive, monitor, keyboard)
- Lab Secure Boot keys available (stored on disk or backup USB)
- Limine bootloader built with Secure Boot support
- Serial console cable (optional but recommended for debugging)

---

## 3. Fresh Enrollment (First-Time Key Setup)

### 3.1 Preparation

1. **Power off lab hardware** and ensure no external USB drives attached
2. **Generate lab keys** (if not already generated):
   ```bash
   # On a trusted build machine
   cd /home/dmort/citadel
   tools/generate_keys.py --environment lab --output-dir /tmp/citadel-lab-keys
   # Output: /tmp/citadel-lab-keys/{PK, KEK, db}.{pub,priv}
   ```
3. **Back up keys to recovery USB** (air-gapped):
   ```bash
   # Format a clean USB drive
   sudo mkfs.vfat /dev/sdX1
   
   # Mount and copy keys
   sudo mount /dev/sdX1 /mnt/usb
   sudo cp /tmp/citadel-lab-keys/* /mnt/usb/
   sudo umount /mnt/usb
   
   # Label the USB: "CITADEL-LAB-KEYS-BACKUP"
   # Store in secure drawer (air-gapped, not connected to network)
   ```

### 3.2 UEFI Setup Mode Entry

1. **Power on lab hardware** with serial console connected
2. **Enter BIOS setup** during POST:
   - Press `DEL` or `F2` (varies by firmware)
   - Alternative: `F10` for ASUS/Lenovo, `F12` for Dell
3. **Navigate to Security → Secure Boot**:
   - Set **Secure Boot:** `Enabled`
   - Set **Secure Boot Mode:** `Setup Mode` (allows key enrollment without existing PK)
   - Confirm and save

4. **Exit BIOS** and allow boot to continue (boot will fail; this is expected if no Limine is signed yet)

### 3.3 Enroll Platform Key (PK)

1. **From Linux shell, connect to recovery USB**:
   ```bash
   sudo mount /dev/sdX1 /mnt/usb
   ```

2. **Enroll PK using UEFI tool** (efibootmgr or custom enrollment tool):
   ```bash
   # Using efibootmgr (if available on lab firmware)
   sudo efibootmgr --create-var --var PK --file /mnt/usb/PK.pub
   
   # Or use Limine's UEFI setup utility:
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --pk /mnt/usb/PK.pub
   ```

3. **Reboot and verify** PK is enrolled:
   ```bash
   sudo reboot
   # After boot, check BIOS: Security → Secure Boot
   # Verify "PK Enrolled: Yes"
   ```

### 3.4 Enroll Key Exchange Key (KEK)

1. **After PK is enrolled, boot into Linux**
2. **Mount recovery USB again**:
   ```bash
   sudo mount /dev/sdX1 /mnt/usb
   ```

3. **Enroll KEK**:
   ```bash
   sudo efibootmgr --create-var --var KEK --file /mnt/usb/KEK.pub
   
   # Or:
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --kek /mnt/usb/KEK.pub
   ```

4. **Verify**:
   ```bash
   sudo reboot
   # Check: Security → Secure Boot → KEK Enrolled: Yes
   ```

### 3.5 Enroll db (Allowed Signatures)

1. **Boot into Linux, mount USB**
2. **Enroll db**:
   ```bash
   sudo efibootmgr --create-var --var db --file /mnt/usb/db.pub
   
   # Or:
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --db /mnt/usb/db.pub
   ```

3. **Verify**:
   ```bash
   sudo reboot
   # Check: Security → Secure Boot → db Enrolled: Yes
   ```

### 3.6 Lock Secure Boot Setup Mode

1. **Boot into Linux and lock setup mode**:
   ```bash
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --lock
   # Or via BIOS: Security → Secure Boot → Setup Mode: Disabled
   ```

2. **Reboot and verify** Secure Boot is active:
   ```bash
   sudo reboot
   # Watch serial console for Secure Boot validation messages
   # Expected: "UEFI Secure Boot: ACTIVE"
   ```

3. **Boot to desktop** and verify signed Limine boots successfully

**Enrollment Complete!** ✓

---

## 4. Key Update (Scheduled Rotation)

### 4.1 Prepare New Keys

1. **Generate new key set** (new KEK and db):
   ```bash
   tools/generate_keys.py --environment lab --output-dir /tmp/citadel-lab-keys-new
   ```

2. **Back up old keys** (archive):
   ```bash
   mkdir -p /tmp/citadel-lab-keys-archive
   cp /tmp/citadel-lab-keys/* /tmp/citadel-lab-keys-archive/
   ```

### 4.2 Update KEK

1. **Boot into Linux, mount recovery USB with new keys**
2. **Update KEK in firmware**:
   ```bash
   sudo efibootmgr --update-var --var KEK --file /mnt/usb-new/KEK.pub
   
   # Or via BIOS Setup Mode (requires temporarily enabling Setup Mode with old PK)
   ```

3. **Verify** new KEK is in place:
   ```bash
   sudo reboot
   # Check BIOS fingerprint of KEK (should be different)
   ```

### 4.3 Update db

1. **Update db in firmware**:
   ```bash
   sudo efibootmgr --update-var --var db --file /mnt/usb-new/db.pub
   ```

2. **Sign new Limine and boot artifacts** with new db-signer key
3. **Verify** boot succeeds with new signatures

**Key Update Complete!** ✓

---

## 5. Rollback (Emergency Key Recovery)

### 5.1 Scenario: Incorrect Key Enrolled, Boot Fails

**Symptoms:**
- Machine boots to UEFI message: "Secure Boot Verification Failed"
- No Limine signature accepted
- Stuck in firmware

**Recovery Steps:**

1. **Power off machine**
2. **Re-enter BIOS Setup** with serial console attached
3. **Navigate to Security → Secure Boot**
4. **Set Secure Boot: Disabled** (or Setup Mode: Enabled)
5. **Save and exit**
6. **Boot into Linux** (unsigned boot works now)
7. **Diagnose issue**:
   ```bash
   # Check which key was enrolled
   sudo /boot/efi/EFI/Citadel/show_keys.efi
   # Compare against expected keys in /tmp/citadel-lab-keys/
   ```

8. **If keys are wrong**: Delete enrolled keys and re-enroll correct ones
   ```bash
   sudo efibootmgr --delete-var --var PK   # Delete PK to re-enter Setup Mode
   # Reboot and follow Section 3 (Fresh Enrollment) with correct keys
   ```

9. **Re-enable Secure Boot** once correct keys are enrolled

**Rollback Complete!** ✓

---

## 6. Recovery from Backup (Full System Loss)

### 6.1 Scenario: Firmware Reset or Key Corruption

**Symptoms:**
- All keys cleared (factory reset)
- BIOS shows "Secure Boot: Disabled"
- or BIOS shows "Setup Mode: Enabled"

**Recovery Steps:**

1. **Retrieve recovery USB** (labeled "CITADEL-LAB-KEYS-BACKUP")
2. **Connect recovery USB to lab hardware**
3. **Boot into Linux** (Secure Boot is disabled; unsigned boot allowed)
4. **Mount recovery USB**:
   ```bash
   sudo mount /dev/sdX1 /mnt/usb-backup
   ```

5. **Copy keys from backup to disk**:
   ```bash
   sudo cp /mnt/usb-backup/* /tmp/citadel-lab-keys/
   sudo chown -R $(whoami):$(whoami) /tmp/citadel-lab-keys/
   ```

6. **Follow Section 3 (Fresh Enrollment)** to re-enroll keys
7. **Verify boot succeeds** with Secure Boot enabled

**Recovery Complete!** ✓

---

## 7. Troubleshooting

| Symptom | Cause | Resolution |
|---------|-------|------------|
| "Secure Boot Verification Failed" | Limine not signed with enrolled db key | Re-sign Limine with correct key; redeploy |
| BIOS won't save key changes | Setup Mode not enabled | Enter Setup Mode first; try again |
| Recovery USB not recognized | USB not in UEFI-accessible format | Re-format USB as FAT32; copy keys again |
| Serial console shows "dbx mismatch" | Old (revoked) key signature detected | Update dbx with newest revocation list |
| Machine boots to BIOS menu | No bootable device found | Check that signed Limine is on /boot/efi; re-deploy |

---

## 8. Archival & Documentation

After successful enrollment, document:

1. **Key enrollment date and time** (when PK/KEK/db were enrolled)
2. **Key IDs and fingerprints** (capture from BIOS or `show_keys.efi`)
3. **Firmware version** (may affect Secure Boot behavior)
4. **Serial number** of lab hardware
5. **Recovery procedure executed?** (Yes/No)

**Archive location:** `/tmp/citadel-lab-enrollment-log.txt`

**Example log entry:**
```
========================================
CITADEL LAB SECURE BOOT ENROLLMENT LOG
========================================

Date: 2026-06-16
Time: 14:30:00 UTC
Hardware: Dell PowerEdge R750 (SN: ABC123)
Firmware: Dell BIOS v2.14.1

PK Enrollment:
  Key ID: CITADEL_PK_LAB_v1
  Fingerprint: sha256:abcd1234...
  Status: OK

KEK Enrollment:
  Key ID: CITADEL_KEK_LAB_v1
  Fingerprint: sha256:xyz9876...
  Status: OK

db Enrollment:
  Key ID: CITADEL_BOOT_LAB_v1
  Fingerprint: sha256:pqr5432...
  Status: OK

Secure Boot Status: ACTIVE
First Boot Test: PASSED

Recovery Procedure Executed: NO
Notes: First-time enrollment successful; keys backed up to USB "CITADEL-LAB-KEYS-BACKUP"

Enrolled By: dev@citadel.local
Verified By: dev@citadel.local
```

---

## 9. Quick Reference Checklist

- [ ] Keys generated and backed up to recovery USB
- [ ] BIOS entered; Setup Mode enabled
- [ ] PK enrolled and verified
- [ ] KEK enrolled and verified
- [ ] db enrolled and verified
- [ ] Setup Mode locked
- [ ] Secure Boot enabled
- [ ] Signed Limine boots successfully
- [ ] Serial console shows "Secure Boot: ACTIVE"
- [ ] Enrollment log archived
- [ ] Recovery USB stored safely (air-gapped)

---

## References

- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md)
- [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)
- UEFI Secure Boot Specification (external)
- Limine Bootloader Documentation (external)
