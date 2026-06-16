#!/usr/bin/env python3
"""
Citadel Secure Boot -- Signed-artifact provenance checker.

Verifies that every signed boot artifact in a manifest can be traced back
to a known build graph node: a source directory, a CMake target, and an
expected output path.

Checks performed:
  1. Artifact exists at the path declared in the manifest
  2. Artifact hash matches the manifest entry
  3. Artifact's source directory is a tracked git path
  4. CMake target for the artifact is present in compile_commands.json (if available)
  5. Signature sidecar exists and is readable

Exit 0  → all provenance checks pass
Exit 1  → one or more failures

Usage:
  python3 tools/check_provenance.py \\
      --manifest build/secure-boot-manifest-lab.json \\
      --build-dir build/ \\
      --source-dir /home/dmort/citadel \\
      [--strict]
"""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Expected build-graph nodes  (artifact stem → source dir, cmake target)
# ---------------------------------------------------------------------------
PROVENANCE_MAP = {
    "Limine.efi":  {"source_dir": "limine",       "cmake_target": "limine"},
    "BootGate":    {"source_dir": "BootGate",      "cmake_target": "bootgate"},  # or BootGate
    "vmlinuz":     {"source_dir": "kernel",        "cmake_target": "kernel"},
    "boot.json":   {"source_dir": ".",             "cmake_target": "boot-json"},
    ".ko":         {"source_dir": None,            "cmake_target": None},  # wildcard modules
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def git_tracked(path: Path, source_dir: Path) -> Optional[bool]:
    """Return True if path is in a git-tracked source directory."""
    try:
        result = subprocess.run(
            ["git", "-C", str(source_dir), "ls-files", "--error-unmatch",
             str(path.relative_to(source_dir))],
            capture_output=True,
        )
        return result.returncode == 0
    except Exception:
        return None  # can't determine; treat as warning


def cmake_target_exists(target: str, build_dir: Path) -> Optional[bool]:
    """Check if a CMake target is present in the build system."""
    makefile = build_dir / "Makefile"
    if makefile.exists():
        content = makefile.read_text(errors="replace")
        return f".PHONY: {target}" in content or f"{target}:" in content
    # Fall back: check compile_commands.json for any file from the target's source
    cc = build_dir / "compile_commands.json"
    if cc.exists():
        data = json.loads(cc.read_text())
        return any(target.lower() in cmd.get("file", "").lower() for cmd in data)
    return None  # can't determine


def lookup_provenance(artifact_name: str) -> dict:
    name_lower = artifact_name.lower()
    for stem, info in PROVENANCE_MAP.items():
        if name_lower.startswith(stem.lower()) or name_lower.endswith(stem.lower()):
            return info
    return {"source_dir": None, "cmake_target": None}


def check_entry(entry: dict, source_dir: Path, build_dir: Path, strict: bool) -> list[str]:
    """Return list of failure strings (empty = pass)."""
    failures = []
    warnings = []

    artifact = Path(entry["path"])
    name     = artifact.name

    # 1. File existence
    if not artifact.exists():
        failures.append(f"{name}: artifact file not found at {artifact}")
        return failures  # can't do further checks

    # 2. Hash integrity
    expected_hash = entry.get("hash_sha256")
    if expected_hash:
        actual_hash = sha256_file(artifact)
        if actual_hash != expected_hash:
            failures.append(
                f"{name}: hash mismatch — "
                f"manifest={expected_hash[:12]}... actual={actual_hash[:12]}..."
            )
    else:
        warnings.append(f"{name}: no hash in manifest entry (cannot verify integrity)")

    # 3. Signature sidecar
    sig = artifact.with_suffix(artifact.suffix + ".sig")
    if not sig.exists():
        failures.append(f"{name}: missing signature sidecar {sig.name}")

    # 4. Identity sidecar (informational)
    identity = artifact.with_suffix(artifact.suffix + ".identity.json")
    if not identity.exists():
        warnings.append(f"{name}: missing identity sidecar (run tag_artifact_identity.py)")

    # 5. Build-graph provenance
    prov = lookup_provenance(name)

    # 5a. Source directory tracked by git
    if prov["source_dir"]:
        src = source_dir / prov["source_dir"]
        tracked = git_tracked(src, source_dir)
        if tracked is False:
            (failures if strict else warnings).append(
                f"{name}: source dir '{prov['source_dir']}' not tracked by git"
            )
        elif tracked is None:
            warnings.append(f"{name}: could not check git tracking for source dir")

    # 5b. CMake target present
    if prov["cmake_target"]:
        exists = cmake_target_exists(prov["cmake_target"], build_dir)
        if exists is False:
            (failures if strict else warnings).append(
                f"{name}: CMake target '{prov['cmake_target']}' not found in build system"
            )
        elif exists is None:
            warnings.append(f"{name}: could not verify CMake target (no Makefile/compile_commands.json)")

    if warnings:
        for w in warnings:
            print(f"  [WARN] {w}")

    return failures


def main():
    parser = argparse.ArgumentParser(
        description="Verify signed-artifact provenance against build graph"
    )
    parser.add_argument("--manifest", required=True,
                        help="Manifest JSON from sign_artifacts.py")
    parser.add_argument("--build-dir", default="build",
                        help="CMake build directory")
    parser.add_argument("--source-dir", default=".",
                        help="Root of source tree (git repo)")
    parser.add_argument("--strict", action="store_true",
                        help="Treat warnings as failures")
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.exists():
        print(f"[ERROR] Manifest not found: {manifest_path}")
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    source_dir = Path(args.source_dir).resolve()
    build_dir  = Path(args.build_dir).resolve()

    print(f"Checking provenance for {len(manifest['artifacts'])} artifact(s)...")

    all_failures = []
    for entry in manifest["artifacts"]:
        failures = check_entry(entry, source_dir, build_dir, args.strict)
        name = Path(entry["path"]).name
        if failures:
            for f in failures:
                print(f"  [FAIL] {f}")
            all_failures.extend(failures)
        else:
            print(f"  [  OK] {name:30}  provenance verified")

    print()
    if all_failures:
        print(f"Provenance check FAILED ({len(all_failures)} issue(s)).")
        sys.exit(1)
    else:
        print(f"✓ All {len(manifest['artifacts'])} artifact(s) pass provenance checks.")
        sys.exit(0)


if __name__ == "__main__":
    main()
