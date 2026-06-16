#!/usr/bin/env python3
"""
Citadel Secure Boot - enrolled key drift detection.

Compares live firmware key inventory to approved key manifest.
Input is text exports for portability.

Usage:
  python3 tools/detect_key_drift.py \
    --approved build/approved-key-fingerprints.txt \
    --live build/live-key-fingerprints.txt \
    --json-out build/key-drift-report.json
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_set(path: Path) -> set[str]:
    values = set()
    with open(path) as f:
        for line in f:
            value = line.strip()
            if not value or value.startswith("#"):
                continue
            values.add(value)
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description="Detect enrolled-key drift")
    parser.add_argument("--approved", required=True, help="Approved key fingerprint file")
    parser.add_argument("--live", required=True, help="Live firmware key fingerprint file")
    parser.add_argument("--json-out", help="Optional JSON report")
    args = parser.parse_args()

    approved_path = Path(args.approved)
    live_path = Path(args.live)
    if not approved_path.exists() or not live_path.exists():
        print("[ERROR] approved/live file missing")
        return 1

    approved = load_set(approved_path)
    live = load_set(live_path)

    missing = sorted(approved - live)
    unexpected = sorted(live - approved)

    print(f"Approved keys:   {len(approved)}")
    print(f"Live keys:       {len(live)}")
    print(f"Missing keys:    {len(missing)}")
    print(f"Unexpected keys: {len(unexpected)}")

    if missing:
        print("\n[DRIFT] Missing approved keys:")
        for item in missing:
            print(f"  - {item}")

    if unexpected:
        print("\n[DRIFT] Unexpected live keys:")
        for item in unexpected:
            print(f"  - {item}")

    report = {
        "approved_count": len(approved),
        "live_count": len(live),
        "missing": missing,
        "unexpected": unexpected,
        "drift_detected": bool(missing or unexpected),
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)
        print(f"Report -> {out}")

    return 1 if report["drift_detected"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
