"""
Cross-platform helpers for the user-level shared cache.

Big per-user artifacts (Vulkan SDK, heavy submodule checkouts) live in ONE
shared location per user, and every checkout links to it, so parallel agents
/ multiple worktrees don't each download and store their own copy.

Cache root resolution (first match wins):
1. $YA_CACHE_ROOT                     -- explicit override (e.g. a CI cache dir)
2. macOS:   ~/Library/Caches/ya-engine
3. Windows: %LOCALAPPDATA%/ya-engine/Cache
4. Linux:   $XDG_CACHE_HOME/ya-engine, else ~/.cache/ya-engine

Links: symlink on macOS/Linux; directory junction on Windows (works without
admin rights).
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def cache_root() -> Path:
    env_root = os.environ.get("YA_CACHE_ROOT")
    if env_root:
        return Path(env_root).expanduser().resolve()
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "ya-engine"
    if sys.platform == "win32":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return Path(local_app_data) / "ya-engine" / "Cache"
    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    if xdg_cache:
        return Path(xdg_cache) / "ya-engine"
    return Path.home() / ".cache" / "ya-engine"


def is_dir_link(path: Path) -> bool:
    """True for symlinks and (on Windows) junctions/reparse points."""
    if path.is_symlink():
        return True
    if sys.platform == "win32":
        try:
            return bool(path.stat(follow_symlinks=False).st_reparse_tag)
        except OSError:
            return False
    return False


def make_dir_link(link: Path, target: Path) -> None:
    """Create a directory link at `link` pointing to `target`.

    Replaces an existing link (pointing anywhere); raises FileExistsError if
    a real directory/file occupies `link`. Idempotent when already linked.
    """
    if is_dir_link(link):
        try:
            if link.resolve() == target.resolve():
                return
        except OSError:
            pass
        if sys.platform == "win32":
            subprocess.run(["cmd", "/c", "rmdir", str(link)], check=False)
        else:
            link.unlink()
    elif link.exists():
        raise FileExistsError(f"{link} already exists and is not a link")
    link.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        result = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(target)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise OSError(
                f"mklink /J {link} -> {target} failed: {result.stderr.strip()}"
            )
    else:
        os.symlink(str(target), str(link), target_is_directory=True)
