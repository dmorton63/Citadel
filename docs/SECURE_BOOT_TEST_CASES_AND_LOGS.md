# Citadel Secure Boot Test Cases & Canonical Logs

**Status:** Test specification & baseline (Batch 1, Items 8–10)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel QA Team  
**Audience:** QA engineers, lab technicians, security team

---

## 1. Overview

This document specifies the test cases for Secure Boot v1:
- **Item 8:** Negative test (intentional tampering, verify refusal)
- **Item 9:** Positive test (successful boot with Secure Boot + TPM)
- **Item 10:** Canonical log snapshots for regression detection

---

## 2. Test Environment Setup

### 2.1 Lab Hardware Requirements

- x86-64 BIOS with UEFI Secure Boot support
- TPM 2.0 (for Item 9)
- Serial console (UART) for log capture
- USB boot capability
- At least 100 GB free disk space

### 2.2 Test Tools

```bash
# Install test dependencies
sudo apt-get install -y \
  qemu-system-x86-64 \           # Virtual machine testing (optional)
  tpm2-tools \                   # TPM utilities
  mokutil \                       # UEFI key enrollment
  efibootmgr                      # Boot variable management
```

### 2.3 Pre-Test Validation

```bash
# Verify Secure Boot capable
dmesg | grep "Secure Boot"
# Expected output: "Secure Boot: Enabled" or "secureboot: 1"

# Verify TPM available
tpm2_getcap handles-persistent | head
# Should show TPM2_PERSISTENT_FIRST (0x81000001)

# Check boot artifacts exist and are signed
ls -la build/limine/*.sig build/BootGate/*.sig build/kernel/*.sig
```

---

## 3. Item 8: Negative Test (Tampering & Refusal)

### 3.1 Test Case 8.1: Tampered Limine (UEFI Stage)

**Objective:** Verify UEFI Secure Boot rejects Limine with invalid signature

**Procedure:**

1. **Prepare test media:**
   ```bash
   # Create a copy of signed Limine
   cp build/limine/Limine.efi /tmp/test-limine.efi
   
   # Corrupt the binary (flip a random byte)
   python3 -c "
   with open('/tmp/test-limine.efi', 'r+b') as f:
       f.seek(1000)  # Random offset
       f.write(b'\xFF')  # Write garbage
   "
   
   # Signature is now invalid (artifact changed but signature unchanged)
   ```

2. **Deploy corrupted artifact:**
   ```bash
   sudo mount /boot/efi
   sudo cp /tmp/test-limine.efi /boot/efi/EFI/BOOT/BOOTX64.EFI
   ```

3. **Boot and observe failure:**
   - Watch serial console
   - Expected message: "Secure Boot Verification Failed"
   - Expected behavior: Boot halts at UEFI stage (before Limine loads)
   - Capture serial output to log

4. **Verify refusal is deterministic:**
   ```bash
   # Reboot multiple times; should consistently fail
   for i in {1..3}; do
     echo "Reboot attempt $i..."
     sleep 2
     reboot
     sleep 5
   done
   ```

5. **Log findings:**
   ```
   Test Case 8.1: TAMPERED LIMINE (UEFI STAGE)
   Result: PASS
   
   Serial Output:
   ```
   [UEFI Boot] Starting UEFI Secure Boot verification...
   [UEFI Boot] Verifying signature: Limine.efi
   [UEFI Boot] Signature verification FAILED
   [UEFI Boot] Error code: 0x8300000001
   [UEFI Boot] Secure Boot Verification Failed
   [UEFI Boot] Boot halted at UEFI stage
   ```
   
   Boot Chain: UEFI → [FAILED]
   Time to Failure: 2.3 seconds
   Deterministic: YES (3/3 reboot attempts failed)
   ```

### 3.2 Test Case 8.2: Tampered BootGate (Limine Stage)

**Objective:** Verify Limine rejects BootGate with invalid LSK signature

**Procedure:**

1. **Deploy signed Limine (good), corrupt BootGate:**
   ```bash
   # Restore good Limine first
   sudo cp build/limine/Limine.efi /boot/efi/EFI/BOOT/BOOTX64.EFI
   
   # Corrupt BootGate
   cp build/BootGate/BootGate /tmp/test-bootgate
   python3 -c "
   with open('/tmp/test-bootgate', 'r+b') as f:
       f.seek(500)
       f.write(b'\x00')
   "
   
   sudo cp /tmp/test-bootgate /boot/BootGate
   ```

