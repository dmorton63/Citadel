#!/usr/bin/env python3
"""
Signer environment integrity attestation check.

Validates signer host baseline before key access.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
from pathlib import Path


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout.strip()


def main() -> int:
    ap = argparse.ArgumentParser(description="Check signer environment baseline")
    ap.add_argument("--policy", required=True, help="Baseline policy JSON")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    policy_path = Path(args.policy)
    if not policy_path.exists():
        print(f"[ERROR] policy missing: {policy_path}")
        return 1

    with open(policy_path) as f:
        policy = json.load(f)

    observed = {
        "os": platform.system(),
        "kernel": platform.release(),
        "python": platform.python_version(),
        "secure_boot_hint": os.path.exists("/sys/firmware/efi"),
        "hostname": platform.node(),
    }

    checks = []

    expected_os = policy.get("os")
    if expected_os:
        checks.append(("os", observed["os"] == expected_os, observed["os"], expected_os))

    kernel_prefix = policy.get("kernel_prefix")
    if kernel_prefix:
        checks.append(("kernel_prefix", observed["kernel"].startswith(kernel_prefix), observed["kernel"], kernel_prefix))

    require_efi = policy.get("require_efi", True)
    if require_efi:
        checks.append(("efi_present", observed["secure_boot_hint"] is True, observed["secure_boot_hint"], True))

    required_tools = policy.get("required_tools", [])
    for tool in required_tools:
        path = run(["bash", "-lc", f"command -v {tool}"])
        checks.append((f"tool:{tool}", bool(path), path or "", "installed"))

    passed = True
    for name, ok, actual, expected in checks:
        if ok:
            print(f"[OK] {name}: actual={actual}")
        else:
            print(f"[FAIL] {name}: actual={actual} expected={expected}")
            passed = False

    result = {
        "observed": observed,
        "checks": [
            {"name": n, "ok": ok, "actual": a, "expected": e}
            for n, ok, a, e in checks
        ],
        "status": "PASS" if passed else "FAIL",
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(result, f, indent=2)
        print(f"Report -> {out}")

    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
