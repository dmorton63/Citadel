#!/usr/bin/env python3
"""
Fleet Secure Boot conformance scanner.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Scan fleet Secure Boot conformance")
    ap.add_argument("--fleet", required=True, help="Fleet state JSON")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    fleet_path = Path(args.fleet)
    if not fleet_path.exists():
        print(f"[ERROR] fleet file missing: {fleet_path}")
        return 1

    with open(fleet_path) as f:
        fleet = json.load(f)

    devices = fleet.get("devices", [])
    nonconformant = []

    for d in devices:
        reasons = []
        if d.get("key_state") != "valid":
            reasons.append("invalid key_state")
        if d.get("firmware_policy_mode") not in {"enforced", "audit"}:
            reasons.append("unsupported firmware policy mode")
        if not d.get("approved_version_alignment", False):
            reasons.append("unapproved baseline")

        if reasons:
            nonconformant.append({"device_id": d.get("device_id", "unknown"), "reasons": reasons})

    total = len(devices)
    bad = len(nonconformant)
    good = total - bad
    rate = 0.0 if total == 0 else good / total

    print(f"Fleet devices: {total}")
    print(f"Conformant: {good}")
    print(f"Non-conformant: {bad}")
    print(f"Conformance rate: {rate:.4f}")

    for item in nonconformant:
        print(f"[FAIL] {item['device_id']}: {', '.join(item['reasons'])}")

    result = {
        "total": total,
        "conformant": good,
        "nonconformant": bad,
        "conformance_rate": rate,
        "nonconformant_devices": nonconformant,
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(result, f, indent=2)

    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
