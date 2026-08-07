"""
Cross-platform helpers for the project-owned shared payload.

Big artifacts (Vulkan SDK, heavy submodule checkouts) live as REAL files
inside the MAIN project's Engine/ThirdParty (the original repo-local
layout), and every other worktree exposes them through directory links
(symlink on macOS/Linux, junction on Windows) pointing back into the main
project.

Why the main project?
- Deleting the main project removes everything; system directories
  (~/Library/Caches, %LOCALAPPDATA%, ~/.cache) are never written to.
- The main project keeps standard git semantics for its submodules (real
  checkouts; `git submodule status/update` work there). Only linked
  worktrees need `git update-index --skip-worktree` on those paths.

Main project resolution (first match wins):
1. $YA_CACHE_ROOT            -- explicit override (path of the main project)
2. git config ya.cacheRoot   -- per-checkout override (standalone clones
                                sharing one main project)
3. `git rev-parse --git-common-dir` parent -- git worktrees resolve to the
                                main project's git dir automatically
4. <this checkout>           -- fallback for non-git layouts: a standalone
                                clone owns its own payload

Legacy layouts are migrated into the main project automatically (see
migrate_legacy_payloads): per-user system caches, <main>/.ya-cache, and real
payload dirs left in non-main checkouts.

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

# Submodules big enough to warrant a single shared copy owned by the main
# project. Basenames must be unique across the list.
HEAVY_SUBMODULES = [
    "Engine/ThirdParty/Vulkan-Samples-Assets",
    "Engine/ThirdParty/LearnOpenGL",
]


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


def _configured_main() -> Path | None:
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
        return None
    if result.returncode == 0 and result.stdout.strip():
        return Path(result.stdout.strip()).expanduser().resolve()
    return None


def main_repo_root() -> Path:
    """The project that owns the shared payload (real files)."""
    configured = _configured_main()
    if configured is not None:
        return configured
    common = _git_common_dir()
    if common is not None:
        return common.parent
    return REPO_ROOT


def is_main_repo() -> bool:
    """True when THIS checkout is the payload owner."""
    try:
        return REPO_ROOT.resolve() == main_repo_root().resolve()
    except OSError:
        return False


def sdk_payload_root() -> Path:
    """Main project's repo-local Vulkan SDK root (real files)."""
    return main_repo_root() / "Engine" / "ThirdParty" / "VulkanSDK"


def submodule_payload_root(path: str) -> Path:
    """Main project's real checkout of the given submodule path."""
    return main_repo_root() / path


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


def rmtree_writable(path: Path) -> None:
    """rmtree that survives read-only trees (setup_3rd_party chmod 555)."""
    if path.is_symlink():
        path.unlink()
        return
    if not path.exists():
        return
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


def _legacy_system_roots() -> list[Path]:
    """Per-user system cache roots used by the previous schemes."""
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


def migrate_legacy_payloads() -> None:
    """One-time migration of previous cache layouts into the main project.

    Handles, in order: the <main>/.ya-cache scheme, per-user system caches,
    and real payload dirs left in non-main checkouts (the pre-sharing
    layout). Idempotent; collisions prefer the destination (a real payload
    already in place wins, duplicate cache copies are dropped).
    """
    main = main_repo_root()
    main_sdk = sdk_payload_root()

    def _replace_with_move(src: Path, dst: Path) -> None:
        if not src.exists():
            return
        if dst.is_symlink():
            # Stale link (e.g. a v2 main-project link into the cache being
            # absorbed); the real payload takes its place.
            dst.unlink()
        elif dst.exists():
            if src.resolve() == dst.resolve():
                return
            print(f"-- Dropping duplicate cache copy {src} (payload exists at {dst})")
            rmtree_writable(src)
            return
        dst.parent.mkdir(parents=True, exist_ok=True)
        print(f"-- Moving {src} -> {dst}")
        shutil.move(str(src), str(dst))

    def _absorb_sdk(src_sdk: Path) -> None:
        if not src_sdk.exists():
            return
        for child in sorted(src_sdk.iterdir()):
            if child.name.startswith("."):
                continue
            _replace_with_move(child, main_sdk / child.name)
        src_cache = src_sdk / ".cache"
        if src_cache.exists():
            for zip_file in src_cache.glob("*.zip"):
                _replace_with_move(zip_file, main_sdk / ".cache" / zip_file.name)
        for leftover in (src_cache, src_sdk):
            try:
                leftover.rmdir()
            except OSError:
                pass

    def _absorb_submodules(src_sub: Path) -> None:
        if not src_sub.exists():
            return
        for child in sorted(src_sub.iterdir()):
            if child.name.startswith("."):
                continue
            matches = [p for p in HEAVY_SUBMODULES if Path(p).name == child.name]
            if not matches:
                print(f"   warn: cache entry {child} has no matching submodule; skipping")
                continue
            _replace_with_move(child, main / matches[0])
        try:
            src_sub.rmdir()
        except OSError:
            pass

    # 1. <main>/.ya-cache scheme
    main_ya_cache = main / ".ya-cache"
    if main_ya_cache.is_dir():
        _absorb_sdk(main_ya_cache / "VulkanSDK")
        _absorb_submodules(main_ya_cache / "Submodules")
        try:
            main_ya_cache.rmdir()
        except OSError:
            pass

    # 2. per-user system caches
    for legacy in _legacy_system_roots():
        if not legacy.is_dir():
            continue
        _absorb_sdk(legacy / "VulkanSDK")
        _absorb_submodules(legacy / "Submodules")

    # 3. real payload dirs left in a NON-main checkout (pre-sharing layout)
    if not is_main_repo():
        local_sdk = REPO_ROOT / "Engine" / "ThirdParty" / "VulkanSDK"
        _absorb_sdk(local_sdk)
        for heavy in HEAVY_SUBMODULES:
            src = REPO_ROOT / heavy
            if src.exists() and not src.is_symlink():
                if (src / ".git").exists():
                    try:
                        result = subprocess.run(
                            ["git", "-C", str(src), "status", "--porcelain"],
                            capture_output=True,
                            text=True,
                            check=False,
                        )
                    except OSError:
                        result = None
                    if result is not None and result.stdout.strip():
                        print(
                            f"   warn: {src} has local changes; leaving the real "
                            "checkout alone"
                        )
                        continue
                _replace_with_move(src, main / heavy)

    # v2 left skip-worktree set in the main project's index; clear it so the
    # main project keeps standard git semantics for its submodules.
    if is_main_repo():
        for heavy in HEAVY_SUBMODULES:
            subprocess.run(
                ["git", "update-index", "--no-skip-worktree", "--", heavy],
                cwd=main,
                check=False,
                capture_output=True,
            )
