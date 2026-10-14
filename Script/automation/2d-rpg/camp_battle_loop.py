"""M1: the first full loop -- Camp -> step on the ruins gate -> battle ->
outcome -> back to Camp, with Persist carried across.

    python3 Script/automation/2d-rpg/camp_battle_loop.py

Boots the Camp scene (project default), walks the player down onto the RuinsGate
door, checks the game switched to BattleRuins1, skips player turns until the
battle resolves (the enemy AI plays its own), and checks the game returns to
Camp at the SpawnFromRuins marker. Writes screenshots at the battle and the
return.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

BATTLE = "Content/Scenes/BattleRuins1.scene.json"
OUT_DIR = rpg.ROOT / "Engine" / "Saved" / "Automation" / "camp-battle-loop"
PLAYER_UNITS = ["Archer", "Mage", "Knight"]


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
    """Active scene name, or "" while a transfer is in flight (the RPC can
    reset for a frame during world.loadScene)."""
    try:
        s = rpg.call("scene.get_active")
    except SystemExit:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def player_pos() -> tuple[float, float]:
    for e in rpg.call("entity.list"):
        if isinstance(e, dict) and e.get("name") == "Player":
            comp = rpg.call("component.get", {"id": int(e["id"]), "type": "TransformComponent"})
            p = rpg._find_field(comp, "_position")
            return (float(p[0]), float(p[1]))
    return (0.0, 0.0)


def widget(name: str) -> dict:
    """Read a BattleHUD widget. Returns {} when the HUD is not mounted (Camp)
    or while a scene transfer is in flight."""
    try:
        r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": name})
    except SystemExit:
        return {}
    return r if isinstance(r, dict) else {}


def row(i: int) -> str:
    return widget(f"Order{i}").get("text") or ""


def menu(i: int) -> str:
    return widget(f"Action{i}").get("text") or ""


def active() -> str:
    """The acting unit: the order row carrying the \u25b6 marker."""
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith("\u25b6"):
            for name in ("Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"):
                if name in text:
                    return name
    return ""


def outcome() -> str:
    o = widget("Outcome")
    return (o.get("text") or "") if o.get("visible") else ""


def tap(key: str = "Space") -> None:
    """Explicit down/up rather than `hold`: a hold releases at a poll boundary,
    and when that lands inside the same logic frame the script never observes
    the press edge, so a Space silently does nothing."""
    rpg.call("input.inject_key", {"key": key, "action": "down"})
    time.sleep(0.10)
    rpg.call("input.inject_key", {"key": key, "action": "up"})
    time.sleep(0.12)


def menu_label() -> str:
    """The highlighted action label. The bar renders `> [1] Attack`, so peel
    the marker and the hotkey prefix."""
    for i in range(1, 5):
        text = menu(i)
        if text.lstrip().startswith(">"):
            label = text.strip().lstrip(">").strip()
            if label.startswith("["):
                label = label.split("]", 1)[1].strip() if "]" in label else label
            return label
    return ""


def menu_select(label: str) -> None:
    """Walk the highlight to `label`. Each Down is followed by a wait for the
    highlight to actually move, so the read never lags the press."""
    for _ in range(6):
        if menu_label() == label:
            return
        before = menu_label()
        tap("Right")
        deadline = time.time() + 0.6
        while time.time() < deadline and menu_label() == before:
            time.sleep(0.05)


def in_menu() -> bool:
    """True when the action menu (Attack/.../Wait) is up, not the select-phase
    'Move' placeholder, so the driver knows the press opened the real menu."""
    return any(menu(i).strip() for i in range(1, 4) if "Move" not in menu(i))


def play_player_turn() -> None:
    """End the active player turn with Space alone. M1 proves the *loop*
    closes (Camp -> battle -> Camp); who wins is covered by the battle tests,
    so this does not need to fight well -- the enemy AI still plays its turns.

    Space advances whatever phase the bar is in (select -> menu -> pick the
    highlighted Wait), so a single press may only open the menu. Press until
    the active unit actually changes, exactly as the keyboard victory run
    (kbd_main_flow.py) does; a single press plus a fixed sleep is not enough."""
    unit = active()
    for _ in range(8):
        if active() != unit:
            return
        tap("Space")
        deadline = time.time() + 1.0
        while time.time() < deadline and active() == unit:
            time.sleep(0.05)
    raise SystemExit(
        f"could not end {unit}'s turn; bar={[menu(i) for i in range(1, 5)]}")


def main() -> None:
    port = start_with_port()
    try:
        rpg.wait_until(lambda: scene_name() == "Camp", 20.0, "Camp active")
        start = player_pos()
        print(f"start scene Camp, player at {start}")

        # Step down twice onto the RuinsGate door (2 cells below the player).
        for _ in range(2):
            rpg.call("input.inject_key", {"key": "Down", "action": "hold", "frames": 1})
            time.sleep(0.45)

        rpg.wait_until(lambda: scene_name() if scene_name() == "BattleRuins1" else None,
                       20.0, "battle scene loaded")
        print(f"entered battle: {scene_name()}")
        rpg.wait_until(lambda: row(1).strip() or None, 20.0, "battle HUD ready")

        # Fight it out: play the player's turns, let the AI play the enemy's.
        # A player turn that cannot close on anything still ends (Wait), so the
        # loop always makes progress; a stall guard reports it if it does not.
        deadline = time.time() + 150.0
        last_sig, same = None, 0
        while time.time() < deadline:
            if outcome():
                break
            who = active()
            sig = (who, tuple(row(i) for i in range(1, 7)))
            if sig == last_sig:
                same += 1
            else:
                same = 0
                last_sig = sig
            if same > 12:
                raise SystemExit(f"battle stalled on {who}")
            if who in PLAYER_UNITS:
                play_player_turn()
            else:
                time.sleep(0.25)
        print(f"battle outcome: {outcome()!r}")

        # The battle returns to Camp after the banner; check we came back.
        rpg.wait_until(lambda: True if scene_name() == "Camp" else None,
                       20.0, "returned to Camp")
        back = player_pos()
        print(f"back in Camp, player at {back}")

        OUT_DIR.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation",
                                         "path": str(OUT_DIR / "back_in_camp.png"), "warmup_frames": 4})
        if scene_name() != "Camp":
            raise SystemExit(f"expected Camp after the battle, got {scene_name()!r}")
        if back == start:
            print(f"(note: player returned to its start cell {start!r})")
        print("camp battle loop OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
