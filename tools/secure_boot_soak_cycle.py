#!/usr/bin/env python3
"""
Secure Boot/recovery soak cycle validator.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate soak cycle stability report")
    ap.add_argument("--report", required=True, help="Soak report JSON")
    ap.add_argument("--min-cycles", type=int, default=50)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    path = Path(args.report)
    if not path.exists():
        print(f"[ERROR] soak report missing: {path}")
        return 1

    with open(path) as f:
        report = json.load(f)

    failures = []
    cycles = int(report.get("cycles_completed", 0))
    drift_events = int(report.get("policy_drift_events", 999999))
    state_leakage = bool(report.get("state_leakage_detected", True))

    if cycles < args.min_cycles:
        failures.append(f"insufficient cycles: {cycles} < {args.min_cycles}")
    if drift_events > 0:
        failures.append(f"policy drift events detected: {drift_events}")
    if state_leakage:
        failures.append("state leakage detected")

    status = "PASS" if not failures else "FAIL"
    print(f"Soak cycle validation: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    out_report = {"status": status, "failures": failures, "cycles_completed": cycles}
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(out_report, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
