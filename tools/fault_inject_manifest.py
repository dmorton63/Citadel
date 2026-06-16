#!/usr/bin/env python3
"""
Manifest and signature fault injector for deterministic refusal testing.
"""

from __future__ import annotations

import argparse
import json
from copy import deepcopy
from pathlib import Path


FAULTS = {"missing_hash", "partial_signature", "missing_signer", "bad_schema_version"}


def main() -> int:
    ap = argparse.ArgumentParser(description="Inject manifest faults and verify refusal behavior")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--fault", required=True, choices=sorted(FAULTS))
    ap.add_argument("--json-out")
    args = ap.parse_args()

    path = Path(args.manifest)
    if not path.exists():
        print(f"[ERROR] manifest missing: {path}")
        return 1

    with open(path) as f:
        manifest = json.load(f)

    mutated = deepcopy(manifest)
    art = mutated.get("artifacts", [{}])[0]

    if args.fault == "missing_hash":
        art.pop("hash_sha256", None)
    elif args.fault == "partial_signature":
        art["signature_file"] = "partial.sig"
        art["signature_complete"] = False
    elif args.fault == "missing_signer":
        art.pop("signing_key_id", None)
    elif args.fault == "bad_schema_version":
        mutated["schema_version"] = "broken"

    failures = []
    if not art.get("hash_sha256"):
        failures.append("missing hash_sha256")
    if not art.get("signing_key_id"):
        failures.append("missing signing_key_id")
    if not art.get("signature_file") or art.get("signature_complete") is False:
        failures.append("invalid or partial signature metadata")
    if mutated.get("schema_version") not in {"1.0", None} and not str(mutated.get("schema_version")).startswith("1"):
        failures.append("invalid schema_version")

    status = "PASS" if not failures else "FAIL"
    # Expected behavior: faults should produce FAIL.
    expected = "FAIL"
    result = "PASS" if status == expected else "FAIL"

    print(f"Fault injection outcome: observed={status} expected={expected} result={result}")
    for fmsg in failures:
        print(f"[FAIL] {fmsg}")

    report = {
        "fault": args.fault,
        "observed_status": status,
        "expected_status": expected,
        "result": result,
        "failures": failures,
    }

    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "w") as f:
            json.dump(report, f, indent=2)

    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