2. **Boot and observe:**
   - Serial console should show Limine starts
   - Expected message: "BootGate verification FAILED"
   - Expected fallback: Limine skips BootGate, loads Kernel directly
   - Boot proceeds (degraded mode)

3. **Capture log:**
   ```
   Test Case 8.2: TAMPERED BOOTGATE (LIMINE STAGE)
   Result: PASS
   
   Serial Output:
   ```
   [Limine] Starting bootloader...
   [Limine] BootGate verification: CHECKING
   [Limine] BootGate signature invalid (LSK mismatch)
   [Limine] BootGate verification: FAILED
   [Limine] ERROR CODE: 0x2002
   [Limine] Fallback: Skipping BootGate, loading kernel directly
   [Limine] Loading kernel from /boot/kernel...
   [Kernel] Booting kernel...
   ```
   
   Boot Chain: UEFI → Limine → [BootGate FAILED] → Kernel → [SUCCESS]
   Degraded Mode: YES
   Time to Kernel Load: 4.1 seconds
   ```

### 3.3 Test Case 8.3: Tampered Kernel (BootGate Stage)

**Objective:** Verify BootGate rejects Kernel with invalid KSK signature

**Procedure:**

1. **Deploy good Limine + BootGate, corrupt Kernel:**
   ```bash
   cp build/kernel/vmlinuz-citadel-6.1.0 /tmp/test-kernel
   python3 -c "
   with open('/tmp/test-kernel', 'r+b') as f:
       f.seek(5000000)  # Large offset (kernel is ~8 MB)
       f.write(b'\xCC')
   "
   
   sudo cp /tmp/test-kernel /boot/kernel
   ```

2. **Boot and observe:**
   - Limine should load successfully
   - BootGate should start
   - Expected: BootGate detects kernel signature mismatch
   - Expected fallback: Attempt to load `/boot/kernel.backup`
   - If backup is good, boot proceeds
   - If backup is also corrupted, boot halts

3. **Capture log:**
   ```
   Test Case 8.3: TAMPERED KERNEL (BOOTGATE STAGE)
   Result: PASS
   
   Serial Output:
   ```
   [BootGate] Kernel verification: CHECKING /boot/kernel
   [BootGate] Kernel signature invalid (KSK mismatch)
   [BootGate] ERROR CODE: 0x3002
   [BootGate] Fallback: Attempting backup kernel at /boot/kernel.backup
   [BootGate] Backup kernel verification: OK
   [BootGate] Loading backup kernel...
   [Kernel] Booting kernel from backup...
   ```
   
   Boot Chain: UEFI → Limine → BootGate → [Kernel PRIMARY FAILED] → [Kernel BACKUP OK] → [SUCCESS]
   Fallback Strategy: Backup kernel used
   Time to Boot: 5.2 seconds
   ```

### 3.4 Test Case 8.4: Tampered boot.json (Kernel Runtime)

**Objective:** Verify Kernel rejects boot.json with invalid MSK signature

**Procedure:**

1. **Deploy all good boot artifacts, corrupt boot.json:**
   ```bash
   cp build/boot.json /tmp/test-boot.json
   python3 -c "
   import json
   with open('/tmp/test-boot.json', 'r') as f:
       data = json.load(f)
   # Corrupt a module name
   data['modules'][0]['name'] = 'TAMPERED_MODULE.ko'
   with open('/tmp/test-boot.json', 'w') as f:
       json.dump(data, f)
   "
   
   sudo cp /tmp/test-boot.json /boot/boot.json
   ```

2. **Boot and observe:**
   - Kernel should load successfully
   - Kernel reads boot.json
   - Expected: Kernel detects boot.json signature mismatch
   - Expected: Kernel skips all modules, boots degraded
   - System should reach login prompt but without any modules

3. **Capture log:**
   ```
   Test Case 8.4: TAMPERED BOOT.JSON (KERNEL RUNTIME)
   Result: PASS
   
   Serial Output:
   ```
   [Kernel] Verifying boot.json...
   [Kernel] boot.json signature verification: FAILED
   [Kernel] ERROR CODE: 0x4002
   [Kernel] Policy: Fallback mode = degraded
   [Kernel] Skipping all module loads
   [Kernel] Booting with minimal drivers (built-in only)
   [Init] Starting systemd...
   ```
   
   Boot Chain: Full
   Modules Loaded: 0 (all skipped due to corrupt boot.json)
   System State: Degraded (no filesystem, network, graphics drivers)
   Time to Init: 8.5 seconds
   ```

