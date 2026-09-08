#!/usr/bin/env python3
"""Style-system visual regression baselines (gui-style-system-convergence
Phase 4 closing).

Regenerates headless snapshot dumps for key GUIWorkbench pages and compares
their structural/semantic digests against archived baselines in
Example/GUIWorkbench/Baselines/. This is the runnable, GPU-free visual gate
for the style system: it pins the geometry + colors + text of the shell
pages that theme changes are allowed to alter only deliberately.

Usage:
    python3 Script/gui_style_baseline.py            # verify against baselines
    python3 Script/gui_style_baseline.py --update   # (re)write baselines after review
    python3 Script/gui_style_baseline.py --verbose  # show per-page digests

A digest change means the page's rendered frame changed (layout, colors,
text, or theme values). If the change is intended (theme palette edit,
token retune), regenerate with --update and commit the new baselines.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE_DIR = ROOT / "Example" / "GUIWorkbench" / "Baselines"
BIN = ROOT / "build" / "macosx" / "arm64" / "debug" / "GUIWorkbench"

# (slug, start-page, scenario-lines) — the shell pages that pin shell chrome
# + theme values. theme_light toggles the tree theme via the ThemeToggle
# button (same coordinates as Scenarios/theme.jsonl).
MATRIX: list[tuple[str, str, list[str]]] = [
    ("render", "Render", ['{"frame":2}']),
    ("theme_dark", "Theme", ['{"frame":2}']),
    (
        "theme_light",
        "Theme",
        [
            '{"frame":1}',
            '{"event":"mouse_press","x":566,"y":120,"button":0}',
            '{"event":"mouse_release","x":566,"y":120,"button":0}',
            '{"frame":3}',
        ],
    ),
    ("dock", "Dock", ['{"frame":2}']),
    ("editor", "Editor", ['{"frame":2}']),
]


def run_row(slug: str, page: str, lines: list[str]) -> dict:
    with tempfile.NamedTemporaryFile(
        "w", suffix=".jsonl", delete=False, prefix=f"baseline_{slug}_"
    ) as f:
        f.write("\n".join(lines) + "\n")
        scenario = Path(f.name)
    with tempfile.NamedTemporaryFile(
        "w", suffix=".json", delete=False, prefix=f"dump_{slug}_", encoding="utf-8"
    ) as f:
        dump_path = Path(f.name)
    try:
        subprocess.run(
            [
                str(BIN),
                "--headless",
                f"--start-page={page}",
                f"--scenario={scenario}",
                f"--dump-snapshot-json={dump_path}",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    finally:
        scenario.unlink(missing_ok=True)
    data = json.loads(dump_path.read_text(encoding="utf-8"))
    dump_path.unlink(missing_ok=True)
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--update", action="store_true", help="rewrite baselines from the current build"
    )
    parser.add_argument("--verbose", action="store_true", help="print per-page digests")
    args = parser.parse_args()

    if not BIN.exists():
        print(f"GUIWorkbench binary not found: {BIN} — run 'xmake b GUIWorkbench' first")
        return 2
    BASELINE_DIR.mkdir(parents=True, exist_ok=True)

    all_pass = True
    for slug, page, lines in MATRIX:
        data = run_row(slug, page, lines)
        digests = {
            "structuralDigest": data.get("structuralDigest"),
            "semanticDigest": data.get("semanticDigest"),
            "itemCount": len(data.get("items", [])),
        }
        baseline_path = BASELINE_DIR / f"{slug}.json"

        if args.update:
            baseline_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
            print(f"[update] {slug:12s} {digests}")
            continue

        if not baseline_path.exists():
            print(f"[missing] {slug:12s} baseline not archived — rerun with --update")
            all_pass = False
            continue

        base = json.loads(baseline_path.read_text(encoding="utf-8"))
        base_item_count = len(base.get("items", []))
        mismatch = [
            key
            for key, value in (
                ("structuralDigest", base.get("structuralDigest")),
                ("semanticDigest", base.get("semanticDigest")),
                ("itemCount", base_item_count),
            )
            if digests[key] != value
        ]
        base_path = baseline_path.name
        if mismatch:
            all_pass = False
            print(
                f"[FAIL]    {slug:12s} {digests} vs baseline "
                f"({', '.join(mismatch)} changed; regenerate with --update only "
                f"after review, commit {base_path})"
            )
        else:
            print(f"[pass]    {slug:12s} {digests}")

    print(
        "\nstyle baseline gate: "
        + ("PASS" if all_pass and not args.update else "FAIL" if not all_pass else "updated")
    )
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())