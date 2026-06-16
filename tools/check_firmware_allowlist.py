#!/usr/bin/env python3
"""
Firmware allowlist gate for Secure Boot validation.

Given a discovered firmware version and policy file, blocks unapproved baselines.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Check firmware version against allowlist")
    ap.add_argument("--allowlist", required=True, help="JSON allowlist file")
    ap.add_argument("--vendor", required=True)
    ap.add_argument("--version", required=True)
    args = ap.parse_args()

    allowlist_path = Path(args.allowlist)
    if not allowlist_path.exists():
        print(f"[ERROR] allowlist missing: {allowlist_path}")
        return 1

    with open(allowlist_path) as f:
        policy = json.load(f)

    vendor_map = policy.get("vendors", {})
    allowed_versions = vendor_map.get(args.vendor, [])

    if args.version in allowed_versions:
        print(f"[OK] firmware allowed: vendor={args.vendor} version={args.version}")
        return 0

    print(f"[BLOCK] firmware NOT allowlisted: vendor={args.vendor} version={args.version}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
