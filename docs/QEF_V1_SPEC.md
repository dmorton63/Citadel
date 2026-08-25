# QEF v1 Specification (Draft)

Status: Draft v1
Owner: Citadel runtime/tooling
Scope: User-space executable container for Citadel-native app delivery and launch

## 1. Goals

QEF (Quantum Executable Format) provides:
- A stable, signed executable container for Citadel applications.
- Deterministic metadata for loader validation.
- A clear contract between IDE/build tooling and runtime loader.

QEF v1 is intentionally small: fixed header + section table + optional manifest + optional signature block.

## 2. Endianness and Alignment

- Endianness: little-endian.
- Header size: fixed 64 bytes for v1.
- Section table entry size: fixed 48 bytes for v1.
- Offsets are file-relative byte offsets.
- RVAs are image-relative virtual addresses.

## 3. File Layout (v1)

1. QEF header (64 bytes)
2. Section table (`section_count` entries x 48 bytes)
3. Section payloads
4. Optional manifest payload
5. Optional signature payload

## 4. Header (64 bytes)

Offset | Size | Field | Notes
--- | --- | --- | ---
0 | 4 | magic | ASCII `QEF1`
4 | 2 | header_size | Must be 64 for v1
6 | 2 | version | Must be 1 for v1
8 | 4 | flags | Bitfield (see below)
12 | 4 | entry_rva | Entry point RVA in image
16 | 4 | section_count | Number of section entries
20 | 4 | section_table_offset | File offset to section table
24 | 4 | manifest_offset | File offset to manifest blob (0 if absent)
28 | 4 | manifest_size | Manifest blob size (0 if absent)
32 | 4 | signature_offset | File offset to signature blob (0 if absent)
36 | 4 | signature_size | Signature blob size (0 if absent)
40 | 4 | image_size | Total mapped image bytes
44 | 4 | required_api | Minimum Citadel runtime API level
48 | 16 | reserved | Must be zero in v1

### Header Flags (v1)

Bit | Name | Meaning
--- | --- | ---
0 | `HAS_MANIFEST` | Manifest payload is present
1 | `HAS_SIGNATURE` | Signature payload is present
2 | `RELOCATABLE` | Image supports non-default load base
3 | `SANDBOX_REQUIRED` | Runtime must launch in sandbox profile

## 5. Section Table Entry (48 bytes)

Offset | Size | Field | Notes
--- | --- | --- | ---
0 | 16 | name | Null-terminated ASCII (example: `.text`)
16 | 4 | type | Section type enum
20 | 4 | flags | Section flags
24 | 4 | file_offset | Raw bytes in file
28 | 4 | file_size | Raw bytes length
32 | 4 | rva | Target image RVA
36 | 4 | mem_size | In-memory size (zero-fill tail if `mem_size > file_size`)
40 | 4 | align | Section alignment (power of two)
44 | 4 | reserved | Must be zero

### Section Types (v1)

Value | Name
--- | ---
1 | `TEXT`
2 | `RODATA`
3 | `DATA`
4 | `BSS`
5 | `IMPORTS`
6 | `EXPORTS`
7 | `RELOC`
8 | `DEBUG`

### Section Flags (v1)

Bit | Name
--- | ---
0 | `R`
1 | `W`
2 | `X`
3 | `COMPRESSED`
4 | `INTEGRITY_HASHED`

## 6. Manifest Payload (Optional)

Manifest payload format for v1: UTF-8 JSON.

Recommended keys:
- `appId`: stable runtime app id
- `name`: display name
- `version`: semantic version string
- `entrySymbol`: optional entry symbol string
- `minApi`: runtime API minimum
- `sandboxProfile`: required sandbox profile id
- `capabilities`: string array
- `dependencies`: array of `{ id, minVersion }`

## 7. Signature Payload (Optional)

Signature payload format for v1:
- `sig_type` (u32): algorithm id
- `key_id` (u32): signer key id
- `sig_len` (u32)
- `sig_bytes[sig_len]`

Initial algorithm recommendation:
- `sig_type=1`: SHA-256 digest + detached platform signature envelope.

## 8. Loader Validation Rules (v1)

Loader must reject when:
- magic != `QEF1`
- `header_size != 64`
- `version != 1`
- section table range exceeds file size
- any section range exceeds file size
- overlapping sections violate loader policy
- reserved fields are non-zero (strict mode)
- declared manifest/signature ranges exceed file size
- signature required by policy but absent/invalid

## 9. CLI Surface (planned)

- `qefinfo <path>`: inspect and validate header/offset sanity.
- `qefpack <input> <output.qef>`: build QEF artifact.
- `qefrun <path.qef>`: verify + load + execute.

## 10. Versioning

- QEF version uses `version` field.
- Future versions may extend header/entry size, but v1 readers only accept `header_size=64` and `version=1`.
