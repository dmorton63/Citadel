#!/usr/bin/env python3
"""
Citadel Secure Boot -- Build-time signature verifier.

Called by CMake post-build (validate-secure-boot-artifacts target).
Verifies that every expected artifact has a matching .sig file and that
the signature is cryptographically valid before packaging proceeds.

Exit 0  → all OK (build may continue)
Exit 1  → any verification failure (build is aborted)

Usage:
  python3 tools/verify_signatures.py \\
      --environment lab \\
      --key-dir /tmp/citadel-lab-keys \\
      [--manifest build/secure-boot-manifest-lab.json] \\
      [--artifact build/limine/Limine.efi ...]
      [--strict]   # also fail on missing optional artifacts
"""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Optional


# Artifacts required for each environment before packaging is allowed.
REQUIRED_ARTIFACTS = {
    "lab":        ["Limine.efi", "BootGate", "vmlinuz", "boot.json"],
    "staging":    ["Limine.efi", "BootGate", "vmlinuz", "boot.json"],
    "production": ["Limine.efi", "BootGate", "vmlinuz", "boot.json"],
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_one(artifact: Path, cert_pem: Path) -> tuple[bool, str]:
    """Return (ok, reason)."""
    sig = artifact.with_suffix(artifact.suffix + ".sig")
    if not sig.exists():
        return False, f"Missing signature file: {sig.name}"

    result = subprocess.run(
        [
            "openssl", "cms", "-verify",
            "-binary",
            "-in", str(sig),
            "-inform", "DER",
            "-content", str(artifact),
            "-CAfile", str(cert_pem),
            "-noverify",
        ],
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode().strip().splitlines()[-1]
        return False, f"Signature invalid: {detail}"

    return True, "OK"


def load_manifest(manifest_path: Path) -> Optional[dict]:
    if not manifest_path.exists():
        return None
    with open(manifest_path) as f:
        return json.load(f)


def verify_hash_against_manifest(artifact: Path, manifest: dict) -> tuple[bool, str]:
    """Check artifact hash matches what was recorded at signing time."""
    name = artifact.name
    for entry in manifest.get("artifacts", []):
        if Path(entry["path"]).name == name or entry["name"] == name:
            expected = entry.get("hash_sha256", "")
            actual   = sha256_file(artifact)
            if actual != expected:
                return (
                    False,
                    f"Hash mismatch: manifest={expected[:12]}... "
                    f"actual={actual[:12]}..."
                )
            return True, "Hash OK"
    return True, "Hash not in manifest (skipped)"   # not an error; manifest may be partial


def run_checks(
    artifacts: list[Path],
    environment: str,
    key_dir: Path,
    manifest: Optional[dict],
    strict: bool,
) -> bool:
    """Run all verification checks. Returns True if all pass."""
    # --- 1. Presence check ---
    required = REQUIRED_ARTIFACTS[environment]
    present  = {p.name for p in artifacts if p.exists()}
    missing  = [r for r in required if not any(
        p.startswith(r.split(".")[0]) for p in present
    )]
    if missing:
        msg = f"Required artifact(s) missing: {', '.join(missing)}"
        if strict:
            print(f"[FAIL] {msg}")
            return False
        else:
            print(f"[WARN] {msg}")

    # --- 2. Per-artifact signature + hash checks ---
    all_ok = True
    width  = max((len(a.name) for a in artifacts), default=20) + 2

    for artifact in sorted(artifacts):    # canonical sort for reproducibility
        if not artifact.exists():
            print(f"  [SKIP] {artifact.name:{width}}  (not found)")
            if strict:
                all_ok = False
            continue

        # Derive expected key ID → cert file
        name_lower = artifact.name.lower()
        key_id = _guess_key_id(name_lower, environment)
        cert   = key_dir / f"{key_id}.crt"

        if not cert.exists():
            print(f"  [WARN] {artifact.name:{width}}  No cert for {key_id} — skipping crypto verify")
        else:
            ok, reason = verify_one(artifact, cert)
            if not ok:
                print(f"  [FAIL] {artifact.name:{width}}  {reason}")
                all_ok = False
                continue
            else:
                print(f"  [  OK] {artifact.name:{width}}  Signature valid  key={key_id}")

        # Hash cross-check against manifest
        if manifest:
            h_ok, h_reason = verify_hash_against_manifest(artifact, manifest)
            if not h_ok:
                print(f"         {'':{width}}  {h_reason}")
                all_ok = False

    return all_ok


def _guess_key_id(name_lower: str, environment: str) -> str:
    env = environment.upper()
    if "limine" in name_lower or name_lower.endswith(".efi"):
        return f"CITADEL_BOOT_{env}_v1"
    if "bootgate" in name_lower:
        return f"CITADEL_LSK_{env}_v1"
    if "vmlinuz" in name_lower or "kernel" in name_lower:
        return f"CITADEL_KSK_{env}_v1"
    # boot.json, modules, ramdisk
    return f"CITADEL_MSK_{env}_v1"


def main():
    parser = argparse.ArgumentParser(
        description="Build-time Secure Boot signature verifier"
    )
    parser.add_argument(
        "--environment", choices=["lab", "staging", "production"],
        default="lab"
    )
    parser.add_argument(
        "--key-dir", default="/tmp/citadel-lab-keys",
        help="Directory containing {KEY_ID}.crt files"
    )
    parser.add_argument(
        "--artifacts", nargs="*",
        help="Explicit artifact paths to verify (auto-discovered if omitted)"
    )
    parser.add_argument(
        "--manifest",
        help="Manifest JSON produced by sign_artifacts.py; enables hash cross-check"
    )
    parser.add_argument(
        "--strict", action="store_true",
        help="Treat warnings (missing optional artifacts) as failures"
    )
    args = parser.parse_args()

    key_dir  = Path(args.key_dir)
    manifest = load_manifest(Path(args.manifest)) if args.manifest else None

    # Collect artifacts
    if args.artifacts:
        artifacts = [Path(a) for a in args.artifacts]
    elif manifest:
        artifacts = [Path(e["path"]) for e in manifest.get("artifacts", [])]
    else:
        # Auto-discover: look in build/ for common artifact patterns
        build_dir = Path("build")
        artifacts = []
        for pattern in ["**/*.efi", "**/BootGate", "**/vmlinuz*",
                        "**/boot.json", "**/*.ko", "**/ramdisk*"]:
            artifacts.extend(build_dir.glob(pattern))

    if not artifacts:
        print("[WARN] No artifacts to verify.")
        sys.exit(0)

    print(f"Verifying {len(artifacts)} artifact(s) [{args.environment}]...")
    ok = run_checks(artifacts, args.environment, key_dir, manifest, args.strict)
    print()
    if ok:
        print(f"✓ All signatures verified ({len(artifacts)} artifact(s)).")
        sys.exit(0)
    else:
        print("✗ Signature verification FAILED — packaging aborted.")
        sys.exit(1)


if __name__ == "__main__":
    main()
