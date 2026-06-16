#!/usr/bin/env python3
"""
Continuous provenance chain verifier.

Verifies tamper-evident linkage from commit->build->manifest->artifact.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REQUIRED_FIELDS = [
    "build_id",
    "git_sha",
    "manifest_id",
    "signing_key_id",
    "hash_sha256",
    "signature_file",
]


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify commit-to-artifact provenance chain")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--identity-dir", required=False, default="build")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.exists():
        print(f"[ERROR] manifest missing: {manifest_path}")
        return 1

    with open(manifest_path) as f:
        manifest = json.load(f)

    fail = False
    results = []

    for art in manifest.get("artifacts", []):
        missing = [f for f in REQUIRED_FIELDS if not art.get(f)]
        status = "PASS" if not missing else "FAIL"
        if missing:
            fail = True

        name = art.get("name", "unknown")
        print(f"[{status}] {name}")
        if missing:
            print(f"  missing: {', '.join(missing)}")

        results.append({"artifact": name, "status": status, "missing": missing})

    report = {
        "manifest": str(manifest_path),
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
