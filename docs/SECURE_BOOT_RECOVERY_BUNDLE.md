# Citadel Secure Boot Recovery Bundle

**Status:** Recovery procedure (Batch 1, Item 7)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Lab Operations  
**Audience:** Lab technicians, on-call operators

---

## 1. Overview

A Secure Boot **recovery bundle** is a self-contained, air-gapped collection of keys, documentation, and validation tools that allows recovery from complete loss of Secure Boot keys or firmware corruption.

**Contents:**
- PK/KEK/db private keys (encrypted)
- Key enrollment utilities
- Boot artifacts (Limine, BootGate, kernel)
- Recovery instructions (printed and digital)
- Validation script

**Storage:** Fireproof safe or secure vault (not connected to network)

**Access Control:** Requires dual unlock (two keys/two people)

---

## 2. Creating a Recovery Bundle

### 2.1 Bundle Assembly

Create the bundle with [tools/create_recovery_bundle.py](../../tools/create_recovery_bundle.py):

```python
#!/usr/bin/env python3
"""
Create a Citadel Secure Boot recovery bundle.

Outputs to: recovery-bundle-{date}-{hash}.tar.gz.gpg (encrypted)
"""

import argparse
import json
import sys
import subprocess
import tempfile
from pathlib import Path
from datetime import datetime
import hashlib
import os

def create_recovery_bundle(
    environment,
    key_dir,
    output_dir,
    artifact_dir,
    gpg_recipient_ids
):
    """Create encrypted recovery bundle."""
    
    print(f"Creating recovery bundle for {environment}...")
    
    # Create temporary staging directory
    with tempfile.TemporaryDirectory() as tmpdir:
        staging = Path(tmpdir) / "recovery-bundle"
        staging.mkdir()
        
        # 1. Copy keys
        print("  [1/5] Copying keys...")
        keys_dir = staging / "keys"
        keys_dir.mkdir()
        subprocess.run(["cp", "-r", str(key_dir), str(keys_dir)], check=True)
        
        # 2. Copy boot artifacts
        print("  [2/5] Copying boot artifacts...")
        artifacts = staging / "artifacts"
        artifacts.mkdir()
        for artifact in Path(artifact_dir).glob("*"):
            if artifact.is_file():
                subprocess.run(["cp", str(artifact), str(artifacts)], check=True)
        
        # 3. Copy recovery instructions
        print("  [3/5] Adding recovery instructions...")
        instructions = staging / "RECOVERY_INSTRUCTIONS.md"
        instructions.write_text("""# Citadel Secure Boot Recovery Bundle

## Emergency Recovery Procedure

### Prerequisites
- Physical access to lab hardware
- This recovery bundle (encrypted USB or archive)
- Dual authentication (two operators with keys/passwords)

### Steps

1. **Decrypt bundle** (requires GPG key):
   ```bash
   gpg --decrypt recovery-bundle-YYYYMMDD.tar.gz.gpg > recovery-bundle.tar.gz
   tar xzf recovery-bundle.tar.gz
   ```

2. **Boot lab hardware into Secure Boot Setup Mode**:
   - Power on with serial console attached
   - Enter BIOS (DEL or F2)
   - Security → Secure Boot → Setup Mode: ON
   - Save and reboot

3. **Boot Linux (unsigned, since Setup Mode is on)**
4. **Mount recovery USB** containing this bundle:
   ```bash
   sudo mount /dev/sdX1 /mnt/recovery
   cd /mnt/recovery/recovery-bundle
   ```

5. **Enroll keys** (follow UEFI enrollment procedure):
   ```bash
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --pk keys/PK.pub
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --kek keys/KEK.pub
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --db keys/db.pub
   sudo /boot/efi/EFI/Citadel/enroll_keys.efi --lock
   ```

6. **Deploy signed boot artifacts**:
   ```bash
   sudo cp artifacts/* /boot/
   ```

7. **Reboot and verify**:
   - Serial console should show "Secure Boot: ACTIVE"
   - Limine should boot successfully
   - Kernel should load and proceed to desktop

### If Bundle Fails

- Verify GPG decryption succeeded (check tar contents)
- Check BIOS shows Setup Mode enabled
- Verify keys directory contains {PK, KEK, db}.pub files
- Contact security team if corruption suspected

---

**Bundle Created:** {timestamp}
**Environment:** {environment}
**Bundle Hash:** {bundle_hash}
""".format(
    timestamp=datetime.utcnow().isoformat(),
    environment=environment,
    bundle_hash="[WILL_BE_FILLED_AFTER_ARCHIVE]"
))
        
        # 4. Create manifest
        print("  [4/5] Creating manifest...")
        manifest = {
            "bundle_created": datetime.utcnow().isoformat(),
            "environment": environment,
            "contents": {
                "keys": sorted([f.name for f in (staging / "keys").glob("**/*") if f.is_file()]),
                "artifacts": sorted([f.name for f in (staging / "artifacts").glob("*") if f.is_file()]),
                "instructions": "RECOVERY_INSTRUCTIONS.md"
            }
        }
        
        manifest_file = staging / "MANIFEST.json"
        with open(manifest_file, "w") as f:
            json.dump(manifest, f, indent=2)
        
        # 5. Archive and encrypt
        print("  [5/5] Archiving and encrypting...")
        
        archive_name = f"recovery-bundle-{datetime.now().strftime('%Y%m%d')}-{environment}"
        archive_path = Path(output_dir) / f"{archive_name}.tar.gz"
        encrypted_path = Path(output_dir) / f"{archive_name}.tar.gz.gpg"
        
        # Tar the staging directory
        subprocess.run(
            ["tar", "-czf", str(archive_path), "-C", str(staging.parent), staging.name],
            check=True
        )
        
        # Calculate bundle hash
        with open(archive_path, "rb") as f:
            bundle_hash = hashlib.sha256(f.read()).hexdigest()
        
        # Encrypt with GPG
        gpg_recipients = ["-r " + r for r in gpg_recipient_ids]
        subprocess.run(
            f"gpg --trust-model always {' '.join(gpg_recipients)} --encrypt {archive_path}".split(),
            check=True
        )
        
        # Delete unencrypted archive
        archive_path.unlink()
        
        print(f"\n✓ Recovery bundle created: {encrypted_path}")
        print(f"  Bundle hash: {bundle_hash}")
        print(f"  Size: {encrypted_path.stat().st_size} bytes")
        
        return encrypted_path, bundle_hash

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create Secure Boot recovery bundle")
    parser.add_argument("--environment", choices=["lab", "staging"], default="lab")
    parser.add_argument("--key-dir", required=True, help="Directory containing PK/KEK/db keys")
    parser.add_argument("--artifact-dir", required=True, help="Directory containing boot artifacts")
    parser.add_argument("--output-dir", default=".", help="Output directory for bundle")
    parser.add_argument("--gpg-recipients", nargs="+", required=True, help="GPG recipient IDs for encryption")
    
    args = parser.parse_args()
    
    encrypted_path, bundle_hash = create_recovery_bundle(
        args.environment,
        args.key_dir,
        args.output_dir,
        args.artifact_dir,
        args.gpg_recipients
    )
    
    print(f"\nBundle ready for archival: {encrypted_path}")
