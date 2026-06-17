# Secure Boot CI Stabilization (2026-06-17)

Scope: Secure Boot GitHub Actions workflow reliability hardening after repeated pipeline failures.

## What Was Fixed

- Added noninteractive apt behavior in secure-boot workflows to reduce runner noise and avoid restart prompts.
- Installed nasm in CI build jobs so CMake ASM_NASM configuration succeeds.
- Enabled recursive submodule checkout in secure-boot workflows so limine headers are present during kernel builds.
- Added missing submodule mapping for CQL_Database_Engine/external/imgui in .gitmodules.
- Hardened artifact handoff by staging canonical secure-boot artifact names before upload and failing early when expected files are missing.

## Result

Secure Boot workflow runs complete successfully after these changes.

## Follow-up

Continue Phase 3 Item 23 work: ACPI and shutdown fallback diagnostics hardening.
