"""R1: the fade overlay (screen transition) driven from a scene controller.

    python3 Script/automation/2d-rpg/fade_overlay.py

Loads FadeScene, whose FadeDemo controller fades to black then back. The test
screenshots the clear frame and the black frame and checks the brightness
changed -- proving the fade is a full-screen UI panel, not a render pass.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg
from PIL import Image

SCENE = "Content/Scenes/FadeScene.scene.json"
OUT_DIR = rpg.ROOT / "Engine" / "Saved" / "Automation" / "fade"


def start_with_port() -> int:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "180"],
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
    rpc(port, "capture_screenshot", {"target": "presentation", "path": str(path), "warmup_frames": 3})
    return Image.open(path).convert("RGB")


def mean(img: Image.Image) -> float:
    w, h = img.size
    box = img.crop((w // 4, h // 3, 3 * w // 4, 2 * h // 3))
    px = list(box.getdata())
    return sum(sum(p) for p in px) / (3.0 * len(px))


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "FadeScene" else None, 20.0, "FadeScene active")
        # On mount the overlay fades in from black (intro); wait past it before
        # sampling the clear frame. Then FadeDemo fades out, holds, fades in.
        time.sleep(1.6)
        clear = mean(shoot(port, "fade_clear.png"))
        print(f"clear brightness: {clear:.1f}")

        time.sleep(1.4)  # into the black hold
        black = mean(shoot(port, "fade_black.png"))
        print(f"black brightness: {black:.1f}")

        if black > clear * 0.10:
            raise SystemExit(f"fade to black did not darken ({clear:.1f} -> {black:.1f})")

        time.sleep(3.0)  # fade back in + settle
        back = mean(shoot(port, "fade_back.png"))
        print(f"back brightness:  {back:.1f}")
        if back < clear * 0.8:
            raise SystemExit(f"fade did not clear ({clear:.1f} -> {back:.1f})")
        print("fade overlay OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()

