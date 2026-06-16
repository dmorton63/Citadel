# Secure Boot Operations SLOs

## Scope
Detection, triage, recovery, and remediation closure for Secure Boot operations.

## Service-Level Objectives

- Detection SLO: >= 99% of critical Secure Boot failures detected within 15 minutes.
- Triage SLO: >= 95% of critical incidents triaged within 30 minutes.
- Recovery SLO: >= 95% of signing-service incidents recovered within 4 hours.
- Remediation Closure SLO: >= 90% of high-severity findings closed within 30 days.

## Error Budgets

- Detection error budget: 1% monthly.
- Triage error budget: 5% monthly.
- Recovery error budget: 5% monthly.

## Measurement Inputs

- CI gate failures and timestamps
- Incident response timeline logs
- Drill reports and closeout records
- Governance action tracker
