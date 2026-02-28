Citadel JSON Function Modules — MVP Spec v0.1 (Experimental)

This document locks a minimal, deterministic JSON IR for “function modules” that Citadel can validate and execute via an interpreter. It is deliberately constrained so it can be trusted before any JIT/DLL work begins.

Normative keywords: MUST, MUST NOT, SHOULD, MAY.

---

## 1) MVP Goals / Non‑Goals

### Goals
- Load JSON function modules, validate deterministically, register them, and execute via a single interpreter backend.
- Keep semantics analyzable: deterministic order, strict typing, no implicit conversions, no side effects.
- Preserve an upgrade path to signature enforcement, overrides, JIT, and DLL promotion later.

### Non‑Goals (out of scope for MVP)
- JIT / AOT / DLL generation and loading.
- Calling arbitrary native C/C++ or kernel functions.
- Complex control flow (loops), pointers, raw memory access, syscalls.

---

## 2) File & Loading Conventions (MVP)

- Files MUST be strict JSON (no comments / JSONC).
- Recommended naming: `<name>.fn.json`.
- Search paths (conceptual; mount points can change):
  - System: `/QJSystem/QJSYSFunctions/`
  - User: `/QJUser/QJFunctions/`
- Loading behavior: `LOAD_ON_USE` (first request loads + validates + registers).

---

## 3) Top‑Level JSON Shape

- Root MUST be exactly: `{ "function": { ... } }`.
- Unknown keys at the top level MUST be rejected in all modes.

Schema strictness (MVP lock):
- In both `DEBUG` and `PRODUCTION`, unknown keys within the `function` object MUST be rejected.
- The only escape hatch for experimental or tool-specific data in MVP is `function.metadata`.
  - `metadata` MAY contain arbitrary keys/structures.
  - `metadata` MUST NOT affect execution in MVP.
  - `metadata` remains covered by `auth.hash` and `auth.signature` (because it is part of the signed payload).

---

## 4) Identity, Trust, and Module Class

### Required classification fields
Inside `function`:
- `mode`: `"DEBUG"` or `"PRODUCTION"`
- `visibility`: `"private"` or `"public"`

### `auth` block
- In `PRODUCTION`, `auth` MUST be present and valid.
- In `DEBUG`, `auth` MAY be omitted.

`auth` object fields:
- `author_id` (string)
- `authority` (string enum; initial set: `"core_dev" | "admin" | "user"`)
- `key_id` (string)
- `signed_at` (RFC3339 string)
- `hash` (hex string)
- `signature` (base64 string)

Trust store (MVP): a static table mapping `key_id` → public key + allowed `authority`. Revocation MAY be unsupported in MVP.

---

## 4.1) Spec Versioning (MVP lock)

To prevent old loaders from silently misinterpreting newer module formats, every module MUST declare a spec version.

- `function.spec_version` MUST be present.
- For this document, `function.spec_version` MUST equal `1`.
- A loader/validator MUST reject any module with an unknown or unsupported `spec_version`.

Readability requirement (MVP lock):
- `spec_version` MUST be the first key within the `function` object as it appears in the JSON file.
- This requirement is for human review only; canonical hashing remains order-independent due to key-sorting.

`spec_version` is part of the signed payload (it is included in the canonicalized `function` object and therefore covered by `auth.hash` / `auth.signature`).

---

## 5) Canonical Hashing & Signing (MVP lock)

### 5.1 Hash scope (avoids circular hashing)
`auth.hash` MUST equal the SHA‑256 digest of the canonicalized `function` object **excluding**:
- `function.auth.hash`
- `function.auth.signature`

Everything else in `function` MUST remain included.

### 5.2 Signature scope
`auth.signature` MUST be computed over the raw 32-byte SHA‑256 digest (not over the raw JSON text).

### 5.3 Canonicalization rules
Canonicalization MUST:
- Sort object keys lexicographically (bytewise ASCII).
- Preserve array order.
- Use standard JSON escaping.
- Use deterministic number formatting.

### 5.4 Canonical number serialization (final lock-in)

To ensure deterministic hashing and signature verification, all numeric values MUST be serialized in canonical form according to the rules below.

#### 5.4.1 Formatting rules (normative)
1) No exponent notation
- Numbers MUST NOT use exponent or scientific notation.

2) No leading plus sign
- Numbers MUST NOT include a leading `+`.

3) No trailing decimal point
- Numbers MUST NOT end with a trailing `.`.

