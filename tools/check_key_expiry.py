#!/usr/bin/env python3
"""
Citadel Secure Boot - key expiry / validity alerting.

Scans certificate files and emits alerts when expiry is within threshold.

Usage:
  python3 tools/check_key_expiry.py --cert-dir /tmp/citadel-lab-keys --warn-days 90
  python3 tools/check_key_expiry.py --cert-dir /etc/citadel/keys --json-out build/key-expiry-report.json
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def openssl_not_after(cert_path: Path) -> datetime:
    result = subprocess.run(
        ["openssl", "x509", "-in", str(cert_path), "-noout", "-enddate"],
        capture_output=True,
        text=True,
        check=True,
    )
    line = result.stdout.strip()
    # Format: notAfter=Jun 16 12:00:00 2027 GMT
    value = line.split("=", 1)[1].strip()
    dt = datetime.strptime(value, "%b %d %H:%M:%S %Y %Z")
    return dt.replace(tzinfo=timezone.utc)


def main() -> int:
    parser = argparse.ArgumentParser(description="Secure Boot cert expiry alerting")
    parser.add_argument("--cert-dir", required=True, help="Directory containing .crt/.pem certs")
    parser.add_argument("--warn-days", type=int, default=90, help="Warn when expiry is <= this many days")
    parser.add_argument("--json-out", help="Optional JSON report output path")
    args = parser.parse_args()

    cert_dir = Path(args.cert_dir)
    if not cert_dir.exists():
        print(f"[ERROR] cert-dir not found: {cert_dir}")
        return 1

    certs = sorted(list(cert_dir.glob("*.crt")) + list(cert_dir.glob("*.pem")))
    if not certs:
        print(f"[WARN] no certificates found in {cert_dir}")
        return 0

    now = datetime.now(timezone.utc)
    report = []
    rc = 0

    print(f"Checking {len(certs)} certificate(s) in {cert_dir}")
    for cert in certs:
        try:
            expiry = openssl_not_after(cert)
        except Exception as exc:  # pragma: no cover
            print(f"[FAIL] {cert.name}: cannot parse certificate ({exc})")
            rc = 1
            continue

        days_left = (expiry - now).days
        status = "OK"
        if days_left < 0:
            status = "EXPIRED"
            rc = 1
        elif days_left <= args.warn_days:
            status = "WARN"
            rc = 1

        print(f"[{status:7}] {cert.name:35} expires={expiry.isoformat()} days_left={days_left}")
        report.append(
            {
                "cert": cert.name,
                "expires_at": expiry.isoformat(),
                "days_left": days_left,
                "status": status,
            }
        )

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump({"generated_at": now.isoformat(), "certificates": report}, f, indent=2)
        print(f"Report -> {out}")

    if rc != 0:
        print("Expiry alert triggered (WARN/EXPIRED present).")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
