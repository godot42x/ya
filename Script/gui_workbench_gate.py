#!/usr/bin/env python3
"""One GUIWorkbench gate.

Headless replay of every ``Example/GUIWorkbench/Scenarios/*.jsonl`` (the page
and ``--scenario-render`` come from the file's first comment), then the
committed structural/semantic digest baselines, then a same-frame windowed
GPU / offscreen compare.

The digest JSON under ``Example/GUIWorkbench/Baselines/`` is the cross-platform
gate. Pixel goldens are not compared here: MoltenVK and native Vulkan pixels
are not required to match, and a captured set belongs under
``Baselines/pixels/<platform>/`` rather than next to the digests.

Usage:
    python3 Script/gui_workbench_gate.py
    python3 Script/gui_workbench_gate.py --skip-build --skip-gpu
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCENARIO_DIR = ROOT / "Example" / "GUIWorkbench" / "Scenarios"
LOG_DIR = ROOT / "Engine" / "Saved" / "Automation" / "gui-workbench-gate"
sys.path.insert(0, str(ROOT / "Script" / "automation" / "gui"))
from workbench_bin import find_workbench  # noqa: E402

PAGE_RE = re.compile(r"--start-page=([A-Za-z0-9_]+)")


def scenario_launch(path: Path) -> tuple[str, bool]:
    first = path.read_text(encoding="utf-8").splitlines()[0]
    if not first.startswith("#"):
        raise RuntimeError(f"{path.name}: first line must comment --start-page=<Page>")
    match = PAGE_RE.search(first)
    if match is None:
        raise RuntimeError(f"{path.name}: first comment has no --start-page=")
    return match.group(1), "--scenario-render" in first


def tail(path: Path, lines: int = 40) -> str:
    if not path.is_file():
        return ""
    return "".join(path.read_text(encoding="utf-8", errors="replace").splitlines(True)[-lines:])


def run_logged(cmd: list[str], log_path: Path, timeout_s: float) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout_s,
            text=True,
        )
    except subprocess.TimeoutExpired as exc:
        log_path.write_text(exc.stdout or "", encoding="utf-8")
        print(f"[FAIL]    timed out after {timeout_s:.0f}s: {' '.join(cmd)}", file=sys.stderr)
        return 124
    log_path.write_text(proc.stdout or "", encoding="utf-8")
    return proc.returncode


def run_scenarios(workbench: str) -> list[str]:
    failed: list[str] = []
    scenarios = sorted(SCENARIO_DIR.glob("*.jsonl"))
    if not scenarios:
        raise RuntimeError(f"no scenarios in {SCENARIO_DIR}")
    for path in scenarios:
        page, render = scenario_launch(path)
        cmd = [
            workbench,
            "--headless",
            f"--start-page={page}",
            f"--scenario={path.relative_to(ROOT)}",
        ]
        if render:
            cmd.append("--scenario-render")
        log_path = LOG_DIR / f"{path.stem}.log"
        code = run_logged(cmd, log_path, 180.0)
        if code == 0:
            print(f"[pass]    {path.name}  page={page}")
        else:
            failed.append(path.name)
            print(f"[FAIL]    {path.name}  exit={code}  page={page}", file=sys.stderr)
            print(tail(log_path), file=sys.stderr)
    return failed


def run_script(script: Path, extra: list[str]) -> int:
    cmd = [sys.executable, str(script), *extra]
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--skip-gpu",
        action="store_true",
        help="Skip the windowed GPU / offscreen compare (headless assertions and digests still run)",
    )
    args = parser.parse_args()

    if not args.skip_build:
        built = subprocess.run(["xmake", "b", "GUIWorkbench"], cwd=ROOT)
        if built.returncode != 0:
            return built.returncode

    try:
        workbench = find_workbench(str(ROOT))
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 2

    failed = run_scenarios(workbench)
    digest = run_script(ROOT / "Script" / "gui_style_baseline.py", [])
    gpu = 0
    if not args.skip_gpu:
        gpu = run_script(
            ROOT / "Script" / "automation" / "gui" / "run_workbench_gpu_parity.py",
            ["--skip-build"],
        )

    print(
        f"\nworkbench gate: scenarios={len(list(SCENARIO_DIR.glob('*.jsonl')))} "
        f"failed={len(failed)} digest={'PASS' if digest == 0 else 'FAIL'} "
        f"gpu={'skipped' if args.skip_gpu else 'PASS' if gpu == 0 else 'FAIL'}"
    )
    if failed:
        print("failed scenarios: " + ", ".join(failed))
    if failed or digest != 0 or gpu != 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
