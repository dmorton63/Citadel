# Citadel Secure Boot Artifact Inventory

**Status:** Build specification (Batch 1, Item 2)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Build System Team  
**References:** [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md), [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)

---

## 1. Overview

This document inventories all Citadel boot artifacts that must be signed for Secure Boot compliance. For each artifact, it specifies:
- **Build step** where the artifact is created
- **Signing key** required (LSK/KSK/MSK)
- **Signature format** (detached signature file location)
- **Deployment location** (where the artifact is installed for booting)
- **Environment override** (lab/staging/production key used)

---

## 2. Artifact Summary Table

| Artifact | Stage | Build Step | Signing Key | Signature File | Deployment Location |
|----------|-------|-----------|-------------|-----------------|----------------------|
| **Limine.efi** | UEFI → Limine | CMake `limine/` target | Limine Boot Key | `Limine.efi.sig` | `/boot/efi/BOOT/BOOTX64.EFI` |
| **BootGate** | Limine → BootGate | CMake `BootGate/` target | LSK | `BootGate.sig` | `/boot/BootGate` |
| **Kernel** | BootGate → Kernel | CMake `kernel/` target | KSK | `kernel.sig` | `/boot/kernel` |
| **Kernel (Backup)** | BootGate → Kernel | CMake `kernel/` target | KSK | `kernel.backup.sig` | `/boot/kernel.backup` |
| **boot.json** | Kernel → Modules | Manual JSON creation | MSK | `boot.json.sig` | `/boot/boot.json` |
| **QFS Core Module** | Kernel → Modules | CMake `QFileSystem/` target | MSK | `qfs_core.ko.sig` | `/lib/modules/qfs_core.ko` |
| **QNet Driver** | Kernel → Modules | CMake `QNetwork/` target | MSK | `qnet_driver.ko.sig` | `/lib/modules/qnet_driver.ko` |
| **QGraphics Driver** | Kernel → Modules | CMake `QGraphics/` target | MSK | `qgraphics.ko.sig` | `/lib/modules/qgraphics.ko` |
| **Ramdisk (initramfs)** | Kernel → Modules | Build script `build.sh` | MSK | `ramdisk.sig` | `/boot/ramdisk` (optional) |

---

## 3. Detailed Artifact Specifications

### 3.1 Limine.efi (UEFI Bootloader)

**Purpose:** First-stage bootloader; verified by UEFI Secure Boot firmware

**Build Location:**
- Source: [`limine/`](../../limine/)
- Output: `build/limine/Limine.efi` (or `build/cmake-prod/limine/`)

**Build Command:**
```bash
cd /home/dmort/citadel
cmake --build build/ --target limine
# Output: build/limine/Limine.efi
```

**Signing Process:**
- Artifact: `Limine.efi` (binary, ~512 KB)
- Signing key: Environment-specific Limine Boot Key
  - Lab: `CITADEL_BOOT_LAB_v1`
  - Staging: `CITADEL_BOOT_STAGING_v1`
  - Production: `CITADEL_BOOT_PROD_v1`
- Signature format: PKCS#7 (or es25519 detached signature)
- Signature file: `Limine.efi.sig` (stored alongside binary)
- Verification: UEFI firmware uses PK/KEK/db to verify signature

**Deployment:**
- Copy `Limine.efi` + `Limine.efi.sig` to boot partition
- Final location: `/boot/efi/EFI/BOOT/BOOTX64.EFI` (or UEFI-standard location)

**Rollout:**
- Lab: Every build (disposable)
- Staging: Per-release (tested on staging hardware)
- Production: Per-release (after security review and dual-control approval)

---

### 3.2 BootGate (Second-Stage Loader)

**Purpose:** Optional second-stage boot manager; verifies kernel, allows module selection at boot time

**Build Location:**
- Source: [`BootGate/`](../../BootGate/) (if exists; TBD: confirm structure)
- Output: `build/BootGate/BootGate` (binary, ~200 KB)

**Build Command:**
```bash
cd /home/dmort/citadel
cmake --build build/ --target BootGate
# Output: build/BootGate/BootGate
```

**Signing Process:**
- Artifact: `BootGate` (binary, ~200 KB)
- Signing key: Environment-specific LSK (Limine Signing Key)
  - Lab: `CITADEL_LSK_LAB_v1`
  - Staging: `CITADEL_LSK_STAGING_v1`
  - Production: `CITADEL_LSK_PROD_v1`
