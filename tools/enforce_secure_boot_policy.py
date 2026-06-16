#!/usr/bin/env python3
"""
Policy-as-code enforcement for Secure Boot operating rules.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REQUIRED_CONTEXT_FLAGS = [
    "secure_boot_enabled",
    "signed_boot_artifacts_present",
    "dual_control_enforced",
    "provenance_chain_valid",
    "release_block_on_nonconformance",
    "policy_version_declared",
]


def load_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser(description="Enforce Secure Boot policy-as-code checks")
    ap.add_argument("--rules", required=True, help="Policy rules JSON")
    ap.add_argument("--context", required=True, help="Observed context JSON")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    rules_path = Path(args.rules)
    context_path = Path(args.context)

    if not rules_path.exists():
        print(f"[ERROR] rules file not found: {rules_path}")
        return 1
    if not context_path.exists():
        print(f"[ERROR] context file not found: {context_path}")
        return 1

    rules = load_json(rules_path).get("rules", {})
    context = load_json(context_path)

    failures = []

    for flag in REQUIRED_CONTEXT_FLAGS:
        if flag not in context:
            failures.append(f"missing context flag: {flag}")

    mapping = {
        "require_secure_boot_enabled": "secure_boot_enabled",
        "require_signed_boot_artifacts": "signed_boot_artifacts_present",
        "require_dual_control_for_production_signing": "dual_control_enforced",
        "require_provenance_chain": "provenance_chain_valid",
        "block_release_on_nonconformance": "release_block_on_nonconformance",
        "require_policy_version_declaration": "policy_version_declared",
    }

    for rule_key, context_key in mapping.items():
        if rules.get(rule_key, False) and not context.get(context_key, False):
            failures.append(f"rule violated: {rule_key} (context {context_key}=false)")

    max_age = int(rules.get("max_revocation_list_age_hours", 168))
    observed_age = int(context.get("revocation_list_age_hours", 10**9))
    if rules.get("require_revocation_list_fresh", False) and observed_age > max_age:
        failures.append(
            f"rule violated: revocation freshness {observed_age}h exceeds {max_age}h"
        )

    status = "PASS" if not failures else "FAIL"
    print(f"Status: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    report = {
        "status": status,
        "rules_file": str(rules_path),
        "context_file": str(context_path),
        "failures": failures,
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
