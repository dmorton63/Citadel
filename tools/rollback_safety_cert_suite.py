#!/usr/bin/env python3
"""
Rollback safety certification suite evaluator.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REQUIRED_CASES = [
    "firmware_downgrade",
    "key_rollback",
    "mixed_version_media",
]


def main() -> int:
    ap = argparse.ArgumentParser(description="Evaluate rollback safety certification cases")
    ap.add_argument("--results", required=True, help="JSON test case results")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    path = Path(args.results)
    if not path.exists():
        print(f"[ERROR] results missing: {path}")
        return 1

    with open(path) as f:
        data = json.load(f)

    failures = []
    for case in REQUIRED_CASES:
        status = data.get(case)
        if status != "PASS":
            failures.append(f"{case} not PASS")

    status = "PASS" if not failures else "FAIL"
    print(f"Rollback safety suite: {status}")
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
