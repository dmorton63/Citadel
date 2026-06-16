#!/usr/bin/env python3
"""
Evaluate fleet-level Secure Boot health scorecard thresholds.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Secure Boot health scorecard")
    ap.add_argument("--metrics", required=True)
    ap.add_argument("--thresholds", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    metrics_path = Path(args.metrics)
    thresholds_path = Path(args.thresholds)

    if not metrics_path.exists() or not thresholds_path.exists():
        print("[ERROR] missing metrics or thresholds file")
        return 1

    with open(metrics_path) as f:
        metrics = json.load(f)
    with open(thresholds_path) as f:
        thresholds = json.load(f).get("thresholds", {})

    failures = []

    def check_min(key: str) -> None:
        val = float(metrics.get(key, -1.0))
        min_val = float(thresholds.get(f"{key}_min", 0.0))
        if val < min_val:
            failures.append(f"{key} below threshold: {val} < {min_val}")

    def check_max(metric_key: str, threshold_key: str) -> None:
        val = float(metrics.get(metric_key, 10**9))
        max_val = float(thresholds.get(threshold_key, 10**9))
        if val > max_val:
            failures.append(f"{metric_key} above threshold: {val} > {max_val}")

    check_min("fleet_conformance_rate")
    check_min("policy_compliance_rate")
    check_min("drill_success_rate")
    check_max("revocation_freshness_hours", "revocation_freshness_hours_max")
    check_max("signing_incident_mttr_hours", "signing_incident_mttr_hours_max")

    status = "PASS" if not failures else "FAIL"
    print(f"Scorecard status: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    report = {
        "status": status,
        "failures": failures,
        "metrics": metrics,
        "thresholds": thresholds,
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
