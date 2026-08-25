
# **Citadel Language & IDE Design — Session Summary**

## **1. Core Language Structure**
CiteLang programs consist of explicit block types:

- **main()** — implicit entry point (only function without `func`)
- **func** — procedural logic
- **mod** — modules / namespaces
- **data** — structured data blocks
- **template** — declarative UI blocks

Example shape:

```
data config {
    theme: "Dark",
    autosave: true
}

template EditorWindow {
    title: "CiteEdit",
    width: 900,
    height: 600
}

mod utils {
    func greet(name) {
        Print("Hello " + name);
    }
}

func init() {
    Print("Initializing...");
}

main() {
    init();
    utils.greet("David");
    Open(EditorWindow);
}
```

---

## **2. Primitive Commands (CiteLang Kernel)**
These are the built‑in operations the runtime understands.

### **Console**
- `Print(text)`
- `Print(target, text)` — polymorphic (window, control, file, etc.)

### **Window / UI**
- `Open(resource)`
- `Close(resource)`
- `move(resource, x, y)` — polymorphic window movement

### **File I/O**
- `FOPEN(path)` → returns FILEBLOCK
- `FCLOSE(fileblock)`
- `list(path)` → directory listing

### **Control Flow**
- `for`
- `while`
- `do`
- `end while` (optional)
- `end do` (optional)

---

## **3. Polymorphic Commands**
Commands adapt based on argument types.

### **Print**
```
Print("Hello World");
Print(AppWindow, "Hello World");
Print(fb, "Writing to file");
Print("Hello %s", name);
```

### **move**
```
move(EditorWindow, 100, 200);       // move window
move("/a.txt", "/b.txt");           // move file
move(data_a, data_b);               // copy/replace data
```

Polymorphism is a core language feature.

---

## **4. FILEBLOCK — Managed File Object**
A structured, safe file abstraction with automatic cleanup.

### **Definition**
```
FILEBLOCK fb {
    structure: DataStructure,
    data: Data,
    filenum: 0,
    mode: readwrite,
    autoflush: true,
    autoclose: true
}
```

### **Behavior**
- `FOPEN` creates a FILEBLOCK
- `FCLOSE` closes it
- memory is **always** released on scope exit  
- `autoclose` ensures file safety  
- `autoflush` ensures write consistency  
- `structure` binds serialization rules  
- `data` binds in‑memory representation  

### **Runtime Lifecycle**
- On creation: open file, allocate buffer, bind structure/data
- On write: serialize → write → flush (if enabled)
- On read: read → deserialize
- On scope exit: close (if autoclose), release memory

`autorelease` was removed because scope cleanup already handles memory release.

---

## **5. IDE Concepts**
The Citadel IDE (CiteStudio) will include:

- Project Explorer  
- CiteEdit text editor  
- Terminal  
- Output Console  
- Build/Run pipeline  
- Syntax highlighting  
- Tabs  
- Status bar  
- Declarative UI templates  
- Integration with CiteLang runtime  

Window layout follows a classic tri‑pane design.

---

## **6. Type System & Data Types (CiteLang Draft)**

This section defines the first-pass type system so parser and runtime work can start.

### **6.1 Scalar Types**

- `Int` - signed whole number (default: 64-bit)
- `UInt` - unsigned whole number (default: 64-bit)
- `Float` - binary floating point (default: 64-bit)
- `Decimal` - base-10 precise number for money/financial math
- `Bool` - `true` / `false`
- `Char` - single UTF-8 character
- `String` - UTF-8 text sequence

Examples:

```
count: Int = 42;
distance: Float = 3.14159;
price: Decimal = 19.99d;
enabled: Bool = true;
grade: Char = 'A';
name: String = "Citadel";
```

### **6.2 Integer and Float Variants (Optional Explicit Widths)**

Use explicit-width aliases where required by drivers, disk formats, and protocol layers:

- Signed: `Int8`, `Int16`, `Int32`, `Int64`
- Unsigned: `UInt8`, `UInt16`, `UInt32`, `UInt64`
- Floating: `Float32`, `Float64`

If no width is specified, language defaults apply (`Int` -> `Int64`, `Float` -> `Float64`).

### **6.3 Nullability**

All value types are non-null by default. Nullable types use `?`.

```
title: String? = null;
retries: Int? = null;
```

