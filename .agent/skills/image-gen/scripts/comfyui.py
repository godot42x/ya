#!/usr/bin/env python3
"""本地 ComfyUI 生图 CLI（HTTP API 封装）。

调用本机 ComfyUI Desktop 的 8188 HTTP API 生成游戏 2D 资源。
前提：ComfyUI Desktop 正在运行（127.0.0.1:8188）。

用法:
  python3 comfyui.py "a gold coin, pixel art game sprite" --pixel
  python3 comfyui.py "a knight NPC" --seed 42 --size 512 512
  python3 comfyui.py "a red potion" --transparent   # 输出透明 PNG（需已装背景移除模型）
  python3 comfyui.py "a treasure chest" --gacha 4   # 抽 4 个变体

参数传递规范:
  --model      默认 sd_xl_turbo_1.0_fp16.safetensors（4步采样，M5 上 ~30s/张）
  --size W H   默认 512 512；游戏精灵建议 512（SDXL 原生分辨率，出图后再缩放）
  --pixel / --flat  像素模板 / 卡通模板（提示词由脚本组装，见 SKILL.md）
  --transparent     在文生图后串本地抠图节点输出透明 PNG（需模型就绪）
  --gacha N    从 --seed 起连抽 N 个变体
  --out DIR    输出目录，默认 /Users/mark/ComfyUI-Shared/output
"""
import argparse
import json
import os
import time
import urllib.request

BASE = "http://127.0.0.1:8188"
DEFAULT_MODEL = "sd_xl_turbo_1.0_fp16.safetensors"

PIXEL_STYLE = ("pixelated, 8-bit retro game style, blocky chunky pixels, "
               "flat colors, limited color palette")
FLAT_STYLE = "flat cartoon game style, simple shapes, cel shading"
NEGATIVE = "text, watermark, logo, low quality, blurry, photorealistic, 3d render, extra objects"


def build_workflow(prompt_text: str, negative: str, model: str,
                   width: int, height: int, seed: int,
                   transparent: bool = False, bg_model: str = ""):
    """构造文生图 workflow（API 格式）。transparent=True 时串本地抠图节点。"""
    wf = {
        "3": {"class_type": "KSampler", "inputs": {
            "seed": seed, "steps": 4, "cfg": 1.0, "sampler_name": "euler",
            "scheduler": "normal", "denoise": 1.0,
            "model": ["4", 0], "positive": ["6", 0], "negative": ["7", 0],
            "latent_image": ["5", 0]}},
        "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": model}},
        "5": {"class_type": "EmptyLatentImage", "inputs": {
            "width": width, "height": height, "batch_size": 1}},
        "6": {"class_type": "CLIPTextEncode", "inputs": {
            "text": prompt_text, "clip": ["4", 1]}},
        "7": {"class_type": "CLIPTextEncode", "inputs": {
            "text": negative, "clip": ["4", 1]}},
        "8": {"class_type": "VAEDecode", "inputs": {
            "samples": ["3", 0], "vae": ["4", 2]}},
    }
    if transparent:
        # 本地抠图链（实测修正）：LoadBackgroundRemovalModel -> RemoveBackground(mask)
        #   -> InvertMask(BiRefNet mask 语义相反，必须反转) -> JoinImageWithAlpha -> SaveImage
        wf["10"] = {"class_type": "LoadBackgroundRemovalModel",
                    "inputs": {"bg_removal_name": bg_model}}
        wf["11"] = {"class_type": "RemoveBackground",
                    "inputs": {"bg_removal_model": ["10", 0], "image": ["8", 0]}}
        wf["13"] = {"class_type": "InvertMask", "inputs": {"mask": ["11", 0]}}
        wf["12"] = {"class_type": "JoinImageWithAlpha",
                    "inputs": {"image": ["8", 0], "alpha": ["13", 0]}}
        wf["9"] = {"class_type": "SaveImage",
                   "inputs": {"filename_prefix": "cli_gen", "images": ["12", 0]}}
    else:
        wf["9"] = {"class_type": "SaveImage",
                   "inputs": {"filename_prefix": "cli_gen", "images": ["8", 0]}}
    return wf


def queue_prompt(workflow: dict) -> str:
    data = json.dumps({"prompt": workflow}).encode()
    req = urllib.request.Request(f"{BASE}/prompt", data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read())["prompt_id"]


def wait_and_download(prompt_id: str, out_dir: str, timeout: float = 180) -> str:
    """轮询 history 直到出图，用 /view 下载到 out_dir，返回本地绝对路径。

    ComfyUI 的 SaveImage 是服务端保存（写它自己的 output 目录），
    所以这里不拼本地路径，而是通过 /view 下载，让 --out 真正生效。
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        with urllib.request.urlopen(f"{BASE}/history/{prompt_id}", timeout=10) as r:
            hist = json.loads(r.read())
        if prompt_id in hist:
            outputs = hist[prompt_id].get("outputs", {})
            for node, out in outputs.items():
                images = out.get("images", [])
                for img in images:
                    filename = img["filename"]
                    subfolder = img.get("subfolder", "")
                    view = f"{BASE}/view?filename={urllib.request.quote(filename)}"
                    if subfolder:
                        view += f"&subfolder={urllib.request.quote(subfolder)}"
                    os.makedirs(out_dir, exist_ok=True)
                    dest = os.path.join(out_dir, filename)
                    with urllib.request.urlopen(view, timeout=30) as r2:
                        data = r2.read()
                    with open(dest, "wb") as f:
                        f.write(data)
                    return dest
        time.sleep(1)
    raise TimeoutError(f"timed out after {timeout}s")


def main():
    ap = argparse.ArgumentParser(description="Local ComfyUI image gen (game 2D assets)")
    ap.add_argument("prompt", help="asset subject, e.g. 'a gold coin'")
    ap.add_argument("--negative", default=NEGATIVE, help="negative prompt")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--size", type=int, nargs=2, default=[512, 512])
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--pixel", action="store_true", help="2D pixel style template")
    ap.add_argument("--flat", action="store_true", help="flat cartoon template")
    ap.add_argument("--transparent", action="store_true",
                    help="chain local bg-removal to output transparent PNG")
    ap.add_argument("--bg-model", default="", help="bg removal model file name (if --transparent)")
    ap.add_argument("--gacha", type=int, default=None, help="draw N variants")
    ap.add_argument("--out", default="/Users/mark/ComfyUI-Shared/output")
    args = ap.parse_args()

    style = PIXEL_STYLE if args.pixel else (FLAT_STYLE if args.flat else "")
    prompt_text = f"{args.prompt}, game asset sprite, {style}" if style else args.prompt

    if args.gacha and args.gacha > 1:
        base_seed = args.seed if args.seed is not None else 1000
        for i in range(args.gacha):
            wf = build_workflow(prompt_text, args.negative, args.model,
                                args.size[0], args.size[1], base_seed + i,
                                args.transparent, args.bg_model)
            pid = queue_prompt(wf)
            path = wait_and_download(pid, args.out)
            print(f"OK  {path} (seed={base_seed + i})")
    else:
        wf = build_workflow(prompt_text, args.negative, args.model,
                            args.size[0], args.size[1],
                            args.seed if args.seed is not None else int(time.time()),
                            args.transparent, args.bg_model)
        pid = queue_prompt(wf)
        print(f"queued prompt_id={pid}, waiting...")
        path = wait_and_download(pid, args.out)
        print(f"DONE  {path}")


if __name__ == "__main__":
    main()
