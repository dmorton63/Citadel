# boot_strict.json vs boot.json (why BootGate rejected it)

## What happened
`kernel/Boot/Config/boot_strict.json` is a **JSON-Schema** document.

- It describes what a valid boot config *should look like* (it contains keys like `$schema`, `type`, `properties`, `required`).
- It does **not** contain the actual configuration values (e.g. no `ram_mib_required: 2048`).

The BootGate loader (BootJson.cpp) is a **config loader**, not a schema validator, so when it tries to read `BOOT.JSN` it expects concrete values it can use immediately.

That’s why you saw the earlier “schema invalid / config shape invalid” message: the parser successfully parsed the JSON *syntax*, but the *shape* didn’t match any supported config format.

## What the BootGate parser supports (today)
BootGate currently recognizes either of these shapes:

### Format A (legacy)
```json
{
  "requirements": {
    "min_ram_mib": 2048,
    "recommended_ram_mib": 2048,
    "require_tpm": false
  }
}
```

### Format B (your richer config)
```json
{
  "schema_version": 1,
  "min_spec": {
    "ram_mib_required": 2048,
    "ram_mib_recommended": 2048,
    "tpm": { "mode": "optional" }
  }
}
```

BootGate uses only these fields right now:

- `min_spec.ram_mib_required` → minimum RAM MiB gate
- `min_spec.ram_mib_recommended` (optional) → “Reduced Memory Mode” threshold
- `min_spec.tpm.mode` → if exactly `"required"`, sets `require_tpm = true` (enforcement is separate)

Everything else (profile/security/paths/cpu flags) is currently ignored by the BootGate gate logic.

## Why the strict schema fails specifically
In `boot_strict.json`, `min_spec` is itself a schema object:

- `min_spec.ram_mib_required` is an object like `{ "type": "integer", "minimum": 1 }`
- BootGate expects `min_spec.ram_mib_required` to be a **number** like `2048`

So it can’t extract any concrete values, and rejects the file as “config shape invalid”.

## How to use strict validation in the future
If you want “strict mode” at boot:

- Keep the schema as something like `boot.schema.json` (developer-time validation)
- Keep the runtime config as `boot.json` (concrete values)
- Optionally add a kernel-side schema validation step later (would increase boot complexity and code size)

## “Mode” lines you’ll see in serial logs
There are a few different subsystems that log a “mode”, and they’re independent:

- `BootSig: mode=development|production`
  - This is the **BootSig enforcement posture** (fail-open vs fail-closed) and is controlled by the **build-time** CMake option `CITADEL_PRODUCTION` (e.g. `build.sh --prod`).
  - It is **not** controlled by `BOOT.JSN`.
- `Startup mode loaded: ...`
  - This is the **UI/startup path selection** (e.g. `DESKTOP`) loaded from startup config on the ramdisk.
- `Security Center mode loaded: ...`
  - This is the **runtime “security center” feature mode** (e.g. `BYPASS`) loaded from startup config.
  - It does **not** mean you are in BootSig development mode.
