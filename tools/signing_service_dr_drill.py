#!/usr/bin/env python3
"""
Annual signing-service disaster recovery drill recorder.

Captures measured recovery objectives and pass/fail outcomes.
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Record signing-service DR drill metrics")
    ap.add_argument("--scenario", required=True, help="Scenario label")
    ap.add_argument("--rto-min", type=int, required=True, help="Measured recovery time objective in minutes")
    ap.add_argument("--key-recovery-min", type=int, required=True)
    ap.add_argument("--attestation-restore-min", type=int, required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    # Targets from Batch 7 policy
    target_rto = 240
    target_key = 120
    target_att = 60

    status = "PASS"
    failures = []

    if args.rto_min > target_rto:
        status = "FAIL"
        failures.append(f"rto_min {args.rto_min} > {target_rto}")
    if args.key_recovery_min > target_key:
        status = "FAIL"
        failures.append(f"key_recovery_min {args.key_recovery_min} > {target_key}")
    if args.attestation_restore_min > target_att:
        status = "FAIL"
        failures.append(f"attestation_restore_min {args.attestation_restore_min} > {target_att}")

    report = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "scenario": args.scenario,
        "measured": {
            "rto_min": args.rto_min,
            "key_recovery_min": args.key_recovery_min,
            "attestation_restore_min": args.attestation_restore_min
        },
        "targets": {
            "rto_min": target_rto,
            "key_recovery_min": target_key,
            "attestation_restore_min": target_att
        },
        "status": status,
        "failures": failures
    }

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w") as f:
        json.dump(report, f, indent=2)

    print(f"Report -> {out}")
    print(f"Status -> {status}")
    if failures:
        for fmsg in failures:
            print(f"[FAIL] {fmsg}")

    return 1 if status == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
