# CommandCenter Split Plan (Scaffolded)

The command registrar now has dedicated split scaffolds so implementations can migrate incrementally without changing command ABI.

## Scaffold Units

- `QKCmdAuth.*`: auth/session/access-control ownership.
- `QKCmdParse.*`: token/string/parser utility ownership.
- `QKCmdPathFs.*`: path canonicalization and filesystem policy helpers.
- `QKCmdBuiltins.*`: built-in command topic files.
- `QKCmdDebugTest.*`: debug/test command ownership.
- `QKCmdNet.*`: networking command ownership.

## Current Wiring

- `QKCommandCenter.cpp` remains the thin registrar entrypoint and touches split modules at registration time.
- Build includes each split unit so logic can move in small, compile-safe steps.

## Migration Rule

- Move behavior from `QKCommandCenter.cpp` into split units in topic-sized patches while preserving existing command names and metadata.
