#!/usr/bin/env python3
import argparse
import datetime
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


@dataclass
class SourceRef:
    path: str
    line: int


@dataclass
class TodoItem:
    text: str
    bucket: str  # "now" | "later"
    score: int
    sources: List[SourceRef] = field(default_factory=list)


HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.+?)\s*$")
OPEN_TASK_RE = re.compile(r"^\s*(?:[-*]\s+)?\[\s*\]\s*(.+?)\s*$")
DONE_TASK_RE = re.compile(r"^\s*(?:[-*]\s+)?\[\s*[xX]\s*\]\s*(.+?)\s*$")
BULLET_TODO_RE = re.compile(r"^\s*[-*]\s+(TODO|FIXME|NOTE)\b[:\-]?\s*(.+?)\s*$", re.IGNORECASE)
FENCE_RE = re.compile(r"^\s*```")


def _bucket_from_heading(heading_text: str) -> Optional[str]:
    h = heading_text.strip().lower()
    if any(k in h for k in ["done", "completed", "complete", "resolved"]):
        return "done"
    if any(k in h for k in ["later", "backlog", "future", "brainstorm", "ideas", "idea", "someday", "not now", "archive", "defer"]):
        return "later"
    return None


def _is_critical_marker(text: str) -> bool:
    t = text.lower()
    return any(k in t for k in [
        "p0",
        "priority 0",
        "critical",
        "urgent",
        "blocker",
        "security",
        "secure boot",
        "signing",
        "tpm",
    ])


def _score_text(text: str) -> int:
    t = text.lower()
    score = 0

    # Priority tokens
    if "p0" in t or "priority 0" in t:
        score += 60
    if "p1" in t or "priority 1" in t:
        score += 40
    if "p2" in t or "priority 2" in t:
        score += 20

    # Urgency / risk
    if any(k in t for k in ["critical", "urgent", "blocker"]):
        score += 50
    if any(k in t for k in ["security", "secure boot", "signing", "vuln", "exploit"]):
        score += 45
    if any(k in t for k in ["crash", "panic", "hang", "deadlock"]):
        score += 35
    if any(k in t for k in ["bug", "fix", "regression"]):
        score += 20

    # Subsystems (light weighting)
    if any(k in t for k in ["kernel", "filesystem", "driver", "boot", "network"]):
        score += 15
    if any(k in t for k in ["desktop", "ui", "cuiml", "renderer"]):
        score += 10

    # De-prioritizers
    if any(k in t for k in ["nice to have", "optional", "maybe", "idea", "brainstorm"]):
        score -= 20
    if any(k in t for k in ["doc", "documentation", "style guide"]):
        score -= 5

    return score


def _normalize_text(text: str) -> str:
    # Keep readable, but normalize whitespace for dedupe.
    t = text.strip()
    # Remove common leading/trailing Markdown decoration without being too clever.
    t = t.strip("*_ ")
    return re.sub(r"\s+", " ", t)


def _is_garbage_item(text: str) -> bool:
    t = text.strip()
    if len(t) < 5:
        return True
    # Punctuation-only / bracket fragments
    if re.fullmatch(r"[\W_]+", t):
        return True
    return False


def _default_bucket_for_file(path: Path) -> str:
    p = str(path.as_posix()).lower()
    name = path.name.lower()
    if p.startswith("docs/"):
        return "later"
    if "jsonfunction" in name:
        return "later"
    if "brainstorm" in name or "plan" in name or "review_summary" in name or "summary" in name:
        return "later"
    if name == "todo_readme.md":
        return "later"
    return "now"


def _is_planning_doc(path: Path) -> bool:
    name = path.name.lower()
    return (
        "brainstorm" in name
        or "plan" in name
        or "review_summary" in name
        or name.endswith("_summary.md")
        or name == "md_review_summary.md"
    )


