#!/usr/bin/env python3
"""
Citadel Secure Boot -- Reproducibility check.

Runs the Secure Boot signing pipeline twice on the same source tree and
verifies that both runs produce identical artifact hashes and manifests
(excluding timestamps and signing timestamps, which are expected to differ).

A build is reproducible when:
  - All artifact SHA-256 hashes are identical across both runs
  - All artifact sizes are identical
  - Key IDs are identical
  - No artifact is missing in run 2 that was present in run 1

Exit 0  → fully reproducible
Exit 1  → one or more discrepancies

Usage:
  python3 tools/check_reproducibility.py \\
      --manifest-a build/run1/secure-boot-manifest-lab.json \\
      --manifest-b build/run2/secure-boot-manifest-lab.json \\
      [--json-out build/reproducibility-report.json]
"""

import argparse
import json
import sys
from pathlib import Path

# Fields excluded from comparison (legitimately differ between runs)
EXCLUDED_FIELDS = {
    "build_timestamp", "signing_timestamp", "tagged_at",
    "build_id", "manifest_id", "signer_id",
}


def load_manifest(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def index_by_name(manifest: dict) -> dict:
    return {entry["name"]: entry for entry in manifest.get("artifacts", [])}


def compare_manifests(manifest_a: dict, manifest_b: dict) -> list[str]:
    failures = []
    a_index = index_by_name(manifest_a)
    b_index = index_by_name(manifest_b)

    # Artifacts in A but not B
    for name in sorted(set(a_index) - set(b_index)):
        failures.append(f"MISSING IN RUN 2: {name}")

    # Artifacts in B but not A
    for name in sorted(set(b_index) - set(a_index)):
        failures.append(f"EXTRA IN RUN 2 (not in run 1): {name}")

    # Per-artifact field comparison
    for name in sorted(set(a_index) & set(b_index)):
        a = a_index[name]
        b = b_index[name]

        for field in set(a) | set(b):
            if field in EXCLUDED_FIELDS:
                continue
            av = a.get(field)
            bv = b.get(field)
            if av != bv:
                failures.append(
                    f"MISMATCH {name}.{field}: "
                    f"run1={repr(av)!r:.60} run2={repr(bv)!r:.60}"
                )

    return failures


def main():
    parser = argparse.ArgumentParser(
        description="Verify Secure Boot build reproducibility across two runs"
    )
    parser.add_argument("--manifest-a", required=True, help="Manifest from run 1")
    parser.add_argument("--manifest-b", required=True, help="Manifest from run 2")
    parser.add_argument("--json-out", help="Write structured report to JSON")
    args = parser.parse_args()

    path_a = Path(args.manifest_a)
    path_b = Path(args.manifest_b)

    for p in (path_a, path_b):
        if not p.exists():
            print(f"[ERROR] Manifest not found: {p}")
            sys.exit(1)

    manifest_a = load_manifest(path_a)
    manifest_b = load_manifest(path_b)

    n = len(manifest_a.get("artifacts", []))
    print(f"Comparing {n} artifact(s) across two manifest runs...")
    print(f"  Run 1: {path_a}")
    print(f"  Run 2: {path_b}")

    failures = compare_manifests(manifest_a, manifest_b)

    if failures:
        print(f"\n✗ {len(failures)} reproducibility issue(s):")
        for f in failures:
            print(f"  - {f}")
    else:
        print(f"\n✓ Build is reproducible — all {n} artifact hashes and fields match.")

    if args.json_out:
        report = {
            "manifest_a": str(path_a),
            "manifest_b": str(path_b),
            "artifact_count": n,
            "reproducible": len(failures) == 0,
            "failures": failures,
        }
        with open(args.json_out, "w") as f:
            json.dump(report, f, indent=2)
        print(f"Report → {args.json_out}")

    sys.exit(0 if not failures else 1)


if __name__ == "__main__":
    main()
