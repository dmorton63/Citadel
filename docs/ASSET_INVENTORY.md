# Asset Inventory

Generated baseline inventory for scripted swaps (sizes/formats/locations).

## Core Art Paths

- wallpapers: `/ramdisk/wallpapers/` (runtime desktop assets)
- desktop config roots: `/desktop*.json`, `/desktop*.cml`
- screenshots/docs: `/docs/screenshots/`
- icon/image decode targets: `.png`, `.bmp`, `.ico`

## Format Targets

- `.png`: preferred for lossless UI assets.
- `.bmp`: legacy/simple pipeline compatibility.
- `.ico`: icon bundle compatibility (initial embedded PNG/BMP support).

## Script-Friendly Conventions

- Keep asset names stable and version via suffixes (example: `wallpaper_v2.png`).
- Update only config pointers in JSON/CML when swapping variants.
- Store generated/temporary art in `/build/` and promote curated files into runtime paths.

## Swap Checklist

1. Add/replace the file in its runtime asset directory.
2. Update the corresponding desktop/config entry.
3. Validate decode support (`png|bmp|ico`) and rendering path.
4. Rebuild and smoke-test desktop load.
