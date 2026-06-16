# Secure Boot Escrow Access Logging and Review Policy

## Purpose
Protect recovery and enrollment materials through controlled, auditable escrow access.

## Policy Controls
- Escrow materials are encrypted at rest and in transit.
- Access requires dual-authorizer release approval.
- Every access event must include requester, approver, reason, and ticket ID.
- Access logs are immutable and retained for 7 years.

## Access Workflow
1. Request submitted with incident/change ticket reference.
2. Two independent approvers authorize release.
3. Escrow release event logged automatically.
4. Materials returned/resealed after use and integrity checked.

## Review Cadence
- Quarterly access-log review by Security Governance.
- Annual control effectiveness review in Secure Boot governance meeting.

## Mandatory Fields for Escrow Access Log
- timestamp_utc
- requester_id
- approver_id
- purpose
- ticket_id
- material_scope
- release_duration_minutes
- return_verification_status
