#!/usr/bin/env python3
"""Generate the R4 scale scene: 64x64, three layers, 20 NPCs.

Deterministic (fixed seed), so the measurement fixture is reproducible:
    python3 Example/2DRpgPrototype/Tools/make_scale_scene.py
writes Content/Scenes/TownLarge.scene.json.

It is a measurement fixture, not a hand-authored map: the editor can open and
edit it, and running this script again overwrites it. Layer cells are written
inline (one line per layer) so the 64x64 arrays stay reviewable; everything
else is ordinary indented JSON.

Tile indices are Tiny Town indices; a cell value is tile + 1 (0 = empty).
"""
from __future__ import annotations

import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # Example/2DRpgPrototype
SCENE = ROOT / "Content/Scenes/TownLarge.scene.json"

WIDTH, HEIGHT = 64, 64
NPC_COUNT = 20
# Overridable for the scale curve (see main): the committed fixture is 64x64
# with 20 NPCs, a smaller map is generated on demand for the same measurement.
_CURRENT_SIZE = [WIDTH, HEIGHT]
_CURRENT_NPCS = [NPC_COUNT]

# Tile indices in tiny_town.png (12x11 atlas).
GRASS = 0
BUSH, MUSHROOM = 1, 2
TREE_CANOPY, TREE_TRUNK = 4, 16
SIGN = 5
FENCE = 81

CLEAR_RADIUS = 2

HERO_SHEET = "Content:Textures/hero_walk.png"
ATLAS_TILESET = "Content:Tilesets/town.yatileset.json"
STAND_DOWN_FRAME = (1 / 3, 0.0, 2 / 3, 0.25)

# hero_walk.png is a 3x4 grid (left foot, stand, right foot; down, left, right,
# up). The clips live in Content/Animations, not in the scene. Player walks;
# NPCs only idle. Both start on idle_down.
HERO_ANIMATION = "Content:Animations/Hero.yaanim.json"
NPC_ANIMATION = "Content:Animations/Npc.yaanim.json"


def hero_animation(walks: bool) -> dict:
    return {
        "animation": {"__base__": {"AssetRefBase": {"_path": HERO_ANIMATION if walks else NPC_ANIMATION}}},
        "clip": "idle_down",
    }