### **6.4 Collections**

- `List<T>` - ordered dynamic sequence
- `Map<K, V>` - key/value store
- `Set<T>` - unique value collection

```
ports: List<Int> = [80, 443, 8080];
env: Map<String, String> = { "MODE": "dev", "TZ": "UTC" };
tags: Set<String> = { "core", "ui", "stable" };
```

### **6.5 Structured Types**

- `data` blocks define named structured records.
- `mod` can namespace shared types.

```
data User {
    id: UInt64,
    name: String,
    email: String?,
    active: Bool
}
```

### **6.6 Inference and Explicit Typing**

- Use explicit typing in public data structures and APIs.
- Local variables may use inference with `let`.

```
let total = 100;          // inferred Int
let ratio = 0.5;          // inferred Float
let label = "Editor";    // inferred String
```

### **6.7 Conversion Rules (Draft)**

- Widening numeric conversions are implicit (`Int32` -> `Int64`, `Float32` -> `Float64`).
- Narrowing conversions require explicit cast.
- `Int` to `Float` is implicit.
- `Float` to `Int` requires explicit cast.
- `Decimal` does not implicitly mix with `Float` (must cast intentionally).

```
small: Int32 = 120;
large: Int64 = small;              // implicit widening

f: Float = 12;                     // Int -> Float implicit
i: Int = Int(12.75);               // explicit cast

price: Decimal = Decimal("12.75");
```

### **6.8 Literal Forms (Draft)**

- Int: `123`, `-99`, `0xFF`, `0b1010`
- Float: `3.14`, `2.0e8`
- Decimal: `19.95d` or `Decimal("19.95")`
- Bool: `true`, `false`
- Char: `'x'`
- String: `"text"`
- Null: `null`

### **6.9 Runtime Safety Rules (Draft)**

- Overflow checking mode is configurable (`checked` vs `wrap`).
- Null access on nullable values must be guarded.
- Invalid casts throw typed runtime errors.

### **6.10 EBNF Grammar (Types and Declarations Draft)**

```ebnf
type_name        = scalar_type | width_type | collection_type | user_type ;
scalar_type      = "Int" | "UInt" | "Float" | "Decimal" | "Bool" | "Char" | "String" ;
width_type       = "Int8" | "Int16" | "Int32" | "Int64"
                 | "UInt8" | "UInt16" | "UInt32" | "UInt64"
                 | "Float32" | "Float64" ;
collection_type  = list_type | map_type | set_type ;
list_type        = "List" "<" type_ref ">" ;
map_type         = "Map" "<" type_ref "," type_ref ">" ;
set_type         = "Set" "<" type_ref ">" ;
user_type        = identifier ;

type_ref         = type_name [ "?" ] ;

field_decl       = identifier ":" type_ref [ "=" expr ] ";" ;
var_decl         = identifier ":" type_ref [ "=" expr ] ";"
                 | "let" identifier "=" expr ";" ;

data_decl        = "data" identifier "{" { data_field } "}" ;
data_field       = identifier ":" type_ref [ "," ] ;

cast_expr        = type_name "(" expr ")" ;
```

### **6.11 Operator and Type Compatibility (Draft)**

#### **Arithmetic Operators**

| Operator | Valid Types | Result Type | Notes |
|---|---|---|---|
| `+` | Int-family, UInt-family | widest numeric input | overflow mode applies |
| `+` | Float-family | widest float input | IEEE-style float behavior |
| `+` | Decimal | Decimal | exact decimal arithmetic |
| `+` | String + String | String | concatenation |
| `-` | Int/UInt/Float/Decimal | numeric family result | same widening rules |
| `*` | Int/UInt/Float/Decimal | numeric family result | same widening rules |
| `/` | Int/UInt | Float | integer division promoted to Float |
| `/` | Float/Decimal | same family | divide-by-zero runtime error |
| `%` | Int/UInt | integer family | not valid for String |

#### **Comparison Operators**

| Operator | Valid Types | Result |
|---|---|---|
| `==`, `!=` | all scalar types, nullable references | Bool |
| `<`, `<=`, `>`, `>=` | Int/UInt/Float/Decimal/Char/String | Bool |

Rules:
- Numeric cross-family compares are allowed with widening conversion.
- String comparison is lexicographic and UTF-8 aware.
- Comparing `null` is only valid against nullable types.