def iter_md_files(root: Path, exclude_dirs: Iterable[str], exclude_files: Iterable[str]) -> List[Path]:
    exclude = set(exclude_dirs)
    exclude_file_set = set(exclude_files)
    result: List[Path] = []
    for dirpath, dirnames, filenames in os.walk(root):
        # Prune excluded directories in-place
        # Special-case: keep backups/todo_archive_* but exclude other backups content.
        rel_dir = Path(dirpath).resolve()
        try:
            rel = rel_dir.relative_to(root)
        except Exception:
            rel = None

        if rel is not None and len(rel.parts) >= 1 and rel.parts[0].lower() == "backups":
            # Only keep backups/todo_archive_*/
            if len(rel.parts) == 1:
                dirnames[:] = [d for d in dirnames if d.lower().startswith("todo_archive_")]
            else:
                # Inside backups/, allow walking only within todo_archive_*
                if not rel.parts[1].lower().startswith("todo_archive_"):
                    dirnames[:] = []
                    continue

        # Apply generic excludes, but never exclude backups/ outright here;
        # we gate it via the todo_archive_* logic above.
        dirnames[:] = [d for d in dirnames if (d not in exclude) or (d.lower() == "backups")]
        for fn in filenames:
            if fn.lower().endswith(".md"):
                rel_path = str((Path(dirpath) / fn).resolve().relative_to(root.resolve())).replace(os.sep, "/")
                if rel_path in exclude_file_set:
                    continue
                result.append(Path(dirpath) / fn)
    result.sort()
    return result


def extract_todos(path: Path) -> List[TodoItem]:
    items: List[TodoItem] = []
    current_bucket = _default_bucket_for_file(path)
    current_done_section = False
    in_fence = False

    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception:
        return items

    for idx, line in enumerate(lines, start=1):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue

        m = HEADING_RE.match(line)
        if m:
            bucket = _bucket_from_heading(m.group(2))
            if bucket == "done":
                current_done_section = True
                current_bucket = _default_bucket_for_file(path)
            elif bucket == "later":
                current_done_section = False
                current_bucket = "later"
            else:
                current_done_section = False
                current_bucket = _default_bucket_for_file(path)
            continue

        m = DONE_TASK_RE.match(line)
        if m:
            continue

        m = OPEN_TASK_RE.match(line)
        if m:
            if current_done_section:
                continue
            text = _normalize_text(m.group(1))
            if not text or _is_garbage_item(text):
                continue

            bucket = current_bucket
            if _is_planning_doc(path) and not _is_critical_marker(text):
                bucket = "later"

            items.append(
                TodoItem(
                    text=text,
                    bucket=bucket,
                    score=_score_text(text),
                    sources=[SourceRef(path=str(path.as_posix()), line=idx)],
                )
            )
            continue

        m = BULLET_TODO_RE.match(line)
        if m:
            if current_done_section:
                continue
            text = _normalize_text(m.group(2))
            if not text or _is_garbage_item(text):
                continue

            bucket = current_bucket
            if _is_planning_doc(path) and not _is_critical_marker(text):
                bucket = "later"

            items.append(
                TodoItem(
                    text=text,
                    bucket=bucket,
                    score=_score_text(text),
                    sources=[SourceRef(path=str(path.as_posix()), line=idx)],
                )
            )
            continue

    return items


def merge_items(all_items: List[TodoItem]) -> Tuple[List[TodoItem], List[TodoItem]]:
    merged: Dict[str, TodoItem] = {}

    def key_for(item: TodoItem) -> str:
        return item.text.strip().lower()

    for it in all_items:
        k = key_for(it)
        if k not in merged:
            merged[k] = it
            continue
        existing = merged[k]

        # Prefer "now" over "later" if either says now
        if existing.bucket != "now" and it.bucket == "now":
            existing.bucket = "now"
        existing.score = max(existing.score, it.score)
        existing.sources.extend(it.sources)

    now = [v for v in merged.values() if v.bucket == "now"]
    later = [v for v in merged.values() if v.bucket == "later"]

    def sort_key(it: TodoItem):
        return (-it.score, it.text.lower())

    now.sort(key=sort_key)
    later.sort(key=sort_key)
    return now, later


