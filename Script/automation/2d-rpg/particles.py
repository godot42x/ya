"""X1: the particle subsystem from gameplay.

    python3 Script/automation/2d-rpg/particles.py

Loads FadeScene, whose ParticleDemo bursts into the SparkEmitter on a timer.
The test checks live particles actually reach the frame: it screenshots during a
burst and counts the emitter texture's bright pixels, which the flat grass has
none of.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg
from PIL import Image

SCENE = "Content/Scenes/FxScene.scene.json"
OUT_DIR = rpg.ROOT / "Engine" / "Saved" / "Automation" / "particles"


def start_with_port() -> int:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "150"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    for line in result.stdout.splitlines():
        if "control port " in line:
            return int(line.split("control port ", 1)[1].split()[0])
    raise SystemExit(f"no control port:\n{result.stdout}")


def rpc(port: int, method: str, params: dict | None = None, timeout: float = 60.0) -> dict:
    payload = (json.dumps({"id": 1, "method": method, "params": params or {}}) + "\n").encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(("127.0.0.1", port))
        sock.sendall(payload)
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    finally:
        sock.close()
    response = json.loads(data.split(b"\n", 1)[0].decode("utf-8"))
    if not response.get("ok"):
        raise SystemExit(f"{method} failed: {response.get('error')}")
    result = response.get("result")
    return result if isinstance(result, dict) else {}


def scene_name() -> str:
    try:
        s = rpg.call("scene.get_active")
    except SystemExit:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def shoot(port: int, name: str) -> Image.Image:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / name
    rpc(port, "capture_screenshot", {"target": "presentation", "path": str(path), "warmup_frames": 2})
    return Image.open(path).convert("RGB")


def effect_pixels(img: Image.Image, baseline: set[tuple[int, int, int]]) -> int:
    """Pixels the particles added: outside the board band, anything that is
    neither the grass colour nor the black letterbox."""
    w, h = img.size
    box = img.crop((60, 60, w - 60, h - 60))
    count = 0
    for r, g, b in box.getdata():
        if (r, g, b) in baseline:
            continue
        if r < 20 and g < 20 and b < 20:  # letterbox / shadows
            continue
        count += 1
    return count


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "FxScene" else None, 20.0, "FxScene active")
        time.sleep(1.0)
        # Baseline: a frame with no particles should be only grass + letterbox.
        # The emitter only emits on the burst, so sampling between bursts gives
        # the baseline colours to subtract.
        baseline_img = shoot(port, "particles_baseline.png")
        w, h = baseline_img.size
        baseline = {(r, g, b) for r, g, b in baseline_img.crop((60, 60, w - 60, h - 60)).getdata()}
        print(f"baseline colours: {len(baseline)}")
        # ParticleDemo bursts every ~0.8s into a ~1.2s pool, so most frames are
        # mid-burst. Take the busiest of several samples.
        best = 0
        for i in range(8):
            time.sleep(0.4)
            img = shoot(port, f"particles_{i}.png")
            added = effect_pixels(img, baseline)
            best = max(best, added)
            print(f"sample {i}: {added} particle pixels")
        if best < 40:
            raise SystemExit(f"no particle pixels found (best {best})")
        print(f"particles OK ({best} particle pixels at peak)")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
