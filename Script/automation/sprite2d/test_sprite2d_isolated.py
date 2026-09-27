#!/usr/bin/env python3
"""RPC-driven sprite2d regression assertions (see run_sprite2d_isolated.py).

Runs against the game runtime, whose displayed view publishes its images as soon
as it renders (the editor viewport additionally needs a laid-out tab). The
regression scene puts an opaque sprite in front of a mesh occluder and a second
opaque sprite behind it, so counting probe-texture pixels in captured frames
answers the questions the pass exists for:

  * the sprite is drawn at all (pixels exist),
  * the scene's geometry hides the sprite behind it (hiding the occluder adds
    roughly one more sprite's worth of pixels),
  * the sprite's size is authored in world units (moving the camera away shrinks
    it, instead of a distance-compensating quad staying the same size on screen).
"""

import argparse
import json
import os
import socket
import sys
import time

import numpy as np
from PIL import Image

PROBE_TEXTURE = "Engine:Content/TestTextures/sprite2d_probe_64.png"


class AutomationClient:
    def __init__(self, port: int):
        self.port = port

    def call(self, method: str, timeout: float = 30.0, **params) -> dict:
        request = {"id": 1, "method": method, "params": params}
        payload = (json.dumps(request) + "\n").encode("utf-8")

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect(("127.0.0.1", self.port))
            sock.sendall(payload)

            response_data = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_data += chunk
                if b"\n" in response_data:
                    break

            if not response_data:
                raise RuntimeError("empty automation response")
            return json.loads(response_data.split(b"\n")[0].decode("utf-8"))
        finally:
            sock.close()


