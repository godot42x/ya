#!/usr/bin/env python3
"""Download Khronos glTF Sponza into Engine/Saved/PerfAssets/Sponza/.

The destination is not committed: the repo .gitignore pattern `Saved` ignores it.
The script checks the upstream README for the license text before saving any
model file. A missing or changed URL is a hard error with the URL in the message;
that failure is a fetch problem, not a missing PerfLab scene.

Source:
  https://github.com/KhronosGroup/glTF-Sample-Models/tree/master/2.0/Sponza
License text (required substrings of 2.0/Sponza/README.md):
  the Crytek / Frank Meinl / Marko Dabrovic notice, "donated to the public".
"""
from __future__ import annotations

import json
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "Engine" / "Saved" / "PerfAssets" / "Sponza"

REPO = "KhronosGroup/glTF-Sample-Models"
REF = "master"
MODEL_DIR = "2.0/Sponza/glTF"
README_URL = f"https://raw.githubusercontent.com/{REPO}/{REF}/2.0/Sponza/README.md"
# The contents API 404s on ?ref=master (raw.githubusercontent.com still serves
# that ref). The default branch listing is the one that resolves.
API_URL = f"https://api.github.com/repos/{REPO}/contents/{MODEL_DIR}"
SOURCE_PAGE = f"https://github.com/{REPO}/tree/{REF}/2.0/Sponza"

# Distinctive phrases from the upstream README "Licensing notes" block
# (copyright.txt inside SponzaPBR_Textures.rar, quoted by Khronos).
LICENSE_MARKERS = (
    "Licensing notes",
    "Original copyright",
    "Sponza Model",
    "Marko Dabrovic",
    "Frank Meinl",
    "donated to the public",
)


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def fetch_text(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": "ya-perf-fetch-sponza"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        fail(f"cannot download {url} (HTTP {error.code})")
    except urllib.error.URLError as error:
        fail(f"cannot download {url} ({error.reason})")
    return ""


def check_license(readme: str) -> None:
    missing = [marker for marker in LICENSE_MARKERS if marker not in readme]
    if missing:
        fail(
            "Sponza README at "
            f"{README_URL} does not contain the expected license text "
            f"({', '.join(missing)}). Refusing to save the model."
        )


def list_files() -> list[dict]:
    try:
        payload = json.loads(fetch_text(API_URL))
    except json.JSONDecodeError as error:
        fail(f"Sponza file list at {API_URL} is not JSON ({error})")
    if isinstance(payload, dict) and payload.get("message"):
        fail(f"Sponza file list at {API_URL} failed: {payload['message']}")
    if not isinstance(payload, list) or not payload:
        fail(f"Sponza file list at {API_URL} is empty")
    files = [item for item in payload if item.get("type") == "file" and item.get("download_url")]
    names = {item["name"] for item in files}
    if "Sponza.gltf" not in names or "Sponza.bin" not in names:
        fail(f"Sponza file list at {API_URL} has no Sponza.gltf and Sponza.bin")
    return files


def download(url: str, dest: Path, expected_size: int | None) -> None:
    if dest.is_file() and expected_size and dest.stat().st_size == expected_size:
        print(f"keep {dest.name} ({expected_size} bytes)", flush=True)
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    partial = dest.with_name(dest.name + ".partial")
    command = ["curl", "-fL", "--retry", "3", "--retry-delay", "2", "-o", str(partial), url]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        partial.unlink(missing_ok=True)
        detail = (result.stderr or result.stdout).strip()
        fail(f"failed to download {url}\ncurl: {detail}")
    got = partial.stat().st_size
    if expected_size and got != expected_size:
        partial.unlink(missing_ok=True)
        fail(f"downloaded {url} is {got} bytes, expected {expected_size}")
    partial.replace(dest)
    print(f"saved {dest.relative_to(ROOT)} ({dest.stat().st_size} bytes)", flush=True)


def main() -> None:
    print(f"source: {SOURCE_PAGE}", flush=True)
    print(f"license: {README_URL}", flush=True)
    readme = fetch_text(README_URL)
    check_license(readme)
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "LICENSE_SOURCE.md").write_text(readme, encoding="utf-8")
    (DEST / "SOURCE.txt").write_text(
        "\n".join([
            f"page: {SOURCE_PAGE}",
            f"license: {README_URL}",
            f"model: {MODEL_DIR}/Sponza.gltf",
            "license check: README contains the Crytek / Frank Meinl / Marko Dabrovic",
            "notice and the phrase \"donated to the public\". Full text: LICENSE_SOURCE.md.",
            "",
        ]),
        encoding="utf-8",
    )
    print("license text matches the expected Crytek / Meinl / Dabrovic notice", flush=True)

    for item in list_files():
        size = item.get("size")
        download(item["download_url"], DEST / item["name"], size if isinstance(size, int) else None)

    gltf = DEST / "Sponza.gltf"
    binary = DEST / "Sponza.bin"
    if not gltf.is_file() or not binary.is_file():
        fail(f"Sponza download finished without {gltf} and {binary}")
    print(f"ready {gltf.relative_to(ROOT)}", flush=True)


if __name__ == "__main__":
    main()
