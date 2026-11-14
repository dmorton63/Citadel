# CITADEL IDE Subset Charter (Monorepo)

Status: Proposed
Owner: Citadel maintainers
Last updated: 2026-08-29

## Objective

Define a self-contained Citadel IDE subset inside the main Citadel repository so work can move quickly now, while remaining easy to split into a standalone repository later if needed.

## Decision

Keep the IDE/parser/editor effort inside the main Citadel repository for now.

Rationale:
- Shares runtime types, build surfaces, and platform assumptions with Citadel.
- Avoids duplicate CI, release, and dependency management.
- Keeps docs, parser rules, and runtime integration in one review path.

## Subset Boundaries

In scope for IDE subset v0:
- Language specification and parser conformance docs.
- Parser conformance fixtures/checklists.
- Validation scripts and CI checks for parser conformance artifacts.
- Desktop/editor planning documents that define IDE behavior.

Current tracked subset files:
- docs/CITADEL_IDE_KEYWORDS.md
- docs/CITADEL_PARSER_CONFORMANCE_CHECKLIST.md
- docs/CITADEL_PARSER_CONFORMANCE_CHECKLIST.csv
- tools/validate_parser_conformance_csv.py
- .github/workflows/parser-conformance-csv.yml
- docs/CITADEL_DESKTOP_EDITOR_V1.md

Out of scope for this subset charter:
- Secure boot governance/docs.
- Kernel-wide naming refactors.
- Unrelated runtime modules not required by IDE/parser work.

## Working Structure

Use this logical structure for all new IDE subset additions:
- docs/IDE/
  - Specs and language docs
  - Parser conformance plans
  - Editor architecture notes
- tools/ide/
  - Data validators
  - One-shot migration helpers
- tests/ide/
  - Parser and conformance fixtures
  - Expected outputs/diagnostics

Note: Existing files can remain in current locations; use this structure for net-new files to avoid disruptive moves.

## Ownership and Review Model

Suggested review policy:
- At least 1 maintainer approval for docs-only IDE subset changes.
- At least 1 maintainer approval plus CI pass for tooling/CI changes.
- For parser behavior/spec changes, require checklist and CSV updates in the same PR.

Optional CODEOWNERS snippet when maintainers are ready:

```
# IDE subset ownership (fill actual GitHub handles)
/docs/CITADEL_IDE_* @OWNER1
/docs/CITADEL_PARSER_* @OWNER1
/tools/validate_parser_conformance_csv.py @OWNER1
/.github/workflows/parser-conformance-csv.yml @OWNER1
```

## PR Rules For Subset Consistency

When changing parser precedence/keyword behavior:
- Update docs/CITADEL_IDE_KEYWORDS.md.
- Update docs/CITADEL_PARSER_CONFORMANCE_CHECKLIST.md if expectations changed.
- Update docs/CITADEL_PARSER_CONFORMANCE_CHECKLIST.csv rows affected.
- Run: python3 tools/validate_parser_conformance_csv.py.

## Extraction-Ready Rules

To keep future split simple:
- Keep IDE subset tools free of kernel-only include dependencies unless required.
- Prefer explicit file paths and stable CSV/schema headers.
- Keep parser tests deterministic and text-based.
- Avoid cross-linking subset docs to unrelated domains where possible.

## Trigger To Reevaluate New Repository

Create a standalone repository only when at least two conditions are true:
- Independent release cadence is needed.
- Different contributor/permission model is required.
- Tooling starts being reused outside Citadel.
- CI time/cost is materially reduced by split.
- Runtime coupling to Citadel core drops significantly.

## Immediate Next Steps

1. For all net-new IDE artifacts, place files under docs/IDE, tools/ide, and tests/ide.
2. Add maintainers to CODEOWNERS once GitHub handles are confirmed.
3. Add a parser conformance runner under tests/ide that writes pass/fail back into CSV or an adjacent report.
