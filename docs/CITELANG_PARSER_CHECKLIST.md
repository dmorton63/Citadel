# CiteLang Parser Checklist

This checklist translates the language draft into concrete parser implementation tasks.

## 1. Lexer

- [ ] Define token set for keywords, identifiers, literals, operators, punctuation.
- [ ] Implement numeric literal lexer: int, float, decimal suffix and exponent forms.
- [ ] Implement string and char literal escaping rules.
- [ ] Track source span metadata on every token (file, line, column, length).
- [ ] Emit stable lexer errors with codes.

Acceptance:
- [ ] Token snapshot tests pass for representative language samples.
- [ ] Lexer returns deterministic token streams across repeated runs.

## 2. AST Model

- [ ] Define AST nodes for top-level declarations: import, mod, data, template, func, main.
- [ ] Define AST nodes for statements: declaration, return, if, while, for, block, expression statement.
- [ ] Define AST nodes for expressions: assignment, binary, unary, call, index, member, cast, literals.
- [ ] Include source span on all AST nodes.

Acceptance:
- [ ] AST schema can represent every production in docs/CITELANG_GRAMMAR.ebnf.

## 3. Parser Core

- [ ] Implement recursive-descent parser matching docs/CITELANG_GRAMMAR.ebnf.
- [ ] Implement precedence climbing or layered methods for expression parsing.
- [ ] Enforce statement terminators and block delimiters.
- [ ] Parse nullable types and generic collection types.

Acceptance:
- [ ] Valid sample programs parse with no diagnostics.
- [ ] Invalid samples produce the expected primary error codes.

## 4. Diagnostics

- [ ] Implement parser error codes from IDE.md (E_PARSE_UNEXPECTED_TOKEN, E_PARSE_UNCLOSED_BLOCK).
- [ ] Add contextual messages with expected vs found token.
- [ ] Implement panic-mode synchronization at safe boundaries (`;`, `}`, top-level keyword).

Acceptance:
- [ ] Parser continues after first error and reports multiple useful diagnostics.
- [ ] Diagnostics include source spans and stable error codes.

## 5. Language Rules in Parse Phase

- [ ] Enforce exactly one main declaration per file graph entry.
- [ ] Reject import cycles during module graph build (`E_IMPORT_CYCLE`).
- [ ] Validate restricted constructs at parse boundary where possible.

Acceptance:
- [ ] Single-main and import-cycle checks are covered by tests.

## 6. Golden Tests

- [ ] Add positive golden tests for each top-level construct and expression family.
- [ ] Add negative golden tests for malformed syntax and missing delimiters.
- [ ] Add precedence tests to prove parse tree shape.

Acceptance:
- [ ] Golden suite passes in CI.
- [ ] Parse tree snapshots are stable and reviewed.

## 7. Handoff Outputs

- [ ] Emit AST in debug JSON format for downstream type checker bring-up.
- [ ] Provide parser API contract doc (entry points, error collection behavior).
- [ ] Write minimal parser integration note for IDE syntax diagnostics.

Acceptance:
- [ ] Type checker prototype consumes emitted AST without schema mismatch.
- [ ] IDE integration can display parse diagnostics with source spans.
