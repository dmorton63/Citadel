#!/usr/bin/env python3
"""
Citadel Secure Boot -- Artifact identity tagger.

Stamps each build artifact with a deterministic identity block containing:
  - build_id    (ISO-8601 UTC timestamp + git short-SHA)
  - signer_id   (key ID from sign_artifacts.py manifest)
  - manifest_id (SHA-256 of the manifest file itself)

The identity is written as a companion JSON sidecar:
  <artifact>.identity.json

It is also appended to the sign_artifacts manifest so every manifest entry
carries complete provenance.

Usage:
  python3 tools/tag_artifact_identity.py \\
      --manifest build/secure-boot-manifest-lab.json \\
      --git-dir  /home/dmort/citadel \\
      [--update-manifest]   # rewrite manifest with identity fields added
"""

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def git_short_sha(git_dir: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", git_dir, "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        )
        return result.stdout.strip()
    except Exception:
        return "unknown"


def git_dirty(git_dir: str) -> bool:
    try:
        result = subprocess.run(
            ["git", "-C", git_dir, "status", "--porcelain"],
            capture_output=True, text=True, check=True,
        )
        return bool(result.stdout.strip())
    except Exception:
        return False


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def build_identity(
    artifact_path: Path,
    signer_id: str,
    manifest_id: str,
    git_sha: str,
    dirty: bool,
) -> dict:
    ts = datetime.now(timezone.utc).isoformat()
    return {
        "schema_version": "1.0",
        "artifact": artifact_path.name,
        "artifact_path": str(artifact_path),
        "artifact_hash_sha256": sha256_file(artifact_path),
        "build_id": f"{ts[:10]}-{git_sha}" + ("-dirty" if dirty else ""),
        "git_sha": git_sha,
        "git_dirty": dirty,
        "signer_id": signer_id,
        "manifest_id": manifest_id,
        "tagged_at": ts,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Tag Secure Boot artifacts with deterministic identity"
    )
    parser.add_argument("--manifest", required=True,
                        help="Manifest JSON from sign_artifacts.py")
    parser.add_argument("--git-dir", default=".",
                        help="Root of git repo (for SHA extraction)")
    parser.add_argument("--update-manifest", action="store_true",
                        help="Rewrite manifest with identity fields added")
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.exists():
        print(f"[ERROR] Manifest not found: {manifest_path}")
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    # Manifest identity = SHA-256 of its current serialised form
    manifest_id = sha256_file(manifest_path)
    git_sha = git_short_sha(args.git_dir)
    dirty   = git_dirty(args.git_dir)

    print(f"Tagging {len(manifest['artifacts'])} artifact(s)...")
    print(f"  git SHA:     {git_sha}" + (" (dirty)" if dirty else ""))
    print(f"  manifest ID: {manifest_id[:16]}...")

    updated_entries = []
    for entry in manifest["artifacts"]:
        artifact = Path(entry["path"])
        if not artifact.exists():
            print(f"  [SKIP] {artifact.name}  (not found)")
            updated_entries.append(entry)
            continue

        identity = build_identity(
            artifact,
            signer_id=entry.get("signing_key_id", "unknown"),
            manifest_id=manifest_id,
            git_sha=git_sha,
            dirty=dirty,
        )

        # Write sidecar
        sidecar = artifact.with_suffix(artifact.suffix + ".identity.json")
        with open(sidecar, "w") as f:
            json.dump(identity, f, indent=2)

        build_id = identity["build_id"]
        print(f"  ✓ {artifact.name:30}  build_id={build_id}")

        # Merge identity fields back into manifest entry
        entry["build_id"]    = identity["build_id"]
        entry["git_sha"]     = git_sha
        entry["git_dirty"]   = dirty
        entry["manifest_id"] = manifest_id
        updated_entries.append(entry)

    if args.update_manifest:
        manifest["artifacts"] = updated_entries
        manifest["manifest_id"] = manifest_id
        with open(manifest_path, "w") as f:
            json.dump(manifest, f, indent=2, sort_keys=True)
        print(f"\n✓ Manifest updated with identity fields: {manifest_path}")
    else:
        print(f"\n✓ Identity sidecars written (manifest not modified; "
              f"use --update-manifest to embed)")

    sys.exit(0)


if __name__ == "__main__":
    main()