#### **Logical Operators**

| Operator | Valid Types | Result |
|---|---|---|
| `&&`, `||` | Bool | Bool |
| `!` | Bool | Bool |

#### **Assignment Operators**

| Operator | Requirement |
|---|---|
| `=` | RHS must match LHS type or be explicitly castable |
| `+=`, `-=`, `*=`, `/=` | same rules as arithmetic operators |

### **6.12 FILEBLOCK Serialization Type Mapping (Draft)**

Serialization profile defaults to little-endian for binary mode and UTF-8 for text mode.

| CiteLang Type | Binary Encoding | Text Encoding | Notes |
|---|---|---|---|
| `Int8`/`UInt8` | 1 byte | decimal string | fixed width |
| `Int16`/`UInt16` | 2 bytes LE | decimal string | fixed width |
| `Int32`/`UInt32` | 4 bytes LE | decimal string | fixed width |
| `Int64`/`UInt64` | 8 bytes LE | decimal string | fixed width |
| `Float32` | IEEE 754 4 bytes LE | decimal/scientific string | precision loss possible in text |
| `Float64` | IEEE 754 8 bytes LE | decimal/scientific string | default float width |
| `Decimal` | scaled integer + scale byte(s) | canonical decimal string | exact decimal preserved |
| `Bool` | 1 byte (`0`/`1`) | `true`/`false` | strict values only |
| `Char` | UTF-8 codepoint bytes + length | single-char UTF-8 | validate one scalar value |
| `String` | length-prefixed UTF-8 bytes | UTF-8 text | length prefix avoids delimiter issues |

#### **Collection Encoding Rules**
- `List<T>`: element_count + repeated serialized element payloads.
- `Set<T>`: same as List, with uniqueness validated on decode.
- `Map<K,V>`: pair_count + repeated key/value payload pairs.

#### **Nullable Encoding Rule**
- `T?` is encoded as: presence_flag (1 byte) + payload (only if present).

#### **Data Block Encoding Rule**
- `data` fields serialize in declaration order.
- Optional future mode: tagged-field encoding for schema evolution.

#### **FILEBLOCK Structure Binding Example**

```citelang
data UserRecord {
    id: UInt64,
    username: String,
    score: Decimal,
    active: Bool,
    note: String?
}

FILEBLOCK users {
    structure: UserRecord,
    data: List<UserRecord>,
    filenum: 1,
    mode: readwrite,
    autoflush: true,
    autoclose: true
}
```

---

## **7. Next Steps Identified**
- Build the parser  
- Build the interpreter  
- Build CiteEdit  
- Build IDE shell  
- Build project system  

---

## **8. Expression Grammar and Precedence (Parser Draft)**

### **8.1 Expression EBNF**

```ebnf
expr              = assignment_expr ;

assignment_expr   = logical_or_expr
                  | unary_expr assignment_op assignment_expr ;

assignment_op     = "=" | "+=" | "-=" | "*=" | "/=" ;

logical_or_expr   = logical_and_expr { "||" logical_and_expr } ;
logical_and_expr  = equality_expr { "&&" equality_expr } ;
equality_expr     = compare_expr { ("==" | "!=") compare_expr } ;
compare_expr      = additive_expr { ("<" | "<=" | ">" | ">=") additive_expr } ;
additive_expr     = multiplicative_expr { ("+" | "-") multiplicative_expr } ;
multiplicative_expr = unary_expr { ("*" | "/" | "%") unary_expr } ;

unary_expr        = ("!" | "-" | "+") unary_expr
                  | postfix_expr ;

postfix_expr      = primary_expr { call_suffix | index_suffix | member_suffix } ;
call_suffix       = "(" [ arg_list ] ")" ;
index_suffix      = "[" expr "]" ;
member_suffix     = "." identifier ;

arg_list          = expr { "," expr } ;

primary_expr      = literal
                  | identifier
                  | "(" expr ")"
                  | cast_expr ;
```

### **8.2 Precedence and Associativity**

