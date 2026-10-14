#!/usr/bin/env python3
"""Editor chrome localization lint.

Scans the GameEditor sources for `editorTr("key"[, "fallback"])` and
`editorText("key"[, "fallback"])` call sites, then checks every key against the
shipped editor language tables (`Engine/Content/Locale/editor.<lang>.json`).

A key is satisfied when it appears in at least one table, OR when the call site
supplies a fallback source string (English is the passthrough: an untranslated
long key degrades to its fallback text, never to the identifier). A key with
neither is a hard error: it would render the raw identifier.

Exit code 0 when clean, 1 when a call site has no fallback AND no table entry.

Usage:
    python3 Script/editor_locale_lint.py [--root <workspace>]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# `editorTr("key")` / `editorTr("key", "fallback")` / `editorText(...)`.
# The first string literal is the key; an optional second literal is the
# fallback source. Nested `editorTr("a" + suffix)` calls are not resolved here
# (dynamic keys are reported separately as "dynamic").
CALL_RE = re.compile(
    r"\b(?:editorTr|editorText)\s*\(\s*"
    r'(?P<key>"(?:[^"\\]|\\.)*")'
    r'(?P<rest>[^)]*)',
    re.DOTALL,
)
FALLBACK_RE = re.compile(r'^\s*,\s*"(?:[^"\\]|\\.)*"')

GAMEDITOR_DIR = Path("Engine/Source/Applications/GameEditor")
LOCALE_DIR = Path("Engine/Content/Locale")


def unescape_c_string(literal: str) -> str:
    """Minimal C string literal unescape for the keys we actually write."""
    body = literal[1:-1]
    return (
        body.replace("\\\"", "\"")
        .replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace("\\\\", "\\")
    )


def load_tables(root: Path) -> dict[str, set[str]]:
    tables: dict[str, set[str]] = {}
    locale_dir = root / LOCALE_DIR
    if not locale_dir.is_dir():
        return tables
    for path in sorted(locale_dir.glob("editor.*.json")):
        lang = path.name[len("editor.") : -len(".json")]
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            print(f"error: cannot parse {path}: {error}", file=sys.stderr)
            raise SystemExit(1)
        if not isinstance(data, dict):
            print(f"error: {path} must be a JSON object", file=sys.stderr)
            raise SystemExit(1)
        tables[lang] = {str(key) for key in data}
    return tables


def iter_call_sites(root: Path):
    editor_dir = root / GAMEDITOR_DIR
    if not editor_dir.is_dir():
        return
    for path in sorted(editor_dir.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in CALL_RE.finditer(text):
            key_literal = match.group("key")
            key = unescape_c_string(key_literal)
            has_fallback = bool(FALLBACK_RE.match(match.group("rest")))
            line = text.count("\n", 0, match.start()) + 1
            yield path, line, key, has_fallback


def main() -> int:
    parser = argparse.ArgumentParser(description="Editor chrome localization lint")
    parser.add_argument("--root", default=None, help="workspace root (default: repo root)")
    args = parser.parse_args()

    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parent.parent
    tables = load_tables(root)
    known: set[str] = set()
    for entries in tables.values():
        known |= entries

    if not tables:
        print("warning: no editor language tables found under Engine/Content/Locale", file=sys.stderr)

    missing: list[tuple[Path, int, str]] = []
    covered = 0
    for path, line, key, has_fallback in iter_call_sites(root):
        if key in known or has_fallback:
            covered += 1
            continue
        missing.append((path, line, key))

    for path, line, key in missing:
        rel = path.relative_to(root)
        print(f"{rel}:{line}: key '{key}' has no table entry and no fallback source")

    if missing:
        print(
            f"\n{len(missing)} untranslatable key(s): add them to a table under "
            f"Engine/Content/Locale or pass an English fallback string.",
            file=sys.stderr,
        )
        return 1

    languages = ", ".join(sorted(tables)) if tables else "(none)"
    print(f"editor locale lint: {covered} call site(s) covered by tables [{languages}] or fallback")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
