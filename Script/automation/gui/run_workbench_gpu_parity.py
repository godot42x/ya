#!/usr/bin/env python3

"""Phase 9E GUIWorkbench release gate: route-trace scenario, snapshot digest, GPU/offscreen parity."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys


PARITY_RE = re.compile(
    r"offscreen parity diff: pass=(true|false) differing=(\d+) ratio=([0-9.]+)"
)


def tail(path: str, lines: int = 80) -> str:
    if not os.path.exists(path):
        return ""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return "".join(handle.readlines()[-lines:])


def find_workbench(workspace: str) -> str:
    build_root = os.path.join(workspace, "build")
    candidates: list[str] = []
    for root, _dirs, files in os.walk(build_root):
        for name in ("GUIWorkbench", "GUIWorkbench.exe"):
            if name in files:
                candidates.append(os.path.join(root, name))
    if not candidates:
        raise RuntimeError("GUIWorkbench binary not found under build/")
    def rank(path: str) -> tuple[int, float]:
        parts = path.split(os.sep)
        mode_rank = 0 if "debug" in parts else 1
        return (mode_rank, -os.path.getmtime(path))
    candidates.sort(key=rank)
    return candidates[0]


def run_logged(cmd: list[str], cwd: str, log_path: str, timeout_s: float) -> int:
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "w", encoding="utf-8") as log_handle:
        try:
            proc = subprocess.run(
                cmd,
                cwd=cwd,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
                timeout=timeout_s,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"timed out after {timeout_s:.0f}s: {' '.join(cmd)}") from exc
    return proc.returncode


def require_file(path: str, what: str) -> None:
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        raise RuntimeError(f"{what} missing or empty: {path}")


def assert_snapshot_digest(path: str) -> None:
    require_file(path, "snapshot JSON")
    with open(path, "r", encoding="utf-8") as handle:
        dump = json.load(handle)
    if "structuralDigest" not in dump or "semanticDigest" not in dump:
        raise RuntimeError(f"snapshot JSON missing digest keys: {path}")
    if not dump.get("items"):
        raise RuntimeError(f"snapshot JSON has no draw items: {path}")


def assert_route_checkpoint(path: str, target: str) -> None:
    require_file(path, "route checkpoint")
    with open(path, "r", encoding="utf-8") as handle:
        dump = json.load(handle)
    route = dump.get("lastRoute") or {}
    if route.get("target") != target:
        raise RuntimeError(f"{path}: lastRoute.target={route.get('target')!r}, expected {target!r}")
    if not route.get("policyName"):
        raise RuntimeError(f"{path}: lastRoute.policyName missing")


def parse_parity(log_path: str) -> None:
    text = ""
    if os.path.exists(log_path):
        with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    matches = PARITY_RE.findall(text)
    if not matches:
        raise RuntimeError("offscreen parity log line not found")
    passed, differing, _ratio = matches[-1]
    if passed != "true" or differing != "0":
        raise RuntimeError(f"offscreen parity failed: pass={passed} differing={differing}")


def main() -> int:
    parser = argparse.ArgumentParser(description="GUIWorkbench GPU/offscreen + route-trace gate")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-gpu", action="store_true", help="Only run headless digest/route gates")
    parser.add_argument("--shot-frame", type=int, default=20)
    parser.add_argument("--exit-after-frame", type=int, default=40)
    parser.add_argument(
        "--out-dir",
        default="Engine/Saved/Automation/gui-gpu-parity",
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))
    out_dir = os.path.join(workspace, args.out_dir)
    os.makedirs(out_dir, exist_ok=True)

    xmake = ["xmake"]
    if not args.skip_build:
        built = subprocess.run(["xmake", "b", "GUIWorkbench"], cwd=workspace)
        if built.returncode != 0:
            return built.returncode
    workbench = find_workbench(workspace)

    route_dir = os.path.join(out_dir, "route")
    os.makedirs(route_dir, exist_ok=True)
    headless_json = os.path.join(out_dir, "headless-snapshot.json")
    headless_log = os.path.join(out_dir, "headless-route.log")
    headless = run_logged(
        [
            workbench,
            "--headless",
            "--start-page",
            "Widgets",
            "--scenario",
            "Example/GUIWorkbench/Scenarios/widgets_interaction.jsonl",
            "--scenario-dump-dir",
            route_dir,
            "--dump-snapshot-json",
            headless_json,
        ],
        workspace,
        headless_log,
        60.0,
    )
    if headless != 0:
        print(tail(headless_log), file=sys.stderr)
        raise RuntimeError(f"headless widgets scenario exited {headless}")
    assert_route_checkpoint(os.path.join(route_dir, "counter_clicked.json"), "Counter")
    assert_route_checkpoint(os.path.join(route_dir, "notes_committed.json"), "NotesField")
    assert_snapshot_digest(headless_json)

    if args.skip_gpu:
        return 0

    gpu_bmp = os.path.join(out_dir, "gpu.bmp")
    offscreen_bmp = os.path.join(out_dir, "offscreen.bmp")
    diff_bmp = os.path.join(out_dir, "diff.bmp")
    windowed_json = os.path.join(out_dir, "windowed-snapshot.json")
    gpu_log = os.path.join(out_dir, "gpu-parity.log")
    gpu = run_logged(
        [
            workbench,
            "--start-page",
            "Widgets",
            f"--exit-after-frame={args.exit_after_frame}",
            "--gpu-shot",
            gpu_bmp,
            f"--gpu-shot-frame={args.shot_frame}",
            "--offscreen-shot",
            offscreen_bmp,
            f"--offscreen-shot-frame={args.shot_frame}",
            "--offscreen-diff",
            diff_bmp,
            "--dump-snapshot-json",
            windowed_json,
            f"--dump-frame={args.shot_frame}",
        ],
        workspace,
        gpu_log,
        180.0,
    )
    if gpu != 0:
        print(tail(gpu_log), file=sys.stderr)
        raise RuntimeError(f"GUIWorkbench GPU parity exited {gpu}")
    parse_parity(gpu_log)
    require_file(gpu_bmp, "GPU shot")
    require_file(offscreen_bmp, "offscreen shot")
    require_file(diff_bmp, "parity diff")
    assert_snapshot_digest(windowed_json)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise
