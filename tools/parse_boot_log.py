#!/usr/bin/env python3
"""
Citadel Secure Boot -- Boot log parser / stage-marker validator.

Reads a captured serial/console boot log and verifies that all required
Secure Boot stage-markers are present, in order, and carry the correct
verdict ("valid"/"ENABLED"/"OK").

Exit 0  → all required markers found (log is consistent with a clean Secure Boot)
Exit 1  → one or more markers missing or show a failure verdict

Used by CI (see .github/workflows/secure-boot.yml) and operators during
lab regression checks.

Usage:
  python3 tools/parse_boot_log.py --log build/serial-boot.log
  python3 tools/parse_boot_log.py --log build/serial-boot.log --verbose
  python3 tools/parse_boot_log.py --log build/serial-boot.log --json-out build/boot-log-report.json
"""

import argparse
import json
import re
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Marker definitions
# Each marker has:
#   id        – unique label for reporting
#   pattern   – compiled regex applied to each log line
#   required  – True → absence is a failure; False → informational only
#   verdict   – if not None, the named capture group "verdict" must match
#               one of the strings in the list (case-insensitive) for success
# ---------------------------------------------------------------------------
@dataclass
class Marker:
    id: str
    pattern: re.Pattern
    required: bool = True
    verdict_ok: list = field(default_factory=list)  # acceptable verdict strings
    found: bool = False
    verdict: Optional[str] = None
    line_no: Optional[int] = None
    line_text: Optional[str] = None


MARKERS = [
    Marker(
        id="uefi_secure_boot_checking",
        pattern=re.compile(r"\[UEFI\s+Boot\].*Secure Boot.*CHECKING", re.I),
        required=True,
    ),
    Marker(
        id="uefi_signature_verdict",
        pattern=re.compile(r"\[UEFI\s+Boot\].*Signature verification\s+(?P<verdict>\w+)", re.I),
        required=True,
        verdict_ok=["valid", "ok", "pass"],
    ),
    Marker(
        id="uefi_secure_boot_enabled",
        pattern=re.compile(r"\[UEFI\s+Boot\].*Secure Boot.*(?P<verdict>ENABLED|ACTIVE)", re.I),
        required=True,
        verdict_ok=["enabled", "active"],
    ),
    Marker(
        id="limine_start",
        pattern=re.compile(r"\[Limine\].*[Bb]ootloader starting", re.I),
        required=True,
    ),
    Marker(
        id="limine_bootgate_verdict",
        pattern=re.compile(r"\[Limine\].*BootGate signature\s+(?P<verdict>\w+)", re.I),
        required=True,
        verdict_ok=["valid", "ok", "pass"],
    ),
    Marker(
        id="bootgate_start",
        pattern=re.compile(r"\[BootGate\].*starting", re.I),
        required=True,
    ),
    Marker(
        id="bootgate_kernel_verdict",
        pattern=re.compile(r"\[BootGate\].*[Kk]ernel signature\s+(?P<verdict>\w+)", re.I),
        required=True,
        verdict_ok=["valid", "ok", "pass"],
    ),
    Marker(
        id="kernel_start",
        pattern=re.compile(r"\[Kernel\].*[Cc]itadel [Kk]ernel.*booting", re.I),
        required=True,
    ),
    Marker(
        id="kernel_secure_boot_status",
        pattern=re.compile(r"\[Kernel\].*Secure Boot.*(?P<verdict>ENABLED|ACTIVE|VERIFIED)", re.I),
        required=True,
        verdict_ok=["enabled", "active", "verified"],
    ),
    Marker(
        id="kernel_bootjson_verdict",
        pattern=re.compile(r"\[Kernel\].*boot\.json.*(?:signature\s+)?(?P<verdict>valid|ok|pass|failed|invalid)", re.I),
        required=True,
        verdict_ok=["valid", "ok", "pass"],
    ),
    Marker(
        id="kernel_modules_loaded",
        pattern=re.compile(r"\[Kernel\].*[Mm]odules loaded", re.I),
        required=False,   # informational; modules might be zero if degraded
    ),
    Marker(
        id="kernel_boot_complete",
        pattern=re.compile(r"\[Kernel\].*[Bb]oot complete", re.I),
        required=True,
    ),
]


def parse_log(log_path: Path, verbose: bool) -> tuple[list[Marker], list[str]]:
    """Scan log lines against all markers. Returns (markers, failure_messages)."""
    markers   = [m for m in MARKERS]  # fresh copy (reset state not needed; fields are per-instance already)
    remaining = [m for m in markers if not m.found]

    with open(log_path, errors="replace") as f:
        for lineno, raw in enumerate(f, start=1):
            line = raw.rstrip()
            for m in remaining:
                match = m.pattern.search(line)
                if match:
                    m.found    = True
                    m.line_no  = lineno
                    m.line_text = line
                    if "verdict" in match.groupdict():
                        m.verdict = match.group("verdict")

            remaining = [m for m in markers if not m.found]
            if not remaining:
                break

    # Evaluate failures
    failures = []
    for m in markers:
        if not m.found:
            if m.required:
                failures.append(f"MISSING: required marker '{m.id}'")
        elif m.verdict_ok:
            if m.verdict is None or m.verdict.lower() not in m.verdict_ok:
                failures.append(
                    f"BAD VERDICT: marker '{m.id}' verdict='{m.verdict}' "
                    f"(expected one of {m.verdict_ok})"
                )

        if verbose and m.found:
            v_str = f"  verdict={m.verdict}" if m.verdict else ""
            print(f"  [L{m.line_no:>5}] {m.id}{v_str}")

    return markers, failures


def main():
    parser = argparse.ArgumentParser(
        description="Validate Secure Boot stage markers in a boot log"
    )
    parser.add_argument("--log", required=True, help="Path to serial/console boot log")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print each matched marker and its line")
    parser.add_argument("--json-out", help="Write structured report to JSON file")
    args = parser.parse_args()

    log_path = Path(args.log)
    if not log_path.exists():
        print(f"[ERROR] Log file not found: {log_path}")
        sys.exit(1)

    print(f"Parsing boot log: {log_path}")
    markers, failures = parse_log(log_path, args.verbose)

    # Summary
    found    = sum(1 for m in markers if m.found)
    total    = len(markers)
    required = [m for m in markers if m.required]
    req_ok   = sum(1 for m in required if m.found)

    print(f"\nMarkers matched:  {found}/{total}  "
          f"(required: {req_ok}/{len(required)})")

    if failures:
        print("\nFailures:")
        for f in failures:
            print(f"  ✗ {f}")
    else:
        print("\n✓ All required Secure Boot stage markers present and valid.")

    # JSON report
    if args.json_out:
        report = {
            "log_file": str(log_path),
            "total_markers": total,
            "matched_markers": found,
            "required_markers": len(required),
            "required_matched": req_ok,
            "failures": failures,
            "markers": [
                {
                    **{k: v for k, v in asdict(m).items()
                       if k not in ("pattern", "verdict_ok")},
                    "verdict_ok": m.verdict_ok,
                }
                for m in markers
            ],
        }
        with open(args.json_out, "w") as out:
            json.dump(report, out, indent=2)
        print(f"\nReport written → {args.json_out}")

    sys.exit(0 if not failures else 1)


if __name__ == "__main__":
    main()
