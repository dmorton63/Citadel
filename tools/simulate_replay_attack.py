#!/usr/bin/env python3
"""
Replay-attack simulation for Secure Boot artifacts.
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
    ap = argparse.ArgumentParser(description="Simulate replay attack policy checks")
    ap.add_argument("--artifact", required=True, help="Artifact metadata JSON")
    ap.add_argument("--max-age-hours", type=int, default=24)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    path = Path(args.artifact)
    if not path.exists():
        print(f"[ERROR] artifact metadata missing: {path}")
        return 1

    with open(path) as f:
        meta = json.load(f)

    failures = []

    signed_at_raw = meta.get("signed_at_utc")
    if not signed_at_raw:
        failures.append("missing signed_at_utc")
    else:
        try:
            signed_at = parse_utc(signed_at_raw)
            age_hours = (datetime.now(timezone.utc) - signed_at).total_seconds() / 3600.0
            if age_hours > args.max_age_hours:
                failures.append(f"artifact age {age_hours:.2f}h exceeds {args.max_age_hours}h")
        except ValueError:
            failures.append("invalid signed_at_utc format")

    if meta.get("nonce_reused", False):
        failures.append("nonce reuse detected")

    if not meta.get("policy_epoch_match", False):
        failures.append("policy epoch mismatch")

    status = "PASS" if not failures else "FAIL"
    print(f"Replay simulation: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    report = {"status": status, "failures": failures}
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
