---
name: image-gen
description: 为游戏生成 2D 资源（角色/NPC、道具、图标、tile、场景精灵图），支持两个后端：本地 ComfyUI（优先，M5 16GB 跑 SDXL-Turbo ~30s/张，可出透明 PNG）与 Pollinations.AI 免费 API（兜底）。当 Agent 需要为游戏生成贴图、精灵图、UI 图标，或把文字描述转成图片时使用。支持像素/卡通两种风格模板、seed 一致性、批量抽卡。
---

# Image Generation (Local ComfyUI + Online Fallback)

生成游戏 2D 资源的统一入口。**优先本地 ComfyUI**（质量稳、可透明、无限制），**免费在线 API 兜底**（无 Key、应急用）。

## 后端选择

| 条件 | 用哪个 | 脚本 |
|---|---|---|
| ComfyUI Desktop 在跑（默认优先） | 本地 ComfyUI | `scripts/comfyui.py` |
| ComfyUI 未启动 / 想快速出图 | Pollinations 免费 API | `scripts/generate.py` |

判断 ComfyUI 是否在跑：`curl -s http://127.0.0.1:8188/system_stats` 有返回即在线。

## 本地 ComfyUI 调用规范（scripts/comfyui.py）

### 调用方式
```bash
# 基础：像素精灵
python3 scripts/comfyui.py "a gold coin" --pixel
# 指定 seed（同一角色多角度一致）
python3 scripts/comfyui.py "a knight NPC" --pixel --seed 42
# 卡通风格（非像素）
python3 scripts/comfyui.py "a cute slime" --flat
# 透明 PNG（需先装背景移除模型，见下）
python3 scripts/comfyui.py "a red potion" --pixel --transparent --bg-model <model.onnx>
# 抽卡：同 prompt 连抽 4 个变体
python3 scripts/comfyui.py "a treasure chest" --pixel --gacha 4 --seed 5000
```

### 参数传递规范
| 参数 | 说明 | 默认 |
|---|---|---|
| prompt | 资源主体描述（不要写整幅场景） | 必填 |
| --pixel | 2D 像素模板：`pixelated, 8-bit retro, blocky, flat colors` | 关 |
| --flat | 卡通模板：`flat cartoon, simple shapes` | 关 |
| --size W H | **SDXL 原生分辨率 512/1024**；精灵图出 512 后缩到 48/64 | 512 512 |
| --seed | 固定种子保证一致 | 随机 |
| --model | checkpoint 文件名（当前 `sd_xl_turbo_1.0_fp16.safetensors`） | 见默认 |
| --gacha N | 同 prompt 连抽 N 个变体，seed 递增 | 1 |
| --transparent | 串本地抠图节点，输出透明 PNG | 关 |
| --bg-model | 抠图模型文件名（装好后必填） | 空 |
| --out | 输出目录 | `/Users/mark/ComfyUI-Shared/output` |

### 透明 PNG（本地抠图链）
节点链：`LoadBackgroundRemovalModel` → `RemoveBackground` → **`InvertMask`（必须）** → `JoinImageWithAlpha` → `SaveImage`。
- **实测关键修正**：BiRefNet 的 mask 语义与 `JoinImageWithAlpha` 的 alpha 期望**相反**，不加 `InvertMask` 会把主体抠成透明、背景留成不透明（输出白色剪影）。脚本已内置 InvertMask。
- 模型已装：`/Users/mark/ComfyUI-Shared/models/background_removal/birefnet.safetensors`（444MB，Comfy-Org/BiRefNet，MIT License）。
- 用法：`--transparent --bg-model birefnet.safetensors`。
- 注意：`BriaRemoveImageBackground` 节点是**云端付费**（$0.018/次），**不要用**，走本地 `RemoveBackground`。

## 在线兜底调用规范（scripts/generate.py）

```bash
python3 scripts/generate.py "a gold coin" --kind item --pixel --gacha 4 --seed 7000 --width 64 --height 64
```

| 参数 | 说明 |
|---|---|
| --model | flux / flux-realism / flux-anime / flux-3d / any-dark / turbo / sana |
| --kind | character / item / tile / prop |
| --pixel / --no-pixel | 像素模板 / 卡通模板 |
| --gacha N | 抽 N 个变体（seed..seed+N-1） |
| --seed | 固定种子（同 prompt 逐像素一致，已实测） |

在线 API 硬限制：**匿名偶发 HTTP 402 限流**、返回 JPEG 无 alpha、像素/物体一致性差——只作兜底与验证方向，**不要作为正式素材来源**。

## 游戏精灵图提示词规范（两后端通用）

**不要生成整幅场景画。** 引擎要的是单个角色/道具精灵：纯色背景、居中、小尺寸。对照项目 `hero_walk.png`(48×96)、`tiny_town.png`(192×176)。

| 要点 | 错误写法 | 正确写法 |
|---|---|---|
| 构图 | 只写 "a villager in a town" | `single character, front view, plain white background, centered, isolated, no scenery` |
| 像素感 | 用 `pixel art` / `SNES`（在线 API 会引图库水印） | 用描述词 `pixelated, 8-bit, blocky, flat colors`（脚本已内置） |
| 负向提示 | 留空 | `text, watermark, logo, low quality, photorealistic, 3d render, extra objects`（本地脚本已内置） |

## 通用工作流（推荐）

1. **先本地**：ComfyUI 在跑 → `comfyui.py --pixel` 出 512 底图。
2. **抽卡选优**：`--gacha 4` 出 4 个变体，挑最好的。
3. **透明化**：需要 alpha → 装抠图模型后 `--transparent`；不需要 → 白底 + 引擎侧抠。
4. **缩放**：512 → 48/64/128（引擎 tile/sprite 规格）。
5. **兜底**：ComfyUI 不在 → `generate.py` 走在线 API 验证方向。

## 触发与降级

- 单张/批量/抽卡：见上。
- ComfyUI 排队慢或未启动：自动切在线 API（Agent 先 curl system_stats 判断）。
- 生成的是 PNG（本地）或 JPG（在线），落盘路径见 `--out`；引擎导入后续走 `resource-system` skill。

## 完整 API 文档

- 本地：ComfyUI HTTP API（`/prompt`、`/history`、`/view`，端口 8188），ComfyUI Desktop 自带。
- 在线：`references/api.md`（Pollinations 官方 APIDOCS 生图章节）。