def player_cell() -> tuple[int, int]:
    return (WIDTH // 2, HEIGHT // 2)


def in_clear_zone(x: int, y: int) -> bool:
    centre = player_cell()
    return abs(x - centre[0]) <= CLEAR_RADIUS and abs(y - centre[1]) <= CLEAR_RADIUS


def build_layers() -> tuple[list[int], list[int], list[int]]:
    rng = random.Random(20260930)
    ground = [GRASS + 1] * (WIDTH * HEIGHT)
    decor = [0] * (WIDTH * HEIGHT)
    overlay = [0] * (WIDTH * HEIGHT)

    def put(layer: list[int], x: int, y: int, tile: int) -> None:
        layer[y * WIDTH + x] = tile + 1

    # A fence ring one cell inside the border. Off-map is solid already; the
    # fence gives the edge a face.
    for x in range(1, WIDTH - 1):
        put(decor, x, 1, FENCE)
        put(decor, x, HEIGHT - 2, FENCE)
    for y in range(1, HEIGHT - 1):
        put(decor, 1, y, FENCE)
        put(decor, WIDTH - 2, y, FENCE)

    # Tree clusters: trunk on decor, canopy above it on overlay, so the player
    # walks behind the crown (depth order, G2).
    trees = 0
    for _ in range(max(20, WIDTH * HEIGHT // 16)):
        x = rng.randrange(3, WIDTH - 3)
        y = rng.randrange(3, HEIGHT - 3)
        if in_clear_zone(x, y) or decor[y * WIDTH + x] != 0:
            continue
        put(decor, x, y, TREE_TRUNK)
        put(overlay, x, y, TREE_CANOPY)
        trees += 1

    # Undergrowth on the free decor cells.
    for _ in range(max(20, WIDTH * HEIGHT // 12)):
        x = rng.randrange(3, WIDTH - 3)
        y = rng.randrange(3, HEIGHT - 3)
        if in_clear_zone(x, y) or decor[y * WIDTH + x] != 0:
            continue
        put(decor, x, y, BUSH if rng.random() < 0.7 else MUSHROOM)

    for fx, fy in ((0.125, 0.125), (0.19, 0.63), (0.75, 0.19), (0.63, 0.75)):
        x = max(2, min(WIDTH - 3, int(WIDTH * fx)))
        y = max(2, min(HEIGHT - 3, int(HEIGHT * fy)))
        if decor[y * WIDTH + x] == 0:
            put(decor, x, y, SIGN)

    print(f"painted cells: ground={sum(1 for v in ground if v)} "
          f"decor={sum(1 for v in decor if v)} overlay={sum(1 for v in overlay if v)} (trees={trees})")
    return ground, decor, overlay


def sprite_entity(entity_id: int, name: str, position: tuple[float, float, float],
                  texture: str, uv_rect: tuple[float, float, float, float],
                  size: tuple[float, float], tint: tuple[float, float, float, float],
                  script: str, animation: dict | None = None) -> dict:
    entity = {
        "id": entity_id,
        "name": name,
        "components": {
            "TransformComponent": {
                "_position": list(position),
                "_rotation": [0.0, 0.0, 0.0],
                "_scale": [1.0, 1.0, 1.0],
            },
            "Sprite2DComponent": {
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
                "uvRect": list(uv_rect),
                "bFlipU": False,
                "bFlipV": False,
                "tint": list(tint),
                "layer": 0,
                "sortOrder": 0,
                "pickId": 0,
            },
            "LuaScriptComponent": {"scripts": [{"enabled": True, "scriptPath": script}]},
        },
    }
    if animation is not None:
        components = entity["components"]
        entity["components"] = {
            key: value for key, value in (
                list(components.items())[:2]
                + [("SpriteAnimationComponent", animation)]
                + list(components.items())[2:])
        }
    return entity


def camera_entity() -> dict:
    return {
        "id": 1001,
        "name": "Camera",
        "components": {
            "TransformComponent": {
                "_position": [WIDTH / 2 + 0.5, HEIGHT / 2 + 0.5, 24.0],
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
            "LuaScriptComponent": {"scripts": [{"enabled": True, "scriptPath": "Content/Scripts/FollowCamera.lua"}]},
        },
    }


def tilemap_entity(ground: list[int], decor: list[int], overlay: list[int]) -> dict:
    return {
        "id": 2000,
        "name": "TilemapGround",
        "components": {
            "TransformComponent": {
                "_position": [-(WIDTH / 2.0), -(HEIGHT / 2.0), 0.0],
                "_rotation": [0.0, 0.0, 0.0],
                "_scale": [1.0, 1.0, 1.0],
            },
            "TilemapComponent": {
                "tileset": {"__base__": {"AssetRefBase": {"_path": ATLAS_TILESET}}},
                "cellSize": [1.0, 1.0],
                "width": WIDTH,
                "height": HEIGHT,
                "layers": [
                    {"name": "Ground", "zOffset": 0.0, "cells": "@@GROUND@@"},
                    {"name": "Decor", "zOffset": 0.03, "cells": "@@DECOR@@"},
                    {"name": "Overlay", "zOffset": 0.2, "cells": "@@OVERLAY@@"},
                ],
                "layer": 0,
            },
        },
    }


def build_entities() -> tuple[list[dict], tuple[list[int], list[int], list[int]]]:
    ground, decor, overlay = build_layers()
    entities: list[dict] = [
        camera_entity(),
        sprite_entity(1021, "Player", (player_cell()[0] + 0.5, player_cell()[1] + 0.5, 0.1), HERO_SHEET, STAND_DOWN_FRAME,
                      (1.0, 1.5), (1.0, 1.0, 1.0, 1.0), "Content/Scripts/Player.lua", hero_animation(True)),
    ]

    rng = random.Random(7)
    for index in range(NPC_COUNT):
        x = 6 + (index * 3) % (WIDTH - 12)
        y = 6 + (index * 5 + rng.randrange(0, 3)) % (HEIGHT - 12)
        while in_clear_zone(x, y):
            y = (y + 1) % (HEIGHT - 12) + 6
        tint = (0.55 + 0.25 * ((index % 3) / 2.0), 0.7 + 0.2 * (index % 2), 1.0, 1.0)
        entities.append(sprite_entity(1100 + index, f"Npc{index + 1}", (x + 0.5, y + 0.5, 0.1),
                                      HERO_SHEET, STAND_DOWN_FRAME, (1.0, 1.5), tint,
                                      "Content/Scripts/Npc.lua", hero_animation(False)))

    entities.append(tilemap_entity(ground, decor, overlay))
    return entities, (ground, decor, overlay)


def main() -> None:
    global WIDTH, HEIGHT, NPC_COUNT, SCENE
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--size", type=int, default=64, help="map edge in cells (default 64)")
    parser.add_argument("--npcs", type=int, default=20, help="villager count (default 20)")
    parser.add_argument("--out", type=Path, default=SCENE, help="scene path to write")
    args = parser.parse_args()
    WIDTH = HEIGHT = args.size
    NPC_COUNT = args.npcs
    SCENE = args.out

    entities, (ground, decor, overlay) = build_entities()

    root = {
        "name": "TownLarge",
        "entities": entities,
        "nodeTree": {
            "name": "scene_root",
            "children": [{"name": e["name"], "entityRef": e["id"]} for e in entities],
        },
        "widgetEntries": [
            {
                "entryId": "Dialogue",
                "zOrder": 10,
                "autoMount": True,
                "document": "Content:UI/Dialogue.yaui.json",
                "rootSlot": {
                    "type": "canvas",
                    "anchorMin": [0.0, 0.0],
                    "anchorMax": [1.0, 1.0],
                    "offset": [0.0, 0.0],
                    "minSize": [0.0, 0.0],
                    "maxSize": [1000000.0, 1000000.0],
                    "offsets": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
                    "alignmentH": 0,
                    "alignmentV": 0,
                    "widthSizeMode": 0,
                    "heightSizeMode": 0,
                    "pivot": [0.0, 0.0],
                    "preferredSize": [0.0, 0.0],
                    "fixedSize": [0.0, 0.0],
                },
            }
        ],
    }

    text = json.dumps(root, indent=2)
    for token, cells in (("@@GROUND@@", ground), ("@@DECOR@@", decor), ("@@OVERLAY@@", overlay)):
        text = text.replace(f'"{token}"', "[" + ", ".join(str(v) for v in cells) + "]")
    SCENE.write_text(text + "\n", encoding="utf-8")

    painted = sum(1 for layer in (ground, decor, overlay) for value in layer if value)
    sprites = sum(1 for e in entities if "Sprite2DComponent" in e["components"])
    print(f"wrote {SCENE.relative_to(ROOT.parent.parent)}")
    print(f"map {WIDTH}x{HEIGHT} x3 layers, painted cells={painted}, sprite entities={sprites}")


if __name__ == "__main__":
    main()
