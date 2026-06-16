#!/usr/bin/env python3
"""
Installer media Secure Boot gate.

Verifies that installer media artifacts match approved manifest hashes
and carry detached signatures.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify installer media against approved SB manifest")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--media-root", required=True)
    args = ap.parse_args()

    manifest_path = Path(args.manifest)
    media_root = Path(args.media_root)

    if not manifest_path.exists() or not media_root.exists():
        print("[ERROR] manifest or media-root missing")
        return 1

    with open(manifest_path) as f:
        manifest = json.load(f)

    fail = False
    for art in manifest.get("artifacts", []):
        rel = Path(art.get("path", "")).name
        media_art = media_root / rel
        media_sig = media_art.with_suffix(media_art.suffix + ".sig")

        if not media_art.exists():
            print(f"[FAIL] missing artifact on media: {rel}")
            fail = True
            continue
        if not media_sig.exists():
            print(f"[FAIL] missing signature on media: {media_sig.name}")
            fail = True
            continue

        media_hash = sha256(media_art)
        expected_hash = art.get("hash_sha256")
        if media_hash != expected_hash:
            print(f"[FAIL] hash mismatch: {rel}")
            fail = True
            continue

        print(f"[OK] {rel}")

    if fail:
        print("Installer media verification FAILED.")
        return 1
    print("Installer media verification PASSED.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
