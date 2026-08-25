#!/usr/bin/env python3
"""Validate Citadel syscall ABI header against JSON registry.

Compares:
- Family numeric IDs in QKSyscallABI.h
- Syscall numeric IDs and naming coverage between:
  - QKernel/Include/QKSyscallABI.h (namespace QK::Syscall::Id)
  - docs/CITADEL_SYSCALL_ABI_V0_1.json

Exit codes:
- 0: registry and header are consistent
- 1: mismatch detected
- 2: parse/usage error
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


FAMILY_NAME_TO_HEADER = {
    "Process": "Process",
    "Thread": "Thread",
    "Memory": "Memory",
    "FileSystem": "File",
    "Time": "Time",
    "IPC": "IPC",
    "Net": "Net",
    "UI": "UI",
    "Graphics": "Graphics",
    "Security": "Security",
}

FAMILY_PREFIX_FOR_CONST = {
    "Process": "Process",
    "Thread": "Thread",
    "Memory": "Memory",
    "File": "File",
    "Time": "Time",
    "IPC": "Ipc",
    "Ipc": "Ipc",
    "Net": "Net",
    "UI": "Ui",
    "Ui": "Ui",
    "Graphics": "Gfx",
    "Gfx": "Gfx",
    "Security": "Sec",
    "Sec": "Sec",
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument(
        "--header",
        default="QKernel/Include/QKSyscallABI.h",
        help="Path to QK syscall ABI header.",
    )
    parser.add_argument(
        "--registry",
        default="docs/CITADEL_SYSCALL_ABI_V0_1.json",
        help="Path to machine-readable syscall registry JSON.",
    )
    return parser.parse_args(argv)


def parse_header_families(header_text: str) -> dict[str, int]:
    family_re = re.compile(r"^\s*([A-Za-z]+)\s*=\s*0x([0-9A-Fa-f]{2})\s*,\s*$", re.MULTILINE)
    out: dict[str, int] = {}
    for name, hexv in family_re.findall(header_text):
        out[name] = int(hexv, 16)
    return out


def parse_header_syscalls(header_text: str, families: dict[str, int]) -> dict[str, int]:
    id_re = re.compile(
        r"^\s*constexpr\s+QC::u16\s+([A-Za-z0-9_]+)\s*=\s*makeId\(Family::([A-Za-z]+),\s*0x([0-9A-Fa-f]{2})\);\s*$",
        re.MULTILINE,
    )
    out: dict[str, int] = {}
    for const_name, family_name, op_hex in id_re.findall(header_text):
        if family_name not in families:
            raise ValueError(f"Unknown family in header syscall constant: {family_name}")
        sys_id = (families[family_name] << 8) | int(op_hex, 16)
        out[const_name] = sys_id
    return out


def expected_const_name(json_syscall_name: str) -> str:
    # Example: "Process.Self" -> "ProcessSelf"
    #          "UI.WindowCreate" -> "UiWindowCreate"
    parts = json_syscall_name.split(".")
    if len(parts) != 2:
        raise ValueError(f"Unexpected syscall name format: {json_syscall_name}")

    family, op = parts
    family_prefix = FAMILY_PREFIX_FOR_CONST.get(family)
    if family_prefix is None:
        raise ValueError(f"Unknown syscall family in JSON name: {family}")
    return f"{family_prefix}{op}"


def load_registry(registry_path: Path) -> dict:
    with registry_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    header_path = Path(args.header)
    registry_path = Path(args.registry)

    if not header_path.exists():
        print(f"ERROR: header not found: {header_path}")
        return 2
    if not registry_path.exists():
        print(f"ERROR: registry not found: {registry_path}")
        return 2

    header_text = header_path.read_text(encoding="utf-8")
    header_families = parse_header_families(header_text)
    header_syscalls = parse_header_syscalls(header_text, header_families)

    registry = load_registry(registry_path)
    families = registry.get("families", [])

    failures: list[str] = []
    seen_consts: set[str] = set()

    for fam in families:
        json_family_name = fam.get("name")
        json_family_id_text = fam.get("id")

        if not isinstance(json_family_name, str) or not isinstance(json_family_id_text, str):
            failures.append(f"Malformed family entry in JSON: {fam}")
            continue

        mapped_header_family = FAMILY_NAME_TO_HEADER.get(json_family_name)
        if mapped_header_family is None:
            failures.append(f"JSON family has no header mapping: {json_family_name}")
            continue

        if mapped_header_family not in header_families:
            failures.append(
                f"Header missing family '{mapped_header_family}' (mapped from JSON '{json_family_name}')"
            )
            continue

        json_family_id = int(json_family_id_text, 16)
        header_family_id = header_families[mapped_header_family]
        if json_family_id != header_family_id:
            failures.append(
                f"Family ID mismatch for {json_family_name}: JSON={json_family_id_text} HEADER=0x{header_family_id:02X}"
            )

        for sc in fam.get("syscalls", []):
            sc_name = sc.get("name")
            sc_id_text = sc.get("id")
            if not isinstance(sc_name, str) or not isinstance(sc_id_text, str):
                failures.append(f"Malformed syscall entry in JSON family {json_family_name}: {sc}")
                continue

            try:
                const_name = expected_const_name(sc_name)
            except ValueError as exc:
                failures.append(str(exc))
                continue

            seen_consts.add(const_name)

            if const_name not in header_syscalls:
                failures.append(f"Header missing syscall constant: Id::{const_name} (from JSON {sc_name})")
                continue

            json_id = int(sc_id_text, 16)
            header_id = header_syscalls[const_name]
            if json_id != header_id:
                failures.append(
                    f"Syscall ID mismatch for {sc_name}/Id::{const_name}: JSON={sc_id_text} HEADER=0x{header_id:04X}"
                )

    # Extra header constants that are not represented in JSON.
    for const_name in sorted(header_syscalls.keys()):
        if const_name not in seen_consts:
            failures.append(f"Header syscall constant not present in JSON registry: Id::{const_name}")

    if failures:
        print("FAILED: syscall registry/header consistency check")
        for item in failures:
            print(f" - {item}")
        return 1

    print(
        "OK: syscall registry/header are consistent "
        f"({len(header_families)} families, {len(header_syscalls)} syscall constants)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
