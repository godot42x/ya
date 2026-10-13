#!/usr/bin/env python3
"""Agent docs lint: keeps `.agent/` navigable and honest.

Checks (see `.agent/plan/agent-files-maintenance/plan.md`):
  1. Index coverage  - every skill dir / memory file is listed in its index
                       (`skills/AGENTS.md`, `memories/AGENTS.md`), and every
                       listed name still exists on disk.
  2. Link resolution - relative `*.md` links must resolve from the referring
                       file's own directory.
  3. Size budget     - `SKILL.md` and `references/*.md` stay within budget.
  4. Line-number refs - skills must not cite bare line numbers (rot fastest).

`plan/` is exempt: it is a historical record by its own declaration
(`plan/AGENTS.md`), so stale paths there are by design.

Usage:
    python3 Script/ya_agent_docs_lint.py            # report violations
    python3 Script/ya_agent_docs_lint.py --quiet    # print summary line only

Exit code 0 = clean; 1 = violations found.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AGENT = os.path.join(ROOT, ".agent")
SKILLS = os.path.join(AGENT, "skills")
MEMORIES = os.path.join(AGENT, "memories")
SKILLS_INDEX = os.path.join(SKILLS, "AGENTS.md")
MEMORIES_INDEX = os.path.join(MEMORIES, "AGENTS.md")

# Size budget (plan.md §A2). Lines are the real measure of read burden;
# the byte ceiling only catches degenerate blobs (a single enormous line).
# It is deliberately loose for bytes because CJK prose is ~3 B/char, so a
# 200-line Chinese skill already exceeds an ASCII-tuned 12 KB.
MAX_LINES = 300
MAX_BYTES = 32 * 1024

# Files exempt from the size budget until their batch lands. Keep this list
# shrinking; each entry is a debt, not a permission. Exempt entries are still
# reported on every run as warnings, but they do not fail the build - otherwise
# the guard could never go green before the debt is paid, and an always-red
# check gets ignored. Batch A3 pays these down; the list must then empty.
SIZE_EXEMPT = {
    "skills/gui-framework/SKILL.md": "A3 pending",
    "skills/ya-build/SKILL.md": "A3 follow-on",
    "skills/render-arch/SKILL.md": "A3 follow-on",
}

# Directories that are historical records: links and paths there may be stale.
EXEMPT_PREFIXES = ("plan/",)

# Bare line-number citations in prose, e.g. "见 foo.cpp:123".
# Deliberately narrow: it matches a source-file path with a line suffix, which
# is the form that rots when code moves. It ignores prose about line counts
# ("不超过 300 行") and pasted tool output, which are not citations.
LINE_REF_RE = re.compile(r"[\w./-]+\.(?:h|cpp|cc|lua|json|slang):\d{2,5}\b")
# Markdown links to .md files.
MD_LINK_RE = re.compile(r"\]\(([^)\s]+\.md)(?:#[^)]*)?\)")
# Backticked .md paths. Only those carrying a separator count as references:
# a bare `plan.md` in prose is an artifact name, not a link (plan/AGENTS.md
# defines plan.md/progress.md as generic artifact names).
CODE_PATH_RE = re.compile(r"`((?:\.{1,2}/|[\w.-]+/)[\w./-]*\.md)`")


def rel(path: str) -> str:
    return os.path.relpath(path, AGENT)


def read(path: str) -> str:
    try:
        return open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def strip_code_blocks(text: str) -> str:
    """Drop fenced blocks: pasted tool output is not a citation and does not
    rot the way a prose reference to source location does."""
    return re.sub(r"```.*?```", "", text, flags=re.S)


def walk_md(base: str, skip_exempt: bool = True):
    for root, dirs, files in os.walk(base):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in sorted(files):
            if not f.endswith(".md"):
                continue
            p = os.path.join(root, f)
            r = rel(p)
            if skip_exempt and r.startswith(EXEMPT_PREFIXES):
                continue
            yield p


# ---------------------------------------------------------------- index checks

def check_skill_index(violations: list[str]) -> None:
    disk = {
        d for d in os.listdir(SKILLS)
        if os.path.isdir(os.path.join(SKILLS, d)) and not d.startswith(".")
    }
    text = read(SKILLS_INDEX)
    listed = set(re.findall(r"^- `([a-z0-9-]+)`", text, re.M))

    for name in sorted(disk - listed):
        violations.append(f"index: skill `{name}` on disk but not in skills/AGENTS.md")
    for name in sorted(listed - disk):
        violations.append(f"index: skills/AGENTS.md lists `{name}` but no such directory")


def check_memory_index(violations: list[str]) -> None:
    disk = {
        f for f in os.listdir(MEMORIES)
        if f.endswith(".md") and f != "AGENTS.md"
    }
    text = read(MEMORIES_INDEX)
    listed = set(re.findall(r"^- `\./([\w.-]+\.md)`", text, re.M))

    for name in sorted(disk - listed):
        violations.append(f"index: memory `{name}` on disk but not in memories/AGENTS.md")
    for name in sorted(listed - disk):
        violations.append(f"index: memories/AGENTS.md lists `{name}` but no such file")


def check_routing_single_source(violations: list[str]) -> None:
    """The priority list must live only in skills/AGENTS.md."""
    marker = "ya-build > gui-framework"
    for entry in ("AGENTS.md", ".agent/AGENTS.md"):
        p = os.path.join(ROOT, entry)
        if os.path.isfile(p) and marker in read(p):
            violations.append(
                f"routing: {entry} repeats the skill priority list; "
                f"keep it only in .agent/skills/AGENTS.md"
            )


# ----------------------------------------------------------- link / size checks

def check_links(violations: list[str]) -> None:
    for base in (SKILLS, MEMORIES, os.path.join(AGENT, "agents")):
        if not os.path.isdir(base):
            continue
        for path in walk_md(base):
            text = read(path)
            refs = [m.group(1) for m in MD_LINK_RE.finditer(text)]
            refs += [m.group(1) for m in CODE_PATH_RE.finditer(text)]
            for target in refs:
                if target.startswith(("http://", "https://", "/", "#", "mailto:")):
                    continue
                # Two conventions are in use: relative to the referring file,
                # and repo-root-relative (`Engine/...`, `.agent/...`). Accept
                # either; only report when neither resolves.
                candidates = [
                    os.path.normpath(os.path.join(os.path.dirname(path), target)),
                    os.path.normpath(os.path.join(ROOT, target)),
                ]
                if not any(os.path.exists(c) for c in candidates):
                    violations.append(
                        f"link: {rel(path)} references `{target}` (not found; "
                        f"tried doc-relative and repo-root)"
                    )


def check_sizes(violations: list[str], warnings: list[str]) -> None:
    for path in walk_md(SKILLS):
        name = os.path.basename(path)
        r = rel(path)
        is_budgeted = name == "SKILL.md" or "/references/" in "/" + r
        if not is_budgeted:
            continue
        text = read(path)
        lines = text.count("\n") + 1
        nbytes = len(text.encode("utf-8"))
        over = []
        if lines > MAX_LINES:
            over.append(f"{lines} lines > {MAX_LINES}")
        if nbytes > MAX_BYTES:
            over.append(f"{nbytes} B > {MAX_BYTES}")
        if not over:
            continue
        detail = f"{r} over budget ({', '.join(over)})"
        if r in SIZE_EXEMPT:
            warnings.append(f"size (debt, {SIZE_EXEMPT[r]}): {detail}")
        else:
            violations.append(f"size: {detail}; split into references/")


def check_line_refs(violations: list[str]) -> None:
    for path in walk_md(SKILLS):
        prose = strip_code_blocks(read(path))
        for i, line in enumerate(prose.splitlines(), 1):
            if LINE_REF_RE.search(line):
                violations.append(
                    f"line-ref: {rel(path)} cites a line number; "
                    f"use the symbol or path instead"
                )


def main() -> int:
    quiet = "--quiet" in sys.argv
    violations: list[str] = []
    warnings: list[str] = []

    check_skill_index(violations)
    check_memory_index(violations)
    check_routing_single_source(violations)
    check_links(violations)
    check_sizes(violations, warnings)
    check_line_refs(violations)

    if not quiet:
        for w in warnings:
            print(f"[ya-agent-docs] {w}")
        for v in violations:
            print(f"[ya-agent-docs] {v}")

    if violations:
        print(f"ya-agent-docs-lint: {len(violations)} violation(s)")
        return 1

    tail = f" ({len(warnings)} known debt)" if warnings else ""
    print(f"ya-agent-docs-lint: ok{tail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
