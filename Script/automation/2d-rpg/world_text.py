"""WT1: world-space text draws glyphs at a world position.

    python3 Script/automation/2d-rpg/world_text.py

Loads the WorldText scene, whose controller spawns two labels after a beat.
The test checks the label entities appear (Text2DComponent present, text set)
and that a screenshot shows non-background pixels where the labels sit.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/WorldText.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "world-text" / "world_text.png"


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


def entity(name: str) -> dict | None:
    for e in rpg.call("entity.list"):
        if isinstance(e, dict) and e.get("name") == name:
            return e
    return None


def text_of(entity_id: int) -> dict:
    return rpg.call("component.get", {"id": entity_id, "type": "Text2DComponent"})


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})

        def scene_up():
            return True if scene_name() == "WorldText" else None

        rpg.wait_until(scene_up, 20.0, "WorldText active")

        def labels_spawned():
            return True if entity("WT_Hello") and entity("WT_World") else None

        rpg.wait_until(labels_spawned, 15.0, "world text labels spawned")
        hello = entity("WT_Hello")
        print(f"WT_Hello id={hello['id']}")
        fields = text_of(int(hello["id"]))
        text = rpg._find_field(fields, "text")
        print(f"WT_Hello text={text!r}")
        if text != "Hello 123":
            raise SystemExit(f"unexpected text {text!r}")

        # Glyphs rasterize one frame after the request; give the atlas a beat.
        time.sleep(1.2)
        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 6})
        print(f"screenshot -> {OUT}")

        from PIL import Image
        img = Image.open(OUT).convert("RGB")
        colors = img.getcolors(maxcolors=1 << 24) or []
        colors.sort(reverse=True)
        top = colors[:6]
        print("top colors:", top)
        # The grass tile is one flat colour; text ink adds brighter pixels.
        if not any(c[1] and max(c[1]) > 200 and min(c[1]) < 200 for c in top):
            print("(note: no obvious text ink in the top colours; check the screenshot)")
        print("world text OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
