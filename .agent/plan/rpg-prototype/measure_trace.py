#!/usr/bin/env python3
"""Summarize a speedscope trace for the R4 scale measurement.

    python3 .agent/plan/rpg-prototype/measure_trace.py <trace.json> [--skip N] [--top N] [--tree]

Two views, because they answer different questions:

* flat  -- self time per scope, summed over the sampled frames and divided by
           the frame count. Self time does not double count parents, so this is
           the honest "where does the frame's CPU go" table.
* tree  -- the inclusive breakdown of the single hottest frame, so the nesting
           (tick -> logic/render -> stage) is readable.

Frame stats (mean / p50 / p95 / max of the root scope duration) come from the
same pass; `--skip N` drops the first N frames (startup, shader warm-up).
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys


class Node:
    __slots__ = ("frame", "start", "end", "children", "self_us")

    def __init__(self, frame: int, start: float) -> None:
        self.frame = frame
        self.start = start
        self.end = start
        self.children: list[Node] = []
        self.self_us = 0.0


_SHORT_RE = re.compile(r"^(.*?):\d+ \(.*?(?:::|[\w>~])?\s*([\w:~<>]+)\s*\(")


def short_name(name: str, max_len: int = 88) -> str:
    match = _SHORT_RE.match(name)
    base = match.group(2) if match else name
    base = base.replace("__cdecl ", "").strip()
    return base if len(base) <= max_len else base[: max_len - 3] + "..."


def load_roots(path: str) -> tuple[list[Node], list[dict]]:
    with open(path, encoding="utf-8") as handle:
        trace = json.load(handle)
    profiles = [p for p in trace.get("profiles", []) if p.get("type") == "evented"]
    if not profiles:
        sys.exit("no evented profile in {}".format(path))
    profile = max(profiles, key=lambda p: len(p.get("events", [])))
    roots: list[Node] = []
    stack: list[Node] = []
    for event in profile["events"]:
        if event["type"] == "O":
            node = Node(event["frame"], event["at"])
            (stack[-1].children if stack else roots).append(node)
            stack.append(node)
        elif event["type"] == "C" and stack:
            stack.pop().end = event["at"]
    for node in roots:
        accumulate(node)
    return roots, trace["shared"]["frames"]


def accumulate(node: Node) -> float:
    total = node.end - node.start
    children_total = 0.0
    for child in node.children:
        children_total += accumulate(child)
    node.self_us = max(0.0, total - children_total)
    return total


def render_tree(node: Node, names: list[dict], out: list[str], indent: int = 0, depth: int = 4) -> None:
    total = (node.end - node.start) / 1000.0
    out.append("{indent}{name}  {total:.3f}ms (self {self:.3f}ms)".format(
        indent="  " * indent, name=short_name(names[node.frame]["name"]), total=total,
        self=node.self_us / 1000.0))
    if indent >= depth:
        return
    for child in sorted(node.children, key=lambda n: n.end - n.start, reverse=True):
        render_tree(child, names, out, indent + 1, depth)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--skip", type=int, default=0, help="drop the first N frames")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--tree", action="store_true", help="also print the hottest frame tree")
    parser.add_argument("--depth", type=int, default=4)
    parser.add_argument("--root-substr", default="iterate",
                        help="only roots whose name contains this are frames (the app loop "
                             "records the event pump as a sibling root, not a child)")
    args = parser.parse_args()

    roots, names = load_roots(args.trace)
    roots.sort(key=lambda n: n.start)
    frames = [r for r in roots if args.root_substr in names[r.frame]["name"]]
    others: dict[str, tuple[int, float]] = {}
    for root in roots:
        if root in frames:
            continue
        key = short_name(names[root.frame]["name"])
        count, total = others.get(key, (0, 0.0))
        others[key] = (count + 1, total + (root.end - root.start) / 1000.0)

    sample = frames[args.skip:]
    if not sample:
        sys.exit("no frames after skipping {} (matched {})".format(args.skip, len(frames)))

    durations_ms = sorted((n.end - n.start) / 1000.0 for n in sample)
    count = len(durations_ms)
    print("frames matching '{}': {} (skipped {}), per-frame root duration:".format(
        args.root_substr, count, args.skip))
    print("  mean {:.3f}ms  p50 {:.3f}ms  p95 {:.3f}ms  max {:.3f}ms  (mean -> {:.0f} fps)".format(
        statistics.fmean(durations_ms), statistics.median(durations_ms),
        durations_ms[min(count - 1, int(0.95 * count))], durations_ms[-1],
        1000.0 / statistics.fmean(durations_ms)))
    if others:
        print("  sibling roots outside the frame (not counted above):")
        for key, (times, total) in sorted(others.items(), key=lambda kv: kv[1][1], reverse=True)[:5]:
            print("    {:>8.3f}ms total over {} call(s)  {}".format(total, times, key))

    self_by_scope: dict[str, float] = {}
    total_by_scope: dict[str, float] = {}
    stack: list[Node] = list(sample)
    while stack:
        node = stack.pop()
        key = short_name(names[node.frame]["name"])
        self_by_scope[key] = self_by_scope.get(key, 0.0) + node.self_us / 1000.0
        total_by_scope[key] = total_by_scope.get(key, 0.0) + (node.end - node.start) / 1000.0
        stack.extend(node.children)

    print("\nself ms/frame by scope (top {}):".format(args.top))
    for key, value in sorted(self_by_scope.items(), key=lambda kv: kv[1], reverse=True)[: args.top]:
        print("  {:>8.3f}  {}".format(value / count, key))

    print("\ninclusive ms/frame by scope (top {}, parents include children):".format(args.top))
    for key, value in sorted(total_by_scope.items(), key=lambda kv: kv[1], reverse=True)[: args.top]:
        print("  {:>8.3f}  {}".format(value / count, key))

    if args.tree:
        hottest = max(sample, key=lambda n: n.end - n.start)
        lines: list[str] = []
        render_tree(hottest, names, lines, 0, args.depth)
        print("\nhottest frame ({:.3f}ms):".format((hottest.end - hottest.start) / 1000.0))
        for line in lines:
            print("  " + line)


if __name__ == "__main__":
    main()
