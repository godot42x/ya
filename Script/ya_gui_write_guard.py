#!/usr/bin/env python3
"""GUI runtime property-write guard (GI-203).

Forbids direct writes (``->_field = ...``) to encapsulated GUI backing fields
outside the GUI owner directory. The fields are ``protected`` (the C++ C2248
access check is the hard gate); this grep is the fast, no-build gate for
pre-commit / CI.

Why grep instead of only the compiler:
  - gives an immediate signal before a full rebuild;
  - survives builds that do not recompile the offending translation unit.

Usage:
    python3 Script/ya_gui_write_guard.py

Exit code 0 = clean; 1 = violations found.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# GUI owner: subclasses and framework internals here may read/write the
# protected backing fields directly (paint / layout / measure / constructors).
GUI_OWNER = os.path.normpath(os.path.join(ROOT, "Engine", "Source", "Framework", "GUI"))

# Directories whose `_position`/`_size`/... are unrelated to the GUI backing
# fields (e.g. TransformComponent::_position is a glm::vec3). These modules
# are barred from depending on GUI by ya_module_lint, so they cannot legally
# touch a UIElement; a grep cannot distinguish the same-named fields.
NON_GUI_DIRS = [
    os.path.normpath(os.path.join(ROOT, "Engine", "Source", "Framework", "ECS")),
]

# Encapsulated backing fields (GI-202). A direct ->_field = write outside the
# GUI owner is a boundary violation and must go through the changed-only
# setter (setPosition / setSize / setVisibility / setText).
#
# `_color` is deliberately excluded: it is protected on UIPanel but still
# public (authoring-only) on UIText, so a grep cannot distinguish the two
# without type information. Add it once UIText::_color is also encapsulated.
FIELDS = ("position", "size", "visibility", "text")

# ->_field = ... (a direct write). The (?!=) excludes `==` comparisons.
WRITE_RE = re.compile(r"->\s*_(" + "|".join(FIELDS) + r")\s*=\s*(?!=)")

# Scan roots outside the GUI owner. Subclass-internal access in the GUI owner
# directory is legal and is excluded by the startswith check below.
SCAN_ROOTS = [
    os.path.join(ROOT, "Engine", "Source"),
    os.path.join(ROOT, "Engine", "Test"),
    os.path.join(ROOT, "Example"),
]


def _is_excluded(path: str) -> bool:
    norm = os.path.normpath(path)
    if norm.startswith(GUI_OWNER):
        return True
    return any(norm.startswith(d) for d in NON_GUI_DIRS)


def _strip_line_comments(text: str) -> str:
    # Drop //-to-end-of-line so commented-out writes (dead code) do not trip
    # the guard. Block comments are rare in these write positions; if one
    # appears it is a small, acceptable false positive for a transition gate.
    return re.sub(r"//[^\n]*", "", text)


def scan() -> list:
    violations = []
    for base in SCAN_ROOTS:
        if not os.path.isdir(base):
            continue
        for root, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in ("build", ".git")]
            for f in files:
                if not f.endswith((".h", ".cpp")):
                    continue
                path = os.path.join(root, f)
                if _is_excluded(path):
                    continue
                try:
                    text = open(path, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                text = _strip_line_comments(text)
                for m in WRITE_RE.finditer(text):
                    line = text.count("\n", 0, m.start()) + 1
                    violations.append(
                        f"[ya-gui-write-guard] {os.path.relpath(path, ROOT)}:{line}: "
                        f"direct write ->_{m.group(1)} = ... outside GUI owner; use the setter"
                    )
    return violations


def main() -> int:
    violations = scan()
    for v in violations:
        print(v)
    if violations:
        print(f"ya-gui-write-guard: {len(violations)} violation(s)")
        return 1
    print("ya-gui-write-guard: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
