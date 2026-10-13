#!/usr/bin/env python3
"""Generate the tactics-RPG overworld hub (Camp) and battle maps.

Deterministic (fixed seeds), so maps are reproducible and reviewable:
    python3 Example/2DRpgPrototype/Tools/make_maps.py
writes Content/Scenes/Camp.scene.json and Content/Scenes/BattleRuins1.scene.json.

The editor can open and edit the result; running this again overwrites it.
Layer cells are written one map-row per line so the arrays stay reviewable.

Atlases
    Camp        : tiny_town.png    (12x11, grass / tree / fence / sign) -- Tilesets/town.yatileset.json
    BattleRuins : tiny_dungeon.png (12x11, stone floor / wall / crate)  -- Tilesets/dungeon.yatileset.json

Cell values are tile + 1 (0 = empty).
"""
from __future__ import annotations

import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # Example/2DRpgPrototype

TOWN_GRASS = 0
TOWN_BUSH, TOWN_MUSHROOM = 1, 2
TOWN_TREE_CANOPY, TOWN_TREE_TRUNK = 4, 16
TOWN_SIGN = 5
TOWN_FENCE = 81
TOWN_TILESET = "Content:Tilesets/town.yatileset.json"
TOWN_ATLAS = "Content:Textures/tiny_town.png"

DUN_FLOOR = 48
DUN_WALL = 14
DUN_CRATE = 58
DUN_DOOR = 45   # closed door panel (a door tile in the dungeon atlas)
DUN_TILESET = "Content:Tilesets/dungeon.yatileset.json"
DUN_ATLAS = "Content:Textures/tiny_dungeon.png"

CHAR_ELDER = 85
CHAR_MAGE = 84
CHAR_KNIGHT = 96
CHAR_ARCHER = 98
MON_SLIME = 108
MON_BAT = 120
MON_GHOST = 121

HERO_SHEET = "Content:Textures/hero_walk.png"
HERO_ANIMATION = "Content/Animations/Hero.yaanim.json"
NPC_ANIMATION = "Content/Animations/Npc.yaanim.json"
STAND_DOWN_FRAME = (1 / 3, 0.0, 2 / 3, 0.25)
CHARACTER_PIVOT = (0.5, 1 / 3)
WHITE = (1.0, 1.0, 1.0, 1.0)


def tile_uv(index: int, columns: int = 12, rows: int = 11) -> tuple[float, float, float, float]:
    """uvRect (u0, v0, u1, v1) of one atlas cell. Image space, v down.

    Sprite2DWorld samples `lerp(uvRect.xy, uvRect.zw, quadUv)`, so the rect is
    two corners, not (origin, size).
    """
    col, row = index % columns, index // columns
    return (col / columns, row / rows, (col + 1) / columns, (row + 1) / rows)


# ── emitters ──────────────────────────────────────────────────────────────

def map_origin(width: int, height: int) -> tuple[float, float]:
    return (-(width / 2.0), -(height / 2.0))


def cell_world(origin: tuple[float, float], cx: int, cy: int, z: float) -> list[float]:
    return [origin[0] + cx + 0.5, origin[1] + cy + 0.5, z]


def camera_entity(cell: tuple[int, int], origin: tuple[float, float],
                  script: str | None = "Content/Scripts/FollowCamera.lua") -> dict:
    components: dict = {
        "TransformComponent": {
            "_position": cell_world(origin, cell[0], cell[1], 24.0),
            "_rotation": [0.0, 0.0, 0.0],
            "_scale": [1.0, 1.0, 1.0],
        },
        "CameraComponent": {
            "bPrimary": True,
            "_projection": "Orthographic",
            "_orthoHalfHeight": 8.0,
            "_pixelPerfect": True,
            "_pixelsPerUnit": 16.0,
            "_referenceHeightPx": 192.0,
            "_fov": 45.0,
            "_aspectRatio": 1.7777778,
            "_nearClip": 0.1,
            "_farClip": 100.0,
            "_fixedAspectRatio": False,
            "_distance": 24.0,
            "_focusPoint": [0.0, 0.0, 0.0],
        },
    }
    if script:
        components["LuaScriptComponent"] = {"scripts": [{"enabled": True, "scriptPath": script}]}
    return {"id": 1001, "name": "Camera", "components": components}