def probe_pixels(path: str, half: str = "all") -> int:
    """Count pixels matching the flat magenta probe texture.

    `half` selects a screen half, because the two sprites are placed on opposite
    sides: the side with the occluder must stay free of probe pixels while the
    mesh hides the sprite behind it.
    """
    image = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    width = image.shape[1]
    if half == "left":
        image = image[:, : width // 2]
    elif half == "right":
        image = image[:, width // 2 :]
    red, green, blue = image[:, :, 0], image[:, :, 1], image[:, :, 2]
    return int(np.count_nonzero((red > 180) & (blue > 180) & (green < 90)))


def main() -> int:
    parser = argparse.ArgumentParser(description="Sprite2d isolated regression")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--screenshot", required=True)
    args = parser.parse_args()

    client = AutomationClient(args.port)

    def require_ok(result: dict, label: str) -> dict:
        if not result.get("ok"):
            raise RuntimeError(f"{label} failed: {result.get('error')}")
        return result.get("result", {})

    def create_scene(camera_distance: float, occluder_visible: bool,
                     projection: str = "perspective", ortho_half_height: float = 2.0) -> dict:
        return require_ok(
            client.call(
                "create_sprite2d_regression_scene",
                camera_distance=camera_distance,
                occluder_visible=occluder_visible,
                projection=projection,
                ortho_half_height=ortho_half_height,
            ),
            "create_sprite2d_regression_scene",
        )

    def capture(suffix: str, attempts: int = 30) -> str:
        path = f"{args.screenshot}-{suffix}.png"
        # A scene swap needs a few frames before the view has published images
        # for the new scene; an enqueue failure means "not yet".
        last_error = None
        for attempt in range(attempts):
            time.sleep(1.0)
            result = client.call("capture_screenshot", path=path, target="viewport", warmup_frames=60)
            if result.get("ok"):
                captured = (result.get("result") or {}).get("path") or path
                if not os.path.exists(captured) or os.path.getsize(captured) <= 0:
                    raise RuntimeError(f"screenshot was not written: {captured}")
                print(f"   captured after {attempt + 1} attempt(s): {captured}")
                return captured
            last_error = result.get("error")
        raise RuntimeError(f"capture_screenshot({suffix}) failed: {last_error}")

    print("1. create isolated sprite2d regression scene")
    scene = create_scene(camera_distance=6.0, occluder_visible=True)
    occluder_id = scene.get("occluder_entity_id", 0)
    print(f"   scene={scene.get('scene_name')} entities={scene.get('entity_count')} occluder={occluder_id}")
    if not occluder_id:
        raise RuntimeError("regression scene did not report its occluder entity")

    print(f"2. capture with the occluder in place (probe {PROBE_TEXTURE})")
    occluded_shot = capture("occluded")
    open_pixels = probe_pixels(occluded_shot, "right")
    hidden_pixels = probe_pixels(occluded_shot, "left")
    print(f"   open side={open_pixels} hidden side={hidden_pixels}")
    if open_pixels < 2000:
        raise RuntimeError(
            f"the open sprite is not visible: only {open_pixels} probe pixels "
            "(expected a 2x2 world-unit quad to cover more)"
        )
    if hidden_pixels > open_pixels * 0.1:
        raise RuntimeError(
            f"the mesh occluder does not hide the sprite behind it: {hidden_pixels} probe pixels are visible "
            f"on the occluded side (a sprite behind scene geometry must be depth-tested away)"
        )

    print("3. hide the occluder: the sprite behind it must appear")
    require_ok(client.call("entity_set_mesh_visible", id=occluder_id, visible=False), "entity_set_mesh_visible")
    revealed_shot = capture("revealed")
    revealed_open = probe_pixels(revealed_shot, "right")
    revealed_hidden = probe_pixels(revealed_shot, "left")
    print(f"   open side={revealed_open} hidden side={revealed_hidden}")
    if revealed_hidden < 2000:
        raise RuntimeError(
            f"the sprite behind the occluder was never drawn: only {revealed_hidden} probe pixels once the "
            f"mesh is gone ({revealed_open} on the open side)"
        )
    if revealed_open < open_pixels * 0.9 or revealed_open > open_pixels * 1.1:
        raise RuntimeError(
            f"the open sprite changed when the occluder was hidden: {open_pixels} -> {revealed_open} pixels"
        )

    print("4. pull the camera back: an authored world size must shrink on screen")
    create_scene(camera_distance=12.0, occluder_visible=False)
    far_shot = capture("far")
    far_open = probe_pixels(far_shot, "right")
    far_hidden = probe_pixels(far_shot, "left")
    print(f"   open side={far_open} hidden side={far_hidden}")
    if far_open + far_hidden > (revealed_open + revealed_hidden) * 0.45:
        raise RuntimeError(
            f"the sprites did not shrink with distance: {revealed_open + revealed_hidden} pixels at z=6 vs "
            f"{far_open + far_hidden} at z=12 (a distance-compensated quad would keep its screen size)"
        )

    print("5. orthographic game view: same scene through a plain CameraComponent in ortho mode")
    # Half-heights are chosen so both sprites sit fully inside the horizontal
    # extent (half-width = half-height * aspect), otherwise the counts would
    # measure clipping instead of scale.
    create_scene(camera_distance=6.0, occluder_visible=False, projection="orthographic", ortho_half_height=4.0)
    ortho_shot = capture("ortho")
    ortho_pixels = probe_pixels(ortho_shot)
    print(f"   half-height 4 at z=6: {ortho_pixels} pixels")
    if ortho_pixels < 20000:
        raise RuntimeError(f"the orthographic view did not draw the sprites: {ortho_pixels} probe pixels")

    # Distance must not change an orthographic quad's screen size: that is what
    # separates an ortho game view from the perspective one measured in step 4.
    create_scene(camera_distance=12.0, occluder_visible=False, projection="orthographic", ortho_half_height=4.0)
    ortho_far_pixels = probe_pixels(capture("ortho-far"))
    print(f"   half-height 4 at z=12: {ortho_far_pixels} pixels")
    if abs(ortho_far_pixels - ortho_pixels) > ortho_pixels * 0.1:
        raise RuntimeError(
            f"an orthographic view resized the sprite with distance: {ortho_pixels} pixels at z=6 vs "
            f"{ortho_far_pixels} at z=12 (ortho scale is the camera's half-height, not the distance)"
        )

    # The authored scale of an ortho view is the vertical half-height: doubling it
    # halves the quad in world-to-pixel terms, so its area must quarter.
    create_scene(camera_distance=6.0, occluder_visible=False, projection="orthographic", ortho_half_height=8.0)
    ortho_wide_pixels = probe_pixels(capture("ortho-half"))
    print(f"   half-height 8 at z=6: {ortho_wide_pixels} pixels")
    ratio = ortho_wide_pixels / max(ortho_pixels, 1)
    if not 0.18 <= ratio <= 0.35:
        raise RuntimeError(
            f"the orthographic scale is not the camera half-height: area ratio {ratio:.3f} for a doubled "
            "half-height (expected about 0.25)"
        )

    print("6. quit")
    require_ok(client.call("quit"), "quit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
