"""T3: a skill spends MP and damages the target it lands on.

    python3 Script/automation/2d-rpg/battle_skill_kill.py

Deterministic. The Mage's Firebolt (power 13) is cast at the nearest enemy in
range -- after the Mage moves up two cells that is the Ghost (distance 2), not
the Bat (distance 3). The test asserts the cast landed and the target lost HP,
and writes a screenshot.

Writes Engine/Saved/Automation/battle-skill/.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-skill" / "battle_skill.png"


def start_with_port() -> int:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "300"],
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
    s = rpg.call("scene.get_active")
    return s.get("name") if isinstance(s, dict) else None


def widget(name: str) -> dict:
    r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": name})
    return r if isinstance(r, dict) else {}


def row(i: int) -> str:
    return widget(f"Row{i}").get("text") or ""


def menu(i: int) -> str:
    return widget(f"Menu{i}").get("text") or ""


def active() -> str:
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith(">"):
            for name in ("Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"):
                if name in text:
                    return name
    return ""


def bat_row() -> str:
    for i in range(1, 7):
        if "Bat" in row(i):
            return row(i)
    return ""


def tap(key: str = "Space") -> None:
    rpg.call("input.inject_key", {"key": key, "action": "hold", "frames": 1})


def menu_select(label: str) -> None:
    for _ in range(4):
        sel = next((menu(i).strip().lstrip("> ").strip() for i in range(1, 4)
                    if menu(i).lstrip().startswith(">")), None)
        if sel == label:
            break
        tap("Down")
        time.sleep(0.18)


def end_turn() -> None:
    tap("Space")
    time.sleep(0.3)
    menu_select("Wait")
    tap("Space")
    time.sleep(0.3)


def hp_of(name: str) -> int:
    for i in range(1, 7):
        text = row(i)
        if name in text:
            part = text.split(name, 1)[1].strip()
            try:
                return int(part.split("/", 1)[0].strip())
            except ValueError:
                return -1
    return -1


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: scene_name() if scene_name() == "BattleRuins1" else None,
                       20.0, "BattleRuins1 active")
        rpg.wait_until(lambda: row(1).strip() or None, 20.0, "HUD ready")

        ghost_before = hp_of("Ghost")
        print(f"Ghost before: {ghost_before}")

        # End turns (Bat, then Archer) until the Mage is active.
        deadline = time.time() + 30.0
        while active() != "Mage" and time.time() < deadline:
            end_turn()
        if active() != "Mage":
            raise SystemExit("never reached the Mage's turn")

        # Mage starts at cell (10, 4). Move up twice to (10, 6), which puts the
        # Ghost (10, 7) at distance 1 and the Bat (8, 7) at distance 3 -- both
        # inside Firebolt's range 3; the nearest (Ghost) is snapped.
        for _ in range(2):
            tap("Up")
            time.sleep(0.2)
        tap("Space")            # move -> menu
        time.sleep(1.0)

        labels = [menu(i) for i in range(1, 4)]
        print(f"menu: {labels}")
        if not any("Firebolt" in m for m in labels):
            raise SystemExit(f"Firebolt missing from the menu: {labels}")

        menu_select("Firebolt")
        tap("Space")            # cast -> target phase
        time.sleep(0.5)
        tap("Space")            # confirm snapped target
        time.sleep(1.0)

        ghost_after = hp_of("Ghost")
        print(f"Ghost after: {ghost_after}")
        if ghost_after >= ghost_before or ghost_after < 0:
            raise SystemExit(f"Firebolt did not damage the Ghost ({ghost_before} -> {ghost_after})")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        print(f"screenshot -> {OUT}")
        print("battle skill OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
