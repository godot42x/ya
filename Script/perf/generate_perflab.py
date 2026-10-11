#!/usr/bin/env python3
"""Generate PerfLab stress scenes and PB1 benchmark configs.

Lives in Script/perf/ because PB6's bench_run.py / bench_compare.py are
specified for this directory. PerfLab stays a content project: this script
writes scene JSON and *.bench.json only. It does not write textures, meshes,
or other assets.

The engine has no per-object castShadow field. Every static mesh is submitted
to the shadow pass, so --casters is the number of opaque cube props in a
lights scene (plus the ground plane). Point-light entities are still authored
at the requested count; the renderer currently extracts only MAX_POINT_LIGHTS
(Engine/Config/Engine.jsonc, 6). Raising that cap is not this script's job.

    python3 Script/perf/generate_perflab.py
    python3 Script/perf/generate_perflab.py --lights 50 --casters 16
    python3 Script/perf/generate_perflab.py --meshes 1000
    python3 Script/perf/generate_perflab.py --sprites 500 --map-size 128

Default (no scene flag) writes the preset gradients:
  lights  50 / 200 / 500   (casters default 64)
  meshes  1000 / 5000 / 10000
  sprites 500 / 2000 / 5000 on a 128x128 tilemap
  Sponza  camera path over the fetched glTF (see fetch_sponza.py)
"""
from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PERFLAB = ROOT / "Example" / "PerfLab"
SCENE_DIR = PERFLAB / "Content" / "Scenes"
BENCH_DIR = PERFLAB / "Benchmark"

LIGHT_PRESETS = (50, 200, 500)
MESH_PRESETS = (1000, 5000, 10000)
SPRITE_PRESETS = (500, 2000, 5000)
DEFAULT_CASTERS = 64
DEFAULT_MAP_SIZE = 128

# Tiny Town indices. A cell stores tile + 1; 0 is empty.
GRASS = 0
BUSH, MUSHROOM = 1, 2
TREE_CANOPY, TREE_TRUNK = 4, 16

HERO_SHEET = "Example/2DRpgPrototype/Content/Textures/hero_walk.png"
TILESET = "Content/Tilesets/town.yatileset.json"
STAND_DOWN_UV = (1.0 / 3.0, 0.0, 2.0 / 3.0, 0.25)
# glTF node scale is 0.008. Accessor bounds then sit near
# x[-15.4, 14.4], y[-1.0, 11.4], z[-9.5, 8.8]. The hall runs along X.
SPONZA_MODEL = "Engine/Saved/PerfAssets/Sponza/Sponza.gltf"

BENCH_KEYS = {
    "name",
    "scene",
    "resolution",
    "presentMode",
    "uncapFrameRate",
    "warmupFrames",
    "sampleFrames",
    "fixedDeltaSeconds",
    "cpuTrace",
    "outputDir",
    "cameraPath",
}
CAMERA_PATH_KEYS = {"framesPerSecond", "loop", "keyframes"}
KEYFRAME_KEYS = {"frame", "position", "rotation"}


def r(value: float) -> float:
    return round(float(value), 4)


def vec3(x: float, y: float, z: float) -> list[float]:
    return [r(x), r(y), r(z)]


def grid(count: int, spacing: float, y: float) -> list[list[float]]:
    if count < 0:
        raise ValueError("count must be >= 0")
    if count == 0:
        return []
    cols = math.ceil(math.sqrt(count))
    rows = math.ceil(count / cols)
    origin_x = -((cols - 1) * spacing) / 2.0
    origin_z = -((rows - 1) * spacing) / 2.0
    positions: list[list[float]] = []
    for index in range(count):
        col = index % cols
        row = index // cols
        positions.append(vec3(origin_x + col * spacing, y, origin_z + row * spacing))
    return positions


def extent(positions: list[list[float]]) -> float:
    if not positions:
        return 8.0
    return max(max(abs(p[0]), abs(p[2])) for p in positions)


