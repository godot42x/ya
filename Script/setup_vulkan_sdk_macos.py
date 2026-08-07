"""
Install the LunarG Vulkan SDK for macOS into a shared per-user cache and
expose it to this checkout through a symlink.

Why shared + symlink (multi-agent / multi-worktree safe)?
- Parallel agents and multiple worktrees share ONE SDK copy (~2 GB installed
  + ~350 MB zip cache) instead of each downloading and installing its own.
- The checkout-local discovery contract stays the same: xmake and ya.py still
  look under Engine/ThirdParty/VulkanSDK/<version>/macOS, but those entries
  are now symlinks into the shared install root, so no other tooling needs
  to change.
- Installs are atomic (install into a pid-unique temp dir, then rename into
  place) and all temp paths are pid-unique, so concurrent agents never
  observe or produce a half-installed SDK.

Shared root resolution (first match wins):
1. $YA_VULKAN_SDK_ROOT -- explicit override (e.g. a team CI cache dir)
2. <main project>/Engine/ThirdParty/VulkanSDK, where the main project
   follows Script/ya_shared_cache.py ($YA_CACHE_ROOT / git config
   ya.cacheRoot / git worktrees' common git dir). The real files stay in the
   main project; linked worktrees point back at them, so deleting the main
   project removes everything (nothing is left in system directories).

Layout produced
---------------
    <shared>/<version>/macOS/                   # installed SDK root
        bin/  include/  lib/  share/ ...
    <shared>/.cache/<version>.zip               # download cache (retained)
    <checkout>/Engine/ThirdParty/VulkanSDK/<version> -> <shared>/<version>

Legacy checkout-local installs (real directories from before this change)
are folded into the shared root on the next run, so nothing is re-downloaded.

Usage
-----
    python3 Script/setup_vulkan_sdk_macos.py              # reuse installed SDK, or install if missing
    python3 Script/setup_vulkan_sdk_macos.py --latest     # query and install latest SDK
    python3 Script/setup_vulkan_sdk_macos.py --version 1.4.341.1
    python3 Script/setup_vulkan_sdk_macos.py --force      # reinstall selected SDK even if present

Requires: Python 3.8+, macOS, curl, unzip (ship with macOS).
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path
from typing import Iterable

from ya_shared_cache import (
    is_main_repo,
    migrate_legacy_payloads,
    rmtree_writable,
    sdk_payload_root,
)


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

LATEST_VERSION_URL = "https://vulkan.lunarg.com/sdk/latest/mac.txt"
SDK_URL_TEMPLATE = (
    "https://sdk.lunarg.com/sdk/download/{version}/mac/vulkansdk-macos-{version}.zip"
)


def ensure_macos() -> None:
    if sys.platform != "darwin":
        sys.exit(f"This script only supports macOS. Current platform: {sys.platform}")


def shared_root() -> Path:
    """The main project's repo-local SDK root (real files)."""
    env_root = os.environ.get("YA_VULKAN_SDK_ROOT")
    if env_root:
        return Path(env_root).expanduser().resolve()
    return sdk_payload_root()


def repo_sdk_root() -> Path:
    """Checkout-local discovery path; entries here are symlinks into shared_root()."""
    return REPO_ROOT / "Engine" / "ThirdParty" / "VulkanSDK"


def fetch_latest_version() -> str:
    with urllib.request.urlopen(LATEST_VERSION_URL, timeout=30) as resp:
        return resp.read().decode("utf-8").strip()


def version_key(version: str) -> tuple[int | str, ...]:
    parts: list[int | str] = []
    for token in version.split("."):
        parts.append(int(token) if token.isdigit() else token)
    return tuple(parts)


def download_with_progress(url: str, dst: Path) -> None:
    """Download URL to dst using curl so we get a progress bar + resume support.

    The .part file is pid-unique so concurrent agents never write the same
    temp file; os.replace makes the final file appear atomically. If two
    agents race, the last rename wins with identical bytes.
    """
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_name(dst.name + f".{os.getpid()}.part")
    cmd = [
        "curl",
        "-L",
        "--fail",
        "--progress-bar",
        "-C",
        "-",  # resume if tmp exists
        "-o",
        str(tmp),
        url,
    ]
    print(f"-- Downloading {url}")
    subprocess.run(cmd, check=True)
    os.replace(tmp, dst)


