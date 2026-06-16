#!/usr/bin/env python3
"""
Stale key discovery report.

Finds:
- soon-expiring keys
- expired keys
- orphaned certs (no usage in manifests)
- unused keys (never referenced)
"""

from __future__ import annotations

import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def cert_expiry(cert: Path):
    out = subprocess.run(
        ["openssl", "x509", "-in", str(cert), "-noout", "-enddate"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    value = out.split("=", 1)[1].strip()
    dt = datetime.strptime(value, "%b %d %H:%M:%S %Y %Z")
    return dt.replace(tzinfo=timezone.utc)


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate stale key report")
    ap.add_argument("--key-dir", required=True)
    ap.add_argument("--manifest-dir", required=True)
    ap.add_argument("--soon-days", type=int, default=90)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    key_dir = Path(args.key_dir)
    manifest_dir = Path(args.manifest_dir)

    certs = sorted(key_dir.glob("*.crt"))
    manifests = sorted(manifest_dir.glob("**/*manifest*.json"))

    referenced = set()
    for m in manifests:
        try:
            with open(m) as f:
                data = json.load(f)
            for art in data.get("artifacts", []):
                kid = art.get("signing_key_id")
                if kid:
                    referenced.add(kid)
        except Exception:
            continue

    now = datetime.now(timezone.utc)
    soon = []
    expired = []
    orphaned = []
    unused = []

    for cert in certs:
        key_id = cert.stem
        try:
            exp = cert_expiry(cert)
        except Exception:
            continue
        days_left = (exp - now).days

        if days_left < 0:
            expired.append({"key_id": key_id, "days_left": days_left})
        elif days_left <= args.soon_days:
            soon.append({"key_id": key_id, "days_left": days_left})

        if key_id not in referenced:
            orphaned.append({"key_id": key_id, "reason": "not referenced in manifests"})
            unused.append({"key_id": key_id})

    report = {
        "generated_at": now.isoformat(),
        "soon_expiring": soon,
        "expired": expired,
        "orphaned": orphaned,
        "unused": unused,
        "remediation_owner": {
            "soon_expiring": "Security Lead",
            "expired": "Security Lead + Operations Lead",
            "orphaned": "Engineering Lead",
            "unused": "Engineering Lead",
        },
    }

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w") as f:
        json.dump(report, f, indent=2)

    print(f"Report -> {out}")
    print(f"Soon expiring: {len(soon)} | Expired: {len(expired)} | Orphaned: {len(orphaned)}")

    # fail only on expired keys by default
    return 1 if expired else 0


if __name__ == "__main__":
    raise SystemExit(main())
