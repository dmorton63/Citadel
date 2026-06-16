# Citadel Secure Boot Batch 7 Signing Service Controls

Status: Implemented (Batch 7, Items 1-10)
Last Updated: 2026-06-16
Owner: Security Engineering + Operations

## 1. Signing-Service Architecture Baseline

Trust tiers:
- Offline Root (OR): long-lived offline root key material; never internet-connected.
- Online Intermediate (OI): short-lived signing intermediates for operational signing.
- Workload Signers: environment-specific signers (LAB, STAGING, PRODUCTION).

HSM boundaries:
- OR keys: offline hardware token / escrow-only restoration path.
- OI keys: HSM-only usage; no private key export allowed.
- Production signers: HSM-backed API signing only.

Failover path:
1. Primary signer degraded -> switch to standby signer profile in same HSM trust tier.
2. HSM region outage -> activate secondary region HSM and rehydrate approved cert chain.
3. Full signing-service outage -> emergency offline signing window with dual-control ticket and post-facto attestation upload.

## 2. Mandatory Dual-Control Approval Workflow

Production signing requires two distinct operators:
- requester: initiates signing request
- approver: independently approves

Constraints:
- requester and approver must differ.
- both identities must be present in approved operator roster.
- approval is time-bound (default: 60 minutes).

Enforcement tool:
- tools/dual_control_approve.py

## 3. Signer Environment Integrity Attestation

Signing host must pass baseline attestation prior to key access:
- approved OS fingerprint
- approved kernel version baseline
- secure boot enabled on signer host
- required signer tool versions and hashes
- no known prohibited processes active

Enforcement tool:
- tools/check_signer_environment.py

## 4. Reproducible Signer-Container Pinning

Policy:
- signer images must be pinned by immutable digest.
- tag-only references are prohibited in production.
- digest lock updates require change-control ticket.

Artifacts:
- signing/signer-images.lock

## 5. Dependency Trust Policy for Signing Toolchain

Requirements:
- version pinning for all signer dependencies
- signature verification for downloaded binaries
- provenance metadata required for each dependency
- deny-list support for vulnerable dependency versions

Policy artifact:
- signing/dependency-trust-policy.json

## 6. Emergency Signer Compromise Containment Runbook

Sequence:
1. disable compromised signer key
2. quarantine artifacts signed since compromise window start
3. revoke trust path via dbx/issuer policy update
4. rotate signer identity and re-sign clean artifacts
5. publish customer/operator communication
6. run retrospective and update controls

## 7. Continuous Provenance Verification (Commit -> Artifact)

Every signed artifact must include tamper-evident linkage:
- source commit SHA
- build ID
- signer ID
- manifest ID
- artifact hash
- signature hash

Enforcement tool:
- tools/verify_provenance_chain.py

## 8. Third-Party Firmware Vetting Checklist

Reject criteria:
- unsigned firmware package
- weak signature algorithm (e.g., SHA-1, RSA-1024)
- missing provenance metadata
- failed authenticity verification
- no rollback guidance from supplier

## 9. Secure Escrow Policy for Recovery/Enrollment Materials

Requirements:
- escrow access logging (who, when, why)
- quarterly access review
- dual-authorizer release control
- periodic escrow integrity spot-check

## 10. Annual Signing-Service DR Drill

Cadence: annual mandatory exercise
Measured objectives:
- RTO (service recovery): <= 4 hours
- key recovery validation: <= 2 hours
- attestation pipeline restoration: <= 1 hour

Output artifacts:
- drill timeline
- objective pass/fail summary
- corrective actions and owners