- Signature format: PKCS#7 (or ed25519 detached signature)
- Signature file: `BootGate.sig`
- Verification: Limine verifies signature using LSK public key (embedded in Limine binary)

**Deployment:**
- Copy `BootGate` + `BootGate.sig` to boot partition
- Final location: `/boot/BootGate`

**Fallback:** If BootGate signature invalid, Limine skips BootGate and loads kernel directly (see chain design)

**Rollout:**
- Lab: Every build
- Staging: Per-release
- Production: Per-release (after validation)

---

### 3.3 Kernel Image

**Purpose:** Core OS kernel; verified by BootGate at boot time

**Build Location:**
- Source: [`kernel/`](../../kernel/) + [`QKernel/`](../../QKernel/)
- Output: `build/kernel/vmlinuz-citadel-6.1.0` (or similar, ~8 MB)

**Build Command:**
```bash
cd /home/dmort/citadel
cmake --build build/ --target kernel
# Output: build/kernel/vmlinuz-citadel-6.1.0
```

**Signing Process:**
- Artifact: `vmlinuz-citadel-6.1.0` (kernel binary, ~8 MB compressed)
- Signing key: Environment-specific KSK (Kernel Signing Key)
  - Lab: `CITADEL_KSK_LAB_v1`
  - Staging: `CITADEL_KSK_STAGING_v1`
  - Production: `CITADEL_KSK_PROD_v1`
- Signature format: PKCS#7 (or ed25519 detached signature)
- Signature file: `vmlinuz-citadel-6.1.0.sig`
- Verification: BootGate verifies signature using KSK public key (embedded in BootGate binary)

**Deployment:**
- Primary: `/boot/kernel`
- Backup: `/boot/kernel.backup` (copy of same binary, same signature)

**Version Tracking:**
- Kernel version: `citadel-6.1.0` (example)
- Update: Version bumped on major kernel changes
- Signature valid across all boot attempts (no expiration)

**Rollout:**
- Lab: Every build
- Staging: Per-release
- Production: Per-release (after validation on staging hardware)

---

### 3.4 Kernel Backup Image

**Purpose:** Fallback kernel if primary kernel signature fails verification

**Build Location:**
- Same as primary kernel

**Signing Process:**
- Artifact: `vmlinuz-citadel-6.1.0` (same as primary)
- Signature file: `vmlinuz-citadel-6.1.0.sig` (same as primary)
- Stored as: `/boot/kernel.backup`

**Strategy:**
- Backup kernel is **identical** to primary kernel (same binary, same signature)
- BootGate attempts to boot primary; if signature verification fails, tries backup
- Backup kernel location allows re-imaging one kernel without affecting the other

**Future Enhancement:**
- Store older (stable) kernel version as backup for rollback capability
- Would require maintaining two kernel binaries and two signature files

---

### 3.5 boot.json (Boot Policy)

**Purpose:** Defines kernel modules to load, module order, policy, and runtime verification settings

**Build Location:**
- Source: Manually authored or generated from template
- Location: [`boot.json`](../../boot.json) (root of repo)
- Output: `build/boot.json` (processed version, ready to sign)

**Build Command:**
```bash
cd /home/dmort/citadel
cmake --build build/ --target boot-json
# Output: build/boot.json
# Alternatively: copy boot.json to build/ directory
```

**Content Example:**
```json
{
  "schema_version": "1.0",
  "kernel_name": "citadel-kernel-6.1.0",
  "kernel_hash": "sha256:abcd1234...",
  "ramdisk_required": false,
  "modules": [
    {
      "name": "qfs_core.ko",
      "path": "/lib/modules/qfs_core.ko",
      "hash": "sha256:...",
      "required": true
    },
    {
      "name": "qnet_driver.ko",
      "path": "/lib/modules/qnet_driver.ko",
      "hash": "sha256:...",
      "required": false
    },
    {
      "name": "qgraphics.ko",
      "path": "/lib/modules/qgraphics.ko",
      "hash": "sha256:...",
      "required": false
    }
  ],
  "policy": {
    "verify_modules": true,
    "enforce_signatures": true,
    "fallback_mode": "degraded"
  }
}
```

