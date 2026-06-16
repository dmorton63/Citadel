#!/usr/bin/env python3
"""
Verify Secure Boot program closeout and sustainment transition gate artifacts.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify Secure Boot closeout package")
    ap.add_argument("--policy", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    policy_path = Path(args.policy)
    if not policy_path.exists():
        print(f"[ERROR] policy missing: {policy_path}")
        return 1

    policy = load_json(policy_path)
    failures = []

    for doc_path in policy.get("required_docs", []):
        p = Path(doc_path)
        if not p.exists():
            failures.append(f"missing required doc: {doc_path}")

    gate_path = Path("docs/SECURE_BOOT_SUSTAINMENT_TRANSITION_GATE.md")
    if gate_path.exists():
        gate_text = gate_path.read_text()
        for marker in policy.get("required_signoff_markers", []):
            if marker not in gate_text:
                failures.append(f"missing signoff marker: {marker}")
    else:
        failures.append("missing sustainment transition gate doc")

    status = "PASS" if not failures else "FAIL"
    print(f"Closeout verification: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    report = {
        "status": status,
        "failures": failures,
        "policy": str(policy_path),
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
