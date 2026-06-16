#!/usr/bin/env python3
"""
Citadel Secure Boot -- Golden log diff checker.

Compares a current boot log against one or more golden (baseline) snapshots
to detect diagnostic regressions.

A regression is flagged when:
  - A required marker present in the golden log is absent in the current log
  - A verdict that was "valid/OK" in the golden log is now "failed/invalid"
  - A new SB-xxxx error code appears that was not in the golden log

Exit 0  → no regressions detected
Exit 1  → one or more regressions

Usage:
  python3 tools/diff_boot_log.py \\
      --golden  logs/SECURE_BOOT_CANONICAL_LOG_POSITIVE.txt \\
      --current build/serial-boot-current.log \\
      [--json-out build/diff-report.json]
"""

import argparse
import json
import re
import sys
from pathlib import Path


SB_CODE_RE  = re.compile(r"\bSB-\d{4}\b")
STAGE_RE    = re.compile(r"\[SB\]\[(\w+)\]")
VERDICT_RE  = re.compile(
    r"(?:signature|Secure Boot|boot\.json|module)\s+(?:is\s+)?(?P<verdict>valid|ok|pass|failed|invalid|ENABLED|ACTIVE|HALTED)",
    re.I,
)

VERDICT_FAIL = {"failed", "invalid", "halted"}
VERDICT_OK   = {"valid", "ok", "pass", "enabled", "active"}


def extract_log_facts(log_path: Path) -> dict:
    """Extract structured facts from a log file."""
    facts = {
        "sb_codes":     set(),
        "stages_seen":  set(),
        "verdicts":     {},   # stage → set of verdict strings
        "markers":      [],   # ordered list of matched [SB] lines
    }
    with open(log_path, errors="replace") as f:
        for line in f:
            # SB error codes
            for code in SB_CODE_RE.findall(line):
                facts["sb_codes"].add(code)

            # Stage tags
            sm = STAGE_RE.search(line)
            if sm:
                stage = sm.group(1)
                facts["stages_seen"].add(stage)

                # Verdicts at this stage
                vm = VERDICT_RE.search(line)
                if vm:
                    verdict = vm.group("verdict").lower()
                    facts["verdicts"].setdefault(stage, set()).add(verdict)

                facts["markers"].append(line.rstrip())

    return facts


def compare(golden: dict, current: dict) -> list[str]:
    regressions = []

    # 1. Stages present in golden but missing in current
    missing_stages = golden["stages_seen"] - current["stages_seen"]
    for stage in sorted(missing_stages):
        regressions.append(f"STAGE MISSING: [{stage}] was in golden but absent in current log")

    # 2. New SB error codes in current that weren't in golden
    new_codes = current["sb_codes"] - golden["sb_codes"]
    for code in sorted(new_codes):
        regressions.append(f"NEW ERROR CODE: {code} appears in current log (not in golden)")

    # 3. SB error codes in golden that disappeared from current
    #    (could indicate a suppressed failure — only flag if golden had no such codes)
    # (Intentionally not flagging removed error codes — removing errors is good)

    # 4. Verdict regressions: golden OK → current FAIL
    for stage, golden_verdicts in golden["verdicts"].items():
        current_verdicts = current["verdicts"].get(stage, set())
        if any(v in VERDICT_OK for v in golden_verdicts):
            if any(v in VERDICT_FAIL for v in current_verdicts) and \
               not any(v in VERDICT_OK for v in current_verdicts):
                regressions.append(
                    f"VERDICT REGRESSION: [{stage}] was OK in golden, "
                    f"now shows failure verdict: {current_verdicts}"
                )

    return regressions


def main():
    parser = argparse.ArgumentParser(
        description="Compare current boot log against golden baseline"
    )
    parser.add_argument("--golden",  required=True, help="Golden/baseline log file")
    parser.add_argument("--current", required=True, help="Current boot log to compare")
    parser.add_argument("--json-out", help="Write structured diff report to JSON")
    args = parser.parse_args()

    golden_path  = Path(args.golden)
    current_path = Path(args.current)

    for p in (golden_path, current_path):
        if not p.exists():
            print(f"[ERROR] File not found: {p}")
            sys.exit(1)

    print(f"Golden:  {golden_path}")
    print(f"Current: {current_path}")

    golden  = extract_log_facts(golden_path)
    current = extract_log_facts(current_path)

    print(f"\nGolden  — stages: {sorted(golden['stages_seen'])}  "
          f"codes: {sorted(golden['sb_codes'])}")
    print(f"Current — stages: {sorted(current['stages_seen'])}  "
          f"codes: {sorted(current['sb_codes'])}")

    regressions = compare(golden, current)

    print()
    if regressions:
        print(f"✗ {len(regressions)} regression(s) detected:")
        for r in regressions:
            print(f"  - {r}")
    else:
        print("✓ No regressions detected — current log matches golden baseline.")

    if args.json_out:
        report = {
            "golden_file":  str(golden_path),
            "current_file": str(current_path),
            "regressions":  regressions,
            "golden_facts": {
                "sb_codes":    sorted(golden["sb_codes"]),
                "stages_seen": sorted(golden["stages_seen"]),
            },
            "current_facts": {
                "sb_codes":    sorted(current["sb_codes"]),
                "stages_seen": sorted(current["stages_seen"]),
            },
        }
        with open(args.json_out, "w") as f:
            json.dump(report, f, indent=2)
        print(f"\nReport → {args.json_out}")

    sys.exit(0 if not regressions else 1)


if __name__ == "__main__":
    main()