def unzip(zip_path: Path, extract_to: Path) -> Path:
    """Unzip into extract_to; return the path to the installer .app bundle.

    Recent LunarG zips expand to ``vulkansdk-macOS-<ver>.app``; older ones
    used ``InstallVulkan.app``. Accept any top-level .app bundle.
    """
    extract_to.mkdir(parents=True, exist_ok=True)
    print(f"-- Unzipping {zip_path.name} -> {extract_to}")
    subprocess.run(
        ["unzip", "-q", "-o", str(zip_path), "-d", str(extract_to)],
        check=True,
    )
    apps = sorted(extract_to.glob("*.app"))
    if not apps:
        # Fall back to a deeper scan in case LunarG nests it again someday.
        apps = sorted(p for p in extract_to.rglob("*.app") if p.parent == extract_to)
    if not apps:
        raise RuntimeError(f"No installer .app bundle found inside {extract_to}")
    if len(apps) > 1:
        print(f"-- Multiple .app bundles found, using {apps[0].name}")
    return apps[0]


def run_installer(installer_app: Path, install_dest: Path) -> None:
    """Run the LunarG installer in unattended (headless) mode.

    The installer is a Qt Installer Framework binary. Its CLI supports
    ``install --root <path> --accept-licenses --default-answer --confirm-command``
    for non-interactive installs. Binary name matches the app bundle name.
    """
    macos_dir = installer_app / "Contents" / "MacOS"
    binaries = [p for p in macos_dir.iterdir() if p.is_file() and os.access(p, os.X_OK)]
    if not binaries:
        raise RuntimeError(f"No executable inside {macos_dir}")
    binary = binaries[0]

    install_dest.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(binary),
        "--root",
        str(install_dest),
        "--accept-licenses",
        "--default-answer",
        "--confirm-command",
        "install",
    ]
    print(f"-- Running installer {binary.name} -> {install_dest}")
    subprocess.run(cmd, check=True)


def validate_install(install_dest: Path) -> None:
    """Spot-check that the SDK install produced the files we rely on."""
    required = [
        install_dest / "macOS" / "include" / "vulkan" / "vulkan.h",
        install_dest / "macOS" / "lib" / "libvulkan.dylib",
    ]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        raise RuntimeError(
            "Vulkan SDK install looks incomplete, missing:\n  " + "\n  ".join(missing)
        )


def iter_installed_versions(
    root: Path, *, skip_symlinks: bool = False
) -> Iterable[tuple[str, Path]]:
    if not root.exists():
        return []

    installs: list[tuple[str, Path]] = []
    for child in root.iterdir():
        if child.name.startswith("."):
            continue
        if not child.is_dir():
            continue
        if skip_symlinks and child.is_symlink():
            continue
        installs.append((child.name, child))

    installs.sort(key=lambda item: version_key(item[0]), reverse=True)
    return installs


def find_latest_valid_install() -> tuple[str, Path] | None:
    """Latest valid install across the shared root and any legacy
    checkout-local real directories (pre-symlink installs)."""
    shared_versions = {version for version, _ in iter_installed_versions(shared_root())}
    candidates = list(iter_installed_versions(shared_root()))
    candidates += [
        (version, install_dest)
        for version, install_dest in iter_installed_versions(
            repo_sdk_root(), skip_symlinks=True
        )
        if version not in shared_versions
    ]
    candidates.sort(key=lambda item: version_key(item[0]), reverse=True)
    for version, install_dest in candidates:
        try:
            validate_install(install_dest)
            return version, install_dest
        except RuntimeError:
            continue
    return None


def ensure_download(version: str, shared_cache: Path, repo_cache: Path) -> Path:
    """Return a zip for version, reusing the checkout-local cache if present."""
    zip_path = shared_cache / f"vulkansdk-macos-{version}.zip"
    if zip_path.exists():
        return zip_path
    legacy_zip = repo_cache / f"vulkansdk-macos-{version}.zip"
    if legacy_zip.exists():
        print(f"-- Reusing cached download {legacy_zip}")
        shared_cache.mkdir(parents=True, exist_ok=True)
        os.replace(legacy_zip, zip_path)
        return zip_path
    download_with_progress(SDK_URL_TEMPLATE.format(version=version), zip_path)
    return zip_path


def install_into_shared(version: str, installer_app: Path) -> Path:
    """Atomically install into shared_root()/<version>: build in a pid-unique
    temp dir, validate, then rename into place. Concurrent agents racing here
    are safe: the first rename wins, losers validate the winner and clean up."""
    shared = shared_root()
    final = shared / version
    tmp = shared / f".installing-{version}-{os.getpid()}"
    if tmp.exists():
        shutil.rmtree(tmp, ignore_errors=True)
    try:
        run_installer(installer_app, tmp)
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    validate_install(tmp)
    try:
        os.rename(tmp, final)
    except OSError:
        # Someone else installed the same version first.
        validate_install(final)
        shutil.rmtree(tmp, ignore_errors=True)
    return final


