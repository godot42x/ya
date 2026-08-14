#!/usr/bin/env python3
"""Run the macOS/MoltenVK gate for GUI architecture convergence.

This is intentionally a small local-runner harness, not a second build
system. It creates a macOS-local ScrollSplit baseline, verifies it with a
zero-tolerance diff, checks windowed/headless snapshot identity, and runs the
GUI closure test.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SCENARIO = ROOT / "Example/GUIWorkbench/Scenarios/resize_scrollsplit_interaction_stress.jsonl"
DIAGNOSTIC_PATTERN = re.compile(r"VUID|\[ERROR\]|ASSERT", re.IGNORECASE)


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


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "build/gui-convergence-diagnostics/macos-validation",
        help="Directory for logs, dumps, captures and the macOS-local baseline.",
    )
    args = parser.parse_args()

    if sys.platform != "darwin":
        parser.error(f"requires macOS/MoltenVK; current platform is {sys.platform}")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    baseline_dir = output_dir / "baseline"
    verify_dir = output_dir / "verify"
    baseline_dir.mkdir(exist_ok=True)
    verify_dir.mkdir(exist_ok=True)

    run([sys.executable, "Script/ya.py", "cfg"], output_dir / "cfg.log")
    run(["xmake", "b", "GUIWorkbench"], output_dir / "build-workbench.log")
    run(["xmake", "b", "ya-gui-closure-test"], output_dir / "build-closure.log")
    run(["xmake", "r", "ya-gui-closure-test"], output_dir / "closure.log")

    windowed_json = output_dir / "scrollsplit-windowed.json"
    headless_json = output_dir / "scrollsplit-headless.json"
    run(
        [
            "xmake",
            "r",
            "GUIWorkbench",
            "--start-page=ScrollSplit",
            f"--dump-snapshot-json={windowed_json}",
            "--dump-frame=1",
            "--exit-after-frame=3",
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
            "--start-page=ScrollSplit",
            f"--dump-snapshot-json={headless_json}",
            "--exit-after-frame=3",
        ],
        output_dir / "headless-snapshot.log",
    )
    if sha256(windowed_json) != sha256(headless_json):
        raise RuntimeError("windowed/headless ScrollSplit snapshot JSON differs")

    baseline_bmp = baseline_dir / "scrollsplit.bmp"
    run(
        [
            "xmake",
            "r",
            "GUIWorkbench",
            "--start-page=ScrollSplit",
            f"--scenario={SCENARIO.relative_to(ROOT)}",
            f"--scenario-dump-dir={baseline_dir}",
            f"--scenario-capture={baseline_bmp}",
        ],
        output_dir / "baseline.log",
        require_clean_gui_log=True,
    )

    verify_bmp = verify_dir / "scrollsplit.bmp"
    verify_diff = verify_dir / "scrollsplit-diff.bmp"
    run(
        [
            "xmake",
            "r",
            "GUIWorkbench",
            "--start-page=ScrollSplit",
            f"--scenario={SCENARIO.relative_to(ROOT)}",
            f"--scenario-dump-dir={verify_dir}",
            f"--scenario-capture={verify_bmp}",
            f"--scenario-golden={baseline_bmp}",
            f"--scenario-diff={verify_diff}",
        ],
        output_dir / "verify.log",
        require_clean_gui_log=True,
    )

    print(f"PASS: macOS GUI convergence validation artifacts: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