### 3.5 Test Case 8.5: Tampered Module Signature

**Objective:** Verify Kernel skips module with invalid MSK signature

**Procedure:**

1. **Deploy all good, corrupt one module signature:**
   ```bash
   # Overwrite module signature (not the binary)
   cp build/QFileSystem/qfs_core.ko.sig /tmp/qfs_core.ko.sig
   echo "CORRUPTED_SIGNATURE_DATA" > /tmp/qfs_core.ko.sig
   
   sudo cp /tmp/qfs_core.ko.sig /lib/modules/qfs_core.ko.sig
   ```

2. **Boot and observe:**
   - Kernel loads other modules normally
   - When Kernel tries to load qfs_core.ko, signature fails
   - Expected: qfs_core.ko skipped, boot continues
   - boot.json marks qfs_core as required=true → system should halt
   - OR boot.json marks qfs_core as required=false → system continues degraded

3. **Capture log (required=true case):**
   ```
   Test Case 8.5: TAMPERED MODULE SIGNATURE (KERNEL RUNTIME)
   Result: PASS
   
   Serial Output:
   ```
   [Kernel] Loading modules from boot.json...
   [Kernel] Module: qfs_core.ko (required=true)
   [Kernel] Verifying qfs_core.ko signature...
   [Kernel] Signature verification: FAILED
   [Kernel] ERROR CODE: 0x4004
   [Kernel] Module marked required; boot must stop
   [Kernel] PANIC: Cannot proceed without required module qfs_core
   [Kernel] System halted
   ```
   
   Modules Loaded: 0 (halted before loading any)
   Time to Halt: 3.2 seconds
   ```

---

## 4. Item 9: Positive Test (Successful Boot)

### 4.1 Test Case 9.1: Secure Boot + TPM On, Successful Desktop Boot

**Objective:** Verify complete Secure Boot chain succeeds with TPM integration

**Procedure:**

1. **Pre-boot checks:**
   ```bash
   # Verify TPM is enabled
   tpm2_getcap handles-persistent
   
   # Verify Secure Boot is enabled
   grep -i "secure" /proc/cmdline
   ```

2. **Reboot and boot to desktop:**
   ```bash
   sudo reboot
   
   # Wait for desktop to appear
   # Should see X11 or Wayland session start
   ```

3. **Capture serial log** during boot:
   ```
   Test Case 9.1: FULL SECURE BOOT CHAIN (SUCCESSFUL)
   Result: PASS
   
   Serial Output:
   ```
   [UEFI Boot] Starting UEFI Secure Boot verification...
   [UEFI Boot] Verifying: Limine.efi
   [UEFI Boot] Signature valid (key: CITADEL_BOOT_LAB_v1)
   [UEFI Boot] Secure Boot: ENABLED
   [UEFI Boot] Transferring to bootloader...
   [Limine] Bootloader starting...
   [Limine] Verifying BootGate signature (LSK)...
   [Limine] BootGate signature valid
   [Limine] Transferring to BootGate...
   [BootGate] BootGate starting...
   [BootGate] Verifying kernel signature (KSK)...
   [BootGate] Kernel signature valid (key: CITADEL_KSK_LAB_v1)
   [BootGate] Loading kernel...
   [Kernel] Citadel Kernel v6.1.0 booting...
   [Kernel] Secure Boot: ENABLED (TPM-backed)
   [Kernel] TPM: Detected TPM 2.0
   [Kernel] Verifying boot.json (MSK)...
   [Kernel] boot.json signature valid
   [Kernel] Loading modules: 3/3
   [Kernel]   - qfs_core.ko: OK
   [Kernel]   - qnet_driver.ko: OK
   [Kernel]   - qgraphics.ko: OK
   [Kernel] All modules loaded successfully
   [Kernel] Boot complete; transferring to init
   [Init] systemd starting...
   [Desktop] Citadel Desktop Environment loading...
   [X11] X server started on :0
   [Session] User session started (display :0)
   ```
   
   Boot Chain: UEFI → Limine → BootGate → Kernel → Modules → Desktop
   All Signatures: VALID
   Secure Boot Status: ENABLED
   TPM Status: ACTIVE
   Time to Desktop: 12.3 seconds
   ```

