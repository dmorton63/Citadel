#!/usr/bin/env python3
"""
Citadel Secure Boot -- Tamper simulation pack.

Produces a set of deliberately corrupted artifacts for deterministic
refusal-path testing.  Each tamper case corresponds to a test case in
SECURE_BOOT_TEST_MATRIX.md and an error code from SECURE_BOOT_REFUSAL_TAXONOMY.md.

All output goes to a staging directory (default: build/tamper-pack/).
Original artifacts are never modified.

Tamper modes
------------
byte-flip     Flip one byte at a given offset (default: offset=1000).
              Simulates random in-flight or at-rest corruption.

sig-strip     Delete the .sig sidecar so the artifact appears unsigned.
              Triggers SB-1xxx (unsigned) at the relevant boot stage.

sig-corrupt   Overwrite .sig with garbage bytes.
              Triggers SB-2xxx (bad signature).

Usage:
  # Prepare all tamper variants for lab artifacts
  python3 tools/tamper_pack.py \\
      --manifest build/secure-boot-manifest-lab.json \\
      --output-dir build/tamper-pack \\
      --modes byte-flip sig-strip sig-corrupt

  # Prepare only the Limine tamper variant
  python3 tools/tamper_pack.py \\
      --manifest build/secure-boot-manifest-lab.json \\
      --output-dir build/tamper-pack \\
      --artifact Limine.efi --modes byte-flip
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

TAMPER_MODES = ["byte-flip", "sig-strip", "sig-corrupt"]

# Where in the binary to flip a byte for each artifact type.
# Offsets chosen to land in a data section (not ELF/PE header magic bytes)
# so the file is still parseable but the hash/signature will fail.
FLIP_OFFSETS = {
    "Limine.efi": 1024,
    "BootGate":   512,
    "vmlinuz":    4096,
    "boot.json":  64,
    ".ko":        256,
    "ramdisk":    2048,
    "default":    1000,
}


def flip_offset_for(name: str) -> int:
    name_lower = name.lower()
    for stem, offset in FLIP_OFFSETS.items():
        if name_lower.startswith(stem.lower()) or name_lower.endswith(stem.lower()):
            return offset
    return FLIP_OFFSETS["default"]


def tamper_byte_flip(src: Path, dst: Path) -> None:
    data = bytearray(src.read_bytes())
    offset = flip_offset_for(src.name)
    if offset < len(data):
        data[offset] ^= 0xFF  # flip all bits at this byte
    dst.write_bytes(bytes(data))
    # Copy original .sig so the corrupt artifact still has a sig (triggers hash/sig mismatch)
    sig_src = src.with_suffix(src.suffix + ".sig")
    sig_dst = dst.with_suffix(dst.suffix + ".sig")
    if sig_src.exists():
        shutil.copy2(sig_src, sig_dst)


def tamper_sig_strip(src: Path, dst: Path) -> None:
    shutil.copy2(src, dst)
    # Deliberately do NOT copy the .sig → artifact appears unsigned (SB-1xxx)


def tamper_sig_corrupt(src: Path, dst: Path) -> None:
    shutil.copy2(src, dst)
    sig_dst = dst.with_suffix(dst.suffix + ".sig")
    sig_dst.write_bytes(b"INVALID_SIGNATURE_DATA_CITADEL_TAMPER_TEST\n" * 10)


TAMPER_FNS = {
    "byte-flip":   tamper_byte_flip,
    "sig-strip":   tamper_sig_strip,
    "sig-corrupt": tamper_sig_corrupt,
}

# Expected error codes per mode, per artifact type (informational)
EXPECTED_CODES = {
    "byte-flip": {
        "Limine.efi": "SB-5001", "BootGate": "SB-5002",
        "vmlinuz": "SB-5003", "boot.json": "SB-5004", ".ko": "SB-5005",
    },
    "sig-strip": {
        "Limine.efi": "SB-1001", "BootGate": "SB-1002",
        "vmlinuz": "SB-1003", "boot.json": "SB-1004", ".ko": "SB-1005",
    },
    "sig-corrupt": {
        "Limine.efi": "SB-2001", "BootGate": "SB-2002",
        "vmlinuz": "SB-2003", "boot.json": "SB-2004", ".ko": "SB-2005",
    },
}


def expected_code(mode: str, name: str) -> str:
    name_lower = name.lower()
    codes = EXPECTED_CODES.get(mode, {})
    for stem, code in codes.items():
        if name_lower.startswith(stem.lower()) or name_lower.endswith(stem.lower()):
            return code
    return "SB-?????"


def main():
    parser = argparse.ArgumentParser(
        description="Generate tampered artifacts for Secure Boot refusal testing"
    )
    parser.add_argument("--manifest", required=True,
                        help="Manifest JSON from sign_artifacts.py")
    parser.add_argument("--output-dir", default="build/tamper-pack",
                        help="Directory to write tampered artifacts into")
    parser.add_argument("--modes", nargs="+", choices=TAMPER_MODES,
                        default=TAMPER_MODES,
                        help="Tamper modes to apply (default: all)")
    parser.add_argument("--artifact",
                        help="Limit to a single artifact name (e.g. Limine.efi)")
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.exists():
        print(f"[ERROR] Manifest not found: {manifest_path}")
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    out_root = Path(args.output_dir)
    out_root.mkdir(parents=True, exist_ok=True)

    index = []  # summary of what was produced

    for entry in manifest["artifacts"]:
        src = Path(entry["path"])
        if not src.exists():
            print(f"  [SKIP] {src.name}  (not found)")
            continue

        if args.artifact and src.name != args.artifact:
            continue

        for mode in args.modes:
            mode_dir = out_root / mode
            mode_dir.mkdir(exist_ok=True)

            dst = mode_dir / src.name
            TAMPER_FNS[mode](src, dst)

            code = expected_code(mode, src.name)
            print(f"  [{mode:12}] {src.name:30}  → {dst}  (expected: {code})")

            index.append({
                "artifact":       src.name,
                "tamper_mode":    mode,
                "output_path":    str(dst),
                "expected_code":  code,
                "source_hash":    entry.get("hash_sha256", ""),
            })

    # Write index for test runner
    index_path = out_root / "tamper-index.json"
    with open(index_path, "w") as f:
        json.dump({"tamper_cases": index}, f, indent=2)

    print(f"\n✓ {len(index)} tamper variant(s) written to {out_root}/")
    print(f"  Index: {index_path}")


if __name__ == "__main__":
    main()
