#!/usr/bin/env python3
"""
Citadel Secure Boot - signed boot-component update attestation.

Verifies every artifact in a manifest has a valid detached signature and that
the signer key matches the expected environment assignment.

Usage:
  python3 tools/attest_update_chain.py \
    --manifest build/secure-boot-manifest-release.json \
    --environment staging \
    --key-dir /etc/citadel/keys \
    --json-out build/update-attestation.json
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

EXPECTED = {
    "lab": {
        "limine": "CITADEL_BOOT_LAB_v1",
        "bootgate": "CITADEL_LSK_LAB_v1",
        "vmlinuz": "CITADEL_KSK_LAB_v1",
        "boot.json": "CITADEL_MSK_LAB_v1",
        ".ko": "CITADEL_MSK_LAB_v1",
    },
    "staging": {
        "limine": "CITADEL_BOOT_STAGING_v1",
        "bootgate": "CITADEL_LSK_STAGING_v1",
        "vmlinuz": "CITADEL_KSK_STAGING_v1",
        "boot.json": "CITADEL_MSK_STAGING_v1",
        ".ko": "CITADEL_MSK_STAGING_v1",
    },
    "production": {
        "limine": "CITADEL_BOOT_PROD_v1",
        "bootgate": "CITADEL_LSK_PROD_v1",
        "vmlinuz": "CITADEL_KSK_PROD_v1",
        "boot.json": "CITADEL_MSK_PROD_v1",
        ".ko": "CITADEL_MSK_PROD_v1",
    },
}


def expected_key(name: str, env: str) -> str | None:
    n = name.lower()
    for k, v in EXPECTED[env].items():
        if n.startswith(k) or n.endswith(k):
            return v
    return None


def verify(artifact: Path, cert: Path) -> bool:
    sig = artifact.with_suffix(artifact.suffix + ".sig")
    if not sig.exists() or not cert.exists():
        return False
    result = subprocess.run(
        [
            "openssl", "cms", "-verify", "-binary",
            "-in", str(sig), "-inform", "DER",
            "-content", str(artifact), "-CAfile", str(cert), "-noverify",
        ],
        capture_output=True,
    )
    return result.returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Attest signed update chain")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--environment", choices=["lab", "staging", "production"], required=True)
    ap.add_argument("--key-dir", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    manifest = Path(args.manifest)
    if not manifest.exists():
        print(f"[ERROR] manifest missing: {manifest}")
        return 1

    with open(manifest) as f:
        data = json.load(f)

    results = []
    fail = False

    for entry in data.get("artifacts", []):
        artifact = Path(entry["path"])
        signer = entry.get("signing_key_id")
        exp = expected_key(artifact.name, args.environment)
        cert = Path(args.key_dir) / f"{signer}.crt"
        sig_ok = verify(artifact, cert)
        signer_ok = signer == exp if exp else False
        ok = sig_ok and signer_ok
        fail = fail or (not ok)

        print(f"[{'OK' if ok else 'FAIL'}] {artifact.name:30} signer={signer} expected={exp} sig_ok={sig_ok}")
        results.append(
            {
                "artifact": artifact.name,
                "signer": signer,
                "expected_signer": exp,
                "signature_ok": sig_ok,
                "signer_ok": signer_ok,
                "status": "PASS" if ok else "FAIL",
            }
        )

    report = {
        "manifest": str(manifest),
        "environment": args.environment,
        "artifact_count": len(results),
        "status": "PASS" if not fail else "FAIL",
        "results": results,
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)
        print(f"Report -> {out}")

    return 1 if fail else 0


if __name__ == "__main__":
    raise SystemExit(main())
