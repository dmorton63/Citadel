#!/usr/bin/env python3
"""
Auditable policy promotion gate for lab/staging/production.
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
    ap = argparse.ArgumentParser(description="Enforce policy promotion approval gate")
    ap.add_argument("--policy-versions", required=True)
    ap.add_argument("--request", required=True)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    p_path = Path(args.policy_versions)
    r_path = Path(args.request)
    if not p_path.exists() or not r_path.exists():
        print("[ERROR] missing policy versions or request file")
        return 1

    with open(p_path) as f:
        policy = json.load(f)
    with open(r_path) as f:
        req = json.load(f)

    failures = []

    version = req.get("policy_version")
    from_env = req.get("from_env")
    to_env = req.get("to_env")
    requester = req.get("requester")
    approvers = req.get("approvers", [])
    ticket = req.get("ticket_id")
    expires_at = req.get("expires_at")

    if not ticket:
        failures.append("ticket_id is required")

    version_obj = None
    for item in policy.get("versions", []):
        if item.get("version") == version:
            version_obj = item
            break

    if not version_obj:
        failures.append(f"unknown policy version: {version}")
    else:
        step = f"{from_env}->{to_env}"
        if step not in version_obj.get("allowed_promotions", []):
            failures.append(f"promotion step not allowed for version: {step}")

    min_approvers = int(policy.get("minimum_approvers", 2))
    if len(approvers) < min_approvers:
        failures.append(f"insufficient approvers: {len(approvers)} < {min_approvers}")

    if len(set(approvers)) != len(approvers):
        failures.append("approvers must be unique")

    if policy.get("require_distinct_requester", True) and requester in approvers:
        failures.append("requester cannot also be an approver")

    if not expires_at:
        failures.append("expires_at is required")
    else:
        try:
            expiry = parse_utc(expires_at)
            if expiry <= datetime.now(timezone.utc):
                failures.append("approval request has expired")
        except ValueError:
            failures.append("expires_at must be ISO-8601 UTC")

    status = "APPROVED" if not failures else "DENIED"
    print(f"Promotion gate: {status}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    report = {
        "status": status,
        "request": req,
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
