---
name: font-rendering
description: YA 引擎字体栈（FontManager / DynamicFontAtlas / Bitmap+SDF Rasterizer / Sprite2D 文本着色器）的架构、flavor split 契约与 CJK/emoji fallback 最佳实践。
---

## 适用场景

- 改 `Engine/Source/Framework/Render/Resources/` 下的 `FontManager.*` `DynamicFontAtlas.*` `BitmapFontRasterizer.*` `SDFFontRasterizer.*`
- 改字体相关着色器 `Engine/Shader/Slang/Sprite2D.slang` 的 bitmap / SDF 分支
- 改字体 atlas 采样器 `Engine/Source/Framework/RHI/Backend/TextureLibrary.*`
- 排查中文/emoji 渲染：缺字、方块、亮度不一、边缘发虚、竖笔过窄、SDF 咬边
- 在 `GUIAppHost` / `AppLifecycle` 调整字体栈注册

## 字体栈架构

```text
FontManager               字体注册/缓存/尺寸驱动 flavor split；fallback 链
  ├─ BitmapFontRasterizer  FreeType FT_RENDER_MODE_NORMAL，1:1 栅格，灰度覆盖
  ├─ SDFFontRasterizer     FreeType FT_RENDER_MODE_SDF，固定 64px base，距离场
  └─ DynamicFontAtlas      动态打包，kGlyphPadding=1 四边透明 padding，多 page
TextureLibrary            atlas 纹理 + 采样器解析（FontAtlas_/SDFFontAtlas_ 前缀）
Sprite2D.slang            bitmap 分支：texColor * color（premultiplied white）
                          SDF 分支：fwidth() 距离场 -> smoothstep 抗锯齿
QuadRender.drawText       逐字形取 atlas、像素对齐、下发顶点
```

## 关键契约（改字体栈前必读）

1. **两种 flavor，按尺寸分流（flavor split）**
   - `chooseModeForSize()`：`size ≤ kBitmapMaxSize(48)` → Bitmap；否则 SDF。
   - Bitmap：1:1 栅格 + **Nearest** 采样 + 像素对齐（`glm::round(scaledGlyphSize)`），最锐利，适合 UI 小字。
   - SDF：固定 64px base，缩放无损失，shader 内 `fwidth()` 抗锯齿，适合大缩放（标题/世界空间文本）。
   - **SDF 永不在 ≤48px 使用**；给中文小字强制 SDF 必定发虚。

2. **Premultiplied alpha（覆盖存储）**
   - atlas 像素 = `vec4(255,255,255,coverage)`（premultiplied 白），blend = `SrcAlpha, OneMinusSrcAlpha`（straight alpha 语义）。
   - shader bitmap 分支直接 `return texColor * input.color`，**不要**再乘 alpha。

3. **Fallback 链 = 不同脚本才并列，同脚本必须单一**
   - `character.atlasIndex > 0` 表示走 fallback；`fallbacks[atlasIndex-1].fontPath` 是 fallback 字体路径；`atlasSlot >> 16` 是 page。
   - 合法组合：**主拉丁 + 一个中文(全覆封面孔) + 一个 emoji(色字体)**。
   - **禁止**注册多个同脚本 fallback（尤其 CJK）：字符会按命中顺序分散到不同 face，hinting/笔画权重不同 → 相邻字亮度/粗细跳变（见 `memories/font_cjk_fallback_brightness_regression.md`）。
   - `attachFallbackToBase` 用 **base 的 `renderMode`** 覆盖 fallback 的 mode，所以小字 base 是 bitmap → fallback 也是 bitmap；注册时不要强制 SDF。

4. **候选顺序 `findCjkFontCandidates()` = best-first 单一全覆封面孔优先**
   - macOS `PingFang.ttc` / Windows `msyh.ttc` → 打包 Noto/SourceHan → 其余子集系统字体。
   - 调用方只取**第一个存在**的候选注册一次（见 `GUIAppHost.cpp`）。

5. **DPI（自适应）**
   - `FontManager::setActiveDpiScale(scale)` 设置激活 DPI；bitmap rasterSize = `round(fontSize * effectiveDpi)`，视图目标 = 逻辑 fontSize。
   - 当前 `GUIAppHost` 用 `presentExtent/logicalExtent` 比值设 DPI（非真机 DPR）；HiDPI 需改系统 API 取真机 DPR（架构改进项，非紧急）。

## 排查清单

- 加临时 CJK trace（`atlasIdx / fallback / page / scale / charSize`），而非靠截图猜。
- `scale=1.0 + 全 Bitmap(renderMode=0) + page=0` → 排除 DPI / 分页 / SDF 因素，问题在 fallback 分散或 hinting。
- 缺字/方块 → fallback 未命中或 `atlasIndex` 解析错。
- 亮度不一 → 多同脚本 fallback（本 memory 回归）。
- 边缘虚 → 小字误走 SDF / 未像素对齐 / Linear 采样了 bitmap atlas。
- 竖笔过窄/缺笔 → bitmap 未 1:1 栅格 / 读了错误 `bitmap.pitch` 行宽 / padding 覆盖。

## 测试入口

- `Example/GUIWorkbench/` 的 **"中文测试"** 页：只渲染中文，9–40px 固定字号 + 连续段落 + 标点混合 + 逐字对比行，用于验收 CJK fallback 修复后的亮度/边缘一致性。
- 构建：`xmake b GUIWorkbench`；运行：`python3 Script/ya.py run-editor --project Example/GUIWorkbench/GUIWorkbench.yaproject`。
