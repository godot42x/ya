"""
Download + install the LunarG Vulkan SDK for macOS into a repo-local directory.

Why repo-local?
- Keeps the toolchain reproducible per-checkout and avoids polluting the system.
- xmake auto-discovers the repo-local SDK from Engine/ThirdParty/VulkanSDK,
  so no separate shell env sync step is required.

Usage
-----
    python3 Script/setup_vulkan_sdk_macos.py              # latest
    python3 Script/setup_vulkan_sdk_macos.py --version 1.4.341.1
    python3 Script/setup_vulkan_sdk_macos.py --force      # reinstall even if present

Requires: Python 3.8+, macOS, curl, unzip (ship with macOS).

Layout produced
---------------
    Engine/ThirdParty/VulkanSDK/<version>/           # installed SDK root
        macOS/
            bin/  include/  lib/  share/ ...
    Engine/ThirdParty/VulkanSDK/.cache/<version>.zip # download cache (retained)
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

SDK_INSTALL_ROOT = REPO_ROOT / "Engine" / "ThirdParty" / "VulkanSDK"
DOWNLOAD_CACHE = SDK_INSTALL_ROOT / ".cache"

LATEST_VERSION_URL = "https://vulkan.lunarg.com/sdk/latest/mac.txt"
SDK_URL_TEMPLATE = (
    "https://sdk.lunarg.com/sdk/download/{version}/mac/vulkansdk-macos-{version}.zip"
)


def ensure_macos() -> None:
    if sys.platform != "darwin":
        sys.exit(f"This script only supports macOS. Current platform: {sys.platform}")


def fetch_latest_version() -> str:
    with urllib.request.urlopen(LATEST_VERSION_URL, timeout=30) as resp:
        return resp.read().decode("utf-8").strip()


def download_with_progress(url: str, dst: Path) -> None:
    """Download URL to dst using curl so we get a progress bar + resume support."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".part")
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
    tmp.replace(dst)


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


def main() -> int:
    ensure_macos()

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument(
        "--version",
        default=os.environ.get("VULKAN_SDK_VERSION") or None,
        help="Pin a specific Vulkan SDK version (default: query latest).",
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

    version = args.version or fetch_latest_version()
    print(f"-- Vulkan SDK version: {version}")

    install_dest = SDK_INSTALL_ROOT / version
    if install_dest.exists() and not args.force:
        try:
            validate_install(install_dest)
            print(f"-- SDK already installed at {install_dest} (use --force to redo)")
            return 0
        except RuntimeError as e:
            print(f"-- Existing install is incomplete ({e}); reinstalling")

    if args.force and install_dest.exists():
        print(f"-- Removing existing install at {install_dest}")
        shutil.rmtree(install_dest)

    zip_path = DOWNLOAD_CACHE / f"vulkansdk-macos-{version}.zip"
    if not zip_path.exists():
        download_with_progress(SDK_URL_TEMPLATE.format(version=version), zip_path)
    else:
        print(f"-- Using cached download {zip_path}")

    extract_dir = DOWNLOAD_CACHE / f"extract-{version}"
    if extract_dir.exists():
        shutil.rmtree(extract_dir)
    installer_app = unzip(zip_path, extract_dir)

    try:
        run_installer(installer_app, install_dest)
        validate_install(install_dest)
    finally:
        if not args.keep_extracted and extract_dir.exists():
            shutil.rmtree(extract_dir, ignore_errors=True)

    print(f"-- SDK installed at: {install_dest}")
    print()
    print("Next steps:")
    print("  make cfg && make b t=ya")
    print("  or: xmake f -m debug -y && xmake b ya")
    return 0


if __name__ == "__main__":
    sys.exit(main())
