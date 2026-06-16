# Secure Boot Independent Verification Checkpoint

## Purpose
Require independent review for high-severity Secure Boot changes by a non-implementing team.

## Trigger Conditions
- Any change with high-severity security impact.
- Any change to key hierarchy, promotion policy, or release blockers.
- Any change that modifies trust-verification code paths.

## Required Review Inputs
- Change summary and impact assessment
- Test evidence from adversarial suite
- Rollback safety certificate draft
- Risk acceptance statement for residual risk

## Approval Rules
- Reviewer must not be author or direct implementer.
- Minimum two reviewers: one security, one verification/release.
- All blockers resolved before production promotion.

## Decision Record Fields
- Change ID
- Reviewer identities
- Decision (Approved / Changes Required / Rejected)
- Open risks
- Follow-up actions and due dates