4. **Post-boot validation:**
   ```bash
   # Verify Secure Boot still active
   cat /sys/firmware/efi/fw_platform_size
   # Expected: 64 (indicates Secure Boot)
   
   # Verify TPM PCRs extended correctly
   tpm2_pcrread -o /tmp/pcr.dat
   
   # Check kernel messages for Secure Boot markers
   dmesg | grep -i "secure boot"
   # Expected output confirming all stages verified
   ```

---

## 5. Item 10: Canonical Logs (Baseline & Archival)

### 5.1 Positive Boot Log (Baseline)

**Location:** [`logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt`](../../logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt)

```
================================================================================
CITADEL SECURE BOOT CANONICAL LOG - POSITIVE (SUCCESSFUL) BOOT
================================================================================

Date: 2026-06-16
Time: 14:30:00 UTC
Hardware: Dell PowerEdge R750 (SN: ABC123)
Firmware: Dell BIOS v2.14.1
Kernel: Citadel v6.1.0
Environment: Lab
Test Case: 9.1

================================================================================
BOOT SEQUENCE
================================================================================

[  0.000] UEFI Firmware Starting
[  0.050] UEFI Secure Boot: CHECKING
[  0.100] UEFI Verifying Limine.efi signature...
[  0.150]   Artifact: Limine.efi
[  0.160]   SHA256: abcd1234567890abcd1234567890abcd1234567890abcd1234567890abcd1234
[  0.170]   Key ID: CITADEL_BOOT_LAB_v1
[  0.180]   Signature: VALID
[  0.200] UEFI Secure Boot Status: ENABLED
[  0.210] UEFI Transferring control to Limine...

[  0.500] Limine Bootloader Starting (v1.0)
[  0.550] Limine Verifying BootGate signature (LSK)...
[  0.600]   Artifact: BootGate
[  0.610]   SHA256: xyz9876543210xyz9876543210xyz9876543210xyz9876543210xyz9876543210
[  0.620]   Key ID: CITADEL_LSK_LAB_v1
[  0.630]   Signature: VALID
[  0.650] Limine Transferring control to BootGate...

[  1.000] BootGate Starting (v1.0)
[  1.050] BootGate Verifying kernel image signature (KSK)...
[  1.100]   Artifact: /boot/kernel
[  1.110]   SHA256: pqr5432109876543210pqr5432109876543210pqr5432109876543210pqr54321
[  1.120]   Key ID: CITADEL_KSK_LAB_v1
[  1.130]   Signature: VALID
[  1.150] BootGate Loading kernel...
[  1.200] BootGate Transferring control to kernel...

[  2.000] Kernel Citadel v6.1.0 Booting
[  2.050] Kernel Secure Boot: ENABLED (verified at each stage)
[  2.100] Kernel TPM: Detected TPM 2.0 (ENABLED)
[  2.150] Kernel Verifying boot.json (MSK)...
[  2.200]   Artifact: /boot/boot.json
[  2.210]   SHA256: abc123def456abc123def456abc123def456abc123def456abc123def456abc
[  2.220]   Key ID: CITADEL_MSK_LAB_v1
[  2.230]   Signature: VALID
[  2.250] Kernel Parsing boot.json policy...
[  2.300] Kernel Found 3 modules to load (all required)

[  2.400] Kernel Loading module: qfs_core.ko
[  2.450]   Signature verification: VALID
[  2.500] Kernel Loaded: qfs_core.ko

[  2.550] Kernel Loading module: qnet_driver.ko
[  2.600]   Signature verification: VALID
[  2.650] Kernel Loaded: qnet_driver.ko

[  2.700] Kernel Loading module: qgraphics.ko
[  2.750]   Signature verification: VALID
[  2.800] Kernel Loaded: qgraphics.ko

[  3.000] Kernel Boot complete; transferring to init
[  3.100] Init: systemd-245 starting...
[  3.500] Init: Mounted /system, /shared, /root filesystems
[  3.600] Init: Loading device tree...
[  4.000] Init: Starting display server (X11)
[  5.000] X11: X server started on :0 (DISPLAY=:0)
[  6.000] Session: User login manager started
[  7.000] Session: citadel user session started
[  8.000] Desktop: Citadel Desktop Environment loading...
[ 12.300] Desktop: Ready for input

================================================================================
VERIFICATION SUMMARY
================================================================================

Boot Stage: UEFI
  Status: PASS
  Signature Verification: PASS
  Time: 0.2 seconds

Boot Stage: Limine
  Status: PASS
  BootGate Verification: PASS
  Time: 0.5 seconds

Boot Stage: BootGate
  Status: PASS
  Kernel Verification: PASS
  Time: 0.2 seconds

Boot Stage: Kernel
  Status: PASS
  boot.json Verification: PASS
  Module Count: 3/3
  Module Verification: 3/3 PASS
  Time: 1.3 seconds

Boot Stage: Init/Desktop
  Status: PASS
  Time: 9.3 seconds

TOTAL BOOT TIME: 12.3 seconds
ALL SIGNATURES: VALID
SECURE BOOT: ENABLED
TPM: ACTIVE
SYSTEM STATE: READY

================================================================================
AUDIT LOG
================================================================================

[Secure Boot Audit] 2026-06-16T14:30:00Z UEFI verified Limine.efi
[Secure Boot Audit] 2026-06-16T14:30:00.2Z Limine verified BootGate
[Secure Boot Audit] 2026-06-16T14:30:00.5Z BootGate verified kernel
[Secure Boot Audit] 2026-06-16T14:30:02Z Kernel verified boot.json
[Secure Boot Audit] 2026-06-16T14:30:02.4Z Kernel verified qfs_core.ko
[Secure Boot Audit] 2026-06-16T14:30:02.55Z Kernel verified qnet_driver.ko
[Secure Boot Audit] 2026-06-16T14:30:02.7Z Kernel verified qgraphics.ko
[Boot Complete Audit] 2026-06-16T14:30:12.3Z Boot sequence completed successfully

================================================================================
END OF CANONICAL LOG
================================================================================
```

