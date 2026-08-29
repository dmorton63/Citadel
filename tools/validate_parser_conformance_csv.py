#!/usr/bin/env python3
"""Validate parser conformance CSV integrity.

Checks:
- Required columns are present.
- IDs are exactly E01..E24, each appearing once.
- Status values are limited to: Pass, Fail, Not Run.

Exit codes:
- 0: valid
- 1: validation failure
- 2: file/parse/usage error
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path


ALLOWED_STATUSES = {"Pass", "Fail", "Not Run"}
REQUIRED_COLUMNS = ["id", "status", "input", "expected_result", "notes"]
EXPECTED_IDS = [f"E{i:02d}" for i in range(1, 25)]
ID_PATTERN = re.compile(r"^E\d{2}$")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument(
        "--csv",
        default="docs/CITADEL_PARSER_CONFORMANCE_CHECKLIST.csv",
        help="Path to parser conformance CSV.",
    )
    return parser.parse_args(argv)


def validate_csv(path: Path) -> tuple[bool, list[str]]:
    errors: list[str] = []

    if not path.exists():
        return False, [f"CSV not found: {path}"]

    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            return False, ["CSV appears to be empty or missing a header row."]

        missing_cols = [c for c in REQUIRED_COLUMNS if c not in reader.fieldnames]
        extra_cols = [c for c in reader.fieldnames if c not in REQUIRED_COLUMNS]
        if missing_cols:
            errors.append(f"Missing required columns: {', '.join(missing_cols)}")
        if extra_cols:
            errors.append(f"Unexpected columns: {', '.join(extra_cols)}")

        rows = list(reader)

    if errors:
        return False, errors

    found_ids: list[str] = []
    seen: set[str] = set()

    for idx, row in enumerate(rows, start=2):
        row_id = (row.get("id") or "").strip()
        status = (row.get("status") or "").strip()

        if not ID_PATTERN.match(row_id):
            errors.append(f"Line {idx}: invalid id format '{row_id}' (expected E##)")
        else:
            found_ids.append(row_id)
            if row_id in seen:
                errors.append(f"Line {idx}: duplicate id '{row_id}'")
            seen.add(row_id)

        if status not in ALLOWED_STATUSES:
            errors.append(
                f"Line {idx}: invalid status '{status}' (allowed: Pass, Fail, Not Run)"
            )

    expected_set = set(EXPECTED_IDS)
    found_set = set(found_ids)

    missing_ids = sorted(expected_set - found_set)
    unexpected_ids = sorted(found_set - expected_set)

    if missing_ids:
        errors.append(f"Missing expected IDs: {', '.join(missing_ids)}")
    if unexpected_ids:
        errors.append(f"Unexpected IDs: {', '.join(unexpected_ids)}")
    if len(rows) != len(EXPECTED_IDS):
        errors.append(
            f"Expected {len(EXPECTED_IDS)} data rows, found {len(rows)}"
        )

    return len(errors) == 0, errors


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    csv_path = Path(args.csv)

    try:
        ok, errors = validate_csv(csv_path)
    except (OSError, csv.Error) as exc:
        print(f"ERROR: failed to read/parse CSV: {exc}")
        return 2

    if not ok:
        print("FAILED: parser conformance CSV validation")
        for err in errors:
            print(f" - {err}")
        return 1

    print(
        "OK: parser conformance CSV is valid "
        f"({len(EXPECTED_IDS)} IDs, statuses in allowed set)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