4) No trailing zeros after decimal
- If a fractional part exists, it MUST NOT end in `0`.

5) Zero normalization (Option A: chosen)
- `-0` MUST be serialized as `"0"`.
- `-0.0` MUST be serialized as `"0"`.

6) Integer formatting
- Integers MUST NOT contain leading zeros (except `"0"` itself).
- Zero MUST NOT have a sign.

7) Float formatting
- A float MUST be serialized as: `<integer_part>[.<fractional_part>]`.
- If present, `fractional_part` MUST NOT be empty and MUST NOT end in `0`.
- `integer_part` MUST NOT have leading zeros (except `"0"`).
- The decimal point MUST be omitted if the fractional part becomes empty after stripping zeros.

#### 5.4.2 Deterministic conversion from `QC::JSON` number storage (normative)

Because `QC::JSON` stores numbers internally as IEEE‑754 `double` and does not preserve the original textual lexeme, the canonicalizer MUST re‑emit numbers deterministically from the parsed `double` value.

The canonicalizer MUST:
- Reject any non‑finite values (NaN, +Inf, -Inf).
- Re-emit the `double` to a decimal string deterministically (implementation-defined; MUST be locale-independent).
  - In hosted builds, an implementation MAY use `std::to_chars`.
  - In freestanding/kernel builds, an equivalent custom routine MUST be used.
- Apply all canonical formatting rules in 5.4.1.

The validator MUST reject numeric literals that are not representable in the declared target type.

#### 5.4.3 Examples (normative)

Valid canonical outputs:
- `1.5000` → `"1.5"`
- `1.0` → `"1"`
- `0.250000` → `"0.25"`
- `-0.0` → `"0"`
- `3.000` → `"3"`
- `0.0001` → `"0.0001"`

Invalid (MUST be rejected or canonicalized, depending on where encountered):
- `1e3` (exponent form; valid JSON but forbidden by MVP canonical rules)
- `1.` (invalid JSON number syntax)
- `1.250` (trailing fractional zeros; valid JSON but not canonical)
- `+5` (invalid JSON number syntax)
- `00012` (invalid JSON number syntax)
- `NaN`, `Infinity` (non-finite; also invalid JSON number syntax)

---

## 6) Required Fields (MVP)

Inside `function`:
- `spec_version` (u32, MUST be `1` for MVP)
- `name` (string, pattern: `[A-Za-z_][A-Za-z0-9_]{0,63}`)
- `version` (u32, `>= 1`)
- `mode` ("DEBUG" | "PRODUCTION")
- `visibility` ("private" | "public")
- `ownership` (string)
- `inputs` (array of `{name,type}`)
- `outputs` (array of `{name,type}`)
- `steps` (array of step objects)

### 6.1 `ownership` (reserved)
- `ownership` MUST be present.
- `ownership` MUST NOT affect execution in MVP.

### 6.2 Optional `id` (strongly recommended)
- `id` MAY be present: `"id": "citadel.fn.examplefunction"`.
- If present, it MUST be globally unique and stable across renames.
- `id` MUST NOT affect execution in MVP.

### 6.3 Optional fields reserved for later
- `requires` (array) MAY be present; if present it MUST be empty unless dependency validation is enabled.
- `calls` (array) MAY be present; if present it MUST be empty unless `call` op is enabled.
- `metadata` (object) MAY be present; runtime MUST ignore it (except logs/debug UI).

---

## 7) Types (MVP)

Supported scalar types:
- `bool`, `i32`, `u32`, `f32`, `f64`

Rules:
- No implicit widening/narrowing or mixed-type ops in MVP.
- All values live in a typed frame (no pointers, no user-defined structs).

---

## 8) Values: References and Typed Constants

In `steps[*].args`, each argument MUST be either:

1) A reference string naming an input or a previously defined local (an earlier step’s `out`).

2) A typed constant:
```json
{ "const": { "type": "f32", "value": 0.5 } }
```

Typed constant rules:
- A constant MUST be encoded as: `{ "const": { "type": "<scalar>", "value": <json_number_or_bool> } }`.
- The value MUST be representable in the target type.
  - MUST reject NaN / ±Inf.
  - MUST reject overflow.
- Numeric constants MUST adhere to the canonical number rules in 5.4.
- The interpreter MUST reject constants that cannot be safely converted.

---

## 9) Steps: Interpreter Semantics + Allowed Ops

Step shape:
```json
{ "op": "<opname>", "args": [ ... ], "out": "<varname>" }
```