```

### 2.2 Usage

```bash
# Create recovery bundle for lab
python3 tools/create_recovery_bundle.py \
  --environment lab \
  --key-dir /tmp/citadel-lab-keys \
  --artifact-dir build/ \
  --output-dir /tmp/recovery-bundles \
  --gpg-recipients dev@citadel.local ops@citadel.local

# Output: /tmp/recovery-bundles/recovery-bundle-20260616-lab.tar.gz.gpg
```

---

## 3. Recovery Bundle Validation

### 3.1 Decryption Test

Before archiving the bundle, validate it can be decrypted:

```bash
# Decrypt test
gpg --decrypt recovery-bundle-20260616-lab.tar.gz.gpg > /tmp/test-bundle.tar.gz

# Verify archive integrity
tar tzf /tmp/test-bundle.tar.gz | head -20
# Should list keys/, artifacts/, MANIFEST.json, RECOVERY_INSTRUCTIONS.md

# Check manifest
tar xzOf /tmp/test-bundle.tar.gz recovery-bundle/MANIFEST.json | jq .
```

### 3.2 Key Verification

```bash
# Verify keys are present
tar xzOf /tmp/test-bundle.tar.gz recovery-bundle/MANIFEST.json | jq '.contents.keys'

# Example output:
# [
#   "PK.priv",
#   "PK.pub",
#   "KEK.priv",
#   "KEK.pub",
#   "db.priv",
#   "db.pub"
# ]
```

### 3.3 Artifact Verification

```bash
# Verify boot artifacts are present
tar xzOf /tmp/test-bundle.tar.gz recovery-bundle/MANIFEST.json | jq '.contents.artifacts'