**Signing Process:**
- Artifact: `boot.json` (JSON file, ~2 KB)
- Signing key: Environment-specific MSK (Module Signing Key)
  - Lab: `CITADEL_MSK_LAB_v1`
  - Staging: `CITADEL_MSK_STAGING_v1`
  - Production: `CITADEL_MSK_PROD_v1`
- Signature format: PKCS#7 (or ed25519 detached signature)
- Signature file: `boot.json.sig`
- Verification: Kernel verifies signature using MSK public key (compiled into kernel)

**Deployment:**
- Copy `boot.json` + `boot.json.sig` to boot partition
- Final location: `/boot/boot.json`

**Update Procedure:**
- When boot.json changes (new module list, policy change, etc.):
  1. Update `boot.json` in repo
  2. Re-build: `cmake --build build/ --target boot-json`
  3. Sign with MSK: `./tools/sign_artifact.py boot.json MSK_PROD`
  4. Deploy via OTA: `./tools/deploy_ota.py /boot/boot.json`
  5. Reboot to activate

**Rollout:**
- Lab: Every build
- Staging: Per-release (tested)
- Production: On-demand (when module changes needed) or periodic (annual hygiene)

---

### 3.6 Kernel Modules (QFS, QNet, QGraphics, etc.)

**Purpose:** Loadable kernel modules; verified by kernel at runtime using MSK

**Build Locations:**
- QFS Core: [`QFileSystem/`](../../QFileSystem/)
- QNet Driver: [`QNetwork/`](../../QNetwork/)
- QGraphics Driver: [`QGraphics/`](../../QGraphics/)
- (Other modules as added)

**Build Commands:**
```bash
cd /home/dmort/citadel
cmake --build build/ --target qfs_core      # Output: build/QFileSystem/qfs_core.ko
cmake --build build/ --target qnet_driver   # Output: build/QNetwork/qnet_driver.ko
cmake --build build/ --target qgraphics     # Output: build/QGraphics/qgraphics.ko
```

**Signing Process (Per Module):**
- Artifact: `qfs_core.ko` (binary, ~50–500 KB per module)
- Signing key: Environment-specific MSK
  - Lab: `CITADEL_MSK_LAB_v1`
  - Staging: `CITADEL_MSK_STAGING_v1`
  - Production: `CITADEL_MSK_PROD_v1`
- Signature format: PKCS#7 (or ed25519 detached signature)
- Signature file: `qfs_core.ko.sig` (stored alongside .ko file)
- Verification: Kernel verifies signature before loading module

**Deployment:**
- Copy `*.ko` + `*.ko.sig` to module directory
- Final locations:
  - `/lib/modules/qfs_core.ko` + `qfs_core.ko.sig`
  - `/lib/modules/qnet_driver.ko` + `qnet_driver.ko.sig`
  - `/lib/modules/qgraphics.ko` + `qgraphics.ko.sig`

**Module Registration:**
- Each module must be listed in `boot.json` (with hash and signature)
- Kernel checks boot.json before loading any module
- If module not listed in boot.json: rejected (unauthorized)

**Rollout:**
- Lab: Every build
- Staging: Per-release (tested)
- Production: Per-release or on-demand OTA update

---

### 3.7 Ramdisk (initramfs)

**Purpose:** Optional initial RAM filesystem; used for early boot drivers, configuration, or recovery

**Build Location:**
- Source: Build script [`build.sh`](../../build.sh) or CMake target
- Output: `build/ramdisk.cpio` or `build/ramdisk.cpio.gz` (compressed)

**Build Command:**
```bash
cd /home/dmort/citadel
./build.sh --target ramdisk
# Output: build/ramdisk.cpio.gz (~10 MB)
```

**Signing Process:**
- Artifact: `ramdisk.cpio.gz` (compressed CPIO archive, ~10 MB)
- Signing key: Environment-specific MSK
  - Lab: `CITADEL_MSK_LAB_v1`
  - Staging: `CITADEL_MSK_STAGING_v1`
  - Production: `CITADEL_MSK_PROD_v1`
- Signature format: PKCS#7 (or ed25519 detached signature)
- Signature file: `ramdisk.cpio.gz.sig`
- Verification: Kernel verifies signature before mounting ramdisk

**Deployment:**
- Copy `ramdisk.cpio.gz` + `ramdisk.cpio.gz.sig` to boot partition
- Final location: `/boot/ramdisk` (or `/boot/initramfs`)

