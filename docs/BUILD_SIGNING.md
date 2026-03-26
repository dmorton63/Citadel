# Build + Signing (Boot / Ramdisk UI Assets)

This document describes what Citadel’s build pipeline currently **packages and signs** for early-boot UI, and what “production” mode enforces.

## Where This Happens

- Build script: [build.sh](../build.sh)
- Ramdisk source tree: `ramdisk/` (copied into a FAT32 image as part of `build.sh`)

## Why 8.3 Names Matter

Citadel’s early FAT32 reader does not implement Long File Names (LFN). For files that must be readable early (boot-time UI), the build packs key assets using **8.3** names (e.g. `DESKTOP.CML`, `DESKTOP.SIG`).

## Production Mode (“Fail-Closed”)

When `build.sh` is run with `--prod`, it enforces that certain boot-time UI assets are signed. If a required signature is missing (and cannot be generated), the build fails.

### Desktop Definition Signatures

There are two paths the build supports:

1. Project-root `.cuiml` desktop (packed as `DESKTOP.CML`)
   - Source searched at:
     - `desktop.cuiml`
     - `shared/desktop.cuiml`
   - Packed into ramdisk image as: `/DESKTOP.CML`
   - Signature packed as: `/DESKTOP.SIG`
   - In production mode: `DESKTOP.SIG` must exist (or be generated) and must be exactly **256 bytes**.

2. Preferred trusted path: `/SYSTEM/UI/DESKTOP.CML`
   - Source searched at:
     - `ramdisk/system/ui/desktop.cml`
     - `ramdisk/system/ui/DESKTOP.CML`
   - Signature packed as: `/SYSTEM/UI/DESKTOP.SIG`
   - In production mode: the signature must exist (or be generated) and must be exactly **256 bytes**.

### CUIMLSS Stylesheet Signatures (`*.cxs`)

- The build scans `ramdisk/system/ui/*.cxs` (and `*.CXS`).
- Each stylesheet must have a matching signature (same base name, `.sig`/`.SIG`) or be generatable.
- In production mode:
  - Every stylesheet signature must exist (or be generated)
  - And must be exactly **256 bytes**.
- When packed into the boot image, signatures are stored as uppercased 8.3 names under `/SYSTEM/UI/`.

## Signature Generation

If a signature file is not present, `build.sh` will attempt to generate one with OpenSSL:

- Signing key searched at:
  - `keys/bootgate_rsa_priv.pem`
  - or `build/bootgate_rsa_priv.pem`
- Command used (conceptually): `openssl dgst -sha256 -sign <key> -out <sig> <file>`
- Signature format: raw RSA-2048 signature bytes (expected size: **256** bytes)

If OpenSSL or the private key is not available, production builds require you to provide the signature files manually.

## What Is *Not* Covered (Today)

- HTML/CSS used by the HTML viewer is not part of the current production signature sweep.
- The `.cui` component/icon/role library format is intentionally **not** treated as CUIMLSS and is not included in the `*.cxs` signature loop.

If you want HTML (`.html`) or browser CSS (`.css`) to be part of the trusted boot-time UI surface, the build script will need an additional explicit signing + enforcement step for those file patterns.
