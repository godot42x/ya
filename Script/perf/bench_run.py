"""Run a set of benchmark configs through ya-runtime in one command.

One invocation produces one directory tree of `bench_summary.json` files, which
is what `bench_compare.py` diffs. The layout is

    <out>/<scene>/run<N>/bench_summary.json
    <out>/index.json

so a stability check (`--repeat 3`) and a before/after comparison read the same
shape.

The runtime is driven directly rather than through `xmake r`, because `xmake r`
would add its own argument handling and the run's own timeout is what bounds a
hung frame loop. The Vulkan SDK library path is the same one `Script/ya.py`
sets up, so the binary links the way it does under `ya.py run`.

Usage
-----
    python3 Script/perf/bench_run.py \
        --project Example/PerfLab/PerfLab.yaproject \
        --bench-dir Example/PerfLab/Benchmark \
        --out .agent/plan/perf-baseline/baselines/macos/<label> \
        --repeat 3
"""
from __future__ import annotations

import argparse
import json
import platform
import re
import subprocess
import sys
import time
from fnmatch import fnmatch
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
WORKSPACE_ROOT = SCRIPT_DIR.parent.parent
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def build_env() -> dict[str, str]:
    env = dict(__import__("os").environ)
    if platform.system() == "Darwin":
        sdk_lib_dirs = sorted(
            {
                str(path)
                for path in (WORKSPACE_ROOT / "Engine" / "ThirdParty" / "VulkanSDK").glob("*/macOS/lib")
                if path.is_dir()
            }
        )
        if sdk_lib_dirs:
            existing = env.get("DYLD_LIBRARY_PATH", "")
            merged = sdk_lib_dirs + ([existing] if existing else [])
            env["DYLD_LIBRARY_PATH"] = ":".join(merged)
    return env


