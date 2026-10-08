"""Diff two benchmark summaries into a markdown table.

Inputs are `bench_summary.json` files (or directories holding / pointing at
them, including the tree `bench_run.py` produces). The table is what goes into
a progress entry: one row per metric, before / after / delta, with anything
past `--threshold` percent marked.

Two things the table deliberately says out loud rather than hiding:

- The two runs' `meta` are compared first. Different commit, build mode, GPU or
  resolution means the numbers are not apples-to-apples, and that is printed at
  the top instead of being folded into the deltas.
- Facts (non-series values such as `gpu_memory.supported` or
  `process_memory.unifiedMemory`) are listed separately; a percentage change
  would be meaningless for them.

Usage
-----
    python3 Script/perf/bench_compare.py --before A --after B --threshold 3
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# The statistics a before/after table is read for. `count` and `min` are
# diagnostics of the sampling, not of the engine, so they are left out unless
# asked for.
DEFAULT_METRICS = ["p50", "p95", "p99", "mean", "max"]

# Sections in the order a reader looks for them. Anything not listed sorts last.
SECTION_ORDER = ["frame", "cpu_stages", "gpu_passes", "render_counters", "gpu_memory", "process_memory"]


def load_summary(path: Path) -> dict:
    """Accept a summary file, a run directory, a scene directory holding
    `run<N>/`, or a run-set root (index.json).

    A scene directory is ambiguous when it holds more than one run, so it
    resolves to the lowest run number and says so -- comparing run2 against
    run3 by accident is the kind of mistake that produces a plausible-looking
    delta.
    """
    if path.is_file() and path.name == "index.json":
        data = json.loads(path.read_text(encoding="utf-8"))
        return load_summary(Path(data["runs"][0]["path"]))
    if path.is_file():
        return json.loads(path.read_text(encoding="utf-8"))
    if (path / "bench_summary.json").is_file():
        return json.loads((path / "bench_summary.json").read_text(encoding="utf-8"))
    if (path / "index.json").is_file():
        return load_summary(path / "index.json")

    def run_sort_key(run_dir: Path) -> tuple[int, str]:
        digits = "".join(c for c in run_dir.name if c.isdigit())
        return (int(digits) if digits else 0, run_dir.name)

    run_dirs = sorted((d for d in path.glob("run*") if d.is_dir()), key=run_sort_key)
    if run_dirs:
        if len(run_dirs) > 1:
            print(
                f"-- {path} holds {len(run_dirs)} runs; using {run_dirs[0].name}. "
                "Pass <scene>/<run> to pick another.",
                file=sys.stderr,
            )
        return load_summary(run_dirs[0])
    raise SystemExit(f"cannot read a benchmark summary from {path}")


def series_keys(section: dict) -> dict:
    """The series of one section: values that carry the window statistics."""
    return {k: v for k, v in section.items() if isinstance(v, dict) and "p50" in v}


def facts(section: dict) -> dict:
    return {k: v for k, v in section.items() if k not in series_keys(section)}


def fmt(value: float | None) -> str:
    if value is None:
        return "-"
    if value == 0:
        return "0"
    if abs(value) >= 100:
        return f"{value:.1f}"
    return f"{value:.3f}"


def meta_value(meta: dict, key: str) -> str:
    """`meta` is flat in the JSON: the C++ host facts are merged into it, and
    the render resolution is an object, not two width/height fields."""
    if key == "resolution":
        size = meta.get("renderResolution") or {}
        return f"{size.get('width')}x{size.get('height')}" if size else ""
    if key == "scene":
        scene = str(meta.get("scene", ""))
        return Path(scene).name if scene else ""
    if key == "dirty":
        return "yes" if meta.get("gitDirty") else "no"
    value = meta.get(key)
    return "" if value is None else str(value)


def meta_lines(before: dict, after: dict) -> list[str]:
    b = before.get("meta", {})
    a = after.get("meta", {})
    # A differing commit / build mode / GPU means the deltas are not a clean
    # before/after; say so rather than letting the reader assume they are.
    fields = [
        ("scene", "scene", False),
        ("commit", "gitCommit", True),
        ("dirty checkout", "dirty", False),
        ("build mode", "buildMode", True),
        ("gpu", "gpu", True),
        ("graphics api", "graphicsApi", False),
        ("resolution", "resolution", True),
        ("present mode", "presentMode", True),
        ("sampled frames", "sampledFrames", False),
    ]
    lines = ["| field | before | after |", "| --- | --- | --- |"]
    notes: list[str] = []
    for label, key, must_match in fields:
        bv = meta_value(b, key)
        av = meta_value(a, key)
        flag = ""
        if must_match and bv and av and bv != av:
            flag = " **DIFFERENT**"
            notes.append(f"- **{label} differs** ({bv} -> {av}): the deltas below are not a clean before/after.")
        lines.append(f"| {label} | {bv or '-'} | {av or '-'}{flag} |")
    return lines + notes


def build_table(before: dict, after: dict, metrics: list[str], threshold: float) -> list[str]:
    sections = sorted(
        set(before.keys()) | set(after.keys()),
        key=lambda s: (SECTION_ORDER.index(s) if s in SECTION_ORDER else len(SECTION_ORDER), s),
    )
    lines = [
        "| section | metric | statistic | before | after | delta | % |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for section in sections:
        if section in ("meta", "schemaVersion"):
            continue
        b_section = before.get(section, {})
        a_section = after.get(section, {})
        keys = sorted(set(series_keys(b_section)) | set(series_keys(a_section)))
        for key in keys:
            b_series = b_section.get(key, {})
            a_series = a_section.get(key, {})
            for metric in metrics:
                bv = b_series.get(metric)
                av = a_series.get(metric)
                if bv is None and av is None:
                    continue
                if bv is None or av is None:
                    lines.append(f"| {section} | {key} | {metric} | {fmt(bv)} | {fmt(av)} | - | n/a (one side missing) |")
                    continue
                delta = av - bv
                pct = (delta / bv * 100.0) if bv else 0.0
                mark = ""
                if threshold > 0 and abs(pct) > threshold:
                    mark = " **▲**" if delta > 0 else " **▼**"
                lines.append(
                    f"| {section} | {key} | {metric} | {fmt(bv)} | {fmt(av)} "
                    f"| {fmt(delta)} | {pct:+.2f}%{mark} |"
                )
    return lines


def fact_lines(before: dict, after: dict) -> list[str]:
    rows: list[str] = []
    for section in sorted(set(before.keys()) | set(after.keys())):
        if section in ("meta", "schemaVersion"):
            continue
        b_facts = facts(before.get(section, {}))
        a_facts = facts(after.get(section, {}))
        for key in sorted(set(b_facts) | set(a_facts)):
            bv = b_facts.get(key)
            av = a_facts.get(key)
            if isinstance(bv, list) or isinstance(av, list):
                bv = json.dumps(bv, separators=(",", ":"))
                av = json.dumps(av, separators=(",", ":"))
            if bv == av:
                rows.append(f"- `{section}.{key}`: {bv} (unchanged)")
            else:
                rows.append(f"- `{section}.{key}`: {bv} -> {av}")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Diff two benchmark summaries into a markdown table.")
    parser.add_argument("--before", required=True, help="Summary file, run dir, or run-set root.")
    parser.add_argument("--after", required=True, help="Summary file, run dir, or run-set root.")
    parser.add_argument("--metrics", default=",".join(DEFAULT_METRICS), help="Comma-separated statistics to compare.")
    parser.add_argument("--threshold", type=float, default=3.0, help="Percent beyond which a row is marked.")
    parser.add_argument("--out", help="Write the markdown here instead of stdout.")
    args = parser.parse_args()

    before = load_summary(Path(args.before))
    after = load_summary(Path(args.after))
    metrics = [m.strip() for m in args.metrics.split(",") if m.strip()]

    lines: list[str] = ["# Benchmark comparison", ""]
    lines.append(f"- before: `{args.before}`")
    lines.append(f"- after: `{args.after}`")
    lines.append("")
    lines.append("## Run metadata")
    lines.append("")
    lines.extend(meta_lines(before, after))
    lines.append("")
    lines.append(f"## Metrics (marked beyond ±{args.threshold:g}%)")
    lines.append("")
    lines.extend(build_table(before, after, metrics, args.threshold))
    lines.append("")
    fact_rows = fact_lines(before, after)
    if fact_rows:
        lines.append("## Facts")
        lines.append("")
        lines.extend(fact_rows)
        lines.append("")

    text = "\n".join(lines)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
        print(f"-- wrote {args.out}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
