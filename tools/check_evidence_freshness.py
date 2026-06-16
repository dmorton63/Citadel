#!/usr/bin/env python3
"""
Check evidence bundle freshness relative to release candidate build time.

Fails when any required evidence artifact is older than max age.
"""

from __future__ import annotations

import argparse
import os
from datetime import datetime, timezone
from pathlib import Path


def age_hours(path: Path) -> float:
    mtime = datetime.fromtimestamp(path.stat().st_mtime, tz=timezone.utc)
    now = datetime.now(timezone.utc)
    return (now - mtime).total_seconds() / 3600.0


def main() -> int:
    ap = argparse.ArgumentParser(description="Evidence freshness check")
    ap.add_argument("--max-age-hours", type=float, default=24)
    ap.add_argument("files", nargs="+", help="Evidence files that must be fresh")
    args = ap.parse_args()

    fail = False
    for f in args.files:
        p = Path(f)
        if not p.exists():
            print(f"[FAIL] missing evidence file: {p}")
            fail = True
            continue
        h = age_hours(p)
        if h > args.max_age_hours:
            print(f"[FAIL] stale evidence: {p} age={h:.2f}h > {args.max_age_hours:.2f}h")
            fail = True
        else:
            print(f"[OK] {p} age={h:.2f}h")

    return 1 if fail else 0


if __name__ == "__main__":
    raise SystemExit(main())