def runtime_binary() -> Path:
    result = subprocess.run(
        ["xmake", "show", "-t", "ya-runtime"],
        cwd=WORKSPACE_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    match = re.search(r"targetfile:\s+([^\n]+)", ANSI_ESCAPE_RE.sub("", result.stdout))
    if not match:
        raise SystemExit("cannot resolve the ya-runtime binary from `xmake show -t ya-runtime`")
    binary = (WORKSPACE_ROOT / match.group(1).strip()).resolve()
    if not binary.is_file():
        raise SystemExit(f"ya-runtime is not built at {binary}; run `python3 Script/ya.py build` first")
    return binary


def scene_name(config: Path) -> str:
    try:
        data = json.loads(config.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return config.stem
    name = data.get("name")
    return str(name) if isinstance(name, str) and name else config.stem


def collect_configs(args: argparse.Namespace) -> list[Path]:
    configs: list[Path] = []
    for raw in args.bench or []:
        path = Path(raw)
        if not path.is_absolute():
            path = WORKSPACE_ROOT / path
        if not path.is_file():
            raise SystemExit(f"bench config not found: {path}")
        configs.append(path.resolve())
    if args.bench_dir:
        bench_dir = Path(args.bench_dir)
        if not bench_dir.is_absolute():
            bench_dir = WORKSPACE_ROOT / bench_dir
        if not bench_dir.is_dir():
            raise SystemExit(f"bench dir not found: {bench_dir}")
        configs.extend(sorted(bench_dir.glob("*.bench.json")))
    if args.filter:
        # fnmatch on the scene name, so `Lights_50` selects exactly that scene
        # and `Lights*` selects the family. A plain substring would pull
        # `Lights_500` in with `Lights_50`, which is a silent extra hour of
        # benchmarking. The name is the file name without its `.bench.json`
        # suffix -- matching the raw stem would make the exact form match
        # nothing at all.
        configs = [c for c in configs if fnmatch(scene_name(c), args.filter)]
    # De-duplicate while keeping order: --bench and --bench-dir may overlap.
    seen: set[Path] = set()
    unique: list[Path] = []
    for config in configs:
        if config not in seen:
            seen.add(config)
            unique.append(config)
    if not unique:
        raise SystemExit("no bench configs selected")
    return unique


def rel_to_root(path: Path, root: Path) -> str:
    """Path relative to the index that records it, so the run tree can be moved
    or committed without rewriting it. Paths outside the root (a bench config in
    the workspace while the suite was written to /tmp) stay absolute: a `../..`
    chain that walks out of the tree would resolve against whatever directory the
    reader happens to run from."""
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def run_one(binary: Path, project: Path, config: Path, out_dir: Path, timeout_s: float, env: dict[str, str]) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(binary),
        f"--ya-project={project}",
        f"--benchmark={config}",
        f"--benchmark-output={out_dir}",
    ]
    started = time.time()
    try:
        completed = subprocess.run(
            cmd,
            cwd=WORKSPACE_ROOT,
            env=env,
            timeout=timeout_s,
            text=True,
            capture_output=True,
        )
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": f"timed out after {timeout_s:.0f}s"}
    summary_path = out_dir / "bench_summary.json"
    if completed.returncode != 0 or not summary_path.is_file():
        tail = "\n".join(completed.stderr.strip().splitlines()[-15:])
        return {"ok": False, "error": f"exit {completed.returncode}; no summary\n{tail}"}
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        return {"ok": False, "error": f"unreadable summary: {error}"}
    meta = summary.get("meta", {})
    return {
        "ok": True,
        "seconds": round(time.time() - started, 2),
        # `meta` is flat: `buildBenchSummaryJson` merges the host facts into it
        # rather than nesting them under `host`, so a `meta.host.gitCommit`
        # lookup returns nothing and the index silently records an empty commit.
        "commit": meta.get("gitCommit", ""),
        "dirty": bool(meta.get("gitDirty", False)),
        "frameP50": summary.get("frame", {}).get("frameMs", {}).get("p50"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a set of benchmark configs through ya-runtime.")
    parser.add_argument("--project", required=True, help="Path to the .yaproject the bench configs belong to.")
    parser.add_argument("--bench", action="append", default=[], help="A single bench config (repeatable).")
    parser.add_argument("--bench-dir", help="Directory of *.bench.json configs.")
    parser.add_argument("--filter", help="Glob on the config stem, e.g. `Lights_50` or `Lights*`.")
    parser.add_argument("--out", required=True, help="Output root for the summary tree.")
    parser.add_argument("--repeat", type=int, default=1, help="Runs per config (stability check).")
    parser.add_argument("--timeout", type=float, default=900.0, help="Per-run timeout in seconds.")
    parser.add_argument("--no-build", action="store_true", help="Skip `xmake b ya-runtime`.")
    args = parser.parse_args()

    project = Path(args.project)
    if not project.is_absolute():
        project = WORKSPACE_ROOT / project
    project = project.resolve()
    if not project.is_file():
        raise SystemExit(f"project not found: {project}")

    out_root = Path(args.out)
    if not out_root.is_absolute():
        out_root = WORKSPACE_ROOT / out_root
    out_root = out_root.resolve()

    configs = collect_configs(args)
    env = build_env()

    if not args.no_build:
        # The project's own module is a separate target (examples ship their
        # gameplay code as a dylib the runtime loads), so `ya-runtime` alone is
        # not enough -- same target list `ya.py run` builds.
        targets = subprocess.run(
            [sys.executable, str(WORKSPACE_ROOT / "Script" / "ya_bundle_tool.py"), "build-targets", "--project", str(project)],
            cwd=WORKSPACE_ROOT,
            check=True,
            text=True,
            capture_output=True,
            env=env,
        ).stdout
        target_list = [line.strip() for line in targets.splitlines() if line.strip()] or ["ya-runtime"]
        for target in target_list:
            print(f"-- building {target}")
            subprocess.run(["xmake", "b", target], cwd=WORKSPACE_ROOT, check=True, env=env)
    binary = runtime_binary()

    index: list[dict] = []
    failures: list[str] = []
    for config in configs:
        scene = scene_name(config)
        for run in range(1, args.repeat + 1):
            run_dir = out_root / scene / f"run{run}"
            print(f"-- {scene} run{run}/{args.repeat}")
            record = run_one(binary, project, config, run_dir, args.timeout, env)
            record.update(
                {
                    "scene": scene,
                    "run": run,
                    # Relative to the index, which is what makes the tree
                    # movable: an index that spells out `/tmp/...` stops
                    # resolving the moment the suite is copied into the repo
                    # (which is where a committed baseline ends up).
                    "config": rel_to_root(config, out_root),
                    "path": rel_to_root(run_dir / "bench_summary.json", out_root),
                }
            )
            index.append(record)
            if record["ok"]:
                print(f"   ok  {record['seconds']}s  frame p50 {record['frameP50']}")
            else:
                failures.append(f"{scene} run{run}: {record['error']}")
                print(f"   FAIL {record['error'].splitlines()[0]}")

    out_root.mkdir(parents=True, exist_ok=True)
    index_path = out_root / "index.json"
    # A suite is often run in batches (`--filter Lights*` then `--filter
    # Meshes*`), so the index accumulates instead of being overwritten: a lost
    # index silently turns a partial suite into a complete-looking one.
    runs = list(index)
    if index_path.is_file():
        try:
            runs = json.loads(index_path.read_text(encoding="utf-8")).get("runs", []) + runs
        except json.JSONDecodeError:
            print(f"   warn: {index_path} is unreadable; rewriting it with this run only")
    index_path.write_text(
        json.dumps({"project": str(project), "repeat": args.repeat, "runs": runs}, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"-- wrote {index_path} ({len(runs)} runs)")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