# Verify artifact hashes (create file list)
tar tzf /tmp/test-bundle.tar.gz | grep "^recovery-bundle/artifacts/" | sort
```

---

## 4. Recovery Bundle Storage

### 4.1 Recommended Storage Locations

| Location | Type | Access | Use Case |
|----------|------|--------|----------|
| On-site fireproof safe | Physical media (USB/DVD) | Dual lock | Primary recovery |
| Encrypted external drive | USB 3.0 encrypted SSD | Dual unlock + password | Backup recovery |
| Offline cloud vault | AWS S3 Glacier / Azure Archive | MFA + encrypted key | Disaster recovery |

### 4.2 Storage Checklist

- [ ] Bundle encrypted with GPG (no unencrypted copies)
- [ ] GPG key shared with at least 2 operators
- [ ] Physical media labeled with creation date, environment, and bundle hash
- [ ] Storage location documented and accessible to on-call team
- [ ] Dual-lock mechanism implemented (if physical safe)
- [ ] Annual rotation scheduled (refresh keys if changed)
- [ ] Audit log created (who accessed bundle, when)

---

## 5. Recovery Bundle Lifecycle

### 5.1 Creation → Storage

```
1. Create bundle (tools/create_recovery_bundle.py)
   ↓
2. Validate decryption (dry run)
   ↓
3. Create checksum (SHA-256)
   ↓
4. Archive to physical media (USB or DVD)
   ↓
5. Store in secure location (fireproof safe)
   ↓
6. Document access procedures
   ↓
7. Log creation in audit trail
```

### 5.2 Scheduled Updates

- **When to recreate bundle:**
  - PK or KEK rotated (every 1–3 years)
  - New boot artifacts released
  - Recovery procedure updated
  - Emergency recovery executed

- **Frequency:** Annually minimum (refresh encryption, update instructions)

### 5.3 Retirement

- Destroy old encrypted media (degauss or physical destruction)
- Verify chain-of-custody (who destroyed it, when)
- Archive certification of destruction (for compliance)

---

## 6. Emergency Recovery Drill

### 6.1 Quarterly Drill Procedure

**Purpose:** Validate recovery bundle is still usable; train operators

**Procedure:**

1. **Preparation:**
   - Designate two operators
   - Schedule 2-hour window
   - Retrieve recovery bundle from secure storage

2. **Dry-run decryption:**
   ```bash
   gpg --decrypt recovery-bundle-*.tar.gz.gpg > /tmp/test-bundle.tar.gz
   tar xzf /tmp/test-bundle.tar.gz
   ls -la recovery-bundle/
   ```

3. **Verify contents:**
   - Check all keys present
   - Check boot artifacts present
   - Read and understand recovery instructions

4. **Document findings:**
   - Did decryption succeed?
   - Were all keys present?
   - Any issues encountered?
   - How long did recovery take?

5. **Update audit log:**
   ```
   [2026-06-16] Q2 2026 Recovery Drill
   Date: 2026-06-16T15:00:00Z
   Operators: dev@citadel.local, ops@citadel.local
   Bundle: recovery-bundle-20260401-lab.tar.gz.gpg
   Decryption: OK (42 seconds)
   Contents: All verified
   Issues: None
   Recovery time estimate: 15 minutes
   Next drill: 2026-09-16 (Q3)
   ```

6. **Schedule next drill** (quarterly)

---

## 7. Audit & Compliance

### 7.1 Recovery Bundle Registry

Maintain a registry of all recovery bundles:

```json
{
  "bundles": [
    {
      "bundle_id": "recovery-bundle-20260616-lab",
      "environment": "lab",
      "created_date": "2026-06-16T14:00:00Z",
      "bundle_hash_sha256": "abcd1234...",
      "keys_included": ["PK", "KEK", "db"],
      "boot_artifacts": ["Limine", "BootGate", "kernel", "boot.json"],
      "storage_location": "Secure safe, drawer B",
      "access_control": "Dual lock (2 keys required)",
      "gpg_recipients": ["dev@citadel.local", "ops@citadel.local"],
      "last_validated": "2026-06-16T15:30:00Z",
      "next_drill_scheduled": "2026-09-16",
      "status": "ACTIVE"
    }
  ]
}
```

### 7.2 Access Logging

Log all bundle access attempts:

```
[2026-06-16T15:00:00Z] dev@citadel.local retrieved bundle from safe
[2026-06-16T15:05:00Z] ops@citadel.local verified dual lock
[2026-06-16T15:10:00Z] Bundle decrypted and contents verified
[2026-06-16T15:35:00Z] Bundle returned to safe and locked
```

---

## References

- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md)
- [SECURE_BOOT_FIRMWARE_ENROLLMENT_RUNBOOK.md](SECURE_BOOT_FIRMWARE_ENROLLMENT_RUNBOOK.md)
- [tools/create_recovery_bundle.py](../../tools/create_recovery_bundle.py)