Rules:
- Steps MUST execute in array order.
- Execution is atomic per step; any failure halts the function.

Allowed ops (pure set):
- `add`, `sub`, `multiply`, `divide`
- `neg`
- `sqrt` (domain: input MUST be >= 0 for floats; otherwise runtime error)
- `min`, `max`, `abs`
- `cmp_eq`, `cmp_lt`, `cmp_gt` (output `bool`)
- `and`, `or`, `not` (`bool` only)
- `select` (args: `[cond(bool), a(T), b(T)]` → out `T`)

Not allowed in MVP:
- Branch/loop (`if`, `while`, `goto`), memory ops, filesystem ops, service calls, native calls.

Optional MVP extension (composition):
- Add `call` op only if you want inter-function composition immediately.
- Call target MUST be another validated JSON function with `visibility: "public"`.
- No calls into compiled code.
- MUST enforce `MAX_CALL_DEPTH`.

---

## 10) Locals, Single Assignment, and Output Rules

Single assignment:
- Every step `out` name MUST be unique within the function.
- No step may assign to the same name twice.
- Every `out` name becomes a local variable.

Name resolution:
- All referenced names MUST be either:
  - an input, or
  - a previously defined `out`.

Outputs:
- Every output variable MUST be produced by some step `out`.
- MVP MUST NOT allow “pass-through” outputs (output = input) unless a `move` op is introduced later.

---

## 11) Validation Rules (deterministic)

Validator MUST enforce:
- Required fields present with correct types.
- `spec_version` MUST be `1`.
- `spec_version` MUST be the first key in the `function` object (file order).
- No unknown keys in the `function` object (except that `metadata` is freeform).
- No duplicate input/output names.
- No overlap between input/output names and local names.
- Each step:
  - has `op`, `args`, `out`
  - has correct arg count for the op
  - resolves every reference to a known, correctly typed value
- Limits (below) are enforced before execution.

---

## 12) Hard Limits (anti-brick / anti-DOS)

- `MAX_STEPS = 256`
- `MAX_ARGS_PER_STEP = 8`
- `MAX_LOCALS = 512` (inputs + outputs + intermediates)
- `MAX_CALL_DEPTH = 16` (only if `call` enabled)
- `MAX_NAME_LEN = 64`
- File size SHOULD be limited (e.g., reject > 128KB in MVP)

---

## 13) Runtime Error Model (MVP)

Execution returns either:
- Success: typed outputs
- Failure: structured error object

Error object fields:
- `code`: enum (`E_SCHEMA`, `E_AUTH`, `E_HASH`, `E_SIG`, `E_LIMIT`, `E_OP`, `E_TYPE`, `E_DIV0`, `E_DOMAIN`, `E_CALL`)
- `function`: `{name, version, hash}`
- `step_index`: u32 or null (if pre-exec failure)
- `message`: short string (DEBUG only; PRODUCTION maps to stable codes)

---

## 14) Function Registry (MVP behavior)

Registry key: exact `(name, version)` match.

Stored per entry:
- `json_path`
- `mode`, `visibility`
- `hash` (computed; required in PRODUCTION)
- `state`: `draft | validated | rejected`
- `backend.current = "interpreter"` (fixed in MVP)

---

## Appendix A — Minimal MVP Example (PRODUCTION)

```json
{
  "function": {
    "spec_version": 1,
    "auth": {
      "author_id": "your.name.here",
      "authority": "user",
      "key_id": "your-key-id",
      "signed_at": "2026-02-24T00:00:00Z",
      "hash": "HEX_SHA256_OF_CANONICAL_PAYLOAD",
      "signature": "BASE64_SIGNATURE_OVER_HASH"
    },
    "ownership": "0-0-0-0",
    "id": "citadel.fn.examplefunction",
    "name": "ExampleFunction",
    "version": 1,
    "mode": "PRODUCTION",
    "visibility": "private",
    "inputs": [
      { "name": "x", "type": "f32" },
      { "name": "y", "type": "f32" }
    ],
    "outputs": [
      { "name": "result", "type": "f32" }
    ],
    "steps": [
      { "op": "add", "args": ["x", "y"], "out": "sum" },
      {
        "op": "multiply",
        "args": ["sum", { "const": { "type": "f32", "value": 0.5 } }],
        "out": "result"
      }
    ],
    "requires": [],
    "calls": [],
    "metadata": {
      "description": "Computes (x + y) * 0.5.",
      "tags": ["example", "math", "mvp"]
    }
  }
}
