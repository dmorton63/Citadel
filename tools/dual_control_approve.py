#!/usr/bin/env python3
"""
Dual-control approval enforcement for production signing.

Validates requester and approver are distinct and in approved roster.
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate dual-control approval payload")
    ap.add_argument("--requester", required=True)
    ap.add_argument("--approver", required=True)
    ap.add_argument("--roster", required=True, help="JSON array of approved operator identities")
    ap.add_argument("--ttl-minutes", type=int, default=60)
    ap.add_argument("--out", help="Optional approval token output JSON")
    args = ap.parse_args()

    roster_path = Path(args.roster)
    if not roster_path.exists():
        print(f"[ERROR] roster file missing: {roster_path}")
        return 1

    with open(roster_path) as f:
        roster = json.load(f)

    if args.requester == args.approver:
        print("[FAIL] requester and approver must be different identities")
        return 1

    if args.requester not in roster:
        print(f"[FAIL] requester not in approved roster: {args.requester}")
        return 1

    if args.approver not in roster:
        print(f"[FAIL] approver not in approved roster: {args.approver}")
        return 1

    issued = datetime.now(timezone.utc)
    expires = issued + timedelta(minutes=args.ttl_minutes)

    token = {
        "requester": args.requester,
        "approver": args.approver,
        "issued_at": issued.isoformat(),
        "expires_at": expires.isoformat(),
        "status": "APPROVED",
    }

    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(token, f, indent=2)
        print(f"[OK] dual-control approval token -> {out}")
    else:
        print("[OK] dual-control approved")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
