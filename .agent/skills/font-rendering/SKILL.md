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
   - Bitmap：1:1 栅格 + **FreeType autohint** + **Nearest** 采样 + 像素对齐（`glm::round(scaledGlyphSize)`），最锐利，适合 UI 小字。不要用 native TT bytecode（见契约 4）。
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

4. **Bitmap 栅格必须 FORCE_AUTOHINT，不要跑 native TrueType bytecode**
   - `BitmapFontRasterizer`：`FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_FORCE_AUTOHINT`。
   - Apple CJK face（Hiragino 等）的 hint bytecode 是给 Core Text 写的。FreeType 在 12ppem 执行它会把 `'4'` 横笔 snap 出像素格（覆盖≈0），Fonts.app 大字预览仍是完整轮廓。这不是缺 glyph，也不是换字体能修的。
   - `FT_LOAD_TARGET_LIGHT` 对带 bytecode 的 TTF **不会**关掉 native hinter；必须 `FORCE_AUTOHINT`。
   - `NO_BITMAP` 忽略只在特定 ppem 存在的 sbit strike。

5. **候选顺序 `findCjkFontCandidates()` = best-first 单一全覆封面孔优先**
   - macOS `PingFang.ttc` / Windows `msyh.ttc` → 打包 Noto/SourceHan → 其余子集系统字体。
   - 调用方只取**第一个存在**的候选注册一次（见 `GUIAppHost.cpp`）。

6. **DPI（自适应）**
   - `FontManager::setActiveDpiScale(scale)` 设置激活 DPI；bitmap rasterSize = `round(fontSize * effectiveDpi)`，视图目标 = 逻辑 fontSize。
   - 当前 `GUIAppHost` 用 `presentExtent/logicalExtent` 比值设 DPI（非真机 DPR）；HiDPI 需改系统 API 取真机 DPR（架构改进项，非紧急）。

7. **主字面必须打包进仓，不要探测系统字体**
   - `FGUIWindowHostConfig::fontPath` 默认 `Engine/Content/Fonts/Inter-Regular.ttf`（Inter，OFL，随仓；许可在 `Engine/Content/Fonts/Inter-OFL.txt`）。
   - 理由：**chrome 排版要比例字体**。等宽字面（曾用的 JetBrainsMono）让每个 label / menu / field 都像终端输出，并且固定前进宽度在密集工具面板里浪费横向空间。
   - 打包而不是走系统路径，是为了让文本度量在 macOS / Windows 完全一致：golden 图像与 `dumpSnapshot` 摘要是**跨 run** 比对，系统字体探测会让它们跨机漂移。JetBrains Mono 仍在 `Engine/Content/Fonts/`，给需要等宽的 code / console 面用。
   - 注册名 `DEFAULT_RUNTIME_FONT_NAME`（`RuntimeDefault`）**不要改**：大量测试用它注册合成字体。换字体 = 换 `fontPath`，不是换这个名字。

## 排查清单

- GameEditor Window 工具 tab `font-atlases`（Fonts / Font Atlases）列出 `FontManager::collectFontAtlasDebugPages()` 的每一张 GPU page（primary + 每个 fallback bank）。Combo 标签是 `{face stem}  {size}px  {Bitmap|SDF}`（非 primary 才跟 `fallbackN`，多 page 才跟 `p i/N`）；路径 / 像素尺寸 / glyph 数在 detail。预览是 **1:1 texel、左上角、竖向滚动**，走 atlas 自身 sampler（Bitmap = ClampNearest）。不要用 Image `Contain`：会把 512 page letterbox 进矮窗口并非整倍缩小，Nearest 下看起来又小又糊。Bitmap/Color 按白+alpha 预览；SDF/MSDF 按不透明 R 通道预览。
- 加临时 CJK trace（`atlasIdx / fallback / page / scale / charSize`），而非靠截图猜。
- `scale=1.0 + 全 Bitmap(renderMode=0) + page=0` → 排除 DPI / 分页 / SDF 因素，问题在 fallback 分散或 hinting。
- 缺字/方块 → fallback 未命中或 `atlasIndex` 解析错。
- 亮度不一 → 多同脚本 fallback（本 memory 回归）。
- 边缘虚 → 小字误走 SDF / 未像素对齐 / Linear 采样了 bitmap atlas。
- 竖笔过窄/缺笔 → bitmap 未 1:1 栅格 / 读了错误 `bitmap.pitch` 行宽 / padding 覆盖 / **native TT hint 在特定 ppem 把 stem snap 没了**（先对比 `FORCE_AUTOHINT`，不要先换字体）。

## 测试入口

- `Example/GUIWorkbench/` 的 **"中文测试"** 页：只渲染中文，9–40px 固定字号 + 连续段落 + 标点混合 + 逐字对比行，用于验收 CJK fallback 修复后的亮度/边缘一致性。
- 构建：`xmake b GUIWorkbench`；运行：`python3 Script/ya.py run-editor --project Example/GUIWorkbench/GUIWorkbench.yaproject`。