def transform(position: list[float] | None = None,
              rotation: list[float] | None = None,
              scale: list[float] | None = None) -> dict:
    component: dict = {}
    if position is not None:
        component["_position"] = position
    if rotation is not None:
        component["_rotation"] = rotation
    if scale is not None:
        component["_scale"] = scale
    if not component:
        component["_position"] = [0.0, 0.0, 0.0]
    return component


def entity(entity_id: int, name: str, components: dict) -> dict:
    return {"id": entity_id, "name": name, "components": components}


def phong(diffuse: tuple[float, float, float], shininess: float = 32.0) -> dict:
    ambient = tuple(r(channel * 0.25) for channel in diffuse)
    return {
        "_params": {
            "ambient": [ambient[0], ambient[1], ambient[2]],
            "diffuse": [r(diffuse[0]), r(diffuse[1]), r(diffuse[2])],
            "specular": [0.2, 0.2, 0.2],
            "shininess": shininess,
        }
    }


def static_mesh(position: list[float],
                scale: list[float] | None,
                diffuse: tuple[float, float, float],
                lit: bool = True) -> dict:
    # One PhongMaterialComponent creates one PhongMaterial. MaterialDescPool
    # asserts above 2048 materials, so the mesh gradient leaves lighting off
    # and draws through the shared fallback material.
    components = {
        "TransformComponent": transform(position, scale=scale),
        "StaticMeshComponent": {"_mesh": {"_primitiveGeometry": "Cube"}},
        "RenderComponent": {"RenderingLayer": "Opaque"},
    }
    if lit:
        components["PhongMaterialComponent"] = phong(diffuse)
    return components


def camera(position: list[float], rotation: list[float], orthographic: bool = False,
           ortho_half_height: float = 20.0) -> dict:
    component: dict = {"bPrimary": True, "_farClip": 2000.0}
    if orthographic:
        component["_projection"] = "Orthographic"
        component["_orthoHalfHeight"] = r(ortho_half_height)
        component["_pixelPerfect"] = False
    return {
        "TransformComponent": transform(position, rotation),
        "CameraComponent": component,
    }


def directional_light() -> dict:
    return {
        "TransformComponent": transform(rotation=[-55.0, 30.0, 0.0]),
        "DirectionalLightComponent": {
            "_direction": [-0.3, -0.9, -0.2],
            "_color": [1.0, 0.96, 0.9],
            "intensity": 1.0,
            "bEnable": True,
        },
    }


def point_light(position: list[float], color: list[float]) -> dict:
    return {
        "TransformComponent": transform(position),
        "PointLightComponent": {
            "color": color,
            "intensity": 6.0,
        },
    }


LIGHT_COLORS = (
    [1.0, 0.95, 0.85],
    [1.0, 0.55, 0.35],
    [0.45, 0.7, 1.0],
    [0.5, 1.0, 0.6],
    [0.95, 0.4, 0.55],
    [0.75, 0.6, 1.0],
)


def closed_path(start: dict, mid: dict) -> list[dict]:
    """Frame 0 and frame 720 share a pose so a looping 720-frame run is closed."""
    end = {"frame": 720, "position": list(start["position"]), "rotation": list(start["rotation"])}
    return [
        {"frame": 0, **{k: start[k] for k in ("position", "rotation")}},
        {"frame": 360, **{k: mid[k] for k in ("position", "rotation")}},
        end,
    ]


def bench_config(name: str, scene_rel: str, keyframes: list[dict]) -> dict:
    return {
        "name": name,
        "scene": scene_rel,
        "resolution": {"width": 1920, "height": 1080},
        "presentMode": "immediate",
        "uncapFrameRate": True,
        "warmupFrames": 120,
        "sampleFrames": 600,
        "fixedDeltaSeconds": 0.016666667,
        "cpuTrace": False,
        "outputDir": f"Engine/Saved/Benchmark/PerfLab/{name}",
        "cameraPath": {
            "framesPerSecond": 60,
            "loop": True,
            "keyframes": keyframes,
        },
    }


