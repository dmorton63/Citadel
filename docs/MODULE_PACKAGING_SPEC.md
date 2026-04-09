# Citadel Module Packaging Spec (`.drv` / `.dll`)

Status: Draft v1 (MVP)

This document defines the binary container format for loadable kernel modules.
Both `.drv` (driver module) and `.dll` (general loadable library) use the same container.

## Goals

- Deterministic, freestanding-friendly format (no external runtime required).
- Explicit metadata and relocation information for kernel loader use.
- Forward-compatible versioning for future signing/compression/extensions.

## File Overview

Each module file is laid out as:

1. Fixed header (`CITM`) 
2. Section table
3. Relocation table
4. Import table
5. UTF-8 string table
6. Raw section payload bytes

All integer fields are little-endian.

## Header (`ModuleHeaderV1`)

Size: 96 bytes

```c
struct ModuleHeaderV1 {
    char     magic[4];           // "CITM"
    uint16_t formatVersion;      // 1
    uint16_t headerSize;         // sizeof(ModuleHeaderV1)

    uint32_t fileSize;           // full container size in bytes
    uint32_t flags;              // ModuleFlags bitmask
    uint32_t targetArch;         // 1=x86_64 (reserved for future arches)

    uint32_t moduleType;         // 1=Driver(.drv), 2=Library(.dll)
    uint32_t abiVersion;         // kernel/module ABI version

    uint32_t nameOffset;         // into string table
    uint32_t nameLength;         // bytes, no trailing NUL required
    uint32_t entrySymbolOffset;  // into string table (e.g. "module_init")
    uint32_t entrySymbolLength;

    uint32_t sectionCount;
    uint32_t sectionTableOffset;

    uint32_t relocationCount;
    uint32_t relocationTableOffset;

    uint32_t importCount;
    uint32_t importTableOffset;

    uint32_t stringTableSize;
    uint32_t stringTableOffset;

    uint32_t payloadOffset;      // first byte of raw section payload area
    uint32_t payloadSize;        // bytes of payload area

    uint8_t  moduleId[32];       // SHA-256 of canonical module descriptor
};
```

### Header validation rules

- `magic` must be `CITM`.
- `formatVersion` must be supported.
- All `{offset,size,count}` ranges must be inside `fileSize` and non-overlapping where required.
- `sectionCount` must be greater than 0.
- `payloadOffset + payloadSize <= fileSize`.

## Flags

`flags` is a bitmask:

- `0x00000001` executable module contains code section(s)
- `0x00000002` module requires relocation processing
- `0x00000004` module is position-independent
- `0x00000008` module is signed (signature block extension present)
- `0x00000010` module allows unload hook

Unknown flag bits must be rejected in MVP.

## Section Table (`SectionDescV1`)

Each entry describes a logical section in payload bytes.

```c
struct SectionDescV1 {
    uint32_t nameOffset;         // string table offset (e.g. ".text")
    uint32_t nameLength;

    uint32_t sectionType;        // 1=text, 2=rodata, 3=data, 4=bss
    uint32_t sectionFlags;       // read/write/exec bits

    uint32_t payloadOffset;      // relative to payloadOffset in header
    uint32_t payloadSize;        // bytes in file (0 for bss)

    uint32_t virtualSize;        // bytes to map (>= payloadSize)
    uint32_t alignment;          // power-of-two

    uint64_t preferredVaddr;     // optional, 0 for loader-chosen base
};
```

`sectionFlags` bits:

- `0x1` readable
- `0x2` writable
- `0x4` executable

## Relocation Table (`RelocEntryV1`)

Relocations are applied after section mapping and before entry dispatch.

```c
struct RelocEntryV1 {
    uint32_t targetSectionIndex; // section receiving patch
    uint32_t targetOffset;       // byte offset within target section

    uint32_t relocType;          // RelocType enum
    uint32_t symbolIndex;        // import symbol index, or UINT32_MAX for section-relative

    int64_t  addend;             // signed relocation addend
    uint32_t sourceSectionIndex; // section index for section-relative references
    uint32_t reserved;
};
```

### MVP relocation types (x86_64)

- `1` `ABS64`: write 64-bit absolute address
- `2` `REL32`: write 32-bit PC-relative displacement
- `3` `ABS32`: write 32-bit absolute value (range checked)

Loader must fail if any relocation type is unknown or out-of-range.

## Import Table (`ImportEntryV1`)

Imports define symbols the loader must resolve from kernel export registry.

```c
struct ImportEntryV1 {
    uint32_t symbolNameOffset;   // string table
    uint32_t symbolNameLength;

    uint32_t minAbiVersion;      // minimum provider ABI
    uint32_t maxAbiVersion;      // maximum provider ABI (0 = no upper bound)

    uint32_t required;           // 1=must resolve, 0=optional
    uint32_t reserved;
};
```

## Metadata (required keys)

Metadata strings are represented in the string table and referenced by offsets in header/tables.
The following logical fields are required in v1:

- `moduleName` (from `nameOffset/nameLength`)
- `moduleType` (`Driver` or `Library`)
- `moduleVersion` (semantic string, stored in string table, referenced by a reserved extension in v1.1)
- `entrySymbol` (from `entrySymbolOffset/entrySymbolLength`)
- `abiVersion`

For MVP, `moduleVersion` may also be encoded in the module filename until v1.1 metadata extension is implemented.

## Loading Sequence (MVP)

1. Read + validate header and tables.
2. Allocate/map sections according to `virtualSize`, `alignment`, and flags.
3. Copy payload bytes for non-BSS sections.
4. Resolve imports from kernel/module export registry.
5. Apply relocations.
6. Resolve and call entry symbol (`init`/entrypoint).
7. If load succeeds, register module in runtime registry.

## Extension Policy

- `formatVersion` is the compatibility gate.
- New optional blocks must be announced via header flags and appended after known tables.
- Unknown required blocks or flags are load failures in MVP.

## Naming

- Driver modules use `.drv`.
- General shared runtime modules use `.dll`.
- The internal container format is identical; extension controls policy, not parser behavior.
