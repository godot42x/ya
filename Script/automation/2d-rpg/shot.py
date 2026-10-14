"""Screenshot helper for the 2D RPG: capture a frame and tell whether it has content.

The compose path can deliver an all-black frame under automation (capture timing
vs present); a black frame must be retried, not reported as visual evidence.

    import shot
    p = shot.capture(port, "Engine/Saved/Automation/x/shot.png")   # retries until non-black
    shot.stats(p)  # {'luma_std': .., 'is_black': ..}
"""
from __future__ import annotations

import json
import socket
import time
from pathlib import Path

from PIL import Image, ImageStat


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
    return response.get("result") or {}


def stats(path: str | Path) -> dict:
    image = Image.open(path).convert("RGB")
    stat = ImageStat.Stat(image.convert("L"))
    return {
        "path": str(path),
        "size": image.size,
        "luma_mean": stat.mean[0],
        "luma_std": stat.stddev[0],
        "is_black": stat.stddev[0] < 1.0 and stat.mean[0] < 1.0,
    }


def _opaque(path: Path) -> None:
    """The engine's capture writes RGBA with alpha 0 (the presentation target
    has no meaningful alpha); viewers render that fully transparent. Re-save as
    RGB so the file is viewable as-is."""
    image = Image.open(path)
    if image.mode != "RGB":
        image.convert("RGB").save(path)

def capture(port: int, path: str | Path, target: str = "presentation",
            warmup_frames: int = 4, attempts: int = 6) -> Path:
    """Capture until the frame has content; returns the path. Raises when every
    attempt is black (that is a real finding, not something to silently accept).
    `target='viewport'` reads the scene view instead of the composed frame.
    The file is rewritten as opaque RGB so it is viewable directly."""
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    last = None
    for attempt in range(attempts):
        rpc(port, "capture_screenshot", {"target": target, "path": str(out), "warmup_frames": warmup_frames})
        # The write happens on frame completion; give it a moment.
        time.sleep(0.35)
        if not out.exists():
            continue
        _opaque(out)
        last = stats(out)
        if not last["is_black"]:
            return out
        time.sleep(0.4)
    raise SystemExit(f"capture stayed black after {attempts} attempts: {last}")