def validate_bench(config: dict) -> None:
    unknown = set(config) - BENCH_KEYS
    if unknown:
        raise ValueError(f"bench config has keys outside the PB1 schema: {sorted(unknown)}")
    path = config["cameraPath"]
    unknown_path = set(path) - CAMERA_PATH_KEYS
    if unknown_path:
        raise ValueError(f"cameraPath has unknown keys: {sorted(unknown_path)}")
    frames = []
    for keyframe in path["keyframes"]:
        unknown_key = set(keyframe) - KEYFRAME_KEYS
        if unknown_key:
            raise ValueError(f"keyframe has unknown keys: {sorted(unknown_key)}")
        if len(keyframe["position"]) != 3 or len(keyframe["rotation"]) != 3:
            raise ValueError("keyframe position and rotation must be vec3")
        frames.append(keyframe["frame"])
    if frames != sorted(frames) or len(frames) != len(set(frames)):
        raise ValueError(f"keyframe frames must be strictly increasing, got {frames}")
    if config["presentMode"] != "immediate" or config["cpuTrace"] is not False:
        raise ValueError("preset bench config drifted from the PB5 contract")
    if config["warmupFrames"] != 120 or config["sampleFrames"] != 600:
        raise ValueError("preset bench config drifted from the PB5 frame window")
    if config["uncapFrameRate"] is not True:
        raise ValueError("preset bench config must uncap the frame rate")


def write_scene(path: Path, name: str, entities: list[dict]) -> None:
    ids = [item["id"] for item in entities]
    if len(ids) != len(set(ids)):
        raise ValueError(f"{name} has duplicate entity ids")
    lines = ["{", f'  "name": {json.dumps(name)},', '  "entities": [']
    for index, item in enumerate(entities):
        comma = "," if index + 1 < len(entities) else ""
        lines.append("    " + json.dumps(item, separators=(",", ":")) + comma)
    lines.append("  ],")
    lines.append('  "nodeTree": {')
    lines.append('    "name": "scene_root",')
    lines.append('    "children": [')
    for index, item in enumerate(entities):
        comma = "," if index + 1 < len(entities) else ""
        child = {"name": item["name"], "entityRef": item["id"]}
        lines.append("      " + json.dumps(child, separators=(",", ":")) + comma)
    lines.append("    ]")
    lines.append("  }")
    lines.append("}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_bench(path: Path, config: dict) -> None:
    validate_bench(config)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(config, indent=4) + "\n", encoding="utf-8")


def pose(position: list[float], rotation: list[float]) -> dict:
    return {"position": position, "rotation": rotation}


def build_lights(light_count: int, caster_count: int) -> tuple[str, list[dict], list[dict]]:
    if light_count <= 0:
        raise ValueError("light count must be positive")
    if caster_count < 0:
        raise ValueError("caster count must be >= 0")
    name = f"Lights_{light_count}"
    lights = grid(light_count, 4.0, 4.0)
    casters = grid(caster_count, 3.0, 0.5)
    span = max(extent(lights), extent(casters), 8.0)
    entities = [
        entity(1001, "Camera", camera(
            vec3(0.0, max(14.0, span * 0.45), span + 18.0),
            [-22.0, 0.0, 0.0],
        )),
        entity(1002, "DirectionalLight", directional_light()),
        entity(1003, "Ground", static_mesh(
            [0.0, -0.1, 0.0],
            vec3(span * 2.0 + 16.0, 0.2, span * 2.0 + 16.0),
            (0.45, 0.45, 0.48),
        )),
    ]
    next_id = 2000
    for index, position in enumerate(casters):
        entities.append(entity(next_id, f"Caster_{index}", static_mesh(position, None, (0.7, 0.55, 0.4))))
        next_id += 1
    for index, position in enumerate(lights):
        entities.append(entity(
            next_id,
            f"PointLight_{index}",
            point_light(position, LIGHT_COLORS[index % len(LIGHT_COLORS)]),
        ))
        next_id += 1
    look_from = vec3(0.0, max(14.0, span * 0.45), span + 18.0)
    keyframes = closed_path(
        pose(look_from, [-22.0, 0.0, 0.0]),
        pose(vec3(span * 0.65, max(10.0, span * 0.28), span * 0.15), [-16.0, -35.0, 0.0]),
    )
    expected = 3 + caster_count + light_count
    if len(entities) != expected:
        raise ValueError(f"{name} entity count {len(entities)} != {expected}")
    return name, entities, keyframes


