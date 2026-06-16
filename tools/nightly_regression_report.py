#!/usr/bin/env python3
"""
Nightly Secure Boot regression trend tracker.

Consumes one nightly run result JSON and appends/updates a trend file.
Raises non-zero exit when alert thresholds are exceeded.

Input run JSON schema (minimal):
{
  "date": "2026-06-16",
  "total": 120,
  "passed": 118,
  "failed": 2,
  "critical_failed": 0,
  "duration_sec": 904
}
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_json(path: Path, default):
    if not path.exists():
        return default
    with open(path) as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser(description="Update nightly SB regression trend and enforce thresholds")
    ap.add_argument("--run", required=True, help="Nightly run JSON")
    ap.add_argument("--trend", required=True, help="Trend JSON path")
    ap.add_argument("--max-fail-rate", type=float, default=0.02, help="Max allowed failure rate")
    ap.add_argument("--max-critical", type=int, default=0, help="Max allowed critical failures")
    args = ap.parse_args()

    run_path = Path(args.run)
    if not run_path.exists():
        print(f"[ERROR] run file missing: {run_path}")
        return 1

    run = load_json(run_path, {})
    trend_path = Path(args.trend)
    trend = load_json(trend_path, {"runs": []})

    total = int(run.get("total", 0))
    failed = int(run.get("failed", 0))
    critical_failed = int(run.get("critical_failed", 0))
    fail_rate = (failed / total) if total else 1.0

    run["fail_rate"] = fail_rate
    trend["runs"].append(run)
    trend["runs"] = trend["runs"][-90:]  # keep rolling 90-day window

    trend_path.parent.mkdir(parents=True, exist_ok=True)
    with open(trend_path, "w") as f:
        json.dump(trend, f, indent=2)

    print(f"Run date:      {run.get('date')}")
    print(f"Total tests:   {total}")
    print(f"Failed tests:  {failed}")
    print(f"Critical fail: {critical_failed}")
    print(f"Fail rate:     {fail_rate:.4f}")
    print(f"Trend file:    {trend_path}")

    alert = False
    if fail_rate > args.max_fail_rate:
        print(f"[ALERT] fail_rate {fail_rate:.4f} exceeds threshold {args.max_fail_rate:.4f}")
        alert = True
    if critical_failed > args.max_critical:
        print(f"[ALERT] critical_failed {critical_failed} exceeds threshold {args.max_critical}")
        alert = True

    return 1 if alert else 0


if __name__ == "__main__":
    raise SystemExit(main())
