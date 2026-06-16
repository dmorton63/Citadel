# Citadel Secure Boot Build Profile & Configuration

**Status:** Build system integration (Batch 1, Item 4–5)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Build Team

---

## 1. CMake Integration (Item 4)

### 1.1 Secure Boot Profile Flag

Add to [CMakeLists.txt](../../CMakeLists.txt):

```cmake
# Secure Boot Profile Configuration
option(ENABLE_SECURE_BOOT "Enable Secure Boot artifact signing and validation" OFF)
option(SECURE_BOOT_ENVIRONMENT "Secure Boot environment (lab/staging/production)" "lab")

if(ENABLE_SECURE_BOOT)
  message(STATUS "Secure Boot profile: ENABLED (environment: ${SECURE_BOOT_ENVIRONMENT})")
  
  # Define Secure Boot artifact list
  set(SECURE_BOOT_ARTIFACTS
    $<TARGET_FILE:limine>
    $<TARGET_FILE:bootgate>
    $<TARGET_FILE:kernel>
    $<CONFIG_FILE:boot.json>
  )
  
  # Add post-build validation target
  add_custom_target(validate-secure-boot-artifacts ALL
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_secure_boot_artifacts.py
      --artifacts ${SECURE_BOOT_ARTIFACTS}
      --environment ${SECURE_BOOT_ENVIRONMENT}
      --strict
    DEPENDS limine bootgate kernel boot-json
    COMMENT "Validating Secure Boot artifacts..."
  )
  
  # Add signing target
  add_custom_target(sign-secure-boot-artifacts
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/sign_secure_boot_artifacts.py
      --artifacts ${SECURE_BOOT_ARTIFACTS}
      --environment ${SECURE_BOOT_ENVIRONMENT}
      --manifest build/secure-boot-manifest-${SECURE_BOOT_ENVIRONMENT}.json
    DEPENDS validate-secure-boot-artifacts
    COMMENT "Signing Secure Boot artifacts (${SECURE_BOOT_ENVIRONMENT})..."
  )
  
else()
  message(STATUS "Secure Boot profile: DISABLED (unsigned artifacts)")
endif()
```

### 1.2 Usage

**Build with Secure Boot (lab):**
```bash
cmake -DENABLE_SECURE_BOOT=ON -DSECURE_BOOT_ENVIRONMENT=lab ..
cmake --build . --target sign-secure-boot-artifacts
```

**Build with Secure Boot (staging):**
```bash
cmake -DENABLE_SECURE_BOOT=ON -DSECURE_BOOT_ENVIRONMENT=staging ..
cmake --build . --target sign-secure-boot-artifacts
```

**Build without Secure Boot (default):**
```bash
cmake ..
cmake --build .
```

### 1.3 Artifact Validation Script

Create [tools/validate_secure_boot_artifacts.py](../../tools/validate_secure_boot_artifacts.py):

```python
#!/usr/bin/env python3
"""
Validate Secure Boot artifacts before signing.

Checks:
- All artifacts exist and are readable
- Artifact sizes are reasonable (not truncated)
- No temporary or corrupted files
- All required artifacts present for environment
"""

import argparse
import json
import sys
from pathlib import Path
import hashlib

REQUIRED_ARTIFACTS = {
    "lab": ["limine", "bootgate", "kernel", "boot.json"],
    "staging": ["limine", "bootgate", "kernel", "boot.json"],
    "production": ["limine", "bootgate", "kernel", "boot.json"],
}

MIN_ARTIFACT_SIZES = {
    "limine": 100_000,      # ~512 KB
    "bootgate": 50_000,     # ~200 KB
    "kernel": 5_000_000,    # ~8 MB
    "boot.json": 500,       # ~2 KB
}

def validate_artifacts(artifacts, environment, strict=False):
    """Validate artifact files."""
    errors = []
    warnings = []
    
    # Check required artifacts present
    artifact_names = set(Path(a).stem for a in artifacts)
    required = set(REQUIRED_ARTIFACTS[environment])
    
    missing = required - artifact_names
    if missing:
        msg = f"Missing required artifacts for {environment}: {missing}"
        if strict:
            errors.append(msg)
        else:
            warnings.append(msg)
    
    # Check each artifact
    for artifact in artifacts:
        path = Path(artifact)
        
        # File exists
        if not path.exists():
            errors.append(f"Artifact not found: {artifact}")
            continue
        
        # File is readable
        if not path.is_file():
            errors.append(f"Artifact is not a regular file: {artifact}")
            continue
        
        # File size reasonable
        size = path.stat().st_size
        artifact_name = path.stem
        
        if artifact_name in MIN_ARTIFACT_SIZES:
            min_size = MIN_ARTIFACT_SIZES[artifact_name]
            if size < min_size:
                errors.append(
                    f"Artifact {artifact_name} too small: {size} bytes (min {min_size})"
                )
        
        # Calculate hash
        try:
            hash_sha256 = hashlib.sha256()
            with open(path, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hash_sha256.update(chunk)
            
            print(f"✓ {artifact_name:20} {size:12} bytes  SHA256:{hash_sha256.hexdigest()[:16]}...")
        except Exception as e:
            errors.append(f"Failed to hash {artifact}: {e}")
    
    # Report
    if warnings:
        print("\n⚠️  Warnings:")
        for w in warnings:
            print(f"  - {w}")
    
    if errors:
        print("\n✗ Errors:")
        for e in errors:
            print(f"  - {e}")
        return False
    
    print("\n✓ All artifacts validated successfully")
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Validate Secure Boot artifacts"
    )
    parser.add_argument("--artifacts", nargs="+", required=True, help="Artifact files to validate")
    parser.add_argument("--environment", choices=["lab", "staging", "production"], default="lab")
    parser.add_argument("--strict", action="store_true", help="Fail on warnings")
    
    args = parser.parse_args()
    
    success = validate_artifacts(args.artifacts, args.environment, args.strict)
    sys.exit(0 if success else 1)
```

