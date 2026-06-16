# Secure Boot Program Architecture Record

Version: 1.0
Date: 2026-06-16

## Summary
Citadel Secure Boot architecture enforces trust from firmware through signed artifacts with policy gates, governance controls, and recovery pathways.

## Core Decisions

1. Direct signed trust chain for v1 before optional shim/MOK expansion.
2. Distinct key hierarchy across lab, staging, and production.
3. Fail-closed release policy for signature/provenance/control violations.
4. Dual-control for production signing operations.
5. Continuous adversarial validation as promotion blocker.

## Constraints

- HSM-backed signing boundary required for production.
- Recovery path must remain operational under signer or key incident.
- Evidence generation and retention required for governance and audit.

## Accepted Risks

- PCR-policy-bound unseal remains deferred to later phase.
- Wider firmware compatibility coverage incrementally expands over time.

## Deferred Enhancements

- Remote attestation integration
- Expanded adversarial automation depth
- Additional policy-as-code controls for fleet orchestration
