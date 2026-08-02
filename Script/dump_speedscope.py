#!/usr/bin/env python3
"""Sample N frames from a speedscope evented trace and dump them as text.

Usage:
    python3 Script/dump_speedscope.py <trace.json> [-n N] [--start K] [--short]
"""

import argparse
import json
import re
import sys


class Node:
    __slots__ = ("frame", "start", "end", "children", "self_us")

    def __init__(self, frame, start):
        self.frame = frame
        self.start = start
        self.end = start
        self.children = []
        self.self_us = 0


def load_trace(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def pick_profile(trace):
    profiles = trace.get("profiles", [])
    if not profiles:
        sys.exit("no profiles found")
    evented = [p for p in profiles if p.get("type") == "evented"]
    if not evented:
        sys.exit("only 'evented' traces supported (found: {})".format(
            ",".join({p.get("type") for p in profiles})))
    return max(evented, key=lambda p: len(p.get("events", [])))


def build_trees(profile):
    events = profile["events"]
    roots = []
    stack = []
    for e in events:
        if e["type"] == "O":
            node = Node(e["frame"], e["at"])
            if stack:
                stack[-1].children.append(node)
            else:
                roots.append(node)
            stack.append(node)
        elif e["type"] == "C" and stack:
            node = stack.pop()
            node.end = e["at"]
    return roots


def accumulate(node):
    total = node.end - node.start
    children_total = 0
    for c in node.children:
        accumulate(c)
        children_total += c.end - c.start
    node.self_us = max(0, total - children_total)
    return total


def merge_duplicates(node):
    """Merge sibling nodes with the same frame into one (self/total summed)."""
    if not node.children:
        return
    for c in node.children:
        merge_duplicates(c)
    merged = []
    for c in node.children:
        if merged and merged[-1].frame == c.frame:
            dur = c.end - c.start
            merged[-1].end += dur
            merged[-1].self_us += c.self_us
        else:
            merged.append(c)
    node.children = merged


_SHORT_RE = re.compile(r"^(.*?):\d+ \(.*?(?:::|[\w>~])?\s*([\w:~<>]+)\s*\(")


def short_name(name, max_len=72):
    m = _SHORT_RE.match(name)
    base = m.group(2) if m else name
    base = base.replace("__cdecl ", "").strip()
    if len(base) > max_len:
        base = base[: max_len - 3] + "..."
    return base


def render_frame(root, frames, out, indent=0, max_depth=None, short=True):
    total = root.end - root.start
    name = short_name(frames[root.frame]["name"]) if short else frames[root.frame]["name"]
    pct = 100.0 * root.self_us / total if total > 0 else 0.0
    out.append(
        "{indent}{name}  self={self:.3f}ms total={total:.3f}ms ({pct:.0f}% self)".format(
            indent="  " * indent,
            name=name,
            self=root.self_us / 1000.0,
            total=total / 1000.0,
            pct=pct,
        )
    )
    if max_depth is not None and indent >= max_depth:
        return
    for c in sorted(root.children, key=lambda n: n.end - n.start, reverse=True):
        render_frame(c, frames, out, indent + 1, max_depth, short)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", help="speedscope json path")
    ap.add_argument("-n", "--frames", type=int, default=3, help="number of frames to sample")
    ap.add_argument("--start", type=int, default=0, help="skip first K frames (e.g. startup)")
    ap.add_argument("--depth", type=int, default=None, help="limit tree depth")
    ap.add_argument("--merge", action="store_true", help="merge repeated sibling calls")
    ap.add_argument("--long", action="store_true", help="keep full mangled names")
    args = ap.parse_args()

    trace = load_trace(args.trace)
    profile = pick_profile(trace)
    frames = trace["shared"]["frames"]
    roots = build_trees(profile)

    for r in roots:
        accumulate(r)
    roots.sort(key=lambda n: n.end - n.start, reverse=True)

    pool = roots[args.start:]
    if not pool:
        sys.exit("no frames after skipping {} (total {})".format(args.start, len(roots)))
    total_frames = len(pool)
    step = max(1, total_frames // args.frames)
    sample = pool[::step][: args.frames]

    out = []
    out.append("# Profile: {}".format(profile.get("name", "")))
    out.append("# Total frames: {} | sampled {} (step {}) | skipped {} startup frames".format(
        total_frames, len(sample), step, args.start))
    out.append("")

    for i, root in enumerate(sample):
        if args.merge:
            merge_duplicates(root)
        dur_ms = (root.end - root.start) / 1000.0
        out.append("## Frame {}/{}  dur={:.3f} ms".format(i + 1, len(sample), dur_ms))
        render_frame(root, frames, out, max_depth=args.depth, short=not args.long)
        out.append("")

    print("\n".join(out))


if __name__ == "__main__":
    main()
