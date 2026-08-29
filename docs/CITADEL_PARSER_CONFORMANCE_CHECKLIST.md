# CITADEL Parser Conformance Checklist (v0)

Status: Draft
Source matrix: docs/CITADEL_IDE_KEYWORDS.md (Section 13)

CSV companion: docs/CITADEL_PARSER_CONFORMANCE_CHECKLIST.csv

## Usage

- Mark each test as Pass, Fail, or Not Run per milestone.
- Keep notes short and actionable.
- Do not change expected grouping here; update the language spec first if semantics change.
- For automated runs, update the CSV status and notes fields per row (E01-E24).
- Validate CSV integrity with: python3 tools/validate_parser_conformance_csv.py
- CI enforcement: .github/workflows/parser-conformance-csv.yml

## Status Legend

- Pass: parser output matches expected grouping/diagnostic.
- Fail: parser output differs from expected behavior.
- Not Run: test was not executed in current milestone.

## Milestone Tracker

Milestone: __________
Date: __________
Parser build/ref: __________

## Conformance Table

| ID | Status | Input | Expected grouping/result | Notes |
|---|---|---|---|---|
| E01 | Not Run | `a + b * c;` | `a + (b * c)` valid | |
| E02 | Not Run | `a * b + c;` | `(a * b) + c` valid | |
| E03 | Not Run | `a * b / c;` | `(a * b) / c` valid | |
| E04 | Not Run | `a + b - c;` | `(a + b) - c` valid | |
| E05 | Not Run | `-a * b;` | `(-a) * b` valid | |
| E06 | Not Run | `a + -b;` | `a + (-b)` valid | |
| E07 | Not Run | `a < b == c;` | `(a < b) == c` valid | |
| E08 | Not Run | `a == b < c;` | `a == (b < c)` valid | |
| E09 | Not Run | `a <= b + c;` | `a <= (b + c)` valid | |
| E10 | Not Run | `a + b >= c * d;` | `(a + b) >= (c * d)` valid | |
| E11 | Not Run | `a = b;` | `a = b` valid | |
| E12 | Not Run | `a = b = c;` | `a = (b = c)` valid | |
| E13 | Not Run | `a += b * 2;` | `a += (b * 2)` valid | |
| E14 | Not Run | `a *= b + c;` | `a *= (b + c)` valid | |
| E15 | Not Run | `a = b + c * d;` | `a = (b + (c * d))` valid | |
| E16 | Not Run | `a + b = c;` | invalid (lhs not assignable) | |
| E17 | Not Run | `a < b < c;` | invalid in v0 | |
| E18 | Not Run | `a == b == c;` | invalid in v0 | |
| E19 | Not Run | `eq(a, b);` | invalid as function unless defined | |
| E20 | Not Run | `a eq b;` | `a == b` valid if eq alias enabled | |
| E21 | Not Run | `a => b;` | invalid token (use >=) | |
| E22 | Not Run | `a /= b + c / d;` | `a /= (b + (c / d))` valid | |
| E23 | Not Run | `a = -b * -c;` | `a = ((-b) * (-c))` valid | |
| E24 | Not Run | `a = (b + c) * d;` | `a = ((b + c) * d)` valid | |

## Summary Block

Total tests: 24
Pass: ____
Fail: ____
Not Run: ____

## Failure Notes Template

- ID:
- Observed output/grouping:
- Expected output/grouping:
- Diagnostic code/message:
- Suspected parser stage:
- Fix commit/reference:
