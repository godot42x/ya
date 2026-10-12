"""F1: the Camp hub loads, the player and Elder are placed, and a screenshot
proves the map (tiny_dungeon/tiny_town tiles) renders.

    python3 Script/automation/2d-rpg/camp_hub.py

Writes the screenshot to Engine/Saved/Automation/camp-hub/camp_hub.png.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys

import rpg_control as rpg

OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "camp-hub" / "camp_hub.png"


def start_with_port() -> int:
    """Start the game and return its control port (the raw RPC needs it)."""
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "240"],
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
    summary = rpg.call("scene.get_active")
    if not isinstance(summary, dict) or not isinstance(summary.get("name"), str):
        raise SystemExit(f"scene.get_active returned {summary!r}")
    return summary["name"]


def main() -> None:
    port = start_with_port()
    try:
        name = rpg.wait_until(lambda: scene_name() if scene_name() == "Camp" else None,
                              20.0, "Camp scene active")
        entity_id = rpg.player_id()
        position = rpg.player_position(entity_id)
        print(f"scene={name} player id={entity_id} at {position}")

        names = sorted(e["name"] for e in rpg.call("entity.list") if isinstance(e, dict))
        print("entities:", ", ".join(names))
        if "Elder" not in names:
            raise SystemExit("Elder is missing from the Camp scene")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        if not OUT.exists():
            raise SystemExit(f"screenshot not written: {OUT}")
        print(f"screenshot -> {OUT}")
        print("camp hub OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