---

## 2. Signature Manifest Generation (Item 5)

### 2.1 Manifest JSON Schema

```json
{
  "schema_version": "1.0",
  "environment": "lab",
  "build_timestamp": "2026-06-16T14:23:45Z",
  "build_id": "citadel-v1.0-lab-20260616-142345",
  "builder": "ci-agent@citadel.local",
  "artifacts": [
    {
      "name": "Limine.efi",
      "path": "build/limine/Limine.efi",
      "size_bytes": 524288,
      "hash_sha256": "abcd1234567890abcd1234567890abcd1234567890abcd1234567890abcd1234",
      "signing_key_id": "CITADEL_BOOT_LAB_v1",
      "signature_file": "Limine.efi.sig",
      "signature_format": "pkcs7",
      "signing_timestamp": "2026-06-16T14:23:50Z",
      "signer_id": "dev@citadel.local",
      "verification_status": "valid"
    },
    {
      "name": "BootGate",
      "path": "build/BootGate/BootGate",
      "size_bytes": 204800,
      "hash_sha256": "xyz9876543210xyz9876543210xyz9876543210xyz9876543210xyz9876543210",
      "signing_key_id": "CITADEL_LSK_LAB_v1",
      "signature_file": "BootGate.sig",
      "signature_format": "pkcs7",
      "signing_timestamp": "2026-06-16T14:23:52Z",
      "signer_id": "dev@citadel.local",
      "verification_status": "valid"
    }
  ],
  "audit_log_id": "audit-20260616-142345-xyz",
  "manifest_signature": "manifest.sig",
  "manifest_hash_sha256": "manifest_hash_here"
}
```

### 2.2 Manifest Generation Script

Create [tools/generate_signature_manifest.py](../../tools/generate_signature_manifest.py):

```python
#!/usr/bin/env python3
"""
Generate machine-readable signature manifest for Secure Boot artifacts.

Output: secure-boot-manifest-{environment}.json
"""

import argparse
import json
import sys
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import socket
import getpass

def generate_manifest(artifacts, environment, output_path, signing_key_id=None):
    """Generate signature manifest for artifacts."""
    
    manifest = {
        "schema_version": "1.0",
        "environment": environment,
        "build_timestamp": datetime.now(timezone.utc).isoformat(),
        "build_id": f"citadel-v1.0-{environment}-{datetime.now().strftime('%Y%m%d-%H%M%S')}",
        "builder": f"{getpass.getuser()}@{socket.gethostname()}",
        "artifacts": [],
        "audit_log_id": f"audit-{datetime.now().strftime('%Y%m%d-%H%M%S')}-tbd",
    }
    
    # Process each artifact
    for artifact_path in artifacts:
        path = Path(artifact_path)
        
        if not path.exists():
            print(f"⚠️  Artifact not found: {artifact_path}")
            continue
        
        # Calculate hash
        hash_sha256 = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_sha256.update(chunk)
        
        artifact_name = path.stem
        
        entry = {
            "name": artifact_name,
            "path": str(path),
            "size_bytes": path.stat().st_size,
            "hash_sha256": hash_sha256.hexdigest(),
            "signing_key_id": signing_key_id or f"CITADEL_{artifact_name.upper()}_{environment.upper()}_v1",
            "signature_file": f"{artifact_name}.sig",
            "signature_format": "pkcs7",
            "signing_timestamp": None,  # TBD: filled during signing
            "signer_id": f"{getpass.getuser()}@{socket.gethostname()}",
            "verification_status": "pending",
        }
        
        manifest["artifacts"].append(entry)
        print(f"✓ {artifact_name:20} {entry['size_bytes']:12} bytes  {entry['hash_sha256'][:16]}...")
    
    # Write manifest
    output_path = Path(output_path)
    with open(output_path, "w") as f:
        json.dump(manifest, f, indent=2)
    
    print(f"\n✓ Manifest written to {output_path}")
    return manifest

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate signature manifest")
    parser.add_argument("--artifacts", nargs="+", required=True, help="Artifact files")
    parser.add_argument("--environment", choices=["lab", "staging", "production"], default="lab")
    parser.add_argument("--output", required=True, help="Output manifest file")
    parser.add_argument("--signing-key-id", help="Override signing key ID")
    
    args = parser.parse_args()
    
    manifest = generate_manifest(
        args.artifacts,
        args.environment,
        args.output,
        args.signing_key_id
    )
    
    print(json.dumps(manifest, indent=2))
```

### 2.3 Usage

```bash
# Generate manifest for lab artifacts
python3 tools/generate_signature_manifest.py \
  --artifacts build/limine/Limine.efi build/BootGate/BootGate build/kernel/vmlinuz build/boot.json \
  --environment lab \
  --output build/secure-boot-manifest-lab.json

# Output: build/secure-boot-manifest-lab.json
```

---

## 3. References

- [SECURE_BOOT_KEY_HIERARCHY.md](SECURE_BOOT_KEY_HIERARCHY.md)
- [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)
- [SECURE_BOOT_ARTIFACT_INVENTORY.md](SECURE_BOOT_ARTIFACT_INVENTORY.md)
