# Copilot Instructions (Citadel workspace)

## Primary goal
Avoid flooding VS Code chat/terminal UI with repeated text.

## Output rules (strict)
- Never repeat the same sentence/paragraph in one response.
- Keep progress updates to 1 short sentence per tool batch.
- Prefer a single, complete `apply_patch` per file rather than many incremental edits.
- If a change is doc-only, do not propose/perform unrelated code edits.
- When summarizing work, use at most 5 bullets and then stop.

## When running commands
- If command output might be large, suggest piping through `tools/dedupe_lines.py` or `tail`.
- Prefer writing logs to `build/` and summarizing the first distinct failure line.

## Editing style
- Minimal diffs; preserve existing style.
- No reformat-only changes.
- No new features beyond the user’s request.
