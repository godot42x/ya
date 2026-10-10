"""Drive GreedySnake: hit the wall, restart, pause in settings, then move again."""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROJECT = "Example/GreedySnake/GreedySnake.yaproject"


def _ya(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(ROOT / "Script" / "ya.py"), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )


def start() -> int:
    stop()
    result = _ya(
        "control", "start",
        "--project", PROJECT,
        "--game",
        "--lifetime", "240",
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    sys.stderr.write(result.stdout)
    for line in result.stdout.splitlines():
        if "control port " in line:
            return int(line.split("control port ", 1)[1].split()[0])
    raise SystemExit(f"control start did not publish a port:\n{result.stdout}")


def rpc(port: int, method: str, params: dict | None = None, timeout: float = 30.0) -> dict:
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
    if not data:
        raise SystemExit(f"{method} returned an empty automation response")
    response = json.loads(data.split(b"\n", 1)[0].decode("utf-8"))
    if not response.get("ok"):
        raise SystemExit(f"{method} failed: {response.get('error')}")
    result = response.get("result")
    return result if isinstance(result, dict) else {}


def stop() -> None:
    result = _ya("control", "stop", "--project", PROJECT, "--game")
    sys.stderr.write(result.stdout)
    if result.stderr:
        sys.stderr.write(result.stderr)


def call(method: str, params: dict | None = None) -> object:
    result = _ya(
        "control", "call",
        "--project", PROJECT,
        "--game",
        method,
        json.dumps(params or {}),
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise SystemExit(f"{method} failed: {detail}")
    return json.loads(result.stdout)


def wait_until(predicate, timeout: float, what: str):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = predicate()
        if last:
            return last
        time.sleep(0.05)
    raise SystemExit(f"timed out waiting for {what}; last={last!r}")


def query(entry: str, widget: str | None = None) -> dict:
    params: dict = {"entry": entry}
    if widget is not None:
        params["widget"] = widget
    result = call("ui.query", params)
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query returned {result!r}")
    return result


def heads() -> list[dict]:
    entities = call("entity.list")
    if not isinstance(entities, list):
        raise SystemExit(f"entity.list returned {type(entities).__name__}")
    return [entity for entity in entities if isinstance(entity, dict) and entity.get("name") == "SnakeHead"]


def head_x() -> float | None:
    found = heads()
    if len(found) != 1:
        return None
    component = call("component.get", {"id": int(found[0]["id"]), "type": "TransformComponent"})

    def find_position(node: object) -> list | None:
        if isinstance(node, dict):
            position = node.get("_position")
            if isinstance(position, list) and len(position) >= 1:
                return position
            for value in node.values():
                found_position = find_position(value)
                if found_position is not None:
                    return found_position
        return None

    position = find_position(component)
    if position is None:
        raise SystemExit(f"SnakeHead has no _position: {component}")
    return float(position[0])


def main() -> None:
    port = start()
    try:
        def score_ready():
            body = query("HUD", "Score")
            text = body.get("text") or ""
            if body.get("visible") and text.startswith("Score:"):
                return text
            return None

        wait_until(score_ready, 15.0, "HUD score")
        call("input.inject_key", {"key": "Right", "action": "hold", "frames": 1})

        def game_over():
            title = query("GameOver", "Title")
            if title.get("visible") and title.get("text") == "Game Over":
                return title
            return None

        wait_until(game_over, 12.0, "game over")
        shot = Path(tempfile.gettempdir()) / "greedy-snake-game-over.png"
        # Raw control RPC. `ya.py control call` only forwards script commands.
        rpc(port, "capture_screenshot", {"path": str(shot), "target": "presentation", "warmup_frames": 2})
        if not shot.is_file() or shot.stat().st_size == 0:
            raise SystemExit(f"game over screenshot missing: {shot}")
        print(f"game over screenshot: {shot}")

        call("ui.click", {"entry": "GameOver", "widget": "Restart"})

        def restarted():
            score = query("HUD", "Score")
            panel = query("GameOver")
            if score.get("text") == "Score: 0" and not panel.get("visible") and head_x() is not None:
                return score["text"]
            return None

        wait_until(restarted, 5.0, "score back to 0")

        call("input.inject_key", {"key": "Escape", "action": "hold", "frames": 1})

        def settings_open():
            panel = query("Settings")
            if panel.get("visible"):
                return panel
            return None

        wait_until(settings_open, 3.0, "settings")
        held = head_x()
        if held is None:
            raise SystemExit("no single SnakeHead while settings is open")
        time.sleep(0.8)
        if head_x() != held:
            raise SystemExit(f"snake moved while settings paused the game: {held} -> {head_x()}")

        call("ui.click", {"entry": "Settings", "widget": "Fast"})
        speed = query("Settings", "SpeedValue")
        if speed.get("text") != "Speed: Fast":
            raise SystemExit(f"speed label is {speed!r}")

        call("ui.click", {"entry": "Settings", "widget": "Resume"})

        def settings_closed():
            panel = query("Settings")
            return panel if not panel.get("visible") else None

        wait_until(settings_closed, 3.0, "settings closed")

        def moved():
            x = head_x()
            if x is not None and x > held + 0.5:
                return x
            return None

        arrived = wait_until(moved, 3.0, "snake moving after resume")
        print(f"resumed: head x {held} -> {arrived}")
    finally:
        stop()


if __name__ == "__main__":
    main()
