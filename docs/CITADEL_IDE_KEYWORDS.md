# CITADEL IDE Language Keywords and Tokens (v0 Draft)

Status: Draft
Purpose: Starting token and command inventory for lexer/parser implementation.

## 1. Scope

This file defines an initial keyword/operator list and normalization rules.
It is not yet the full grammar, type system, or runtime spec.

## 2. Canonicalization Rules

- Language identifiers and keywords are case-sensitive in v0.
- Canonical form should be used in docs and diagnostics.
- Alias tokens map to canonical tokens during parse only when exact case matches the alias definition.

## 3. Core Control Keywords

Canonical keywords:
- begin
- end
- if
- for
- next
- do
- while

## 4. Print/Output Command

Canonical command:
- print

Syntax:
- print(string);
- print(format, arg1, arg2, ...);

Examples:
- print("Hello World\n");
- print("Hello %s\n", "David");
- print("%d, %s\n", 1, "measurement");

## 5. Operators (Canonical)

Assignment and arithmetic:
- =
- +
- -
- *
- /

Compound assignment:
- +=
- -=
- *=
- /=

Comparison:
- ==
- <
- >
- <=
- >=

Note:
- `=>` is treated as invalid in v0.
- Use `>=` for greater-than-or-equal.

## 6. Word-Form Math Operators and Aliases

Canonical function-style words:
- add
- subtract
- multiply
- divide
- abs
- floor
- avg
- random

Aliases:
- eq -> ==
- rand -> random
- rnd -> random

## 7. Math Library Placeholder

v0 assumes standard math functions will be expanded later.
This document only records currently recognized starter tokens.

## 8. FileBlock Command (Structured I/O Primitive)

Canonical command:
- fileblock

Draft form:

```txt
FILEBLOCK fb {
    structure: DataStructure,   // optional
    data: Data,                 // optional
    count: 1,                   // optional companion field
    bytes: 0,                   // optional companion field
    filenum: 0,
    mode: rw,
    autoflush: true,            // optional
    autoclose: true             // optional
}
```

v0 notes:
- `filenum` and `mode` are required.
- `structure`, `data`, `autoflush`, and `autoclose` are optional.

### FileBlock `structure` and `data` semantics (v0)

- `structure` is a struct type descriptor that defines the data layout contract.
- `data` is a pointer-like reference to the start of a memory block.
- When both are provided, runtime treats `data` as bytes encoded according to `structure`.
- When `structure` is omitted, `data` is treated as raw/untyped bytes.
- When `data` is omitted, FILEBLOCK defines I/O metadata only (no immediate buffer binding).

Minimum expected behavior:
- Write path: serialize fields from `data` using `structure` layout.
- Read path: deserialize file bytes into `data` using `structure` layout.
- Validation: fail if required struct size exceeds available buffer size.

### FileBlock `count` and `bytes` companion fields (v0)

- `count` means number of structure elements in `data`.
- `bytes` means explicit byte length for the `data` block.
- If `structure` is present and `count` is set, expected byte size is `sizeof(structure) * count`.
- If both `count` and `bytes` are set with `structure`, they must match the computed size.
- If `structure` is omitted, `bytes` is the authoritative transfer size.
- If neither `count` nor `bytes` is provided:
    - with `structure`: default `count = 1`
    - without `structure`: size inference is implementation-defined and should be avoided

Validation rules:
- Reject negative/zero `count` (unless explicitly allowed for no-op operations in a future version).
- Reject `bytes = 0` when an actual transfer is requested.
- Reject overflow in `sizeof(structure) * count`.
- Reject transfers where computed/requested size exceeds buffer bounds.

Suggested companion fields for later grammar revisions:
- `count`: number of struct elements in the data block.
- `bytes`: explicit byte length for raw mode.

### FileBlock mode values (v0)

Canonical mode tokens:
- read
- write
- rw
- append

Accepted aliases:
- r -> read
- w -> write
- readwrite -> rw
- wr -> rw
- a -> append

Optional binary variants (reserved for v0.1+):
- rb, wb, rwb, ab

Examples:

```txt
FILEBLOCK inputFile {
    filenum: 1,
    mode: read,
    autoclose: true
}

FILEBLOCK outputFile {
    filenum: 2,
    mode: write,
    autoflush: true
}

FILEBLOCK logFile {
    filenum: 3,
    mode: append
}

FILEBLOCK workFile {
    filenum: 4,
    mode: rw
}

FILEBLOCK samples {
    structure: SensorSample,
    data: sampleBuffer,
    count: 64,
    filenum: 5,
    mode: rw,
    autoflush: true
}

FILEBLOCK rawPacket {
    data: packetBuffer,
    bytes: 1500,
    filenum: 6,
    mode: write
}
```

