#!/usr/bin/env python3
"""Collapse consecutive repeated lines in a stream.

This is useful for taming "spam" logs where the same message is printed
thousands of times in a tight loop.

Example:
  some_command 2>&1 | python3 -u tools/dedupe_lines.py | tee build/deduped.log

Behavior:
- Prints the first instance of a line.
- Suppresses subsequent identical consecutive lines.
- When the line changes (or at EOF), emits a single summary line:
    [previous line repeated N times]
"""

from __future__ import annotations

import argparse
import sys


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument(
        "--summary",
        choices=("bracket", "none"),
        default="bracket",
        help="How to emit repeat summaries (default: bracket).",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = _parse_args(argv)

    prev_key: str | None = None
    repeat_count = 0

    def flush_repeat_summary(count: int) -> None:
        if count <= 0:
            return
        if args.summary == "none":
            return
        sys.stdout.write(f"[previous line repeated {count} times]\n")

    for raw_line in sys.stdin:
        # Normalize only for comparisons; preserve original line for output.
        key = raw_line.rstrip("\r\n")

        if prev_key is None:
            prev_key = key
            repeat_count = 0
            sys.stdout.write(raw_line)
            continue

        if key == prev_key:
            repeat_count += 1
            continue

        flush_repeat_summary(repeat_count)
        prev_key = key
        repeat_count = 0
        sys.stdout.write(raw_line)

    flush_repeat_summary(repeat_count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
