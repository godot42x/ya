#!/usr/bin/env python3
"""Painter-order acceptance that step 5a still owed.

Builds a throwaway scene (camera JSON under the temp dir, cube and sprites
through the automation API) and checks two frozen expectations:

1. A yawed cube crosses a sprite plane. Pixels on the near side of the
   intersection stay the cube; pixels on the far side show the sprite.
   Two sprites on the same XY with different z keep the same picture when
   their z values are swapped, and change when their sort orders are swapped.
2. On Town, an Overlay tile (layerOffset 1) really covers a layer-0 y-sorted
   character, and the Decor tile under that character does not. The player
   transform is moved at runtime onto a trunk cell; the scene file and the
   tilemap collision are left alone.

Screenshots land in Engine/Saved/Automation/painter-order/.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image

import rpg_control as rpg

ROOT = rpg.ROOT
OUT_DIR = ROOT / "Engine" / "Saved" / "Automation" / "painter-order"
PROBE = "Engine:Content/TestTextures/sprite2d_probe_64.png"
DEPTH_SCENE_NAME = "PainterDepth"
TOWN = "Content/Scenes/Town.scene.json"

# Ortho camera sits on +Z and looks down -Z. The sprite is the local XY plane
# at z = 0. A unit cube scaled by 2 and yawed 45 degrees has its camera-facing
# surface at z = 1.414 - |x| in local space; shifting the cube to z = -0.7
# puts that surface at 0.714 - |x|. The cube is closer than the sprite for
# |x| < 0.714 and farther for 0.714 < |x| < 1.414.
CUBE_SCALE = 2.0
CUBE_YAW_DEG = 45.0
CUBE_Z = -0.7
ORTHO_HALF_HEIGHT = 3.0
SPRITE_Z = 0.0

# Samples sit well clear of the intersection (|x| = 0.714) and of the cube
# silhouette (|x| = 1.414). The sprite quad is 6 wide, so x = 2.2 is sprite
# with no cube behind it.
SAMPLE_CUBE_FRONT = (0.0, 0.0)
SAMPLE_SPRITE_FRONT = ((-1.05, 0.0), (1.05, 0.0))
SAMPLE_SPRITE_ONLY = (2.2, 0.0)


def _ya(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(ROOT / "Script" / "ya.py"), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )


class Rpc:
    def __init__(self, port: int):
        self.port = port

    def call(self, method: str, timeout: float = 60.0, **params) -> dict:
        payload = (json.dumps({"id": 1, "method": method, "params": params}) + "\n").encode("utf-8")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect(("127.0.0.1", self.port))
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


def start_game() -> Rpc:
    rpg.stop()
    result = _ya(
        "control", "start",
        "--project", rpg.PROJECT,
        "--game",
        "--build",
        "--lifetime", "300",
        "--engine-arg=--width=1280",
        "--engine-arg=--height=720",
    )
    sys.stderr.write(result.stdout)
    if result.stderr:
        sys.stderr.write(result.stderr)
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    for line in result.stdout.splitlines():
        if "control port " in line:
            port = int(line.split("control port ", 1)[1].split()[0])
            return Rpc(port)
    raise SystemExit(f"control start did not publish a port:\n{result.stdout}")


def scene_name() -> str:
    summary = rpg.call("scene.get_active")
    if not isinstance(summary, dict) or not isinstance(summary.get("name"), str):
        raise SystemExit(f"scene.get_active returned {summary!r}")
    return summary["name"]


def wait_scene(name: str) -> None:
    def ready():
        try:
            return scene_name() == name
        except SystemExit:
            return False

    rpg.wait_until(ready, 20.0, f"scene {name}")


def create_entity(name: str) -> int:
    created = rpg.call("entity.create", {"name": name})
    if not isinstance(created, dict) or "id" not in created:
        raise SystemExit(f"entity.create {name} returned {created!r}")
    return int(created["id"])


def add_component(entity_id: int, type_name: str) -> None:
    rpg.call("component.add", {"id": entity_id, "type": type_name})


def set_fields(entity_id: int, type_name: str, fields: dict) -> None:
    rpg.call("component.set", {"id": entity_id, "type": type_name, "fields": fields})


def entity_by_name(name: str) -> int:
    entities = rpg.call("entity.list")
    if not isinstance(entities, list):
        raise SystemExit(f"entity.list returned {type(entities).__name__}")
    for entity in entities:
        if isinstance(entity, dict) and entity.get("name") == name:
            return int(entity["id"])
    raise SystemExit(f"{name} is not in the scene")


def component_of(entity_id: int, type_name: str) -> dict:
    component = rpg.call("component.get", {"id": entity_id, "type": type_name})
    if not isinstance(component, dict):
        raise SystemExit(f"component.get {type_name} returned {component!r}")
    return component


def write_depth_scene() -> Path:
    scene = {
        "name": DEPTH_SCENE_NAME,
        "entities": [
            {
                "name": "Camera",
                "id": 1,
                "components": {
                    "TransformComponent": {
                        "_position": [0.0, 0.0, 8.0],
                        "_rotation": [0.0, 0.0, 0.0],
                        "_scale": [1.0, 1.0, 1.0],
                    },
                    "CameraComponent": {
                        "bPrimary": True,
                        "_projection": "Orthographic",
                        "_orthoHalfHeight": ORTHO_HALF_HEIGHT,
                        "_pixelPerfect": False,
                        "_nearClip": 0.1,
                        "_farClip": 100.0,
                    },
                },
            }
        ],
    }
    path = Path(tempfile.gettempdir()) / "ya-painter-depth.scene.json"
    path.write_text(json.dumps(scene), encoding="utf-8")
    return path


def probe_image() -> dict:
    return {
        "bEnable": True,
        "samplerConfig": {"addressMode": "ClampToEdge", "filterMode": "Nearest"},
        "textureRef": {"__base__": {"AssetRefBase": {"_path": PROBE}}},
        "uvOffset": [0.0, 0.0],
        "uvRotation": 0.0,
        "uvScale": [1.0, 1.0],
    }


def make_sprite(name: str, position: list[float], size: list[float], tint: list[float], sort_order: int) -> int:
    entity_id = create_entity(name)
    add_component(entity_id, "Sprite2DComponent")
    set_fields(entity_id, "TransformComponent", {
        "_position": position,
        "_rotation": [0.0, 0.0, 0.0],
        "_scale": [1.0, 1.0, 1.0],
    })
    set_fields(entity_id, "Sprite2DComponent", {
        "bVisible": True,
        "image": probe_image(),
        "size": size,
        "pivot": [0.5, 0.5],
        "uvRect": [0.0, 0.0, 1.0, 1.0],
        "tint": tint,
        "layer": 0,
        "sortOrder": sort_order,
        "bYSort": False,
    })
    return entity_id


def make_cube() -> int:
    entity_id = create_entity("Occluder")
    add_component(entity_id, "StaticMeshComponent")
    add_component(entity_id, "UnlitMaterialComponent")
    add_component(entity_id, "RenderComponent")
    set_fields(entity_id, "TransformComponent", {
        "_position": [0.0, 0.0, CUBE_Z],
        "_rotation": [0.0, CUBE_YAW_DEG, 0.0],
        "_scale": [CUBE_SCALE, CUBE_SCALE, CUBE_SCALE],
    })
    set_fields(entity_id, "StaticMeshComponent", {
        "_mesh": {"_meshIndex": 0, "_primitiveGeometry": "Cube", "_sourceModelPath": ""},
    })
    set_fields(entity_id, "UnlitMaterialComponent", {
        "_params": {"baseColor0": [0.0, 1.0, 0.0], "baseColor1": [0.0, 1.0, 0.0], "mixValue": 0.0},
    })
    set_fields(entity_id, "RenderComponent", {"RenderingLayer": "Opaque"})
    return entity_id


def capture(rpc: Rpc, name: str, warmup: int = 30) -> Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / f"{name}.png"
    last_error = None
    for attempt in range(12):
        try:
            rpc.call(
                "capture_screenshot",
                path=str(path),
                target="viewport",
                warmup_frames=warmup,
            )
        except SystemExit as error:
            last_error = error
            time.sleep(0.4)
            continue
        if path.is_file() and path.stat().st_size > 0:
            print(f"captured {path}")
            return path
        last_error = SystemExit(f"screenshot was not written: {path}")
        time.sleep(0.4)
    raise SystemExit(f"capture {name} failed: {last_error}")


def load_rgb(path: Path) -> Image.Image:
    return Image.open(path).convert("RGB")


def world_to_pixel(image: Image.Image, world_x: float, world_y: float,
                   cam_x: float, cam_y: float, half_w: float, half_h: float) -> tuple[int, int]:
    width, height = image.size
    sx = (world_x - cam_x) / (2.0 * half_w) * width + width / 2.0
    sy = height / 2.0 - (world_y - cam_y) / (2.0 * half_h) * height
    return int(round(sx)), int(round(sy))


def pixel_to_world(image: Image.Image, sx: float, sy: float,
                   cam_x: float, cam_y: float, half_w: float, half_h: float) -> tuple[float, float]:
    width, height = image.size
    world_x = cam_x + (sx - width / 2.0) / width * (2.0 * half_w)
    world_y = cam_y - (sy - height / 2.0) / height * (2.0 * half_h)
    return world_x, world_y


def window(image: Image.Image, sx: int, sy: int, radius: int = 4) -> list[tuple[int, int, int]]:
    width, height = image.size
    colors = []
    for y in range(sy - radius, sy + radius + 1):
        for x in range(sx - radius, sx + radius + 1):
            if 0 <= x < width and 0 <= y < height:
                colors.append(image.getpixel((x, y)))
    if not colors:
        raise SystemExit(f"sample ({sx}, {sy}) is outside {width}x{height}")
    return colors


def is_red(color: tuple[int, int, int]) -> bool:
    red, green, blue = color
    return red > 150 and red > green + 40 and red > blue + 40


def is_green(color: tuple[int, int, int]) -> bool:
    red, green, blue = color
    return green > 150 and green > red + 40 and green > blue + 40


def is_blue(color: tuple[int, int, int]) -> bool:
    red, green, blue = color
    return blue > 150 and blue > red + 40 and blue > green + 40


def fraction(colors: list[tuple[int, int, int]], predicate) -> float:
    return sum(1 for color in colors if predicate(color)) / len(colors)


def require_fraction(label: str, colors: list[tuple[int, int, int]], predicate, minimum: float) -> None:
    got = fraction(colors, predicate)
    print(f"  {label}: {got:.2f} of {len(colors)} px, sample {colors[len(colors) // 2]}")
    if got < minimum:
        raise SystemExit(f"{label}: expected at least {minimum:.0%} matching pixels, got {got:.0%}")


def scanline(image: Image.Image, half_w: float, half_h: float) -> None:
    parts = []
    x = -2.5
    while x <= 2.5 + 1e-6:
        sx, sy = world_to_pixel(image, x, 0.0, 0.0, 0.0, half_w, half_h)
        colors = window(image, sx, sy, 1)
        mid = colors[len(colors) // 2]
        parts.append(f"{x:+.2f}:{mid[0]:02x}{mid[1]:02x}{mid[2]:02x}")
        x += 0.25
    print("  scan y=0 " + " ".join(parts))


def depth_frustum(image: Image.Image) -> tuple[float, float]:
    width, height = image.size
    half_h = ORTHO_HALF_HEIGHT
    half_w = half_h * (width / height)
    return half_w, half_h


def assert_depth(both: Image.Image, cube_only: Image.Image) -> None:
    half_w, half_h = depth_frustum(both)
    print(f"depth frustum half=({half_w:.3f}, {half_h:.3f}) image={both.size}")
    scanline(both, half_w, half_h)
    scanline(cube_only, half_w, half_h)

    def colors_at(image: Image.Image, world: tuple[float, float]) -> list[tuple[int, int, int]]:
        sx, sy = world_to_pixel(image, world[0], world[1], 0.0, 0.0, half_w, half_h)
        return window(image, sx, sy, 4)

    cube_both = colors_at(both, SAMPLE_CUBE_FRONT)
    cube_only_colors = colors_at(cube_only, SAMPLE_CUBE_FRONT)
    require_fraction("cube covers sprite at center (with sprite)", cube_both, is_green, 0.85)
    require_fraction("cube covers sprite at center (cube only)", cube_only_colors, is_green, 0.85)

    for world in SAMPLE_SPRITE_FRONT:
        require_fraction(
            f"sprite in front of cube at {world}",
            colors_at(both, world), is_red, 0.85)
        require_fraction(
            f"cube visible once the sprite is hidden at {world}",
            colors_at(cube_only, world), is_green, 0.85)

    require_fraction("sprite outside the cube", colors_at(both, SAMPLE_SPRITE_ONLY), is_red, 0.85)
    outside = colors_at(cube_only, SAMPLE_SPRITE_ONLY)
    if fraction(outside, is_green) > 0.15 or fraction(outside, is_red) > 0.15:
        raise SystemExit(f"cube-only shot still has the sprite or the cube at {SAMPLE_SPRITE_ONLY}: {outside[0]}")
    print("  depth intersection: cube in front at center, sprite in front on both wings")


def images_match(left: Image.Image, right: Image.Image, tolerance: int = 2) -> int:
    if left.size != right.size:
        raise SystemExit(f"image sizes differ: {left.size} vs {right.size}")
    left_px = left.load()
    right_px = right.load()
    width, height = left.size
    differ = 0
    for y in range(height):
        for x in range(width):
            a = left_px[x, y]
            b = right_px[x, y]
            if abs(a[0] - b[0]) > tolerance or abs(a[1] - b[1]) > tolerance or abs(a[2] - b[2]) > tolerance:
                differ += 1
    return differ


def assert_order(before: Image.Image, swapped_z: Image.Image, swapped_order: Image.Image) -> None:
    differ = images_match(before, swapped_z)
    print(f"  z swap differing pixels: {differ}")
    if differ > 0:
        raise SystemExit(
            f"swapping Transform.z of two overlapping sprites changed {differ} pixels; "
            "z must not participate in painter order, and sprites do not write depth")

    half_w, half_h = depth_frustum(before)
    sx, sy = world_to_pixel(before, 0.0, 0.0, 0.0, 0.0, half_w, half_h)
    require_fraction("higher sortOrder is on top before the swap", window(before, sx, sy, 6), is_blue, 0.9)
    require_fraction("swapped sortOrder puts red on top", window(swapped_order, sx, sy, 6), is_red, 0.9)
    order_differ = images_match(before, swapped_order)
    print(f"  order swap differing pixels: {order_differ}")
    if order_differ < 100:
        raise SystemExit(f"swapping sortOrder barely changed the picture ({order_differ} px)")
    print("  z swap kept the picture; order swap changed which sprite is on top")


def view_state(rpc: Rpc) -> dict:
    return rpc.call("get_world_view_state")


def town_frustum(camera: dict, extent_w: float, extent_h: float) -> tuple[float, float]:
    if extent_h < 1.0:
        raise SystemExit(f"rendered viewport extent is not ready: {extent_w}x{extent_h}")
    if camera.get("_pixelPerfect"):
        ppu = float(camera.get("_pixelsPerUnit", 16.0))
        reference = float(camera.get("_referenceHeightPx", 192.0))
        zoom = max(1, int(extent_h // reference))
        half_h = extent_h / (2.0 * ppu * zoom)
    else:
        half_h = float(camera["_orthoHalfHeight"])
    half_w = half_h * (extent_w / extent_h)
    return half_w, half_h


def find_overlay_pair(tilemap: dict, origin: tuple[float, float, float]) -> dict:
    width = int(tilemap["width"])
    height = int(tilemap["height"])
    cell = tilemap.get("cellSize") or [1.0, 1.0]
    cell_w, cell_h = float(cell[0]), float(cell[1])
    layers = tilemap["layers"]
    overlay = next((layer for layer in layers if layer.get("name") == "Overlay"), None)
    decor = next((layer for layer in layers if layer.get("name") == "Decor"), None)
    if overlay is None or decor is None:
        raise SystemExit(f"Town tilemap is missing Overlay/Decor: {[layer.get('name') for layer in layers]}")
    if int(overlay.get("layerOffset", 0)) != 1:
        raise SystemExit(f"Overlay layerOffset is {overlay.get('layerOffset')}, expected 1")
    if int(decor.get("layerOffset", 0)) != 0:
        raise SystemExit(f"Decor layerOffset is {decor.get('layerOffset')}, expected 0")

    overlay_cells = overlay["cells"]
    decor_cells = decor["cells"]
    for y in range(1, height):
        for x in range(width):
            if int(overlay_cells[y * width + x]) == 0:
                continue
            if int(decor_cells[(y - 1) * width + x]) == 0:
                continue
            return {
                "cell": (x, y),
                "trunk_cell": (x, y - 1),
                "overlay_value": int(overlay_cells[y * width + x]),
                "decor_value": int(decor_cells[(y - 1) * width + x]),
                "canopy_rect": (
                    origin[0] + x * cell_w,
                    origin[1] + y * cell_h,
                    origin[0] + (x + 1) * cell_w,
                    origin[1] + (y + 1) * cell_h,
                ),
                "trunk_center": (
                    origin[0] + (x + 0.5) * cell_w,
                    origin[1] + (y - 1 + 0.5) * cell_h,
                ),
            }
    raise SystemExit("no Overlay cell with a Decor cell directly under it")


def hold_player_on_trunk() -> tuple[int, dict]:
    # Player.lua writes the transform from its own cell every tick, and
    # LuaScriptComponent keeps `scripts` out of the reflected field set, so
    # component.set cannot disable that one script. Removing the component
    # stops the overwrite; the scene file is untouched.
    player = entity_by_name("Player")
    removed = rpg.call("component.remove", {"id": player, "type": "LuaScriptComponent"})
    if not isinstance(removed, dict) or not removed.get("removed"):
        raise SystemExit(f"could not remove Player.lua so the transform would stick: {removed!r}")

    tilemap_id = entity_by_name("TilemapGround")
    tilemap = component_of(tilemap_id, "TilemapComponent")
    transform = component_of(tilemap_id, "TransformComponent")
    position = transform["_position"]
    origin_xyz = (float(position[0]), float(position[1]), float(position[2]))
    pair = find_overlay_pair(tilemap, origin_xyz)
    print(
        f"overlay cell {pair['cell']} value {pair['overlay_value']}, "
        f"trunk {pair['trunk_cell']} value {pair['decor_value']}, "
        f"placing player at {pair['trunk_center']}"
    )

    sprite = component_of(player, "Sprite2DComponent")
    pivot = sprite["pivot"]
    size = sprite["size"]
    px, py = pair["trunk_center"]
    set_fields(player, "TransformComponent", {"_position": [px, py, 0.1]})

    def stayed():
        actual = rpg.player_position(player)
        return actual if rpg.near(actual, (px, py), 0.02) else None

    rpg.wait_until(stayed, 3.0, f"player held at trunk {pair['trunk_center']} (Player.lua must stay disabled)")
    pair["pivot"] = (float(pivot[0]), float(pivot[1]))
    pair["size"] = (float(size[0]), float(size[1]))
    pair["player"] = player
    return player, pair


def wait_camera(rpc: Rpc, goal: tuple[float, float]) -> dict:
    # FollowCamera eases, then device-pixel snap holds the rendered eye still.
    # The two town shots are only comparable once that eye has stopped.
    deadline = time.time() + 12.0
    stable_since = None
    last = None
    latest = None
    while time.time() < deadline:
        state = view_state(rpc)
        pos = state.get("camera_pos")
        extent = state.get("rendered_viewport_extent") or {}
        if not isinstance(pos, list) or len(pos) < 2 or float(extent.get("height", 0)) < 1.0:
            time.sleep(0.1)
            continue
        current = (float(pos[0]), float(pos[1]))
        if abs(current[0] - goal[0]) > 30 or abs(current[1] - goal[1]) > 30:
            time.sleep(0.1)
            continue
        if last is not None and abs(current[0] - last[0]) < 0.002 and abs(current[1] - last[1]) < 0.002:
            if stable_since is None:
                stable_since = time.time()
            if time.time() - stable_since >= 0.5:
                print(f"camera settled at ({current[0]:.3f}, {current[1]:.3f})")
                return state
        else:
            stable_since = None
        last = current
        latest = state
        time.sleep(0.1)
    raise SystemExit(f"camera did not settle near {goal}; last={latest!r}")


def assert_town(hidden: Image.Image, shown: Image.Image, state: dict, camera: dict, pair: dict) -> None:
    extent = state["rendered_viewport_extent"]
    extent_w = float(extent["width"])
    extent_h = float(extent["height"])
    half_w, half_h = town_frustum(camera, extent_w, extent_h)
    cam = state["camera_pos"]
    cam_x, cam_y = float(cam[0]), float(cam[1])
    print(
        f"town camera ({cam_x:.3f}, {cam_y:.3f}) frustum half=({half_w:.3f}, {half_h:.3f}) "
        f"extent={extent_w:.0f}x{extent_h:.0f} image={shown.size}"
    )
    if hidden.size != shown.size:
        raise SystemExit(f"town shots differ in size: {hidden.size} vs {shown.size}")

    # Map the screenshot through the same frustum the view used. A retina
    # capture can be a different pixel count than the reported extent; the
    # picture is still the full view, so the image size is the pixel grid.
    pivot_x, pivot_y = pair["pivot"]
    size_x, size_y = pair["size"]
    entity_x, entity_y = pair["trunk_center"]
    sprite_min_x = entity_x - pivot_x * size_x
    sprite_max_x = sprite_min_x + size_x
    sprite_min_y = entity_y - pivot_y * size_y
    sprite_max_y = sprite_min_y + size_y
    canopy_min_x, canopy_min_y, canopy_max_x, canopy_max_y = pair["canopy_rect"]
    overlap_min_y = max(sprite_min_y, canopy_min_y)
    overlap_max_y = min(sprite_max_y, canopy_max_y)
    if overlap_max_y - overlap_min_y < 0.2:
        raise SystemExit(
            f"player sprite y [{sprite_min_y:.3f}, {sprite_max_y:.3f}] does not overlap "
            f"the canopy cell y [{canopy_min_y:.3f}, {canopy_max_y:.3f}]"
        )
    overlap_y = (overlap_min_y + overlap_max_y) / 2.0
    overlap_x = entity_x
    trunk_x, trunk_y = pair["trunk_center"]
    print(
        f"  sprite y [{sprite_min_y:.3f}, {sprite_max_y:.3f}] "
        f"canopy y [{canopy_min_y:.3f}, {canopy_max_y:.3f}] "
        f"overlap sample ({overlap_x:.3f}, {overlap_y:.3f}) trunk sample ({trunk_x:.3f}, {trunk_y:.3f})"
    )

    def colors_at(image: Image.Image, world_x: float, world_y: float) -> list[tuple[int, int, int]]:
        sx, sy = world_to_pixel(image, world_x, world_y, cam_x, cam_y, half_w, half_h)
        return window(image, sx, sy, 3)

    hidden_overlap = colors_at(hidden, overlap_x, overlap_y)
    shown_overlap = colors_at(shown, overlap_x, overlap_y)
    overlap_differ = sum(
        1 for a, b in zip(hidden_overlap, shown_overlap)
        if abs(a[0] - b[0]) > 8 or abs(a[1] - b[1]) > 8 or abs(a[2] - b[2]) > 8
    )
    print(f"  canopy overlap differing samples: {overlap_differ}/{len(hidden_overlap)}")
    if overlap_differ > len(hidden_overlap) * 0.15:
        raise SystemExit(
            "showing the player changed the canopy overlap; Overlay (layerOffset 1) should cover the character")

    hidden_trunk = colors_at(hidden, trunk_x, trunk_y)
    shown_trunk = colors_at(shown, trunk_x, trunk_y)
    trunk_differ = sum(
        1 for a, b in zip(hidden_trunk, shown_trunk)
        if abs(a[0] - b[0]) > 30 or abs(a[1] - b[1]) > 30 or abs(a[2] - b[2]) > 30
    )
    print(f"  trunk differing samples: {trunk_differ}/{len(hidden_trunk)} hidden {hidden_trunk[0]} shown {shown_trunk[0]}")
    if trunk_differ < len(hidden_trunk) * 0.7:
        raise SystemExit(
            "the player does not cover the Decor trunk; Decor should paint under the y-sorted character")

    hidden_px = hidden.load()
    shown_px = shown.load()
    width, height = shown.size
    diff_points: list[tuple[float, float]] = []
    stray = 0
    for y in range(height):
        for x in range(width):
            a = hidden_px[x, y]
            b = shown_px[x, y]
            if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) <= 12:
                continue
            world_x, world_y = pixel_to_world(shown, x, y, cam_x, cam_y, half_w, half_h)
            if sprite_min_x - 0.2 <= world_x <= sprite_max_x + 0.2:
                diff_points.append((world_x, world_y))
            else:
                stray += 1
    if stray > 40:
        raise SystemExit(f"town shots differ outside the player ({stray} px); the camera moved between captures")
    if len(diff_points) < 80:
        raise SystemExit(f"player barely changed the picture ({len(diff_points)} px); bVisible may not have applied")

    top = max(point[1] for point in diff_points)
    bottom = min(point[1] for point in diff_points)
    # The canopy cell starts at canopy_min_y. Overlay covers the part of the
    # character inside that cell, so the visible difference stops at that edge
    # instead of at the top of the sprite. A failure (player paints over the
    # crown) pushes `top` up toward sprite_max_y, about half a cell higher.
    print(
        f"  player diff y [{bottom:.3f}, {top:.3f}] px={len(diff_points)} "
        f"canopy edge {canopy_min_y:.3f} sprite top {sprite_max_y:.3f}"
    )
    if top > canopy_min_y + 0.12:
        raise SystemExit(
            f"player pixels show above the canopy edge (diff top {top:.3f}, edge {canopy_min_y:.3f}); "
            "Overlay is not covering the character")
    if top < canopy_min_y - 0.25:
        raise SystemExit(
            f"player diff stops at {top:.3f}, well below the canopy edge {canopy_min_y:.3f}; "
            "the overlap is not what the shot is showing")
    if bottom > sprite_min_y + 0.35:
        raise SystemExit(
            f"player diff starts at {bottom:.3f}, not down at the feet {sprite_min_y:.3f}; "
            "Decor may be covering the character")
    print("  Overlay covers the character; Decor stays under the character")


def run_depth_and_order(rpc: Rpc) -> None:
    scene_path = write_depth_scene()
    rpg.call("scene.load", {"path": str(scene_path)})
    wait_scene(DEPTH_SCENE_NAME)
    print(f"loaded {scene_path}")

    sprite = make_sprite("Plane", [0.0, 0.0, SPRITE_Z], [6.0, 6.0], [1.0, 0.0, 0.0, 1.0], 0)
    cube = make_cube()
    print(f"api created sprite {sprite} and cube {cube}")

    both_path = capture(rpc, "depth-both", warmup=45)
    set_fields(sprite, "Sprite2DComponent", {"bVisible": False})
    cube_path = capture(rpc, "depth-cube", warmup=20)
    assert_depth(load_rgb(both_path), load_rgb(cube_path))

    set_fields(cube, "TransformComponent", {"_position": [40.0, 0.0, CUBE_Z]})
    red = make_sprite("OrderRed", [0.0, 0.0, 0.0], [1.6, 1.6], [1.0, 0.0, 0.0, 1.0], 0)
    blue = make_sprite("OrderBlue", [0.0, 0.0, 0.4], [1.6, 1.6], [0.0, 0.0, 1.0, 1.0], 1)
    before_path = capture(rpc, "order-before", warmup=30)
    set_fields(red, "TransformComponent", {"_position": [0.0, 0.0, 0.4]})
    set_fields(blue, "TransformComponent", {"_position": [0.0, 0.0, 0.0]})
    swapped_z_path = capture(rpc, "order-z-swapped", warmup=15)
    set_fields(red, "Sprite2DComponent", {"sortOrder": 2})
    swapped_order_path = capture(rpc, "order-swapped", warmup=15)
    assert_order(load_rgb(before_path), load_rgb(swapped_z_path), load_rgb(swapped_order_path))


def run_town(rpc: Rpc) -> None:
    rpg.call("scene.load", {"path": TOWN})
    wait_scene("Town")

    def player_ready():
        try:
            return rpg.player_id()
        except SystemExit:
            return None

    rpg.wait_until(player_ready, 15.0, "Town player")
    _player, pair = hold_player_on_trunk()
    state = wait_camera(rpc, pair["trunk_center"])
    camera_id = entity_by_name("Camera")
    camera = component_of(camera_id, "CameraComponent")

    set_fields(pair["player"], "Sprite2DComponent", {"bVisible": False})
    hidden_path = capture(rpc, "town-hidden", warmup=20)
    state_after_hidden = view_state(rpc)
    set_fields(pair["player"], "Sprite2DComponent", {"bVisible": True})
    shown_path = capture(rpc, "town-shown", warmup=20)
    state_after_shown = view_state(rpc)
    hidden_cam = state_after_hidden.get("camera_pos")
    shown_cam = state_after_shown.get("camera_pos")
    if not isinstance(hidden_cam, list) or not isinstance(shown_cam, list):
        raise SystemExit(f"camera_pos missing: {state_after_hidden} {state_after_shown}")
    if abs(float(hidden_cam[0]) - float(shown_cam[0])) > 0.02 or abs(float(hidden_cam[1]) - float(shown_cam[1])) > 0.02:
        raise SystemExit(f"camera moved between town shots: {hidden_cam} -> {shown_cam}")
    assert_town(load_rgb(hidden_path), load_rgb(shown_path), state_after_shown, camera, pair)
    # `state` is the settled view from before the captures; the assertion uses
    # the post-capture camera, which the check above keeps on that settle.
    del state


def main() -> None:
    rpc = start_game()
    try:
        run_depth_and_order(rpc)
        run_town(rpc)
    finally:
        rpg.stop()
    print("painter-order visual acceptance passed")
    print(f"screenshots: {OUT_DIR}")


if __name__ == "__main__":
    main()
