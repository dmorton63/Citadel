# Citadel Registry v0.1

## 1. Purpose

This specification defines the Citadel registry service for system and application configuration.

Design decision:
- Registry data is stored in the built-in database.
- Secret material remains in SecureStore.
- Early-boot critical values are mirrored in a minimal boot cache file until full database services are online.

## 2. Non-Goals

The registry is not a secret vault. Do not store:
- private keys
- bearer tokens
- credential roots
- TPM seed material

Those remain in SecureStore and are accessed through security APIs only.

## 3. Logical Key Spaces

Root namespaces:
- `/machine/*` - system-wide settings (HKLM-equivalent)
- `/user/<uid>/*` - per-user settings (HKCU-equivalent)
- `/classes/*` - type and handler associations (HKCR-equivalent)
- `/apps/<appId>/*` - app-owned settings
- `/runtime/*` - ephemeral runtime state (optional persistence policy)

Naming constraints:
- path segments are case-sensitive ASCII in v0.1
- max key path length: 512 bytes
- max segment length: 64 bytes
- disallow empty segments and `..`

## 4. Data Model

### 4.1 Registry Entry

Fields:
- `path` (primary key, text)
- `value_type` (`int`, `bool`, `string`, `blob`, `json`)
- `value_int` (nullable i64)
- `value_bool` (nullable bool)
- `value_text` (nullable text)
- `value_blob` (nullable blob)
- `version` (u64 monotonic for optimistic concurrency)
- `owner_scope` (`machine`, `user`, `app`, `system`)
- `owner_id` (uid/appId/system component id)
- `flags` (bitfield; read-only, boot-critical, volatile)
- `updated_by` (principal id)
- `updated_at_unix_ns` (u64)

Rules:
- exactly one value payload field is set based on `value_type`
- updates increment `version`
- key create/update/delete are transactional

### 4.2 ACL Table

Fields:
- `path_prefix`
- `principal_kind` (`role`, `uid`, `appId`, `system`)
- `principal_id`
- `allow_mask` (`read`, `write`, `delete`, `enumerate`, `watch`)

Evaluation:
- longest-prefix match first
- explicit deny beats allow
- default deny

### 4.3 Watch Table (optional persisted metadata)

Fields:
- `watch_id`
- `path_prefix`
- `subscriber`
- `event_mask` (`create`, `update`, `delete`)

## 5. Boot Model

Boot phases:
1. Early boot: use minimal cache file for a small allowlist of boot-critical keys.
2. Mid boot: registry service starts after storage and DB mount.
3. Late boot: reconcile cache to DB and clear stale cache markers.

Boot cache scope examples:
- `/machine/boot/active_profile`
- `/machine/boot/recovery_mode`
- `/machine/storage/system_volume_mode`

Recovery rule:
- if DB unavailable, system may continue with cache-only read for allowlisted keys.
- writes in cache-only mode are journaled and replayed once DB is available.

## 6. API Surface (Service-level)

Initial operations:
- `RegGet(path)`
- `RegSet(path, typedValue, expectedVersion?)`
- `RegDelete(path, expectedVersion?)`
- `RegList(prefix, recursive, limit, cursor)`
- `RegWatch(prefix, mask)`
- `RegUnwatch(watchId)`

Behavior:
- all mutating operations are atomic
- `expectedVersion` enables compare-and-swap updates
- watch notifications include old/new version metadata

## 7. Planned Syscall/Runtime Mapping

For platform ABI expansion, reserve family `0x0B` for Registry in a future ABI minor version.

Proposed operation IDs:
- `0x0B01` Registry.Get
- `0x0B02` Registry.Set
- `0x0B03` Registry.Delete
- `0x0B04` Registry.List
- `0x0B05` Registry.Watch
- `0x0B06` Registry.Unwatch

Runtime namespace mapping:
- `core.registry.get`
- `core.registry.set`
- `core.registry.delete`
- `core.registry.list`
- `core.registry.watch`

Note:
- v0.1 docs define these as planned interfaces; they are not yet active in the current syscall ABI file.

## 8. Permissions and Policy

Required manifest permissions:
- `registry.read`
- `registry.write`
- `registry.watch`
- `registry.admin` (for ACL/policy changes)

Policy defaults:
- apps can read/write only under `/apps/<appId>/*` by default
- writes to `/machine/*` require elevated role or signed system component
- `/classes/*` writes require installer/packager authority

## 9. SecureStore Boundary

Boundary rules:
- registry may store secure references (opaque ids), not secret values
- secret fetch operations always route through SecureStore APIs
- if a key is marked `secret_ref`, value must validate as a SecureStore handle format

Example:
- registry path: `/apps/com.citadel.mail/credentials/smtp_ref`
- stored value: `SSREF:8f3c...`
- actual secret: only in SecureStore

## 10. Concurrency and Consistency

- readers are lock-free under snapshot isolation
- writers use transaction commit with conflict detection on `version`
- watch delivery is ordered by commit sequence number

Conflict pattern:
- if expected version mismatches, return `E_REG_VERSION_CONFLICT`

## 11. Error Model (Registry-specific)

Suggested typed errors:
- `E_REG_NOT_FOUND`
- `E_REG_ACCESS_DENIED`
- `E_REG_INVALID_PATH`
- `E_REG_TYPE_MISMATCH`
- `E_REG_VERSION_CONFLICT`
- `E_REG_LIMIT_EXCEEDED`
- `E_REG_BACKEND_UNAVAILABLE`

## 12. Migration Plan from File-based Config

Phased migration:
1. Mirror existing root config JSON values into `/machine/config/*`.
2. Add read-through adapter: registry read falls back to legacy files if missing.
3. Emit deprecation warnings on legacy-file writes.
4. Flip default read path to registry.
5. Remove fallback once parity tests pass.

Initial migration targets:
- `sysconfig.json`
- `boot.json`
- `services.json`
- `security.json`

## 13. Test Matrix (MVP)

- Key CRUD with each supported value type
- ACL enforcement across machine/user/app scopes
- CAS conflict handling on concurrent writers
- watch notification ordering and delivery
- cache-only boot read behavior
- DB recovery replay behavior
- SecureStore reference validation behavior

## 14. Success Criteria

Registry v0.1 is complete when:
- core CRUD/list/watch operations are stable
- ACL and permission checks are enforced end-to-end
- file-config migration read path is functional
- SecureStore boundary is validated by tests
- runtime APIs are available for CiteLang and native apps
