#!/usr/bin/env python3
"""Run the macOS/MoltenVK gate for GUI architecture convergence.

This is intentionally a small local-runner harness, not a second build
system. It configures the macOS Vulkan SDK via `ya.py cfg`, validates the
closure/headless/minimal GUI paths, checks exact ScrollSplit
windowed/headless snapshot identity, and rebuilds a macOS-local zero-diff
GUIWorkbench page matrix.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DIAGNOSTIC_PATTERN = re.compile(r"VUID|\[ERROR\]|ASSERT", re.IGNORECASE)
WORKBENCH_PAGE_MATRIX: tuple[tuple[str, str, Path], ...] = (
    ("render", "Render", ROOT / "Example/GUIWorkbench/Scenarios/render_probe_interaction.jsonl"),
    ("widgets", "Widgets", ROOT / "Example/GUIWorkbench/Scenarios/widgets_interaction.jsonl"),
    ("layout", "Layout", ROOT / "Example/GUIWorkbench/Scenarios/layout_spacing_interaction.jsonl"),
    ("menus", "Menus", ROOT / "Example/GUIWorkbench/Scenarios/menus_popup_interaction.jsonl"),
    ("dragdrop", "DragDrop", ROOT / "Example/GUIWorkbench/Scenarios/dragdrop_interaction.jsonl"),
    ("modal", "Modal", ROOT / "Example/GUIWorkbench/Scenarios/modal_interaction.jsonl"),
    (
        "scrollsplit",
        "ScrollSplit",
        ROOT / "Example/GUIWorkbench/Scenarios/resize_scrollsplit_interaction_stress.jsonl",
    ),
    (
        "editor",
        "Editor",
        ROOT / "Example/GUIWorkbench/Scenarios/editor_inspector_interaction.jsonl",
    ),
)


def run(command: list[str], log_path: Path, *, require_clean_gui_log: bool = False) -> None:
    print("+", " ".join(command))
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    output = result.stdout + result.stderr
    log_path.write_text(output, encoding="utf-8")
    sys.stdout.write(output)

    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}")
    if require_clean_gui_log and DIAGNOSTIC_PATTERN.search(output):
        raise RuntimeError(f"GUI validation diagnostics found; inspect {log_path}")


def repo_relative(path: Path) -> str:
    return str(path.relative_to(ROOT))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_gui_cross_path_smoke(output_dir: Path) -> None:
    run(["xmake", "b", "GUIWorkbench"], output_dir / "build-workbench.log")
    run(["xmake", "b", "ya-gui-closure-test"], output_dir / "build-closure.log")
    run(["xmake", "b", "ya-gui-headless-host-test"], output_dir / "build-headless-host.log")
    run(["xmake", "b", "ya-gui-minimal-host"], output_dir / "build-minimal-host.log")

    run(["xmake", "r", "ya-gui-closure-test"], output_dir / "closure.log", require_clean_gui_log=True)
    run(
        ["xmake", "r", "ya-gui-headless-host-test"],
        output_dir / "headless-host.log",
        require_clean_gui_log=True,
    )
    run(
        ["xmake", "r", "ya-gui-minimal-host", "--exit-after-frame=30"],
        output_dir / "minimal-host.log",
        require_clean_gui_log=True,
    )


def run_scrollsplit_snapshot_parity(output_dir: Path) -> None:
    windowed_json = output_dir / "scrollsplit-windowed.json"
    headless_json = output_dir / "scrollsplit-headless.json"
    common_args = [
        "--start-page=ScrollSplit",
        "--dump-frame=1",
        "--exit-after-frame=3",
    ]
    run(
        [
            "xmake",
            "r",
            "GUIWorkbench",
            *common_args,
            f"--dump-snapshot-json={windowed_json}",
        ],
        output_dir / "windowed-snapshot.log",
        require_clean_gui_log=True,
    )
    run(
        [
            "xmake",
            "r",
            "GUIWorkbench",
            "--headless",
            *common_args,
            f"--dump-snapshot-json={headless_json}",
        ],
        output_dir / "headless-snapshot.log",
        require_clean_gui_log=True,
    )
    if sha256(windowed_json) != sha256(headless_json):
        raise RuntimeError("windowed/headless ScrollSplit snapshot JSON differs")


def run_workbench_page_matrix(output_dir: Path) -> None:
    page_matrix_dir = output_dir / "page-matrix"
    baseline_root = page_matrix_dir / "baseline"
    verify_root = page_matrix_dir / "verify"
    baseline_root.mkdir(parents=True, exist_ok=True)
    verify_root.mkdir(parents=True, exist_ok=True)

    for slug, page, scenario in WORKBENCH_PAGE_MATRIX:
        baseline_dir = baseline_root / slug
        verify_dir = verify_root / slug
        baseline_dir.mkdir(parents=True, exist_ok=True)
        verify_dir.mkdir(parents=True, exist_ok=True)

        baseline_bmp = baseline_dir / f"{slug}.bmp"
        verify_bmp = verify_dir / f"{slug}.bmp"
        verify_diff = verify_dir / f"{slug}-diff.bmp"
        scenario_arg = repo_relative(scenario)

        run(
            [
                "xmake",
                "r",
                "GUIWorkbench",
                f"--start-page={page}",
                f"--scenario={scenario_arg}",
                f"--scenario-dump-dir={baseline_dir}",
                f"--scenario-capture={baseline_bmp}",
            ],
            output_dir / f"page-matrix-{slug}-baseline.log",
            require_clean_gui_log=True,
        )
        run(
            [
                "xmake",
                "r",
                "GUIWorkbench",
                f"--start-page={page}",
                f"--scenario={scenario_arg}",
                f"--scenario-dump-dir={verify_dir}",
                f"--scenario-capture={verify_bmp}",
                f"--scenario-golden={baseline_bmp}",
                f"--scenario-diff={verify_diff}",
            ],
            output_dir / f"page-matrix-{slug}-verify.log",
            require_clean_gui_log=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "build/gui-convergence-diagnostics/macos-validation",
        help="Directory for logs, dumps, captures and the macOS-local baselines.",
    )
    args = parser.parse_args()

    if sys.platform != "darwin":
        parser.error(f"requires macOS/MoltenVK; current platform is {sys.platform}")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    run([sys.executable, "Script/ya.py", "cfg"], output_dir / "cfg.log")
    run_gui_cross_path_smoke(output_dir)
    run_scrollsplit_snapshot_parity(output_dir)
    run_workbench_page_matrix(output_dir)

    print(f"PASS: macOS GUI convergence validation artifacts: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
