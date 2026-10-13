"""R1: post-processing driven from gameplay.

    python3 Script/automation/2d-rpg/post_look.py

Loads the WorldText scene, whose LookDemo controller cycles looks on a timer.
The test screenshots the same view at two phases (neutral vs heavy exposure)
and checks the average brightness actually changed -- proving render.setLook
reached the pipeline from a script.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg
from PIL import Image

SCENE = "Content/Scenes/WorldText.scene.json"
OUT_DIR = rpg.ROOT / "Engine" / "Saved" / "Automation" / "post-look"


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
    rpc(port, "capture_screenshot", {"target": "presentation", "path": str(path), "warmup_frames": 4})
    return Image.open(path).convert("RGB")


def mean_brightness(img: Image.Image) -> float:
    # Ignore the black letterbox band; sample the centre where the board is.
    w, h = img.size
    box = img.crop((w // 4, h // 3, 3 * w // 4, 2 * h // 3))
    px = list(box.getdata())
    return sum(sum(p) for p in px) / (3.0 * len(px))


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "WorldText" else None, 20.0, "WorldText active")
        # The look timeline starts on scene load; wait until the neutral look is
        # up (before the 1.2s mark) is unreliable, so sample by wall clock from
        # a known start: reload and immediately shoot the neutral frame.
        time.sleep(0.4)
        neutral = mean_brightness(shoot(port, "look_neutral.png"))
        print(f"neutral brightness: {neutral:.1f}")

        # Heavy exposure is the 1.2s-2.4s window.
        time.sleep(1.1)
        bright = mean_brightness(shoot(port, "look_bright.png"))
        print(f"bright brightness:  {bright:.1f}")

        # Grayscale is the 2.4s-3.6s window: check saturation drops.
        time.sleep(1.3)
        gray_img = shoot(port, "look_gray.png")
        gray = mean_brightness(gray_img)

        def saturation(img: Image.Image) -> float:
            w, h = img.size
            box = img.crop((w // 4, h // 3, 3 * w // 4, 2 * h // 3))
            px = list(box.getdata())
            return sum((max(p) - min(p)) for p in px) / len(px)

        sat_neutral = saturation(Image.open(OUT_DIR / "look_neutral.png").convert("RGB"))
        sat_gray = saturation(gray_img)
        print(f"neutral saturation: {sat_neutral:.1f}")
        print(f"gray saturation:    {sat_gray:.1f}")

        if bright <= neutral * 1.15:
            raise SystemExit(f"exposure look did not brighten ({neutral:.1f} -> {bright:.1f})")
        if sat_gray >= sat_neutral * 0.5:
            raise SystemExit(f"grayscale look did not desaturate ({sat_neutral:.1f} -> {sat_gray:.1f})")
        print("post look OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
