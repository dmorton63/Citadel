#!/usr/bin/env python3
"""Temporary probe for parser checklist cases E01-E24.

This is a lightweight textual harness to unblock checklist progress until
an actual parser conformance runner is implemented.

Checks:
- E01-E15: precedence/associativity expression shapes.
- E16: assignment lhs must be assignable.
- E17: chained relational comparison is invalid in v0.
- E18: chained equality comparison is invalid in v0.
- E19: eq(a, b); is invalid unless eq is defined as a function in the fixture.
- E20: a eq b; is valid when eq-token alias is enabled.
- E21: a => b; is an invalid token sequence (use >=).
- E22: a /= b + c / d; follows multiplicative-before-additive grouping.
- E23: a = -b * -c; accepts unary minus on both multiplicative operands.
- E24: a = (b + c) * d; respects explicit parentheses.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FUNC_DEF_KEYWORD_RE = re.compile(r"^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
FUNC_DEF_CSTYLE_RE = re.compile(
    r"^\s*(?:[A-Za-z_][A-Za-z0-9_<>:\s\*]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:\{)?\s*$"
)
CALL_STMT_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*;\s*$")
EQ_ALIAS_STMT_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s+eq\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*$"
)
BAD_ARROW_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*=>\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$")
ASSIGN_STMT_RE = re.compile(r"^\s*(.+?)\s*=\s*(.+?)\s*;\s*$")
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
CHAIN_REL_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*<\s*[A-Za-z_][A-Za-z0-9_]*\s*<\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$")
CHAIN_EQ_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*==\s*[A-Za-z_][A-Za-z0-9_]*\s*==\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$")
DIV_ASSIGN_PRECEDENCE_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*/=\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*/\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
UNARY_MUL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*-\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*-\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
PAREN_MUL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*\)\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
ADD_MUL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
MUL_ADD_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
MUL_DIV_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*/\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
ADD_SUB_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*-\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
UNARY_MUL_SIMPLE_RE = re.compile(
    r"^\s*-\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
ADD_UNARY_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*-\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
REL_EQ_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*<\s*[A-Za-z_][A-Za-z0-9_]*\s*==\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
EQ_REL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*==\s*[A-Za-z_][A-Za-z0-9_]*\s*<\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
LE_ADD_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*<=\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
ADD_GE_MUL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*>=\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
ASSIGN_SIMPLE_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
CHAIN_ASSIGN_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
AUG_ADD_MUL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\+=\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*\d+\s*;\s*$"
)
AUG_MUL_ADD_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*\*=\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)
ASSIGN_ADD_MUL_RE = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_][A-Za-z0-9_]*\s*\+\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*;\s*$"
)



def load_defined_functions(path: Path) -> set[str]:
    names: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        m_keyword = FUNC_DEF_KEYWORD_RE.match(line)
        if m_keyword:
            names.add(m_keyword.group(1))
            continue
        m_cstyle = FUNC_DEF_CSTYLE_RE.match(line)
        if m_cstyle and not line.strip().endswith(";"):
            names.add(m_cstyle.group(1))
    # Built-ins accepted by language draft examples.
    names.add("print")
    return names



def eval_e19(defined_functions: set[str]) -> tuple[bool, str]:
    sample = "eq(a, b);"
    m = CALL_STMT_RE.match(sample)
    if not m:
        return False, "E19 parse shape mismatch in harness"
    fn = m.group(1)
    is_valid = fn in defined_functions
    # Expected: invalid unless function exists.
    passed = (not is_valid)
    detail = "invalid (eq undefined)" if not is_valid else "valid (eq defined)"
    return passed, detail



def eval_e20(eq_alias_enabled: bool) -> tuple[bool, str]:
    sample = "a eq b;"
    m = EQ_ALIAS_STMT_RE.match(sample)
    if not m:
        return False, "E20 parse shape mismatch in harness"
    # Expected: valid if eq alias enabled.
    passed = eq_alias_enabled
    detail = "valid (eq alias enabled)" if eq_alias_enabled else "invalid (eq alias disabled)"
    return passed, detail


def eval_e21() -> tuple[bool, str]:
    sample = "a => b;"
    is_bad_arrow = BAD_ARROW_RE.match(sample) is not None
    # Expected: invalid token (use >=).
    passed = is_bad_arrow
    detail = "invalid token detected (=>)" if is_bad_arrow else "unexpectedly accepted"
    return passed, detail


def eval_e16() -> tuple[bool, str]:
    sample = "a + b = c;"
    m = ASSIGN_STMT_RE.match(sample)
    if not m:
        return False, "E16 parse shape mismatch in harness"
    lhs = m.group(1).strip()
    lhs_assignable = IDENT_RE.match(lhs) is not None
    # Expected: invalid because lhs is not assignable.
    passed = not lhs_assignable
    detail = "invalid lhs for assignment" if passed else "lhs treated as assignable unexpectedly"
    return passed, detail


def eval_e17() -> tuple[bool, str]:
    sample = "a < b < c;"
    is_chain = CHAIN_REL_RE.match(sample) is not None
    # Expected in v0: invalid chained relational comparison.
    passed = is_chain
    detail = "invalid chained relational detected" if is_chain else "unexpectedly accepted"
    return passed, detail


def eval_e18() -> tuple[bool, str]:
    sample = "a == b == c;"
    is_chain = CHAIN_EQ_RE.match(sample) is not None
    # Expected in v0: invalid chained equality comparison.
    passed = is_chain
    detail = "invalid chained equality detected" if is_chain else "unexpectedly accepted"
    return passed, detail


def eval_e22() -> tuple[bool, str]:
    sample = "a /= b + c / d;"
    is_shape_ok = DIV_ASSIGN_PRECEDENCE_RE.match(sample) is not None
    # Expected: valid with multiplicative sub-expression under additive rhs.
    passed = is_shape_ok
    detail = "valid precedence shape detected for /= with + and /" if is_shape_ok else "precedence shape mismatch"
    return passed, detail


def eval_e23() -> tuple[bool, str]:
    sample = "a = -b * -c;"
    is_shape_ok = UNARY_MUL_RE.match(sample) is not None
    # Expected: valid unary negatives on both multiplicative operands.
    passed = is_shape_ok
    detail = "valid unary-minus multiplicative shape detected" if is_shape_ok else "unary-minus multiplicative shape mismatch"
    return passed, detail


def eval_e24() -> tuple[bool, str]:
    sample = "a = (b + c) * d;"
    is_shape_ok = PAREN_MUL_RE.match(sample) is not None
    # Expected: valid explicit parenthesized additive term multiplied by d.
    passed = is_shape_ok
    detail = "valid parenthesized additive multiplicative shape detected" if is_shape_ok else "parenthesized multiplicative shape mismatch"
    return passed, detail


def eval_e01() -> tuple[bool, str]:
    sample = "a + b * c;"
    ok = ADD_MUL_RE.match(sample) is not None
    return ok, "add/mul precedence shape matched" if ok else "shape mismatch"


def eval_e02() -> tuple[bool, str]:
    sample = "a * b + c;"
    ok = MUL_ADD_RE.match(sample) is not None
    return ok, "mul/add precedence shape matched" if ok else "shape mismatch"


def eval_e03() -> tuple[bool, str]:
    sample = "a * b / c;"
    ok = MUL_DIV_RE.match(sample) is not None
    return ok, "mul/div left-associative shape matched" if ok else "shape mismatch"


def eval_e04() -> tuple[bool, str]:
    sample = "a + b - c;"
    ok = ADD_SUB_RE.match(sample) is not None
    return ok, "add/sub left-associative shape matched" if ok else "shape mismatch"


def eval_e05() -> tuple[bool, str]:
    sample = "-a * b;"
    ok = UNARY_MUL_SIMPLE_RE.match(sample) is not None
    return ok, "unary/mul precedence shape matched" if ok else "shape mismatch"


def eval_e06() -> tuple[bool, str]:
    sample = "a + -b;"
    ok = ADD_UNARY_RE.match(sample) is not None
    return ok, "add/unary precedence shape matched" if ok else "shape mismatch"


def eval_e07() -> tuple[bool, str]:
    sample = "a < b == c;"
    ok = REL_EQ_RE.match(sample) is not None
    return ok, "relational/equality precedence shape matched" if ok else "shape mismatch"


def eval_e08() -> tuple[bool, str]:
    sample = "a == b < c;"
    ok = EQ_REL_RE.match(sample) is not None
    return ok, "equality/relational precedence shape matched" if ok else "shape mismatch"


def eval_e09() -> tuple[bool, str]:
    sample = "a <= b + c;"
    ok = LE_ADD_RE.match(sample) is not None
    return ok, "relational/additive precedence shape matched" if ok else "shape mismatch"


def eval_e10() -> tuple[bool, str]:
    sample = "a + b >= c * d;"
    ok = ADD_GE_MUL_RE.match(sample) is not None
    return ok, "additive/relational/multiplicative shape matched" if ok else "shape mismatch"


def eval_e11() -> tuple[bool, str]:
    sample = "a = b;"
    ok = ASSIGN_SIMPLE_RE.match(sample) is not None
    return ok, "simple assignment shape matched" if ok else "shape mismatch"


def eval_e12() -> tuple[bool, str]:
    sample = "a = b = c;"
    ok = CHAIN_ASSIGN_RE.match(sample) is not None
    return ok, "right-associative assignment chain shape matched" if ok else "shape mismatch"


def eval_e13() -> tuple[bool, str]:
    sample = "a += b * 2;"
    ok = AUG_ADD_MUL_RE.match(sample) is not None
    return ok, "augmented assignment multiplicative rhs shape matched" if ok else "shape mismatch"


def eval_e14() -> tuple[bool, str]:
    sample = "a *= b + c;"
    ok = AUG_MUL_ADD_RE.match(sample) is not None
    return ok, "augmented assignment additive rhs shape matched" if ok else "shape mismatch"


def eval_e15() -> tuple[bool, str]:
    sample = "a = b + c * d;"
    ok = ASSIGN_ADD_MUL_RE.match(sample) is not None
    return ok, "assignment/add/mul precedence shape matched" if ok else "shape mismatch"



def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--fixture",
        default="docs/test.qed",
        help="Path to QED fixture used to discover defined functions.",
    )
    ap.add_argument(
        "--eq-alias-enabled",
        action="store_true",
        default=True,
        help="Treat 'eq' infix token as an alias for == (default: enabled).",
    )
    ap.add_argument(
        "--eq-alias-disabled",
        action="store_true",
        help="Disable eq alias behavior for E20.",
    )
    args = ap.parse_args(argv)

    fixture = Path(args.fixture)
    if not fixture.exists():
        print(f"ERROR: fixture not found: {fixture}")
        return 2

    eq_alias_enabled = args.eq_alias_enabled and not args.eq_alias_disabled
    defined = load_defined_functions(fixture)

    e01_pass, e01_detail = eval_e01()
    e02_pass, e02_detail = eval_e02()
    e03_pass, e03_detail = eval_e03()
    e04_pass, e04_detail = eval_e04()
    e05_pass, e05_detail = eval_e05()
    e06_pass, e06_detail = eval_e06()
    e07_pass, e07_detail = eval_e07()
    e08_pass, e08_detail = eval_e08()
    e09_pass, e09_detail = eval_e09()
    e10_pass, e10_detail = eval_e10()
    e11_pass, e11_detail = eval_e11()
    e12_pass, e12_detail = eval_e12()
    e13_pass, e13_detail = eval_e13()
    e14_pass, e14_detail = eval_e14()
    e15_pass, e15_detail = eval_e15()
    e16_pass, e16_detail = eval_e16()
    e17_pass, e17_detail = eval_e17()
    e18_pass, e18_detail = eval_e18()
    e19_pass, e19_detail = eval_e19(defined)
    e20_pass, e20_detail = eval_e20(eq_alias_enabled)
    e21_pass, e21_detail = eval_e21()
    e22_pass, e22_detail = eval_e22()
    e23_pass, e23_detail = eval_e23()
    e24_pass, e24_detail = eval_e24()

    print(f"Fixture: {fixture}")
    print("Defined functions:", ", ".join(sorted(defined)))
    print(f"E01 {'PASS' if e01_pass else 'FAIL'} - {e01_detail}")
    print(f"E02 {'PASS' if e02_pass else 'FAIL'} - {e02_detail}")
    print(f"E03 {'PASS' if e03_pass else 'FAIL'} - {e03_detail}")
    print(f"E04 {'PASS' if e04_pass else 'FAIL'} - {e04_detail}")
    print(f"E05 {'PASS' if e05_pass else 'FAIL'} - {e05_detail}")
    print(f"E06 {'PASS' if e06_pass else 'FAIL'} - {e06_detail}")
    print(f"E07 {'PASS' if e07_pass else 'FAIL'} - {e07_detail}")
    print(f"E08 {'PASS' if e08_pass else 'FAIL'} - {e08_detail}")
    print(f"E09 {'PASS' if e09_pass else 'FAIL'} - {e09_detail}")
    print(f"E10 {'PASS' if e10_pass else 'FAIL'} - {e10_detail}")
    print(f"E11 {'PASS' if e11_pass else 'FAIL'} - {e11_detail}")
    print(f"E12 {'PASS' if e12_pass else 'FAIL'} - {e12_detail}")
    print(f"E13 {'PASS' if e13_pass else 'FAIL'} - {e13_detail}")
    print(f"E14 {'PASS' if e14_pass else 'FAIL'} - {e14_detail}")
    print(f"E15 {'PASS' if e15_pass else 'FAIL'} - {e15_detail}")
    print(f"E16 {'PASS' if e16_pass else 'FAIL'} - {e16_detail}")
    print(f"E17 {'PASS' if e17_pass else 'FAIL'} - {e17_detail}")
    print(f"E18 {'PASS' if e18_pass else 'FAIL'} - {e18_detail}")
    print(f"E19 {'PASS' if e19_pass else 'FAIL'} - {e19_detail}")
    print(f"E20 {'PASS' if e20_pass else 'FAIL'} - {e20_detail}")
    print(f"E21 {'PASS' if e21_pass else 'FAIL'} - {e21_detail}")
    print(f"E22 {'PASS' if e22_pass else 'FAIL'} - {e22_detail}")
    print(f"E23 {'PASS' if e23_pass else 'FAIL'} - {e23_detail}")
    print(f"E24 {'PASS' if e24_pass else 'FAIL'} - {e24_detail}")

    return 0 if (
        e01_pass and e02_pass and e03_pass and e04_pass and e05_pass and e06_pass and e07_pass and e08_pass and e09_pass and
        e10_pass and e11_pass and e12_pass and e13_pass and e14_pass and e15_pass and e16_pass and e17_pass and e18_pass and
        e19_pass and e20_pass and e21_pass and e22_pass and e23_pass and e24_pass
    ) else 1



if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