| Level | Operators | Associativity |
|---|---|---|
| 1 (highest) | `()`, `[]`, `.` , function call | left-to-right |
| 2 | unary `!`, unary `+`, unary `-` | right-to-left |
| 3 | `*`, `/`, `%` | left-to-right |
| 4 | `+`, `-` | left-to-right |
| 5 | `<`, `<=`, `>`, `>=` | left-to-right |
| 6 | `==`, `!=` | left-to-right |
| 7 | `&&` | left-to-right |
| 8 | `||` | left-to-right |
| 9 (lowest) | `=`, `+=`, `-=`, `*=`, `/=` | right-to-left |

### **8.3 Statement Terminators**

- Variable declarations and expression statements end with `;`.
- `data`, `mod`, `func`, `template`, and `main` blocks do not require trailing `;`.

---

## **9. Semantics and Runtime Rules (Execution Draft)**

### **9.1 Scope and Lifetime**

- Block scope: names declared in a block are visible only within that block and children.
- Function scope: parameters and locals are function-local.
- Module scope: top-level declarations inside `mod` are namespaced.

### **9.2 Mutability Model**

- `let` creates immutable bindings.
- `var` creates mutable bindings.
- `data` fields are mutable unless future `readonly` annotation is used.

```citelang
let name = "citadel";
var count: Int = 0;
count += 1;
```

### **9.3 Parameter Passing**

- Scalars (`Int`, `Float`, `Bool`, `Char`, `Decimal`) pass by value.
- `String`, `List`, `Map`, `Set`, and `data` values pass by shared reference.
- Future explicit annotations may allow copy semantics for reference types.

### **9.4 Initialization and Evaluation Order**

- Declarations initialize in source order.
- Function arguments evaluate left-to-right.
- Binary operator operands evaluate left-to-right.

### **9.5 Null and Cast Semantics**

- Accessing members on `null` triggers runtime error `E_NULL_ACCESS`.
- Invalid explicit cast triggers runtime error `E_CAST_INVALID`.
- Overflow behavior follows current numeric mode (`checked` or `wrap`).

---

## **10. Module, Import, and Visibility Model (Draft)**

### **10.1 Visibility Keywords**

- `public` exports symbol from module.
- `private` keeps symbol module-local.
- If omitted, default visibility is `private`.

### **10.2 Import Forms**

```citelang
import core.io;
import core.math as math;
import ui.widgets.{Button, Label};
```

### **10.3 Export Forms**

```citelang
mod core.math {
    public func clamp(v: Int, lo: Int, hi: Int) {
        if (v < lo) { return lo; }
        if (v > hi) { return hi; }
        return v;
    }

    private func internalHelper() {
        Print("helper");
    }
}
```

### **10.4 Name Resolution Rules**

- Local scope shadows module scope.
- Module scope shadows imports.
- Aliased import names are resolved before wildcard import symbols.

### **10.5 Cyclic Dependency Rule**

- Import cycles are rejected at compile time with `E_IMPORT_CYCLE`.
- Future enhancement may support cycle-safe interfaces.

---

## **11. Error Model and Diagnostics (Draft)**

### **11.1 Error Classes**

- Parse errors: syntax/grammar violations.
- Type errors: incompatible types, invalid casts, nullability violations.
- Runtime errors: null access, divide-by-zero, IO failures.

### **11.2 Diagnostic Format**

```text
[E_TYPE_MISMATCH] file.cl:12:9
expected: Int
actual: String
message: cannot assign String to Int variable 'count'
```

### **11.3 Initial Error Code Set**

- `E_PARSE_UNEXPECTED_TOKEN`
- `E_PARSE_UNCLOSED_BLOCK`
- `E_TYPE_MISMATCH`
- `E_TYPE_NULLABILITY`
- `E_CAST_INVALID`
- `E_NULL_ACCESS`
- `E_ARITH_DIV_ZERO`
- `E_IMPORT_CYCLE`
- `E_IO_OPEN_FAILED`
- `E_IO_SERIALIZE_FAILED`

---

## **12. Standard Library Baseline (Draft API Surface)**

### **12.1 Core Namespaces**

- `core.strings`
- `core.collections`
- `core.math`
- `core.time`
- `core.fs`
- `core.json`

### **12.2 Baseline Functions**

```citelang
core.strings.length(s: String) -> Int
core.strings.upper(s: String) -> String
core.collections.len<T>(xs: List<T>) -> Int
core.math.abs(x: Int|Float|Decimal) -> same
core.time.nowUnix() -> Int64
core.fs.exists(path: String) -> Bool
core.json.encode<T>(value: T) -> String
core.json.decode<T>(text: String) -> T
```

