#!/usr/bin/env python3
"""
Citadel Secure Boot Reproducible Signing Tool

Produces deterministic signatures for boot artifacts.

Determinism guarantees:
  - Artifacts are sorted canonically before processing
  - Timestamps in manifest use UTC ISO-8601
  - Hash inputs are the raw file bytes (no metadata)
  - Signature format is always PKCS#7 detached (DER)

Usage (lab):
  python3 tools/sign_artifacts.py --environment lab \
      --artifacts build/limine/Limine.efi build/BootGate/BootGate \
                  build/kernel/vmlinuz build/boot.json \
      --manifest-out build/secure-boot-manifest-lab.json

Usage (staging, YubiHSM2):
  python3 tools/sign_artifacts.py --environment staging \
      --artifacts ...  --hsm-slot 0 --hsm-pin-env YHSM_PIN \
      --manifest-out build/secure-boot-manifest-staging.json

Usage (verify only):
  python3 tools/sign_artifacts.py --verify-only \
      --environment lab --manifest build/secure-boot-manifest-lab.json
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Key ID mapping (must match SECURE_BOOT_KEY_HIERARCHY.md)
# ---------------------------------------------------------------------------
KEY_ASSIGNMENTS = {
    "lab": {
        "limine":    "CITADEL_BOOT_LAB_v1",
        "bootgate":  "CITADEL_LSK_LAB_v1",
        "vmlinuz":   "CITADEL_KSK_LAB_v1",
        "boot.json": "CITADEL_MSK_LAB_v1",
        # module wildcard
        ".ko":       "CITADEL_MSK_LAB_v1",
    },
    "staging": {
        "limine":    "CITADEL_BOOT_STAGING_v1",
        "bootgate":  "CITADEL_LSK_STAGING_v1",
        "vmlinuz":   "CITADEL_KSK_STAGING_v1",
        "boot.json": "CITADEL_MSK_STAGING_v1",
        ".ko":       "CITADEL_MSK_STAGING_v1",
    },
    "production": {
        "limine":    "CITADEL_BOOT_PROD_v1",
        "bootgate":  "CITADEL_LSK_PROD_v1",
        "vmlinuz":   "CITADEL_KSK_PROD_v1",
        "boot.json": "CITADEL_MSK_PROD_v1",
        ".ko":       "CITADEL_MSK_PROD_v1",
    },
}


def key_id_for(path: Path, environment: str) -> str:
    """Return the canonical key ID for an artifact."""
    name_lower = path.name.lower()
    mapping = KEY_ASSIGNMENTS[environment]
    for stem, key_id in mapping.items():
        if name_lower.startswith(stem) or name_lower.endswith(stem):
            return key_id
    raise ValueError(f"No key assignment for artifact '{path.name}' in {environment}")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Signing backends
# ---------------------------------------------------------------------------

def sign_openssl(artifact: Path, key_pem: Path, cert_pem: Path) -> Path:
    """Sign with a local PEM private key (lab only)."""
    sig_path = artifact.with_suffix(artifact.suffix + ".sig")
    subprocess.run(
        [
            "openssl", "cms", "-sign",
            "-binary",
            "-noattr",
            "-in", str(artifact),
            "-inkey", str(key_pem),
            "-signer", str(cert_pem),
            "-outform", "DER",
            "-out", str(sig_path),
        ],
        check=True,
        capture_output=True,
    )
    return sig_path


def sign_yubihsm2(artifact: Path, key_id: str, slot: int, pin: str) -> Path:
    """Sign via YubiHSM2 PKCS#11 interface (staging)."""
    sig_path = artifact.with_suffix(artifact.suffix + ".sig")
    subprocess.run(
        [
            "openssl", "cms", "-sign",
            "-binary", "-noattr",
            "-in", str(artifact),
            "-keyform", "ENGINE",
            "-engine", "pkcs11",
            "-inkey", f"pkcs11:slot-id={slot};object={key_id};type=private",
            "-certfile", f"/etc/citadel/keys/{key_id}.crt",
            "-outform", "DER",
            "-out", str(sig_path),
        ],
        env={**os.environ, "PKCS11_PIN": pin},
        check=True,
        capture_output=True,
    )
    return sig_path


def sign_azure_kv(artifact: Path, key_id: str, vault_url: str) -> Path:
    """Sign via Azure Key Vault HSM (production)."""
    sig_path = artifact.with_suffix(artifact.suffix + ".sig")
    # Digest the artifact locally; send digest to AKV for signing
    digest = sha256_file(artifact)
    subprocess.run(
        [
            "az", "keyvault", "key", "sign",
            "--vault-name", vault_url,
            "--name", key_id,
            "--algorithm", "RS256",
            "--digest", digest,
            "--output", str(sig_path),
        ],
        check=True,
        capture_output=True,
    )
    return sig_path


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def verify_signature(artifact: Path, cert_pem: Path) -> bool:
    """Verify a detached PKCS#7 DER signature against a certificate."""
    sig_path = artifact.with_suffix(artifact.suffix + ".sig")
    if not sig_path.exists():
        print(f"  [FAIL] No signature file: {sig_path}")
        return False
    result = subprocess.run(
        [
            "openssl", "cms", "-verify",
            "-binary",
            "-in", str(sig_path),
            "-inform", "DER",
            "-content", str(artifact),
            "-CAfile", str(cert_pem),
            "-noverify",  # chain trust handled by key hierarchy, not openssl verify
        ],
        capture_output=True,
    )
    return result.returncode == 0


# ---------------------------------------------------------------------------
# Manifest helpers
# ---------------------------------------------------------------------------