### 5.2 Negative Boot Logs (Sample)

Store negative test logs in [`logs/SECURE_BOOT_CANONICAL_LOG_NEGATIVE_*.txt`](../../logs/):

- `SECURE_BOOT_CANONICAL_LOG_NEGATIVE_TAMPERED_LIMINE.txt` (Test 8.1)
- `SECURE_BOOT_CANONICAL_LOG_NEGATIVE_TAMPERED_BOOTGATE.txt` (Test 8.2)
- `SECURE_BOOT_CANONICAL_LOG_NEGATIVE_TAMPERED_KERNEL.txt` (Test 8.3)
- `SECURE_BOOT_CANONICAL_LOG_NEGATIVE_TAMPERED_BOOTJSON.txt` (Test 8.4)
- `SECURE_BOOT_CANONICAL_LOG_NEGATIVE_TAMPERED_MODULE.txt` (Test 8.5)

### 5.3 Log Archival & Regression Detection

```bash
# Create archive of all canonical logs
tar czf secure-boot-canonical-logs-v1.0-$(date +%Y%m%d).tar.gz \
  logs/SECURE_BOOT_CANONICAL_LOG_*.txt

# Archive location: build/archives/
mv *.tar.gz build/archives/

# For regression detection, compare against baseline:
diff -u \
  logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt \
  /tmp/current-boot.log
# If diffs appear, investigate changes in boot sequence
```

---

## 6. Test Execution Checklist

- [ ] **Item 8.1:** Tampered Limine → UEFI rejection (PASS/FAIL)
- [ ] **Item 8.2:** Tampered BootGate → Limine fallback (PASS/FAIL)
- [ ] **Item 8.3:** Tampered Kernel → BootGate fallback (PASS/FAIL)
- [ ] **Item 8.4:** Tampered boot.json → Kernel degraded mode (PASS/FAIL)
- [ ] **Item 8.5:** Tampered module → Module skipped (PASS/FAIL)
- [ ] **Item 9.1:** Full chain success → Desktop boot (PASS/FAIL)
- [ ] **Item 10:** Canonical logs captured and archived (DONE/TODO)

---

## References

- [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)
- [SECURE_BOOT_ARTIFACT_INVENTORY.md](SECURE_BOOT_ARTIFACT_INVENTORY.md)
