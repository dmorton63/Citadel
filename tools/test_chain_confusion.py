#!/usr/bin/env python3
"""
Chain-confusion negative test for signer hierarchy enforcement.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Test chain confusion rejection")
    ap.add_argument("--artifact", required=True, help="Artifact signer metadata JSON")
    ap.add_argument("--allowed-signer", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    path = Path(args.artifact)
    if not path.exists():
        print(f"[ERROR] metadata missing: {path}")
        return 1

    with open(path) as f:
        meta = json.load(f)

    actual = meta.get("signer_id", "")
    failures = []

    if actual != args.allowed_signer:
        failures.append(f"signer mismatch: {actual} != {args.allowed_signer}")

    if not meta.get("chain_root_trusted", False):
        failures.append("chain root is not trusted")

    status = "PASS" if not failures else "FAIL"
    print(f"Chain confusion test: {status}")
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
