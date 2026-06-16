#!/usr/bin/env python3
"""
Citadel Secure Boot - SBOM linkage for signature manifests.

Takes a signed-artifact manifest and an SBOM file and emits a linkage file
that binds each artifact hash to an SBOM component/package identifier.

Usage:
  python3 tools/link_sbom_manifest.py \
    --manifest build/secure-boot-manifest-release.json \
    --sbom build/sbom.json \
    --out build/manifest-sbom-linkage.json
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def extract_sbom_components(sbom: dict) -> list[dict]:
    # Supports CycloneDX-like and SPDX-like simple subsets.
    if "components" in sbom:
        return sbom["components"]
    if "packages" in sbom:
        return sbom["packages"]
    return []


def main() -> int:
    ap = argparse.ArgumentParser(description="Link SBOM to secure-boot manifest")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--sbom", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    manifest = load_json(Path(args.manifest))
    sbom = load_json(Path(args.sbom))
    components = extract_sbom_components(sbom)

    # Build a simple lookup by probable filename match.
    comp_by_name = {}
    for c in components:
        name = (c.get("name") or c.get("PackageName") or "").strip()
        if name:
            comp_by_name[name.lower()] = c

    links = []
    unresolved = []

    for art in manifest.get("artifacts", []):
        artifact_name = art["name"]
        match = None
        for n, c in comp_by_name.items():
            if artifact_name.lower().startswith(n) or n in artifact_name.lower():
                match = c
                break

        if match:
            links.append(
                {
                    "artifact": artifact_name,
                    "artifact_hash_sha256": art.get("hash_sha256"),
                    "manifest_signer": art.get("signing_key_id"),
                    "sbom_component": match.get("name") or match.get("PackageName"),
                    "sbom_version": match.get("versionInfo") or match.get("version"),
                    "sbom_purl": match.get("purl"),
                }
            )
        else:
            unresolved.append(artifact_name)

    out_data = {
        "schema_version": "1.0",
        "manifest": args.manifest,
        "sbom": args.sbom,
        "linked": links,
        "unresolved_artifacts": unresolved,
    }

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(out_data, f, indent=2)

    print(f"Linked entries: {len(links)}")
    print(f"Unresolved:     {len(unresolved)}")
    print(f"Output -> {out_path}")

    # unresolved are warning-level; do not fail pipeline unless none linked
    return 1 if not links else 0


if __name__ == "__main__":
    raise SystemExit(main())
