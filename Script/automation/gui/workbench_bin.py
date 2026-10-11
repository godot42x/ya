"""Locate the GUIWorkbench binary without assuming a platform build path."""

from __future__ import annotations

import os


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