def ensure_checkout_sdk(version: str) -> None:
    """Make this checkout see the main project's SDK install.

    Main project: keeps the real install (no link). Linked worktrees: the
    checkout-local path is a link into the main project's dir. Idempotent;
    safe to run from every worktree / agent.
    """
    link = repo_sdk_root() / version
    if is_main_repo():
        if link.is_symlink():
            # Should not happen post-migration; repair by pointing at the
            # real payload the migration restored.
            print(f"   warn: {link} is a link in the main project; replacing")
            link.unlink()
        return
    target = shared_root() / version
    if link.is_symlink():
        try:
            if link.resolve() == target.resolve():
                return
        except OSError:
            pass
        link.unlink()
    elif link.exists():
        raise RuntimeError(
            f"{link} is a real directory; run --force to replace it with a shared-cache symlink"
        )
    repo_sdk_root().mkdir(parents=True, exist_ok=True)
    link.symlink_to(target, target_is_directory=True)
    print(f"-- Linked {link} -> {target}")


def print_update_hint() -> None:
    print("-- To update the SDK explicitly, run one of:")
    print("   make vulkan-sdk-macos")
    print("   python3 Script/setup_vulkan_sdk_macos.py --latest")
    print("   python3 Script/setup_vulkan_sdk_macos.py --version <version>")
    print(f"-- Shared cache root: {shared_root()}")


def main() -> int:
    ensure_macos()
    migrate_legacy_payloads()

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    version_group = parser.add_mutually_exclusive_group()
    version_group.add_argument(
        "--version",
        default=os.environ.get("VULKAN_SDK_VERSION") or None,
        help="Pin a specific Vulkan SDK version.",
    )
    version_group.add_argument(
        "--latest",
        action="store_true",
        help="Query and install the latest Vulkan SDK version explicitly.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Reinstall even if the target directory already exists.",
    )
    parser.add_argument(
        "--keep-extracted",
        action="store_true",
        help="Keep the intermediate extraction directory (for debugging).",
    )
    args = parser.parse_args()

    if not args.version and not args.latest:
        existing = find_latest_valid_install()
        if existing and not args.force:
            version, install_dest = existing
            ensure_checkout_sdk(version)
            print(f"-- Using installed Vulkan SDK: {version}")
            print(f"-- SDK path: {install_dest}")
            print_update_hint()
            return 0

    if args.latest:
        version = fetch_latest_version()
    elif args.version:
        version = args.version
    else:
        version = fetch_latest_version()

    print(f"-- Vulkan SDK version: {version}")
    print(f"-- Shared cache root: {shared_root()}")

    shared = shared_root()
    shared_cache = shared / ".cache"
    install_dest = shared / version

    if args.force:
        print(f"-- Removing existing install at {install_dest}")
        rmtree_writable(install_dest)
        local = repo_sdk_root() / version
        if local.exists() and not local.is_symlink():
            rmtree_writable(local)

    if install_dest.exists():
        try:
            validate_install(install_dest)
            print(f"-- SDK already installed at {install_dest} (use --force to redo)")
        except RuntimeError as e:
            print(f"-- Existing install is incomplete ({e}); reinstalling")
            rmtree_writable(install_dest)
            zip_path = ensure_download(version, shared_cache, repo_sdk_root() / ".cache")
            extract_dir = shared_cache / f"extract-{version}-{os.getpid()}"
            installer_app = unzip(zip_path, extract_dir)
            try:
                install_into_shared(version, installer_app)
            finally:
                if not args.keep_extracted and extract_dir.exists():
                    shutil.rmtree(extract_dir, ignore_errors=True)
    else:
        zip_path = ensure_download(version, shared_cache, repo_sdk_root() / ".cache")
        extract_dir = shared_cache / f"extract-{version}-{os.getpid()}"
        if extract_dir.exists():
            shutil.rmtree(extract_dir, ignore_errors=True)
        installer_app = unzip(zip_path, extract_dir)
        try:
            install_into_shared(version, installer_app)
        finally:
            if not args.keep_extracted and extract_dir.exists():
                shutil.rmtree(extract_dir, ignore_errors=True)

    ensure_checkout_sdk(version)
    validate_install(install_dest)
    print(f"-- SDK installed at: {install_dest}")
    print(f"-- worktree link: {repo_sdk_root() / version} -> {install_dest}")
    print_update_hint()
    return 0


if __name__ == "__main__":
    sys.exit(main())
