"""A1: the audio subsystem from gameplay.

    python3 Script/automation/2d-rpg/audio.py

Loads FadeScene (whose AudioDemo controller plays music and a click on confirm)
and checks the audio device opened and the clips decoded. The proof is in the
log: a headless machine logs "no device" and stays silent, a machine with audio
logs the device and each decoded clip. The test asserts the device line and the
decode lines, which is what the acceptance ("no crash without a device; sound
with one") turns on.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time
from pathlib import Path

import rpg_control as rpg

SCENE = "Content/Scenes/FadeScene.scene.json"


def start_with_port() -> tuple[int, Path]:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "120"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    port = None
    log = None
    for line in result.stdout.splitlines():
        if "control port " in line:
            port = int(line.split("control port ", 1)[1].split()[0])
        if "log " in line and ".log" in line:
            log = Path(line.split("log ", 1)[1].strip())
    if port is None:
        raise SystemExit(f"no control port:\n{result.stdout}")
    return port, log


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


def main() -> None:
    port, log = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "FadeScene" else None, 20.0, "FadeScene active")
        # Confirm presses fire the UI click through audio.play.
        for _ in range(3):
            rpc(port, "invoke", {"name": "input.inject_key",
                                 "args": {"key": "Space", "action": "hold", "frames": 1}})
            time.sleep(0.3)
        time.sleep(1.0)
    finally:
        rpg.stop()

    text = log.read_text(encoding="utf-8", errors="ignore") if log and log.exists() else ""
    audio_lines = [line for line in text.splitlines() if "audio:" in line]
    print("audio log lines:")
    for line in audio_lines[-8:]:
        print("  ", line.strip())

    if not audio_lines:
        raise SystemExit("no audio log lines at all: the subsystem did not run")
    has_device = any("device ready" in line for line in audio_lines)
    no_device = any("no device" in line for line in audio_lines)
    if not (has_device or no_device):
        raise SystemExit("audio ran but neither opened a device nor reported none")
    if has_device:
        decoded = [line for line in audio_lines if "loaded" in line]
        if not decoded:
            raise SystemExit("device opened but no clip decoded")
        print(f"device opened; {len(decoded)} clip(s) decoded")
    else:
        print("no audio device; subsystem degraded silently (expected headless)")
    print("audio OK")


if __name__ == "__main__":
    main()
