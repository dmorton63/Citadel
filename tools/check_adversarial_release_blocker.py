#!/usr/bin/env python3
"""
Release blocker that requires all adversarial checks to pass.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser(description="Check adversarial release blocker")
    ap.add_argument("--policy", required=True)
    ap.add_argument("--reports", required=True, help="Combined report map JSON")
    ap.add_argument("--certificate", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    policy_path = Path(args.policy)
    reports_path = Path(args.reports)
    cert_path = Path(args.certificate)

    if not policy_path.exists() or not reports_path.exists():
        print("[ERROR] missing policy or reports")
        return 1

    policy = load_json(policy_path)
    reports = load_json(reports_path)

    failures = []

    for name in policy.get("required_reports", []):
        if reports.get(name) != "PASS":
            failures.append(f"required report not PASS: {name}={reports.get(name)}")

    if policy.get("require_certificate_signoff", True):
        if not cert_path.exists():
            failures.append("rollback safety certificate file missing")
        else:
            content = cert_path.read_text()
            required_markers = [
                "Security Engineering:",
                "Verification Team:",
                "Release Engineering:",
                "Final approver:",
            ]
            for marker in required_markers:
                if marker not in content:
                    failures.append(f"certificate missing marker: {marker}")

    status = "PASS" if not failures else "FAIL"
    print(f"Adversarial release blocker: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    out_data = {"status": status, "failures": failures}
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(out_data, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