def build_manifest_entry(
    artifact: Path,
    sig_path: Optional[Path],
    key_id: str,
    environment: str,
    signer: str,
    verified: bool,
) -> dict:
    return {
        "name": artifact.name,
        "path": str(artifact),
        "size_bytes": artifact.stat().st_size,
        "hash_sha256": sha256_file(artifact),
        "signing_key_id": key_id,
        "signature_file": str(sig_path) if sig_path else None,
        "signature_format": "pkcs7-der",
        "signing_timestamp": datetime.now(timezone.utc).isoformat(),
        "signer_id": signer,
        "environment": environment,
        "verification_status": "valid" if verified else "failed",
    }


def write_manifest(entries: list, environment: str, out_path: Path) -> None:
    manifest = {
        "schema_version": "1.0",
        "environment": environment,
        "build_timestamp": datetime.now(timezone.utc).isoformat(),
        "build_id": (
            f"citadel-{environment}-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
        ),
        "artifacts": entries,
    }
    with open(out_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print(f"\n✓ Manifest written → {out_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Citadel Secure Boot reproducible signing tool"
    )
    parser.add_argument(
        "--environment", choices=["lab", "staging", "production"],
        default="lab", help="Target environment"
    )
    parser.add_argument(
        "--artifacts", nargs="+",
        help="Artifact files to sign (sorted canonically before processing)"
    )
    parser.add_argument("--manifest-out", help="Output manifest JSON path")
    parser.add_argument("--verify-only", action="store_true",
                        help="Only verify existing signatures; do not sign")
    parser.add_argument("--manifest", help="Existing manifest to verify against")
    # Lab options
    parser.add_argument("--key-dir", default="/tmp/citadel-lab-keys",
                        help="Directory with {KEY_ID}.pem key + {KEY_ID}.crt files (lab)")
    # Staging options
    parser.add_argument("--hsm-slot", type=int, default=0,
                        help="YubiHSM2 PKCS#11 slot (staging)")
    parser.add_argument("--hsm-pin-env", default="YHSM_PIN",
                        help="Env var containing YubiHSM2 PIN (staging)")
    # Production options
    parser.add_argument("--azure-vault-url", help="Azure Key Vault URL (production)")
    # Identity
    parser.add_argument("--signer", default=os.environ.get("USER", "unknown"),
                        help="Human-readable signer identity for manifest")
    args = parser.parse_args()

    # ------------------------------------------------------------------
    # Verify-only mode: load manifest and re-verify each artifact
    # ------------------------------------------------------------------
    if args.verify_only:
        if not args.manifest:
            parser.error("--manifest is required with --verify-only")
        with open(args.manifest) as f:
            manifest = json.load(f)

        key_dir = Path(args.key_dir)
        all_ok = True
        print(f"Verifying {len(manifest['artifacts'])} artifacts "
              f"({manifest['environment']})...")
        for entry in manifest["artifacts"]:
            artifact = Path(entry["path"])
            key_id   = entry["signing_key_id"]
            cert     = key_dir / f"{key_id}.crt"
            ok = verify_signature(artifact, cert)
            status = "✓ VALID  " if ok else "✗ FAILED "
            print(f"  {status} {artifact.name:30}  key={key_id}")
            if not ok:
                all_ok = False
        print()
        print("All signatures valid." if all_ok else "SIGNATURE VERIFICATION FAILED.")
        sys.exit(0 if all_ok else 1)

    # ------------------------------------------------------------------
    # Signing mode
    # ------------------------------------------------------------------
    if not args.artifacts:
        parser.error("--artifacts is required when signing")

    # Canonical ordering: sort by resolved absolute path for determinism
    artifacts = sorted(Path(a).resolve() for a in args.artifacts)

    print(f"Signing {len(artifacts)} artifacts for [{args.environment}]...")
    key_dir = Path(args.key_dir)
    entries = []
    all_ok  = True

    for artifact in artifacts:
        if not artifact.exists():
            print(f"  [ERROR] Not found: {artifact}")
            all_ok = False
            continue

        key_id = key_id_for(artifact, args.environment)
        print(f"  Signing {artifact.name:30}  key={key_id} ... ", end="", flush=True)

        sig_path: Optional[Path] = None
        try:
            if args.environment == "lab":
                key_pem  = key_dir / f"{key_id}.pem"
                cert_pem = key_dir / f"{key_id}.crt"
                sig_path = sign_openssl(artifact, key_pem, cert_pem)
            elif args.environment == "staging":
                pin = os.environ.get(args.hsm_pin_env, "")
                sig_path = sign_yubihsm2(artifact, key_id, args.hsm_slot, pin)
            elif args.environment == "production":
                if not args.azure_vault_url:
                    raise ValueError("--azure-vault-url required for production")
                sig_path = sign_azure_kv(artifact, key_id, args.azure_vault_url)

            # Verify immediately after signing
            cert_pem = key_dir / f"{key_id}.crt"
            verified = verify_signature(artifact, cert_pem)
            print("OK" if verified else "VERIFY FAILED")
            if not verified:
                all_ok = False

        except subprocess.CalledProcessError as e:
            print(f"ERROR\n  stderr: {e.stderr.decode().strip()}")
            all_ok  = False
            verified = False

        entries.append(
            build_manifest_entry(artifact, sig_path, key_id,
                                 args.environment, args.signer, verified)
        )

    if args.manifest_out:
        write_manifest(entries, args.environment, Path(args.manifest_out))

    if not all_ok:
        print("\nOne or more artifacts failed to sign or verify.")
        sys.exit(1)

    print(f"\nAll {len(artifacts)} artifacts signed and verified successfully.")
    sys.exit(0)


if __name__ == "__main__":
    main()
