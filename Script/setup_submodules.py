"""
Materialize heavy submodules from the user-level shared cache, exposing them
in each checkout through directory links (symlink on macOS/Linux, junction
on Windows). Small submodules keep standard git semantics.

Why?
- Vulkan-Samples-Assets (~1 GB) and LearnOpenGL (~250 MB) are big asset
  repos; with parallel agents across multiple worktrees every extra
  checkout costs minutes of network plus GBs of disk.
- One canonical checkout lives in the shared cache (Script/ya_shared_cache.py,
  project-local under <main project>/.ya-cache by default, root overridable
  via $YA_CACHE_ROOT or git config ya.cacheRoot); every checkout links to it.
  `git update-index --skip-worktree` keeps `git status` clean for those paths.

Git caveats (by design):
- `git submodule status` / `git submodule update` refuse linked paths
  ("expected submodule path ... not to be a symbolic link"). Heavy
  submodules are owned by this script; refresh them by running
  `python3 Script/ya.py cfg` (or this script directly). Do not run raw
  `git submodule update` on heavy paths.
- In a fresh checkout, run this script (via `ya.py cfg`) BEFORE any raw
  `git submodule update`, otherwise the heavy repos get cloned per-checkout
  first. Clean checkouts are converted back into links automatically; dirty
  ones are left alone with a warning.

Canonical checkout policy:
- Pinned to the exact gitlink SHA recorded in the current checkout's index
  (`git ls-files -s`), so content is deterministic across worktrees.
- Network is touched only when the pinned SHA is missing locally (the common
  path is zero-network). LFS objects are pulled best-effort when git-lfs is
  installed. GIT_TERMINAL_PROMPT=0 prevents interactive hangs.

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

from ya_shared_cache import cache_root, is_dir_link, make_dir_link, migrate_legacy_cache


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

# Submodules big enough to warrant a single shared copy. Basenames must be
# unique across the list (they name the canonical dirs in the cache).
HEAVY_SUBMODULES = [
    "Engine/ThirdParty/Vulkan-Samples-Assets",
    "Engine/ThirdParty/LearnOpenGL",
]

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


def submodule_url(path: str) -> str | None:
    """Worktree-local override first, then .gitmodules."""
    result = _git(
        "config", "--get", f"submodule.{path}.url", cwd=REPO_ROOT, check=False
    )
    if result.returncode == 0:
        return result.stdout.strip()
    result = _git(
        "config",
        "-f",
        str(REPO_ROOT / ".gitmodules"),
        "--get",
        f"submodule.{path}.url",
        check=False,
    )
    if result.returncode == 0:
        return result.stdout.strip()
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


def canonical_dir(path: str) -> Path:
    return cache_root() / "Submodules" / Path(path).name


def missing_tracked_files(path: Path) -> bool:
    """True when tracked files are missing from the working tree (HEAD vs
    worktree deletions). Catches interrupted checkouts, including the empty-
    index case that `git ls-files --deleted` misses."""
    result = _git(
        "diff", "HEAD", "--name-only", "--diff-filter=D", cwd=path, check=False
    )
    return bool(result.stdout.strip())


def _rmtree(path: Path) -> None:
    if path.is_symlink():
        path.unlink()
        return
    if not path.exists():
        return

    # Submodule trees may be read-only (setup_3rd_party chmod 555), which
    # makes rmtree fail midway. Restore write permission bottom-up first.
    for root, dirs, files in os.walk(path, topdown=False):
        for name in files:
            entry = Path(root) / name
            if not entry.is_symlink():
                try:
                    os.chmod(entry, 0o600)
                except OSError:
                    pass
        try:
            os.chmod(root, 0o700)
        except OSError:
            pass
    shutil.rmtree(path)


def ensure_canonical(path: str, url: str, sha: str, seed: Path | None) -> Path:
    """Shared canonical checkout pinned to `sha`; created atomically so
    concurrent agents never observe a half-cloned cache entry."""
    target = canonical_dir(path)
    if target.exists() and not is_git_repo(target):
        print(f"   warn: removing corrupt cache entry {target}")
        _rmtree(target)

    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        tmp = target.parent / f".cloning-{Path(path).name}-{os.getpid()}"
        _rmtree(tmp)
        if seed is not None and is_git_repo(seed):
            print(f"-- Cloning {path} into shared cache (seeded from local checkout)")
            clone = _git("clone", "--quiet", str(seed), str(tmp), check=False)
        else:
            print(f"-- Cloning {path} into shared cache (one-time)")
            clone = _git("clone", "--quiet", url, str(tmp), check=False)
        if clone.returncode != 0:
            _rmtree(tmp)
            raise RuntimeError(f"clone failed: {clone.stderr.strip()}")
        # The canonical must track the real remote, not a seed dir that may
        # be deleted right after conversion.
        _git("remote", "set-url", "origin", url, cwd=tmp, check=False)
        try:
            os.rename(tmp, target)
        except OSError:
            # Another agent cloned it first; use theirs.
            _rmtree(tmp)

    # Pin to the recorded SHA; only touch the network when the SHA is missing.
    if not _git_ok("cat-file", "-e", f"{sha}^{{commit}}", cwd=target):
        print(f"-- Fetching {path} (new commit {sha[:12]})")
        fetch = _git("fetch", "--quiet", "origin", cwd=target, check=False)
        if fetch.returncode != 0:
            print(f"   warn: fetch failed ({fetch.stderr.strip()}); using cached content")
    if _git_ok("cat-file", "-e", f"{sha}^{{commit}}", cwd=target):
        current = _git("rev-parse", "HEAD", cwd=target, check=False).stdout.strip()
        if current != sha:
            _git("checkout", "-f", "--quiet", "--detach", sha, cwd=target, check=False)
            _git("clean", "-fdx", "--quiet", cwd=target, check=False)
            # Fresh checkout runs LFS smudge on demand; a final pull makes
            # sure nothing is left as a pointer. No-op runs never touch the
            # network (git-lfs contacts the remote even when there is
            # nothing to fetch).
            if shutil.which("git-lfs"):
                _git("lfs", "pull", cwd=target, check=False)
        elif missing_tracked_files(target):
            # HEAD matches but the tree is incomplete (agent killed mid-
            # checkout); repair without touching the network.
            print(f"-- Restoring missing files in {target.name}")
            _git("checkout", "-f", "--quiet", "--detach", sha, cwd=target, check=False)
            _git("clean", "-fdx", "--quiet", cwd=target, check=False)
    else:
        print(f"   warn: pinned commit {sha[:12]} not available; keeping cached content")
    return target


def ensure_worktree_link(path: str, sha: str, target: Path) -> None:
    """Link <repo>/<path> to the canonical checkout and hide the path from
    git status via skip-worktree."""
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
                # Broken leftover from an interrupted conversion; the
                # canonical checkout is authoritative.
                print(f"-- Removing broken checkout-leftover {link}")
                _rmtree(link)
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
            head = _git("rev-parse", "HEAD", cwd=link, check=False).stdout.strip()
            if dirty or head != sha:
                print(
                    f"   warn: {path} has local changes or a different commit; "
                    "keeping the real checkout"
                )
                return
            print(f"-- Converting {path} to shared-cache link (checkout was clean)")
            _rmtree(link)
            make_dir_link(link, target)
    else:
        make_dir_link(link, target)

    result = _git("update-index", "--skip-worktree", "--", path, cwd=REPO_ROOT, check=False)
    if result.returncode != 0:
        print(f"   warn: could not set skip-worktree for {path}: {result.stderr.strip()}")


def main() -> int:
    if not is_git_repo(REPO_ROOT):
        print("-- Not a git checkout; skipping submodule setup")
        return 0
    migrate_legacy_cache(cache_root())

    for path in HEAVY_SUBMODULES:
        sha = index_gitlink_sha(path)
        if not sha:
            print(f"-- {path}: not a submodule in the current index; skipping")
            continue
        url = submodule_url(path)
        if not url:
            print(f"-- {path}: no URL in .gitmodules; skipping")
            continue
        local = REPO_ROOT / path
        seed = local if local.exists() and is_git_repo(local) else None
        print(f"-- {path}: pinning {sha[:12]} ({url})")
        try:
            target = ensure_canonical(path, url, sha, seed)
            ensure_worktree_link(path, sha, target)
        except RuntimeError as e:
            print(f"   warn: {path}: {e}")
            continue

    # Small submodules keep standard git semantics. Explicit paths only, so
    # `git submodule update` never touches the heavy links above.
    small = [p for p in all_submodule_paths() if p not in HEAVY_SUBMODULES]
    if small:
        result = _git("submodule", "update", "--init", "--", *small, cwd=REPO_ROOT, check=False)
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
