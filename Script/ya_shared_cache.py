"""
Cross-platform helpers for the project-local shared cache.

Big artifacts (Vulkan SDK, heavy submodule checkouts) live in ONE location
under the MAIN project (<main project>/.ya-cache), and every checkout links
to it, so parallel agents / multiple worktrees don't each download and store
their own copy. Because the cache lives inside the project, deleting the
project directory removes everything -- nothing is left behind in the user's
system (no ~/Library/Caches, %LOCALAPPDATA% etc. droppings).

Cache root resolution (first match wins):
1. $YA_CACHE_ROOT                     -- explicit override (e.g. a CI cache dir)
2. git config ya.cacheRoot            -- per-checkout override (handy for
                                        standalone clones sharing one project)
3. <main project>/.ya-cache           -- derived from `git rev-parse
                                        --git-common-dir`: git worktrees
                                        resolve to the MAIN project's git dir,
                                        so every worktree shares its cache
                                        automatically
4. <checkout>/.ya-cache               -- fallback for non-git layouts

The previous scheme (per-user system cache under ~/Library/Caches,
%LOCALAPPDATA% or ~/.cache) is migrated into the project-local root
automatically on first use (see migrate_legacy_cache).

Links: symlink on macOS/Linux; directory junction on Windows (works without
admin rights).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent


def _git_common_dir() -> Path | None:
    """Main project's git dir (same for every worktree of the repo)."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--git-common-dir"],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    common = Path(result.stdout.strip())
    if not common.is_absolute():
        common = REPO_ROOT / common
    return common.resolve()


def cache_root() -> Path:
    env_root = os.environ.get("YA_CACHE_ROOT")
    if env_root:
        return Path(env_root).expanduser().resolve()

    try:
        result = subprocess.run(
            ["git", "config", "--get", "ya.cacheRoot"],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
    except OSError:
        result = None
    if result is not None and result.returncode == 0 and result.stdout.strip():
        return Path(result.stdout.strip()).expanduser().resolve()

    common = _git_common_dir()
    if common is not None:
        return common.parent / ".ya-cache"
    return REPO_ROOT / ".ya-cache"


def _legacy_cache_roots() -> list[Path]:
    """Per-user system cache roots used by the previous scheme."""
    roots: list[Path] = []
    if sys.platform == "darwin":
        roots.append(Path.home() / "Library" / "Caches" / "ya-engine")
    elif sys.platform == "win32":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            roots.append(Path(local_app_data) / "ya-engine" / "Cache")
    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    if xdg_cache:
        roots.append(Path(xdg_cache) / "ya-engine")
    roots.append(Path.home() / ".cache" / "ya-engine")
    return roots


def migrate_legacy_cache(new_root: Path) -> None:
    """One-time move of the old per-user system cache into the project-local
    root, so existing SDK/submodule installs are reused instead of
    re-downloaded. Idempotent; a no-op when the new root already exists or no
    legacy cache is present."""
    if new_root.exists():
        return
    for legacy in _legacy_cache_roots():
        if not legacy.is_dir():
            continue
        if not (legacy / "VulkanSDK").exists() and not (legacy / "Submodules").exists():
            continue
        new_root.parent.mkdir(parents=True, exist_ok=True)
        print(f"-- Moving shared cache {legacy} -> {new_root}")
        shutil.move(str(legacy), str(new_root))
        return


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