def tilemap_entity(entity_id: int, name: str, atlas: str, tileset: str, width: int, height: int,
                   origin: tuple[float, float], layers: list[dict]) -> dict:
    return {
        "id": entity_id,
        "name": name,
        "components": {
            "TransformComponent": {
                "_position": [origin[0], origin[1], 0.0],
                "_rotation": [0.0, 0.0, 0.0],
                "_scale": [1.0, 1.0, 1.0],
            },
            "TilemapComponent": {
                "tileset": {"__base__": {"AssetRefBase": {"_path": tileset}}},
                "cellSize": [1.0, 1.0],
                "width": width,
                "height": height,
                "layers": layers,
                "layer": 0,
            },
        },
    }


def layer(name: str, cells: list[int], z_offset: float = 0.0,
          layer_offset: int | None = None, b_y_sort: bool = False) -> dict:
    entry: dict = {"name": name, "zOffset": z_offset, "cells": cells}
    if layer_offset is not None:
        entry["layerOffset"] = layer_offset
    if b_y_sort:
        entry["bYSort"] = True
    return entry


def sprite_entity(entity_id: int, name: str, position: list[float], texture: str,
                  uv_rect: tuple[float, float, float, float], size: tuple[float, float],
                  tint: tuple[float, float, float, float], script: str,
                  animation: dict | None = None, pivot: tuple[float, float] | None = None,
                  b_y_sort: bool = False, props: dict | None = None) -> dict:
    sprite = {
        "bVisible": True,
        "image": {
            "bEnable": True,
            "samplerConfig": {"addressMode": "ClampToEdge", "filterMode": "Nearest"},
            "textureRef": {"__base__": {"AssetRefBase": {"_path": texture}}},
            "uvOffset": [0.0, 0.0],
            "uvRotation": 0.0,
            "uvScale": [1.0, 1.0],
        },
        "size": list(size),
        **({"pivot": list(pivot)} if pivot is not None else {}),
        "uvRect": list(uv_rect),
        "bFlipU": False,
        "bFlipV": False,
        "tint": list(tint),
        "layer": 0,
        "sortOrder": 0,
        **({"bYSort": True} if b_y_sort else {}),
        "pickId": 0,
    }
    script_entry: dict = {"enabled": True, "scriptPath": script}
    if props:
        script_entry["propertyOverrides"] = props
    components: dict = {
        "TransformComponent": {
            "_position": list(position),
            "_rotation": [0.0, 0.0, 0.0],
            "_scale": [1.0, 1.0, 1.0],
        },
        "Sprite2DComponent": sprite,
    }
    if animation is not None:
        components["SpriteAnimationComponent"] = animation
    components["LuaScriptComponent"] = {"scripts": [script_entry]}
    return {"id": entity_id, "name": name, "components": components}


def hero_animation(walks: bool) -> dict:
    return {
        "animation": {"__base__": {"AssetRefBase": {"_path": HERO_ANIMATION if walks else NPC_ANIMATION}}},
        "clip": "idle_down",
    }


# ── maps ──────────────────────────────────────────────────────────────────

CAMP_W, CAMP_H = 28, 20
BAT_W, BAT_H = 16, 12