**Current Status:**
- Ramdisk is **optional** for v1 (marked as `ramdisk_required: false` in boot.json)
- **Future:** Ramdisk will be required for advanced boot scenarios (e.g., remote attestation, encrypted root)

**Rollout:**
- Lab: Every build (if ramdisk support is active)
- Staging: Per-release (when ramdisk features are ready)
- Production: TBD (future phase)

---

## 4. Build & Signing Pipeline

### 4.1 Signing Tool Reference

**Tool Location:** [`tools/sign_artifact.py`](../../tools/) (TBD: implement or use existing signing tool)

**Usage:**
```bash
# Sign a single artifact
python3 tools/sign_artifact.py \
  --artifact build/limine/Limine.efi \
  --key-id CITADEL_BOOT_PROD_v1 \
  --environment production \
  --output Limine.efi.sig

# Batch sign multiple artifacts
python3 tools/sign_artifact.py \
  --batch build/artifacts.txt \
  --environment production \
  --hsm yubihsm2
```

**Environment-Specific Behavior:**
- **Lab:** Uses disk-stored disposable keys
- **Staging:** Connects to YubiHSM2 (PIN-protected)
- **Production:** Connects to Azure Key Vault HSM (OAuth2 auth)

### 4.2 Build Integration (CMakeLists.txt)

**Proposed CMake targets:**
```cmake
# Build all artifacts
add_custom_target(all-artifacts DEPENDS limine BootGate kernel boot-json qfs_core qnet_driver qgraphics)

# Build and sign (lab environment)
add_custom_target(build-sign-lab DEPENDS all-artifacts
  COMMAND python3 tools/sign_artifact.py --batch build/artifacts.txt --environment lab
)

# Build and sign (staging environment)
add_custom_target(build-sign-staging DEPENDS all-artifacts
  COMMAND python3 tools/sign_artifact.py --batch build/artifacts.txt --environment staging
)

# Build and sign (production environment; requires dual approval)
add_custom_target(build-sign-prod DEPENDS all-artifacts
  COMMAND echo "WARNING: Production signing requires dual-control approval" && \
          python3 tools/sign_artifact.py --batch build/artifacts.txt --environment production
)
```

### 4.3 Signing Workflow (Lab)

```bash
# Step 1: Build all artifacts
cd /home/dmort/citadel
cmake --build build/ --target all-artifacts

# Step 2: Sign artifacts (automatic, uses lab keys from disk)
cmake --build build/ --target build-sign-lab

# Step 3: Verify signatures
tools/verify_signatures.py build/ --environment lab

# Step 4: Deploy to boot partition (local testing)
sudo cp build/limine/Limine.efi* /boot/efi/BOOT/
sudo cp build/BootGate/BootGate* /boot/
sudo cp build/kernel/vmlinuz* /boot/
sudo cp build/boot.json* /boot/
sudo cp build/QFileSystem/qfs_core.ko* /lib/modules/
sudo cp build/QNetwork/qnet_driver.ko* /lib/modules/
sudo cp build/QGraphics/qgraphics.ko* /lib/modules/
```

### 4.4 Signing Workflow (Staging)

```bash
# Step 1: Build all artifacts
cmake --build build/ --target all-artifacts

# Step 2: Sign artifacts (via YubiHSM2)
cmake --build build/ --target build-sign-staging
# Prompts: "Enter YubiHSM2 PIN: ****"

# Step 3: Verify signatures (against staging keys)
tools/verify_signatures.py build/ --environment staging

# Step 4: Deploy to staging boot partition
./tools/deploy_staging.py build/ --hardware staging-hw-001

# Step 5: Test boot on staging hardware
# Verify serial logs show all signatures validated
```

### 4.5 Signing Workflow (Production)

```bash
# Step 1: Build all artifacts
cmake --build build/ --target all-artifacts

# Step 2: Initiate production signing ceremony
cmake --build build/ --target build-sign-prod

# Prompts:
# "PRODUCTION SIGNING CEREMONY"
# "Approver 1: Enter approval ticket #: SECSEC-12345"
# "Approver 2: Enter approval ticket #: SECSEC-12345"
# "Ceremony operator: Authorize HSM connection? (y/n): y"
# "Enter Azure Key Vault credentials: ..."

# Step 3: HSM signs all artifacts (dual-control)
# Output: All .sig files created and audit-logged

# Step 4: Verify signatures (against production keys)
tools/verify_signatures.py build/ --environment production --audit-log

# Step 5: Create firmware update capsule
./tools/create_firmware_capsule.py build/ --output citadel-v1.0-prod.capsule

# Step 6: Upload to distribution system
./tools/upload_release.py citadel-v1.0-prod.capsule --environment production
```

