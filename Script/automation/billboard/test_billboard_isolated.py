import argparse
import json
import os
import socket
import sys
import time


class AutomationClient:
    def __init__(self, port: int):
        self.port = port

    def call(self, method: str, timeout: float = 10.0, **params) -> dict:
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Billboard isolated regression")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--screenshot", required=True)
    args = parser.parse_args()

    client = AutomationClient(args.port)

    def require_ok(result: dict, label: str) -> dict:
        if not result.get("ok"):
            raise RuntimeError(f"{label} failed: {result.get('error')}")
        return result.get("result", {})

    print("1. create isolated regression scene")
    require_ok(client.call("set_editor_config_value",
                           key="lightBillboards.point.texturePath",
                           value="Engine:Content/TestTextures/icons8-light-64.png"),
               "set_editor_config_value(point.texturePath)")
    require_ok(client.call("set_editor_config_value",
                           key="lightBillboards.point.screenSizePixels",
                           value=48.0),
               "set_editor_config_value(point.screenSizePixels)")
    require_ok(client.call("set_editor_config_value",
                           key="lightBillboards.point.mode",
                           value=1),
               "set_editor_config_value(point.mode)")
    require_ok(client.call("set_editor_config_value",
                           key="lightBillboards.directional.screenSizePixels",
                           value=54.0),
               "set_editor_config_value(directional.screenSizePixels)")
    require_ok(client.call("set_editor_config_value",
                           key="lightBillboards.directional.mode",
                           value=2),
               "set_editor_config_value(directional.mode)")

    scene = require_ok(client.call("create_billboard_regression_scene"), "create_billboard_regression_scene")
    print(f"   scene={scene.get('scene_name')} entities={scene.get('entity_count')}")

    print("2. list billboard components")
    billboards = require_ok(client.call("list_billboard_components"), "list_billboard_components")
    print(f"   count={billboards.get('count')}")
    if billboards.get("count", 0) < 2:
        raise RuntimeError("expected at least two managed billboards in isolated scene")

    managed = [entry for entry in billboards.get("billboards", []) if entry.get("managed_by_light")]
    if len(managed) < 2:
        raise RuntimeError("expected at least two light-managed billboards")

    if not all(entry.get("owner_entity_id") for entry in managed):
        raise RuntimeError("expected every managed billboard to report its owner entity")

    mode_names = {entry.get("mode_name") for entry in managed}
    if "LightIcon" not in mode_names:
        raise RuntimeError("expected a LightIcon billboard in isolated scene")
    if "DirectionalLightIcon" not in mode_names:
        raise RuntimeError("expected a DirectionalLightIcon billboard in isolated scene")

    print("3. focus camera on point light")
    point = require_ok(client.call("get_point_light_pos"), "get_point_light_pos")
    point_pos = point["position"]
    require_ok(client.call("set_editor_camera", look_at=point_pos, distance=4.0, height_offset=0.5), "set_editor_camera")

    print("4. wait for linkage and render warmup")
    time.sleep(1.0)

    print("5. capture viewport screenshot")
    shot = require_ok(client.call("capture_screenshot",
                                  path=args.screenshot,
                                  target="viewport",
                                  warmup_frames=45,
                                  timeout=30.0),
                      "capture_screenshot")
    print(f"   screenshot={shot.get('path')}")

    screenshot_path = shot.get("path") or args.screenshot
    if not os.path.exists(screenshot_path):
        raise RuntimeError(f"screenshot was not written: {screenshot_path}")
    if os.path.getsize(screenshot_path) <= 0:
        raise RuntimeError(f"screenshot is empty: {screenshot_path}")

    print("6. quit")
    require_ok(client.call("quit"), "quit")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise
