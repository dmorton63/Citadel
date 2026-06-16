# Citadel Secure Boot Test Matrix

**Status:** Versioned test specification (Batch 3, Item 1)  
**Last Updated:** 2026-06-16  
**Version:** v1.0  
**Owner:** Citadel QA Team  
**References:** [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md), [SECURE_BOOT_REFUSAL_TAXONOMY.md](SECURE_BOOT_REFUSAL_TAXONOMY.md)

---

## 1. Matrix Dimensions

Each test case is identified by the combination of:

| Dimension | Values |
|-----------|--------|
| **Firmware Mode** | `SB-OFF`, `SB-SETUP`, `SB-ON` |
| **TPM State** | `TPM-OFF`, `TPM-ON` |
| **Key Set** | `LAB`, `STAGING`, `PROD-SIM` |
| **Artifact State** | `CLEAN`, `TAMPERED-LIMINE`, `TAMPERED-BOOTGATE`, `TAMPERED-KERNEL`, `TAMPERED-BOOTJSON`, `TAMPERED-MODULE`, `UNSIGNED`, `REVOKED-DBX` |
| **Expected Result** | `BOOT-OK`, `BOOT-DEGRADED`, `BOOT-HALT`, `SB-BYPASS` |

---

## 2. Test Matrix — Positive Cases

| ID | Firmware | TPM | Key Set | Artifact State | Expected | Error Code | Priority |
|----|----------|-----|---------|---------------|----------|-----------|---------|
| P-01 | SB-ON | TPM-ON | LAB | CLEAN | BOOT-OK | — | P0 |
| P-02 | SB-ON | TPM-OFF | LAB | CLEAN | BOOT-OK | — | P0 |
| P-03 | SB-OFF | TPM-ON | LAB | CLEAN | SB-BYPASS | — | P1 |
| P-04 | SB-OFF | TPM-OFF | LAB | CLEAN | SB-BYPASS | — | P1 |
| P-05 | SB-ON | TPM-ON | STAGING | CLEAN | BOOT-OK | — | P0 |
| P-06 | SB-ON | TPM-ON | PROD-SIM | CLEAN | BOOT-OK | — | P0 |

---

## 3. Test Matrix — Negative Cases (Tampering)

| ID | Firmware | TPM | Key Set | Artifact State | Expected | Error Code | Fallback |
|----|----------|-----|---------|---------------|----------|-----------|---------|
| N-01 | SB-ON | TPM-ON | LAB | TAMPERED-LIMINE | BOOT-HALT | SB-2001 | None (UEFI halts) |
| N-02 | SB-ON | TPM-ON | LAB | TAMPERED-BOOTGATE | BOOT-DEGRADED | SB-2002 | Limine skips BootGate |
| N-03 | SB-ON | TPM-ON | LAB | TAMPERED-KERNEL | BOOT-OK (backup) | SB-2003 | BootGate loads backup kernel |
| N-04 | SB-ON | TPM-ON | LAB | TAMPERED-BOOTJSON | BOOT-DEGRADED | SB-2004 | Kernel boots, modules disabled |
| N-05 | SB-ON | TPM-ON | LAB | TAMPERED-MODULE | BOOT-DEGRADED | SB-2005 | Kernel skips failed module |
| N-06 | SB-ON | TPM-ON | LAB | UNSIGNED | BOOT-HALT | SB-1001 | None |
| N-07 | SB-ON | TPM-ON | LAB | REVOKED-DBX | BOOT-HALT | SB-3001 | None |
| N-08 | SB-ON | TPM-OFF | LAB | TAMPERED-LIMINE | BOOT-HALT | SB-2001 | None |
| N-09 | SB-ON | TPM-ON | STAGING | TAMPERED-KERNEL | BOOT-OK (backup) | SB-2003 | BootGate loads backup kernel |
| N-10 | SB-OFF | TPM-ON | LAB | TAMPERED-LIMINE | SB-BYPASS | — | SB disabled; no check |

---

## 4. Test Matrix — Edge / Stress Cases

| ID | Firmware | TPM | Key Set | Scenario | Expected | Notes |
|----|----------|-----|---------|---------|---------|-------|
| E-01 | SB-ON | TPM-ON | LAB | 10× consecutive cold boots | BOOT-OK ×10 | Detect flaky TPM PCR extension |
| E-02 | SB-ON | TPM-ON | LAB | Rapid reboot (< 5s between boots) | BOOT-OK | Test TPM lockout counter behaviour |
| E-03 | SB-ON | TPM-ON | LAB | Key rotation mid-test-run | BOOT-OK after rotation | Verify old artifacts fail, new pass |
| E-04 | SB-ON | TPM-ON | LAB | Firmware reset (keys cleared) | BOOT-HALT then RECOVER | Recovery bundle procedure |
| E-05 | SB-SETUP | TPM-ON | LAB | Enroll new key set | BOOT-OK after enrollment | Fresh-enrollment runbook test |
| E-06 | SB-ON | TPM-ON | LAB | All modules corrupted | BOOT-DEGRADED | Kernel must not panic if modules optional |
| E-07 | SB-ON | TPM-ON | LAB | Missing backup kernel | BOOT-HALT | No fallback when backup absent |

---

## 5. Test Execution Record Template

Fill one block per test run:

```
Test ID:      ____
Date/Time:    ____  UTC
Hardware:     ____  (make/model/SN)
Firmware:     ____  (version)
Kernel:       ____  (citadel-vX.Y.Z)
Key Set:      ____  (LAB / STAGING / PROD-SIM)
Environment:  ____

Actual Result: BOOT-OK / BOOT-DEGRADED / BOOT-HALT / SB-BYPASS
Error Code:    ____  (or NONE)
Fallback Used: YES / NO

Serial log:   build/logs/test-{ID}-{date}.log
Manifest:     build/secure-boot-manifest-{env}.json
PCR snapshot: build/pcr-snapshot-{date}.txt

Pass/Fail:    PASS / FAIL
Signed off:   ____
```

---

## 6. Coverage Requirements

Before staging promotion, the following IDs **must** have a recorded `PASS`:

**Mandatory (P0):** P-01, P-02, P-05, N-01, N-02, N-03, N-04, N-05, N-06, N-07, E-01  
**Recommended (P1):** P-03, P-04, P-06, N-08, N-09, N-10, E-02, E-03, E-04, E-07

---

## References

- [SECURE_BOOT_REFUSAL_TAXONOMY.md](SECURE_BOOT_REFUSAL_TAXONOMY.md)
- [SECURE_BOOT_TEST_CASES_AND_LOGS.md](SECURE_BOOT_TEST_CASES_AND_LOGS.md)
- [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md)
