#!/usr/bin/env python3
"""
Validate revocation list freshness and propagation status.
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def parse_utc(ts: str) -> datetime:
    if ts.endswith("Z"):
        ts = ts[:-1] + "+00:00"
    return datetime.fromisoformat(ts)


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify revocation list freshness")
    ap.add_argument("--status", required=True, help="Revocation status JSON")
    ap.add_argument("--max-age-hours", type=int, default=168)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    status_path = Path(args.status)
    if not status_path.exists():
        print(f"[ERROR] status file missing: {status_path}")
        return 1

    with open(status_path) as f:
        status = json.load(f)

    failures = []

    last_updated = status.get("last_updated_utc")
    if not last_updated:
        failures.append("missing last_updated_utc")
        age_hours = None
    else:
        try:
            updated = parse_utc(last_updated)
            age_hours = (datetime.now(timezone.utc) - updated).total_seconds() / 3600.0
            if age_hours > args.max_age_hours:
                failures.append(
                    f"revocation metadata stale: age {age_hours:.2f}h > {args.max_age_hours}h"
                )
        except ValueError:
            failures.append("invalid last_updated_utc format")
            age_hours = None

    envs = status.get("environments", [])
    for env in envs:
        name = env.get("name", "unknown")
        if not env.get("propagated", False):
            failures.append(f"revocation list not propagated: {name}")

    report = {
        "status": "PASS" if not failures else "FAIL",
        "age_hours": age_hours,
        "max_age_hours": args.max_age_hours,
        "failures": failures,
    }

    print(f"Status: {report['status']}")
    if age_hours is not None:
        print(f"Revocation age (hours): {age_hours:.2f}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