def build_meshes(mesh_count: int) -> tuple[str, list[dict], list[dict]]:
    if mesh_count <= 0:
        raise ValueError("mesh count must be positive")
    name = f"Meshes_{mesh_count}"
    positions = grid(mesh_count, 2.0, 0.5)
    span = extent(positions)
    eye = vec3(-span * 0.85, max(24.0, span * 0.35), span + 16.0)
    entities = [
        entity(1001, "Camera", camera(eye, [-28.0, 0.0, 0.0])),
        entity(1002, "DirectionalLight", directional_light()),
    ]
    for index, position in enumerate(positions):
        entities.append(entity(2000 + index, f"Mesh_{index}", static_mesh(position, None, (0.62, 0.64, 0.7), lit=False)))
    keyframes = closed_path(
        pose(eye, [-28.0, 0.0, 0.0]),
        pose(vec3(0.0, max(36.0, span * 0.55), 0.0), [-55.0, 0.0, 0.0]),
    )
    if len(entities) != mesh_count + 2:
        raise ValueError(f"{name} entity count drifted")
    return name, entities, keyframes


def build_sprites(sprite_count: int, map_size: int) -> tuple[str, list[dict], list[dict]]:
    if sprite_count <= 0:
        raise ValueError("sprite count must be positive")
    if map_size < 8:
        raise ValueError("map size must be >= 8")
    name = f"Sprites_{sprite_count}"
    ground = [GRASS + 1] * (map_size * map_size)
    decor = [0] * (map_size * map_size)
    overlay = [0] * (map_size * map_size)
    rng = random.Random(20261007 + sprite_count)
    for _ in range(max(16, map_size * map_size // 16)):
        x = rng.randrange(2, map_size - 2)
        y = rng.randrange(2, map_size - 2)
        index = y * map_size + x
        if decor[index] != 0:
            continue
        decor[index] = TREE_TRUNK + 1
        overlay[index] = TREE_CANOPY + 1
    for _ in range(max(16, map_size * map_size // 12)):
        x = rng.randrange(1, map_size - 1)
        y = rng.randrange(1, map_size - 1)
        index = y * map_size + x
        if decor[index] != 0:
            continue
        decor[index] = (BUSH if rng.random() < 0.7 else MUSHROOM) + 1

    origin = -map_size / 2.0
    half_height = max(12.0, map_size * 0.16)
    travel = max(8.0, map_size * 0.3)
    entities = [
        entity(1001, "Camera", camera(
            vec3(-travel, 0.0, 24.0),
            [0.0, 0.0, 0.0],
            orthographic=True,
            ortho_half_height=half_height,
        )),
        entity(2000, "TilemapGround", {
            "TransformComponent": transform(vec3(origin, origin, 0.0)),
            "TilemapComponent": {
                "tileset": {"__base__": {"AssetRefBase": {"_path": TILESET}}},
                "cellSize": [1.0, 1.0],
                "width": map_size,
                "height": map_size,
                "layers": [
                    {"name": "Ground", "zOffset": 0.0, "cells": ground},
                    {"name": "Decor", "zOffset": 0.03, "cells": decor},
                    {"name": "Overlay", "zOffset": 0.2, "layerOffset": 1, "cells": overlay},
                ],
                "layer": 0,
            },
        }),
    ]
    cols = math.ceil(math.sqrt(sprite_count))
    rows = math.ceil(sprite_count / cols)
    usable = map_size - 4
    for index in range(sprite_count):
        col = index % cols
        row = index // cols
        cell_x = 2 + int(col * (usable - 1) / max(cols - 1, 1))
        cell_y = 2 + int(row * (usable - 1) / max(rows - 1, 1))
        tint_r = r(0.7 + 0.3 * ((index * 17) % 8) / 7.0)
        entities.append(entity(3000 + index, f"Sprite_{index}", {
            "TransformComponent": transform(vec3(origin + cell_x + 0.5, origin + cell_y + 0.5, 0.1)),
            "Sprite2DComponent": {
                "bVisible": True,
                "image": {
                    "bEnable": True,
                    "samplerConfig": {"addressMode": "ClampToEdge", "filterMode": "Nearest"},
                    "textureRef": {"__base__": {"AssetRefBase": {"_path": HERO_SHEET}}},
                },
                "size": [1.0, 1.5],
                "pivot": [0.5, r(1.0 / 3.0)],
                "uvRect": [r(STAND_DOWN_UV[0]), r(STAND_DOWN_UV[1]), r(STAND_DOWN_UV[2]), r(STAND_DOWN_UV[3])],
                "tint": [tint_r, 0.85, 0.95, 1.0],
                "bYSort": True,
            },
        }))
    keyframes = closed_path(
        pose(vec3(-travel, 0.0, 24.0), [0.0, 0.0, 0.0]),
        pose(vec3(travel, 0.0, 24.0), [0.0, 0.0, 0.0]),
    )
    if len(entities) != sprite_count + 2:
        raise ValueError(f"{name} entity count drifted")
    return name, entities, keyframes


def build_sponza() -> tuple[str, list[dict], list[dict]]:
    name = "Sponza"
    entities = [
        entity(1001, "Camera", camera([11.0, 1.8, 0.0], [-6.0, 90.0, 0.0])),
        entity(1002, "DirectionalLight", directional_light()),
        entity(1003, "Sponza", {
            "TransformComponent": transform([0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [1.0, 1.0, 1.0]),
            "ModelComponent": {
                "_modelRef": {"__base__": {"AssetRefBase": {"_path": SPONZA_MODEL}}},
                "_materialType": "PBR",
                "_useEmbeddedMaterials": True,
                "_autoCreateChildEntities": True,
                "_flipImportedTextureV": False,
            },
        }),
    ]
    keyframes = closed_path(
        pose([11.0, 1.8, 0.0], [-6.0, 90.0, 0.0]),
        pose([-11.0, 1.8, 0.0], [-6.0, 90.0, 0.0]),
    )
    return name, entities, keyframes


def emit(name: str, entities: list[dict], keyframes: list[dict]) -> None:
    scene_rel = f"Content/Scenes/{name}.scene.json"
    write_scene(SCENE_DIR / f"{name}.scene.json", name, entities)
    write_bench(BENCH_DIR / f"{name}.bench.json", bench_config(name, scene_rel, keyframes))
    print(f"wrote {scene_rel} entities={len(entities)}  Benchmark/{name}.bench.json")


def generate_presets(casters: int, map_size: int) -> None:
    for count in LIGHT_PRESETS:
        emit(*build_lights(count, casters))
    for count in MESH_PRESETS:
        emit(*build_meshes(count))
    for count in SPRITE_PRESETS:
        emit(*build_sprites(count, map_size))
    emit(*build_sponza())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lights", type=int, help="point-light count for one lights scene")
    parser.add_argument("--casters", type=int, default=DEFAULT_CASTERS,
                        help="opaque cube props in a lights scene (default 64)")
    parser.add_argument("--meshes", type=int, help="static-mesh instance count for one mesh scene")
    parser.add_argument("--sprites", type=int, help="sprite count for one 2D scene")
    parser.add_argument("--map-size", type=int, default=DEFAULT_MAP_SIZE,
                        help="tilemap edge in cells for sprite scenes (default 128)")
    parser.add_argument("--sponza", action="store_true", help="write only the Sponza scene and config")
    args = parser.parse_args()

    selected = any(value is not None for value in (args.lights, args.meshes, args.sprites)) or args.sponza
    if not selected:
        generate_presets(args.casters, args.map_size)
        return

    if args.lights is not None:
        emit(*build_lights(args.lights, args.casters))
    if args.meshes is not None:
        emit(*build_meshes(args.meshes))
    if args.sprites is not None:
        emit(*build_sprites(args.sprites, args.map_size))
    if args.sponza:
        emit(*build_sponza())


if __name__ == "__main__":
    main()