Mode behavior defaults:
- `read`: existing file required; no write operations.
- `write`: create/truncate target; write operations only.
- `rw`: create if missing; read and write operations allowed.
- `append`: create if missing; writes go to end of file.

## 9. SQL Keyword Set (Embedded/Interop Draft)

Recognized SQL-related words:
- select
- where
- order by
- limit
- create
- table
- insert
- delete
- update
- set

v0 note:
- Whether these are native statements or an embedded SQL mode is still TBD.

## 10. Grammar Sketch (Non-Normative)

```txt
statement        := print_stmt | control_stmt | fileblock_stmt | sql_stmt | expr_stmt
print_stmt       := "print" "(" arg_list ")" ";"
control_stmt     := if_stmt | for_stmt | while_stmt | do_while_stmt | block_stmt
block_stmt       := "{" statement* "}"
fileblock_stmt   := "fileblock" IDENT "{" fileblock_field* "}"
expr_stmt        := expression ";"
```

v0 grammar policy locks:
- Semicolons are required for all non-block statements.
- Braces are required for all blocks.
- begin/end may remain reserved words for compatibility/migration but are not block delimiters in v0 grammar.

## 11. Expression Precedence and Associativity (v0 Recommended)

Highest precedence appears first.

| Level | Operators | Associativity | Notes |
|---|---|---|---|
| 1 | unary +, unary - | right-to-left | prefix numeric sign |
| 2 | *, / | left-to-right | multiplicative |
| 3 | +, - | left-to-right | additive |
| 4 | <, <=, >, >= | left-to-right | relational |
| 5 | == | left-to-right | equality |
| 6 | =, +=, -=, *=, /= | right-to-left | assignment |

Examples under this table:
- a + b * c parses as a + (b * c)
- a = b = c parses as a = (b = c)
- a += b * 2 parses as a += (b * 2)

Recommended parser behavior:
- Reject chained relational comparisons like a < b < c in v0 unless explicitly transformed by the language later.
- Treat eq as an alias token for == before precedence handling.

## 12. Open Decisions

- Canonical style for SQL casing
- Whether to allow optional semicolons in REPL-only mode
- Whether begin/end should become strict synonyms for braces
- Whether logical operators (&&, ||, !) are added in v0.1

## 13. Expression Parse Test Matrix (v0)

Purpose: parser acceptance tests for precedence and associativity.

Rules applied:
- Multiplicative before additive.
- Relational before equality.
- Assignment operators are right-associative.
- Assignment left-hand side must be assignable.

| ID | Input | Expected grouping | Expected result |
|---|---|---|---|
| E01 | a + b * c; | a + (b * c) | valid |
| E02 | a * b + c; | (a * b) + c | valid |
| E03 | a * b / c; | (a * b) / c | valid |
| E04 | a + b - c; | (a + b) - c | valid |
| E05 | -a * b; | (-a) * b | valid |
| E06 | a + -b; | a + (-b) | valid |
| E07 | a < b == c; | (a < b) == c | valid |
| E08 | a == b < c; | a == (b < c) | valid |
| E09 | a <= b + c; | a <= (b + c) | valid |
| E10 | a + b >= c * d; | (a + b) >= (c * d) | valid |
| E11 | a = b; | a = b | valid |
| E12 | a = b = c; | a = (b = c) | valid |
| E13 | a += b * 2; | a += (b * 2) | valid |
| E14 | a *= b + c; | a *= (b + c) | valid |
| E15 | a = b + c * d; | a = (b + (c * d)) | valid |
| E16 | a + b = c; | (a + b) = c | invalid (lhs not assignable) |
| E17 | a < b < c; | (a < b) < c | invalid in v0 |
| E18 | a == b == c; | (a == b) == c | invalid in v0 |
| E19 | eq(a, b); | alias usage | invalid as function unless defined |
| E20 | a eq b; | a == b | valid if eq-token alias is enabled |
| E21 | a => b; | n/a | invalid token (use >=) |
| E22 | a /= b + c / d; | a /= (b + (c / d)) | valid |
| E23 | a = -b * -c; | a = ((-b) * (-c)) | valid |
| E24 | a = (b + c) * d; | a = ((b + c) * d) | valid |

Recommended test harness output fields:
- input
- parse_ok
- normalized_tokens
- grouped_form
- diagnostic_code

