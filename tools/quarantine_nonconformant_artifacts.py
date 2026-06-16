#!/usr/bin/env python3
"""
Quarantine planner/enforcer for non-conformant artifacts.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description="Quarantine non-conformant artifacts")
    ap.add_argument("--manifest", required=True, help="Artifact manifest JSON")
    ap.add_argument("--quarantine-dir", required=True)
    ap.add_argument("--execute", action="store_true", help="Copy files into quarantine dir")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.exists():
        print(f"[ERROR] manifest missing: {manifest_path}")
        return 1

    with open(manifest_path) as f:
        manifest = json.load(f)

    quarantine_dir = Path(args.quarantine_dir)
    quarantine_dir.mkdir(parents=True, exist_ok=True)

    quarantined = []
    for art in manifest.get("artifacts", []):
        if art.get("conformant", False):
            continue

        entry = {
            "name": art.get("name", "unknown"),
            "path": art.get("path", ""),
            "reason": art.get("reason", "policy non-conformance"),
            "copied": False,
        }

        source = Path(entry["path"]) if entry["path"] else None
        if args.execute and source and source.exists() and source.is_file():
            dest = quarantine_dir / source.name
            shutil.copy2(source, dest)
            entry["copied"] = True

        quarantined.append(entry)
        print(f"[QUARANTINE] {entry['name']} reason={entry['reason']}")

    result = {
        "manifest": str(manifest_path),
        "quarantine_dir": str(quarantine_dir),
        "quarantined_count": len(quarantined),
        "quarantined": quarantined,
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(result, f, indent=2)

    if quarantined:
        print("[FAIL] non-conformant artifacts detected and quarantined")
        return 1

    print("[OK] no non-conformant artifacts found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