---

## 5. Verification Process

### 5.1 Local Verification (Developer)

**Before submitting to CI:**
```bash
# Verify signature on a single artifact
openssl pkeyutl -verify -in Limine.efi -sigfile Limine.efi.sig -pubin -inkey CITADEL_BOOT_LAB_v1.pub

# Or (using custom tool):
tools/verify_signatures.py build/limine/Limine.efi \
  --key CITADEL_BOOT_LAB_v1.pub \
  --signature Limine.efi.sig
```

### 5.2 CI/CD Verification (GitHub Actions / GitLab CI)

**On every commit:**
```yaml
# .github/workflows/sign-and-verify.yml
name: Build, Sign, and Verify
on: [push, pull_request]
jobs:
  build-and-sign:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build all artifacts
        run: cmake --build build/ --target all-artifacts
      - name: Sign artifacts (lab)
        run: cmake --build build/ --target build-sign-lab
      - name: Verify signatures
        run: tools/verify_signatures.py build/ --environment lab
      - name: Report results
        run: echo "All signatures verified!"
```

### 5.3 Deployment Verification (Before Boot)

**On target hardware (staging or production):**
```bash
# Verify signatures before deploying to boot partition
tools/verify_boot_artifacts.sh /boot/ --environment staging

# Output:
# [✓] Limine.efi signature valid
# [✓] BootGate signature valid
# [✓] kernel signature valid
# [✓] boot.json signature valid
# [✓] qfs_core.ko signature valid
# All artifacts verified; safe to reboot
```

---

## 6. Artifact Hash Registry

### 6.1 Hash Format (SHA-256)

**Example Registry Entry:**
```json
{
  "artifact": "Limine.efi",
  "version": "citadel-v1.0",
  "environment": "production",
  "hash_sha256": "abcd1234567890abcd1234567890abcd1234567890abcd1234567890abcd1234",
  "size_bytes": 524288,
  "build_timestamp": "2026-06-16T14:23:45Z",
  "builder": "ci-agent@citadel.local",
  "signed_by_key": "CITADEL_BOOT_PROD_v1",
  "signature_hash": "xyz9876543210xyz9876543210xyz9876543210xyz9876543210xyz9876543210"
}
```

### 6.2 Registry Location

- **Lab:** `build/artifact-registry-lab.json` (local, not committed)
- **Staging:** `build/artifact-registry-staging.json` (uploaded to artifact server)
- **Production:** `releases/artifact-registry-prod-v1.0.json` (committed to repo, immutable)

**Generation:**
```bash
# Auto-generated by build system
tools/generate_artifact_registry.py build/ --output build/artifact-registry-lab.json
```

---

## 7. Maintenance & Updates

### 7.1 When to Re-sign Artifacts

| Scenario | Action |
|----------|--------|
| Source code changes | Rebuild + re-sign with same key |
| Key rotation (scheduled) | Rebuild + re-sign with new key (double-sign during transition) |
| Key compromise (emergency) | Re-sign with new key; publish dbx update immediately |
| Module removed from boot.json | Update boot.json; re-sign with MSK |
| Module added to boot.json | Build module; sign with MSK; update boot.json; re-sign |

### 7.2 Artifact Lifecycle

```
Build → Sign → Verify → Test (staging) → Release (production) → Retire (after N years)
```

**Lab Lifecycle:** Hours (disposable)
**Staging Lifecycle:** Weeks (per-release)
**Production Lifecycle:** Years (with annual rotation of MSK/KSK/LSK)

---

## 8. Future Enhancements

- [ ] Add TPM PCR extension support (measured boot)
- [ ] Implement SBOM (Software Bill of Materials) for supply chain tracking
- [ ] Add runtime signature verification for dynamically loaded modules
- [ ] Support rollback protection (TPM counter or RPMB)
- [ ] Integrate with OTA system for automated secure updates

---

## References

- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md) — Key management policy
- [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md) — Boot chain verification logic
- [`CMakeLists.txt`](../../CMakeLists.txt) — Build configuration
- [`build.sh`](../../build.sh) — Build script
- [`boot.json`](../../boot.json) — Boot policy template