def classify_now(items: List[TodoItem]) -> Tuple[List[TodoItem], List[TodoItem], List[TodoItem]]:
    critical: List[TodoItem] = []
    needed_now: List[TodoItem] = []
    needed_soon: List[TodoItem] = []

    for it in items:
        if _is_critical_marker(it.text) or it.score >= 70:
            critical.append(it)
        elif it.score >= 25:
            needed_now.append(it)
        else:
            needed_soon.append(it)

    def sort_key(it: TodoItem):
        return (-it.score, it.text.lower())

    critical.sort(key=sort_key)
    needed_now.sort(key=sort_key)
    needed_soon.sort(key=sort_key)
    return critical, needed_now, needed_soon


def split_later(items: List[TodoItem]) -> Tuple[List[TodoItem], List[TodoItem]]:
    not_now: List[TodoItem] = []
    maybe: List[TodoItem] = []
    for it in items:
        t = it.text.lower()
        if it.score <= 0 or any(k in t for k in ["maybe", "idea", "brainstorm", "nice to have", "optional"]):
            maybe.append(it)
        else:
            not_now.append(it)

    def sort_key(it: TodoItem):
        return (-it.score, it.text.lower())

    not_now.sort(key=sort_key)
    maybe.sort(key=sort_key)
    return not_now, maybe


def format_sources(sources: List[SourceRef], root: Path) -> str:
    # Limit per-item source spam
    uniq: Dict[Tuple[str, int], None] = {}
    ordered: List[SourceRef] = []
    for s in sources:
        k = (s.path, s.line)
        if k in uniq:
            continue
        uniq[k] = None
        ordered.append(s)

    ordered = ordered[:4]
    links = []
    for s in ordered:
        try:
            rel = str(Path(s.path).resolve().relative_to(root.resolve())).replace(os.sep, "/")
        except Exception:
            rel = s.path
        links.append(f"[{rel}]({rel}#L{s.line})")
    return ", ".join(links)


def render_md(now: List[TodoItem], later: List[TodoItem], scanned_count: int, root: Path, excludes: List[str]) -> str:
    today = datetime.date.today().isoformat()
    excludes_str = ", ".join(f"{d}/" for d in excludes)

    out: List[str] = []
    out.append("# TODO (Main)")
    out.append("")
    out.append(f"Generated: {today}")
    out.append(f"Sources scanned: {scanned_count} Markdown files (excluding {excludes_str}; including backups/todo_archive_*/)")
    out.append("")

    critical, needed_now, needed_soon = classify_now(now)
    not_now, maybe = split_later(later)

    out.append("## Critical")
    if not critical:
        out.append("- (none)")
    else:
        for it in critical:
            src = format_sources(it.sources, root)
            out.append(f"- [ ] {it.text} (sources: {src})")
    out.append("")

    out.append("## Needed Now")
    if not needed_now:
        out.append("- (none)")
    else:
        for it in needed_now:
            src = format_sources(it.sources, root)
            out.append(f"- [ ] {it.text} (sources: {src})")
    out.append("")

    out.append("## Needed (Non-Critical)")
    if not needed_soon:
        out.append("- (none)")
    else:
        for it in needed_soon:
            src = format_sources(it.sources, root)
            out.append(f"- [ ] {it.text} (sources: {src})")
    out.append("")

    out.append("## Not Now")
    if not not_now:
        out.append("- (none)")
    else:
        for it in not_now:
            src = format_sources(it.sources, root)
            out.append(f"- [ ] {it.text} (sources: {src})")
    out.append("")

    out.append("## Maybe")
    if not maybe:
        out.append("- (none)")
    else:
        for it in maybe:
            src = format_sources(it.sources, root)
            out.append(f"- [ ] {it.text} (sources: {src})")
    out.append("")

    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--out", default="build/todo_consolidated.md")
    ap.add_argument("--exclude-dir", action="append", default=["build", "backups", ".git"])
    args = ap.parse_args()

    root = Path(args.root).resolve()
    out_path = (root / args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # Avoid self-referential sources: don’t scan the generated TODO list.
    exclude_files = {"TODO_MAIN.md", args.out.lstrip("/")}
    md_files = iter_md_files(root, args.exclude_dir, exclude_files=exclude_files)
    all_items: List[TodoItem] = []
    for p in md_files:
        all_items.extend(extract_todos(p))

    now, later = merge_items(all_items)
    md = render_md(now, later, scanned_count=len(md_files), root=root, excludes=args.exclude_dir)
    out_path.write_text(md, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