def build_camp() -> dict:
    rng = random.Random(20261013)
    origin = map_origin(CAMP_W, CAMP_H)
    ground = [TOWN_GRASS + 1] * (CAMP_W * CAMP_H)
    decor = [0] * (CAMP_W * CAMP_H)
    overlay = [0] * (CAMP_W * CAMP_H)

    def put(layer_cells: list[int], x: int, y: int, tile: int) -> None:
        layer_cells[y * CAMP_W + x] = tile + 1

    for x in range(1, CAMP_W - 1):
        put(decor, x, 1, TOWN_FENCE)
        put(decor, x, CAMP_H - 2, TOWN_FENCE)
    for y in range(1, CAMP_H - 1):
        put(decor, 1, y, TOWN_FENCE)
        put(decor, CAMP_W - 2, y, TOWN_FENCE)

    spawn = (CAMP_W // 2, 4)
    # Tiny Town trees are two tiles stacked vertically: a trunk at a cell and
    # the canopy one cell above it (world +y). The trunk goes on Decor (behind
    # the player), the canopy on Overlay (covering the player), so the player
    # walks behind the crown.
    for _ in range(22):
        x, y = rng.randrange(3, CAMP_W - 3), rng.randrange(4, CAMP_H - 3)
        if abs(x - spawn[0]) <= 3 and abs(y - spawn[1]) <= 3:
            continue
        if decor[y * CAMP_W + x] != 0 or decor[(y + 1) * CAMP_W + x] != 0:
            continue
        put(decor, x, y, TOWN_TREE_TRUNK)
        put(overlay, x, y + 1, TOWN_TREE_CANOPY)

    for _ in range(14):
        x, y = rng.randrange(3, CAMP_W - 3), rng.randrange(3, CAMP_H - 3)
        if decor[y * CAMP_W + x] == 0:
            put(decor, x, y, TOWN_BUSH if rng.random() < 0.7 else TOWN_MUSHROOM)

    put(decor, spawn[0] - 3, spawn[1], TOWN_SIGN)
    # The ruins gate: a walkable doorway a couple of cells from the player, so
    # the player walks onto it to enter the battle (M1). Its return marker sits
    # just below it, where the battle sends the player back.
    gate_open = (spawn[0], spawn[1] - 2)
    put(decor, gate_open[0], gate_open[1], DUN_DOOR)
    return_cell = (gate_open[0], gate_open[1] - 1)

    entities: list[dict] = [
        camera_entity(spawn, origin),
        sprite_entity(1021, "Player", cell_world(origin, spawn[0], spawn[1], 0.1),
                      HERO_SHEET, STAND_DOWN_FRAME, (1.0, 1.5), WHITE,
                      "Content/Scripts/Player.lua", hero_animation(True), CHARACTER_PIVOT, True),
        sprite_entity(1030, "Elder", cell_world(origin, spawn[0] - 1, spawn[1] + 1, 0.1),
                      DUN_ATLAS, tile_uv(CHAR_ELDER), (1.0, 1.0), WHITE,
                      "Content/Scripts/Elder.lua", None, None, True),
        # A walkable doorway into the ruins, driven by the overworld Door script.
        sprite_entity(1040, "RuinsGate", cell_world(origin, gate_open[0], gate_open[1], 0.05),
                      DUN_ATLAS, tile_uv(DUN_DOOR), (1.0, 1.0), WHITE,
                      "Content/Scripts/Door.lua", None, None, False,
                      {"targetScene": "Content/Scenes/BattleRuins1.scene.json",
                       "spawnName": "", "actorName": ""}),
        # Where the battle returns the player, just south of the gate.
        marker_entity(1050, "SpawnFromRuins", cell_world(origin, return_cell[0], return_cell[1], 0.1)),
        tilemap_entity(2000, "TilemapGround", TOWN_ATLAS, TOWN_TILESET, CAMP_W, CAMP_H, origin,
                       [layer("Ground", ground), layer("Decor", decor, 0.03),
                        layer("Overlay", overlay, 0.2, layer_offset=1)]),
    ]
    return {"name": "Camp", "width": CAMP_W, "height": CAMP_H, "entities": entities,
            "layers": [ground, decor, overlay]}


def marker_entity(entity_id: int, name: str, position: list[float]) -> dict:
    """A named, empty Node3D used as a spawn marker (nothing but a transform)."""
    return {
        "id": entity_id,
        "name": name,
        "components": {
            "TransformComponent": {"_position": list(position), "_rotation": [0.0, 0.0, 0.0],
                                   "_scale": [1.0, 1.0, 1.0]},
        },
    }


def script_entity(entity_id: int, name: str, script: str, props: dict | None = None) -> dict:
    """An entity that only carries a script (no sprite): controllers, markers."""
    entry: dict = {"enabled": True, "scriptPath": script}
    if props:
        entry["propertyOverrides"] = props
    return {
        "id": entity_id,
        "name": name,
        "components": {
            "TransformComponent": {"_position": [0.0, 0.0, 0.0], "_rotation": [0.0, 0.0, 0.0],
                                   "_scale": [1.0, 1.0, 1.0]},
            "LuaScriptComponent": {"scripts": [entry]},
        },
    }


def build_battle_ruins1() -> dict:
    origin = map_origin(BAT_W, BAT_H)
    ground = [DUN_FLOOR + 1] * (BAT_W * BAT_H)
    decor = [0] * (BAT_W * BAT_H)

    def put(x: int, y: int, tile: int) -> None:
        decor[y * BAT_W + x] = tile + 1

    for x in range(BAT_W):
        put(x, 0, DUN_WALL)
        put(x, BAT_H - 1, DUN_WALL)
    for y in range(BAT_H):
        put(0, y, DUN_WALL)
        put(BAT_W - 1, y, DUN_WALL)
    put(BAT_W // 2, BAT_H - 1, 0)  # south entrance

    # Side pillars leave the middle open, so the two lines start in contact
    # range of a move-plus-attack turn.
    for y in range(2, 5):
        put(3, y, DUN_WALL)
        put(BAT_W - 4, y, DUN_WALL)
    for y in range(7, 10):
        put(3, y, DUN_WALL)
        put(BAT_W - 4, y, DUN_WALL)
    for cx, cy in ((6, 5), (9, 6)):
        put(cx, cy, DUN_CRATE)

    # Party on row 4, enemies on row 7: three cells apart, so the first mover
    # can close and strike in one turn. Combat in T3/T4 wants contact.
    spawn = (BAT_W // 2, 4)
    enemy_row = 7
    board_center = (BAT_W // 2, BAT_H // 2)
    entities: list[dict] = [
        # Frame the whole board: the battle camera looks at the board centre,
        # not the party spawn, so both sides are on screen. No follow script --
        # the board is static and there is no "Player" entity here.
        camera_entity(board_center, origin, script=None),
        # The turn controller. It owns no unit list; it finds units through the
        # board (map:entityAt) and reads their own scripts. On an outcome it
        # returns the player to Camp (M1).
        script_entity(1002, "Battle", "Content/Scripts/Battle.lua",
                      {"returnScene": "Content/Scenes/Camp.scene.json",
                       "returnSpawn": "SpawnFromRuins", "returnActor": "Player"}),
    ]
    # name, tile, cell, tint, stats. Skill fields: skillName/skillPower/skillCost/
    # skillRange + maxMp; an empty skillName hides the skill menu entry.
    party_row = spawn[1]
    party = [
        ("UnitKnight", CHAR_KNIGHT, (spawn[0] - 2, party_row), WHITE,
         {"team": "player", "displayName": "Knight", "maxHp": 30, "atk": 7, "def": 3, "spd": 8, "move": 3, "range": 1}),
        ("UnitArcher", CHAR_ARCHER, (spawn[0], party_row), (0.8, 1.0, 0.8, 1.0),
         {"team": "player", "displayName": "Archer", "maxHp": 20, "atk": 6, "def": 1, "spd": 12, "move": 3, "range": 3,
          "maxMp": 8, "skillName": "Pierce", "skillPower": 10, "skillCost": 3, "skillRange": 4}),
        ("UnitMage", CHAR_MAGE, (spawn[0] + 2, party_row), (0.8, 0.85, 1.0, 1.0),
         {"team": "player", "displayName": "Mage", "maxHp": 18, "atk": 5, "def": 0, "spd": 10, "move": 2, "range": 2,
          "maxMp": 12, "skillName": "Firebolt", "skillPower": 13, "skillCost": 4, "skillRange": 3}),
    ]
    for i, (uname, tile, cell, tint, stats) in enumerate(party):
        entities.append(sprite_entity(1100 + i, uname, cell_world(origin, cell[0], cell[1], 0.1),
                                      DUN_ATLAS, tile_uv(tile), (1.0, 1.0), tint,
                                      "Content/Scripts/Unit.lua", None, None, True, stats))
    enemies = [
        ("EnemySlime", MON_SLIME, (spawn[0] - 2, enemy_row),
         {"team": "enemy", "displayName": "Slime", "maxHp": 14, "atk": 4, "def": 0, "spd": 6, "move": 2, "range": 1}),
        ("EnemyBat", MON_BAT, (spawn[0], enemy_row),
         {"team": "enemy", "displayName": "Bat", "maxHp": 10, "atk": 5, "def": 0, "spd": 14, "move": 4, "range": 1}),
        ("EnemyGhost", MON_GHOST, (spawn[0] + 2, enemy_row),
         {"team": "enemy", "displayName": "Ghost", "maxHp": 16, "atk": 6, "def": 1, "spd": 9, "move": 2, "range": 2}),
    ]
    for i, (ename, tile, cell, stats) in enumerate(enemies):
        entities.append(sprite_entity(1200 + i, ename, cell_world(origin, cell[0], cell[1], 0.1),
                                      DUN_ATLAS, tile_uv(tile), (1.0, 1.0), WHITE,
                                      "Content/Scripts/Unit.lua", None, None, True, stats))
    entities.append(tilemap_entity(2000, "TilemapGround", DUN_ATLAS, DUN_TILESET, BAT_W, BAT_H, origin,
                                   [layer("Ground", ground), layer("Decor", decor, 0.03)]))
    return {"name": "BattleRuins1", "width": BAT_W, "height": BAT_H, "entities": entities,
            "layers": [ground, decor], "widgets": BATTLE_WIDGETS}


# ── scene root + write ────────────────────────────────────────────────────

# (entryId, zOrder) mounted per scene. Battle scenes add the turn HUD; every
# scene gets the fade overlay on top (zOrder 90), which transitions drive.
OVERWORLD_WIDGETS = [("Dialogue", 10), ("Inventory", 20), ("Journal", 30),
                     ("QuestNotice", 40), ("Settings", 50), ("FadeOverlay", 90)]
BATTLE_WIDGETS = OVERWORLD_WIDGETS + [("BattleHUD", 5)]


def fullscreen_slot() -> dict:
    return {
        "type": "canvas",
        "anchorMin": [0.0, 0.0], "anchorMax": [1.0, 1.0],
        "offset": [0.0, 0.0], "minSize": [0.0, 0.0], "maxSize": [1000000.0, 1000000.0],
        "offsets": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
        "alignmentH": 0, "alignmentV": 0, "widthSizeMode": 0, "heightSizeMode": 0,
        "pivot": [0.0, 0.0], "preferredSize": [0.0, 0.0], "fixedSize": [0.0, 0.0],
    }


def write_scene(path: Path, map_data: dict) -> None:
    entities = map_data["entities"]
    root = {
        "name": map_data["name"],
        "entities": entities,
        "nodeTree": {"name": "scene_root",
                     "children": [{"name": e["name"], "entityRef": e["id"]} for e in entities]},
        "widgetEntries": [
            {"entryId": eid, "zOrder": z, "autoMount": True,
             "document": f"Content:UI/{eid}.yaui.json", "rootSlot": fullscreen_slot()}
            for eid, z in map_data.get("widgets", OVERWORLD_WIDGETS)
        ],
    }
    tilemap = next(e for e in entities if "TilemapComponent" in e["components"])
    cells_by_layer = map_data["layers"]
    for i, layer_dict in enumerate(tilemap["components"]["TilemapComponent"]["layers"]):
        layer_dict["cells"] = f"@@CELLS{i}@@"

    text = json.dumps(root, indent=2)
    width = map_data["width"]
    for i, cells in enumerate(cells_by_layer):
        rows = [", ".join(str(v) for v in cells[r:r + width]) for r in range(0, len(cells), width)]
        inline = "[\n        " + ",\n        ".join(rows) + "\n      ]"
        text = text.replace(f'"@@CELLS{i}@@"', inline)
    path.write_text(text + "\n", encoding="utf-8")
    print(f"wrote {path}")


def build_world_text_scene() -> dict:
    """A minimal scene for the WT1 (world-space text) and R1 (post-processing)
    tests: a grass tilemap, a camera, a controller that spawns world labels, and
    a controller that cycles post-processing looks."""
    w, h = 16, 12
    origin = map_origin(w, h)
    ground = [TOWN_GRASS + 1] * (w * h)
    spawn = (w // 2, h // 2)
    entities: list[dict] = [
        camera_entity(spawn, origin, script=None),
        script_entity(1002, "TextDemo", "Content/Scripts/WorldTextTest.lua"),
        script_entity(1003, "LookDemo", "Content/Scripts/LookDemo.lua"),
        tilemap_entity(2000, "TilemapGround", TOWN_ATLAS, TOWN_TILESET, w, h, origin,
                       [layer("Ground", ground)]),
    ]
    return {"name": "WorldText", "width": w, "height": h, "entities": entities,
            "layers": [ground]}


def build_fade_scene() -> dict:
    """A minimal scene for the fade-overlay test: a tile floor, a camera, and a
    controller that drives the FadeOverlay entry (post-processing is left
    neutral so the fade's brightness change is unambiguous)."""
    w, h = 16, 12
    origin = map_origin(w, h)
    ground = [TOWN_GRASS + 1] * (w * h)
    spawn = (w // 2, h // 2)
    entities: list[dict] = [
        camera_entity(spawn, origin, script=None),
        script_entity(1004, "FadeDemo", "Content/Scripts/FadeDemo.lua"),
        tilemap_entity(2000, "TilemapGround", TOWN_ATLAS, TOWN_TILESET, w, h, origin,
                       [layer("Ground", ground)]),
    ]
    return {"name": "FadeScene", "width": w, "height": h, "entities": entities,
            "layers": [ground]}


def main() -> None:
    write_scene(ROOT / "Content/Scenes/Camp.scene.json", build_camp())
    write_scene(ROOT / "Content/Scenes/BattleRuins1.scene.json", build_battle_ruins1())
    write_scene(ROOT / "Content/Scenes/WorldText.scene.json", build_world_text_scene())
    write_scene(ROOT / "Content/Scenes/FadeScene.scene.json", build_fade_scene())
    print(f"Camp {CAMP_W}x{CAMP_H}, BattleRuins1 {BAT_W}x{BAT_H}, WorldText 16x12")


if __name__ == "__main__":
    main()
