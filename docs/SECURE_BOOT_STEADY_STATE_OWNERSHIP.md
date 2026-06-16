# Secure Boot Steady-State Ownership Model

Status: Active
Last Updated: 2026-06-16

## Ownership Domains

- Engineering: implementation ownership for trust-chain code, signer tooling, and release integration.
- Security: key policy ownership, incident response authority, exception governance.
- Operations: runtime monitoring, drill execution, evidence retention, and fleet conformance operations.

## Duty Roster

Primary and backup roles:
- Engineering Primary: Release Platform Lead
- Engineering Backup: Kernel Platform Lead
- Security Primary: Product Security Lead
- Security Backup: Security Operations Lead
- Operations Primary: Site Reliability Lead
- Operations Backup: Infrastructure Operations Lead

## Escalation and Decision Rights

- P0 trust-break incidents: Security incident commander has immediate containment authority.
- Release blocker override: prohibited without documented exception approved by Security + Release Engineering.
- Key rotation emergency: Security primary + Operations primary required.

## Cadence

- Weekly operating review
- Monthly governance review
- Quarterly control self-assessment