### **12.3 Stability Rule**

- Baseline APIs are stable across minor language versions.
- Breaking API changes require major version bump.

---

## **13. Tooling Contract (Draft)**

### **13.1 Formatter**

- Canonical indentation: 4 spaces.
- One statement per line.
- Trailing commas allowed in multiline lists/maps/data fields.

### **13.2 Linter Rules (Initial Set)**

- Warn on unused variables/imports.
- Warn on implicit numeric narrowing attempts.
- Warn on nullable values used without guard.
- Warn on shadowed names.

### **13.3 Build and Test Conventions**

- Default source extension: `.cl`.
- Entry symbol must include exactly one `main()`.
- Test files use suffix `_test.cl`.

---

## **14. Versioning and Compatibility Rules (Draft)**

### **14.1 Language Version Declaration**

```citelang
language 0.1;
```

### **14.2 Compatibility Policy**

- Patch version: diagnostics/tooling fixes only.
- Minor version: additive syntax/API/features without breakage.
- Major version: breaking syntax/semantic changes allowed.

### **14.3 Deprecation Policy**

- Deprecated features emit warnings for at least one minor version before removal.
- Removal requires migration guidance in release notes.

### **14.4 Serialization Schema Evolution**

- Append-only field additions are forward-compatible with tagged mode.
- Field removal/rename is breaking unless migration adapter is provided.

---

## **15. Updated Build Roadmap**

- Finalize parser implementation from Sections 6, 8, and 10.
- Implement static type checker from Sections 6, 9, and 11.
- Implement runtime and FILEBLOCK serialization from Sections 4, 6.12, and 9.
- Implement standard library baseline from Section 12.
- Integrate diagnostics, formatter, and linter from Sections 11 and 13.
- Implement version gates and compatibility checks from Section 14.

## **16. Parser Implementation Artifacts**

To keep implementation work separate from narrative design notes, use these files as the parser handoff baseline:

- `docs/CITELANG_GRAMMAR.ebnf` - formal grammar for lexer/parser construction.
- `docs/CITELANG_PARSER_CHECKLIST.md` - phased parser implementation and acceptance checklist.

## **17. Citadel Native App Platform Contract**

Citadel applications are not Windows `.exe` binaries; they target the Citadel-native platform contract.

- `docs/CITADEL_APP_PLATFORM_V0_1.md` - executable format (`.cap`), loader behavior, ABI, syscall families, manifest permissions, FFI constraints, and toolchain contract.
- `docs/CITADEL_SYSCALL_ABI_V0_1.md` - concrete syscall IDs, signatures, permissions, and error code contracts for kernel/runtime integration.
- `docs/CITADEL_SYSCALL_ABI_V0_1.json` - machine-readable syscall registry for code generation and test synchronization.
- `docs/CITADEL_REGISTRY_V0_1.md` - DB-backed registry service model, ACL/permission policy, migration plan, and SecureStore boundary.
- `QKernel/Include/QKSyscallABI.h` - kernel-facing C++ syscall ID/constants/struct stubs aligned to ABI v0.1.
- `QJFunctions/Include/QJFCitadelSyscalls.h` - runtime syscall wrapper declarations for CiteLang integration.
- `docs/CITADEL_PERMISSION_ENFORCEMENT_MATRIX_V0_1.md` - permission-to-syscall mapping and enforcement checklist.

This document is the canonical source for OS hooks and runtime integration requirements.

## **18. Platform Bring-Up Roadmap Addendum**

- Implement `.cap` loader validation and segment mapping.
- Implement app entry ABI and `AppContext` handoff.
- Implement syscall families (FileSystem + Time first).
- Implement syscall dispatcher and wrappers from `docs/CITADEL_SYSCALL_ABI_V0_1.md`.
- Implement runtime adapters for `core.fs` and `core.time`.
- Implement manifest capability enforcement in loader/runtime.
- Expand to UI, IPC, and Net families after core bring-up.
- Implement DB-backed Registry service (`docs/CITADEL_REGISTRY_V0_1.md`) with SecureStore secret-reference boundary.

---

If you paste this into Word, you’ll have a perfect overnight thinking document — structured, complete, and ready for expansion tomorrow.