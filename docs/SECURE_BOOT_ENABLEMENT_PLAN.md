# Secure Boot Enablement Plan

Date: 2026-06-13

## Current Baseline

- Test machine: Secure Boot off, TPM on.
- Current Citadel trust posture already includes:
  - Boot asset signature verification in production mode.
  - TPM-backed SecureStore anchor handling.
  - PCR measurement logging (attestation policy enforcement deferred).

This plan adds UEFI Secure Boot in a controlled sequence without blocking installer MVP completion.

## Recommended Path (Phase Order)

1. Lab bring-up with direct signed bootloader chain.
2. Integrate signing checks into build/package pipeline.
3. Validate fail-closed behavior on Secure Boot enabled hardware.
4. Optionally add shim/MOK path later if wider device compatibility requires it.

Rationale: direct signed path is simpler to debug first and gives deterministic ownership of keys and failure modes.

## Target Boot Trust Model

Initial target (lab/owned devices):

- Firmware Secure Boot validates Citadel bootloader artifact chain.
- Bootloader loads Citadel kernel + module image.
- Citadel production trust checks continue validating boot-time assets.
- TPM remains enabled for sealed-secret and measurement continuity.

Future optional target (distribution-friendly):

- Shim-based chain with MOK enrollment where platform policy requires it.

## Key Management Policy

Define separate key sets for:

- Dev lab keys
- Staging keys
- Production keys

For each environment, define:

- PK owner and storage location
- KEK owner and rotation authority
- db signing cert lifecycle
- dbx revocation update process

Minimum operational rules:

- Never reuse dev keys for production media.
- Require documented key-rotation and incident revocation steps.
- Keep offline backups of enrollment artifacts and recovery instructions.

## Build and Packaging Requirements

The build must fail when required signed artifacts are missing for Secure Boot profile builds.

Required outputs for a Secure Boot profile:

- Signed bootloader artifacts required by the selected boot chain.
- Signature manifest describing artifact hash + signer identity.
- Build summary that explicitly states Secure Boot profile pass/fail.

Non-goal for first Secure Boot step:

- Full PCR-policy-based unseal enforcement; keep this as a later gate after stable Secure Boot bring-up.

## Firmware Enrollment and Recovery

Document and test:

- Fresh enrollment procedure (PK/KEK/db).
- Rollback procedure to previous trusted key set.
- Recovery path when new keys are mis-enrolled or media is rejected.

Must-have recovery property:

- A known-good recovery medium and key bundle can always restore bootability on lab hardware.

## Validation Matrix

## Matrix A: Boot Success

- Secure Boot off + TPM on (baseline)
- Secure Boot on + TPM on (target)

Expected:

- System boots using intended path.
- Citadel production checks still pass.
- Desktop reaches expected runtime state.

## Matrix B: Refusal/Failure Behavior

Inject controlled faults:

- Unsigned boot artifact
- Tampered signed artifact
- Revoked signing key

Expected:

- Boot is refused deterministically.
- Operator-visible diagnostics indicate failed stage/artifact.
- Recovery instructions are actionable.

## Matrix C: TPM Continuity

With Secure Boot enabled:

- Verify TPM-backed anchor path remains active.
- Verify no unintended fallback to weaker recovery path.
- Verify measured-boot logging remains present.

## Exit Criteria for Secure Boot Milestone

- Secure Boot enabled boot is reproducible on primary test hardware.
- Failure modes are deterministic and documented.
- TPM-backed security behavior is unchanged or stronger.
- Build pipeline can produce and verify Secure Boot profile artifacts in CI/lab automation.

## Deferred Items

- Shim/MOK distribution path (if needed later).
- PCR-policy-gated TAS unseal enforcement.
- Remote attestation policy integration.
