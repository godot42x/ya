#!/usr/bin/env python3
"""Pollinations.AI 免费生图脚本，面向游戏 2D 资源批量生成。

用法:
  python3 generate.py [prompt ...] [--out DIR] [--width W] [--height H]
                      [--model M] [--seed N] [--enhance]

示例:
  python3 generate.py "a cute knight NPC, 2d game, front view" --model flux-anime --seed 12345
  多 prompt 同 seed 保一致:
  python3 generate.py "knight front" "knight side" "knight attack" --seed 99 --out knight
"""
import argparse
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

BASE = "https://image.pollinations.ai/prompt/"
VALID_MODELS = {"flux", "flux-realism", "flux-anime", "flux-3d",
                "any-dark", "turbo", "sana"}

# 游戏精灵图提示词模板：把"引擎可用"的要素全部固化，避免生成整幅场景画。
# 重要：不要用 pixel art / SNES 这类词——实测会触发模型模仿图库站水印。
# 像素感改用描述性词：pixelated / 8-bit / blocky / flat colors / limited palette。
ASSET_TEMPLATE = (
    "{subject}, game asset sprite, single {kind}, {pose}, "
    "{style} plain solid white background, centered, "
    "isolated, full body, no scenery no frame, "
    "no text no watermark no logo"
)

# 风格措辞：pixel 用描述词不引水印；flat 默认扁平卡通（略偏3D）
PIXEL_STYLE = ("pixelated, 8-bit retro game style, blocky chunky pixels, "
               "flat colors, limited color palette, low resolution")
FLAT_STYLE = "flat cartoon game style, simple shapes, cel shading"


def build_prompt(subject: str, kind: str = "character",
                 pose: str = "front view", pixel: bool = True,
                 extra: str = "") -> str:
    """按模板组装可用性最高的精灵图提示词。pixel=True 出2D像素，False 出扁平卡通。"""
    style = PIXEL_STYLE if pixel else FLAT_STYLE
    p = ASSET_TEMPLATE.format(subject=subject, kind=kind, pose=pose, style=style)
    if extra:
        p += ", " + extra
    return p


def sanitize_name(prompt: str) -> str:
    name = re.sub(r"[^A-Za-z0-9\u4e00-\u9fff]+", "_", prompt).strip("_")
    return name[:60] or "image"


def generate(prompt: str, out_dir: str, width: int, height: int,
             model: str, seed: int, enhance: bool, retries: int = 8):
    query = {"width": width, "height": height, "model": model}
    if seed is not None:
        query["seed"] = seed
    if enhance:
        query["enhance"] = "true"
    url = BASE + urllib.parse.quote(prompt) + "?" + urllib.parse.urlencode(query)

    base = os.path.join(out_dir, sanitize_name(prompt))

    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "image-gen/1.0"})
            with urllib.request.urlopen(req, timeout=180) as resp:
                data = resp.read()
                ctype = (resp.headers.get("Content-Type") or "").lower()
            if not data:
                raise RuntimeError("empty response")

            # 以响应头真实格式决定扩展名：flux 等可能返回 image/jpeg
            if "jpeg" in ctype or "jpg" in ctype:
                ext = ".jpg"
            elif "png" in ctype:
                ext = ".png"
            else:
                ext = ".bin"

            path = f"{base}{ext}"
            if os.path.exists(path):
                path = f"{base}_{seed if seed is not None else 'dup'}{ext}"
            with open(path, "wb") as f:
                f.write(data)
            print(f"OK  {path}  ({len(data)} bytes, {ctype or 'image'}, model={model})")
            return path
        except urllib.error.HTTPError as e:
            # 401/402/403/429 = 匿名限流，指数退避重试；其他 HTTP 错误直接失败
            if e.code in (401, 402, 403, 429) and attempt < retries:
                backoff = min(5 * attempt, 30)
                print(f"attempt {attempt}/{retries}: HTTP {e.code}, backoff {backoff}s", file=sys.stderr)
                time.sleep(backoff)
                continue
            raise RuntimeError(f"HTTP {e.code}: {prompt}")
        except Exception as e:
            if attempt < retries:
                print(f"attempt {attempt}/{retries}: {e}", file=sys.stderr)
                time.sleep(3 * attempt)
                continue
            raise RuntimeError(f"failed: {prompt}")


def main():
    ap = argparse.ArgumentParser(description="Pollinations free image gen (game 2D assets)")
    ap.add_argument("subjects", nargs="+", help="asset subject(s), e.g. 'a red potion'")
    ap.add_argument("--out", default=None, help="output dir (default: sanitized first subject)")
    ap.add_argument("--width", type=int, default=64)
    ap.add_argument("--height", type=int, default=64)
    ap.add_argument("--model", default="flux", choices=sorted(VALID_MODELS))
    ap.add_argument("--seed", type=int, default=None, help="fixed seed")
    ap.add_argument("--gacha", type=int, default=None,
                    help="draw N variants with seeds seed..seed+N-1 (for picking best)")
    ap.add_argument("--kind", default="character", help="character / item / tile / prop")
    ap.add_argument("--pose", default="front view", help="pose for characters")
    ap.add_argument("--pixel", action="store_true", help="2D pixel style (default)")
    ap.add_argument("--no-pixel", dest="pixel", action="store_false",
                    help="flat cartoon instead of pixel")
    ap.set_defaults(pixel=True)
    ap.add_argument("--extra", default="", help="extra prompt suffix")
    ap.add_argument("--enhance", action="store_true")
    args = ap.parse_args()

    prompts = [build_prompt(s, args.kind, args.pose, args.pixel, args.extra)
               for s in args.subjects]
    out = args.out or sanitize_name(args.subjects[0])
    os.makedirs(out, exist_ok=True)

    if args.gacha and args.gacha > 1:
        base_seed = args.seed if args.seed is not None else 1000
        for p in prompts:
            for i in range(args.gacha):
                generate(p, out, args.width, args.height, args.model,
                         base_seed + i, args.enhance)
    else:
        for p in prompts:
            generate(p, out, args.width, args.height, args.model,
                     args.seed, args.enhance)


if __name__ == "__main__":
    main()
