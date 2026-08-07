"""
Materialize heavy submodules from the MAIN project, exposing them in linked
worktrees through directory links (symlink on macOS/Linux, junction on
Windows). Small submodules keep standard git semantics everywhere.

Why?
- Vulkan-Samples-Assets (~1 GB) and LearnOpenGL (~250 MB) are big asset
  repos; with parallel agents across multiple worktrees every extra
  checkout costs minutes of network plus GBs of disk.
- The MAIN project owns the REAL checkouts (Script/ya_shared_cache.py
  resolves it via $YA_CACHE_ROOT / git config ya.cacheRoot / the common git
  dir of git worktrees). Linked worktrees point back into the main project,
  so deleting the main project removes everything -- system directories are
  never written to.
- In the main project, submodules are completely standard git (`git
  submodule status/update` work normally). Only linked worktrees need
  `git update-index --skip-worktree` on the heavy paths to keep `git status`
  clean.

Git caveats (by design):
- In LINKED worktrees, `git submodule status` / `git submodule update`
  refuse the heavy paths ("expected submodule path ... not to be a symbolic
  link"); the main project's checkout is the single source of content.
  Small submodules keep standard git semantics.
- In a fresh worktree, run this script (via `ya.py cfg`) BEFORE any raw
  `git submodule update`, otherwise the heavy repos get cloned per-checkout
  first. Clean checkouts are converted back into links automatically; dirty
  ones are left alone with a warning.

Content follows the main project: a linked worktree sees whatever commit the
main project's submodule is on. If the worktree's index pins a different
gitlink SHA, a warning is printed (last writer wins; assets rarely move).

Usage
-----
    python3 Script/setup_submodules.py      # idempotent, run from any checkout
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from ya_shared_cache import (
    HEAVY_SUBMODULES,
    is_dir_link,
    is_main_repo,
    main_repo_root,
    make_dir_link,
    migrate_legacy_payloads,
    rmtree_writable,
    submodule_payload_root,
)


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

_GIT_ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0")


def _git(
    *args: str, cwd: Path | None = None, check: bool = True
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            ["git", *args],
            cwd=cwd,
            check=check,
            text=True,
            capture_output=True,
            env=_GIT_ENV,
        )
    except FileNotFoundError:
        raise RuntimeError("git is not available on PATH") from None


def _git_ok(*args: str, cwd: Path | None = None) -> bool:
    return _git(*args, cwd=cwd, check=False).returncode == 0


def is_git_repo(path: Path) -> bool:
    """True only when `path` is itself a git checkout (has its own .git).

    `git rev-parse` must not be used here: it walks up and would report the
    parent repo for an empty submodule placeholder dir.
    """
    try:
        return (path / ".git").exists()
    except OSError:
        return False


def index_gitlink_sha(path: str) -> str | None:
    """Gitlink SHA recorded in the current checkout's index."""
    result = _git("ls-files", "-s", "--", path, cwd=REPO_ROOT, check=False)
    if result.returncode != 0:
        return None
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "160000":
            return parts[1]
    return None


def all_submodule_paths() -> list[str]:
    result = _git(
        "config",
        "-f",
        str(REPO_ROOT / ".gitmodules"),
        "--get-regexp",
        r"^submodule\..*\.path$",
        check=False,
    )
    if result.returncode != 0:
        return []
    return [line.partition(" ")[2].strip() for line in result.stdout.splitlines()]


def _git_worktree_roots() -> list[Path]:
    result = _git("worktree", "list", "--porcelain", cwd=REPO_ROOT, check=False)
    roots: list[Path] = []
    for line in result.stdout.splitlines():
        if line.startswith("worktree "):
            roots.append(Path(line.split(maxsplit=1)[1]))
    return roots


def missing_tracked_files(path: Path) -> bool:
    """True when tracked files are missing from the working tree (HEAD vs
    worktree deletions). Catches interrupted checkouts, including the empty-
    index case that `git ls-files --deleted` misses."""
    result = _git(
        "diff", "HEAD", "--name-only", "--diff-filter=D", cwd=path, check=False
    )
    return bool(result.stdout.strip())


def ensure_worktree_link(path: str, target: Path) -> None:
    """Link <repo>/<path> to the main project's real checkout and hide the
    path from git status via skip-worktree."""
    link = REPO_ROOT / path
    if is_dir_link(link):
        try:
            if link.resolve() != target.resolve():
                make_dir_link(link, target)
        except OSError:
            make_dir_link(link, target)
    elif link.exists():
        if not is_git_repo(link):
            has_gitdir = (link / ".git").exists()
            if has_gitdir:
                # Broken leftover from an interrupted conversion; the main
                # project's checkout is authoritative.
                print(f"-- Removing broken checkout-leftover {link}")
                rmtree_writable(link)
                make_dir_link(link, target)
            elif any(link.iterdir()):
                print(f"   warn: {link} exists but is not a git checkout; leaving it alone")
                return
            else:
                # Empty placeholder created by `git worktree add`; safe to replace.
                link.rmdir()
                make_dir_link(link, target)
        else:
            dirty = _git("status", "--porcelain", cwd=link, check=False).stdout.strip()
            if dirty:
                print(f"   warn: {path} has local changes; keeping the real checkout")
                return
            print(f"-- Converting {path} to a link into the main project")
            rmtree_writable(link)
            make_dir_link(link, target)
    else:
        make_dir_link(link, target)

    result = _git(
        "update-index", "--skip-worktree", "--", path, cwd=REPO_ROOT, check=False
    )
    if result.returncode != 0:
        print(f"   warn: could not set skip-worktree for {path}: {result.stderr.strip()}")


def ensure_small_submodules(small: list[str]) -> None:
    if not small:
        return
    result = _git(
        "submodule", "update", "--init", "--", *small, cwd=REPO_ROOT, check=False
    )
    if result.returncode != 0:
        print(
            "   warn: `git submodule update --init` for small submodules failed:\n"
            f"   {result.stderr.strip()}"
        )
    for path in small:
        sub = REPO_ROOT / path
        if not is_git_repo(sub):
            continue
        # Self-heal interrupted checkouts: `git submodule update` skips
        # repos whose HEAD already matches, even if the working tree was
        # left empty by a killed agent. Missing tracked files are safe to
        # restore (third-party content only); modified files are untouched.
        if missing_tracked_files(sub):
            print(f"-- Restoring missing files in {path} (interrupted checkout?)")
            _git("checkout", "-f", "HEAD", cwd=sub, check=False)


def setup_main_project() -> int:
    """The main project owns real checkouts; standard git semantics."""
    small = [p for p in all_submodule_paths() if p not in HEAVY_SUBMODULES]
    all_paths = HEAVY_SUBMODULES + small
    if all_paths:
        result = _git(
            "submodule", "update", "--init", "--", *all_paths, cwd=REPO_ROOT, check=False
        )
        if result.returncode != 0:
            print(
                "   warn: `git submodule update --init` failed (submodules with "
                "local changes are left untouched):\n"
                f"   {result.stderr.strip()}"
            )
    # Migrated payloads may carry standalone gitdirs, while stale absorbed
    # gitdirs from the pre-sharing layout may still linger in .git/modules
    # (absorbgitdirs would fail with "Directory not empty"). Drop stale ones
    # that no checkout references, then absorb the standalone gitdirs for
    # standard submodule bookkeeping.
    worktrees = _git_worktree_roots()
    for heavy in HEAVY_SUBMODULES:
        modules_gitdir = REPO_ROOT / ".git" / "modules" / heavy
        sub_git = REPO_ROOT / heavy / ".git"
        if modules_gitdir.exists() and sub_git.is_dir() and not sub_git.is_symlink():
            referenced = any((wt / heavy / ".git").is_file() for wt in worktrees)
            if not referenced:
                print(f"-- Removing stale absorbed gitdir {modules_gitdir}")
                rmtree_writable(modules_gitdir)
    _git("submodule", "absorbgitdirs", "--", *HEAVY_SUBMODULES, cwd=REPO_ROOT, check=False)
    return 0


def setup_linked_worktree() -> int:
    """Heavy submodules are links into the main project; small ones are
    standard checkouts."""
    main = main_repo_root()
    for path in HEAVY_SUBMODULES:
        target = submodule_payload_root(path)
        if not target.exists() or not is_git_repo(target):
            print(
                f"   warn: main project ({main}) has no checkout of {path}; "
                "run `ya.py cfg` in the main project first"
            )
            continue
        sha = index_gitlink_sha(path)
        if sha:
            head = _git("rev-parse", "HEAD", cwd=target, check=False).stdout.strip()
            if head and head != sha:
                print(
                    f"   warn: {path}: main project is at {head[:12]}, this "
                    f"checkout pins {sha[:12]}; content follows the main project"
                )
        ensure_worktree_link(path, target)

    small = [p for p in all_submodule_paths() if p not in HEAVY_SUBMODULES]
    ensure_small_submodules(small)
    return 0


def main() -> int:
    if not is_git_repo(REPO_ROOT):
        print("-- Not a git checkout; skipping submodule setup")
        return 0
    migrate_legacy_payloads()

    if is_main_repo():
        return setup_main_project()
    return setup_linked_worktree()


if __name__ == "__main__":
    sys.exit(main())
