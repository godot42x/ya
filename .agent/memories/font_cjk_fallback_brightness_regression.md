# CJK fallback 分散导致相邻中文字亮度/粗细不一致

> 2026-08-24，GUIWorkbench 中文渲染排查时定位；为字体栈设计缺陷（多 CJK fallback 注册），非第三方库 bug。

## 现象

- 同一段连续纯中文里，相邻字**一个亮一个暗 / 一个粗一个细**；部分字边缘发虚。
- 早期还伴随：缺笔画（方块）、SDF 咬边过锐、竖笔过窄。本期聚焦"亮度不一致 + 边缘虚"。
- 环境为 DPI=1.0（非 Retina / 1:1 窗口），scale=1.000、drawSize=designSize=13、全部 Bitmap（renderMode=0）、全部 page=0——排除了 DPI、分页、采样因素。

## 定位手段

- 在 `QuadRender::drawText` 给 CJK（U+4E00–U+9FFF）加临时 trace，打印 `atlasIdx / fallback / page / charSize / scale`。
- 关键证据：同一段中文的字符分散到 **3 个不同 fallback 字体**：
  - `atlasIdx=1` → `AppleGothic.ttf`
  - `atlasIdx=3` → `AquaKana.ttc`
  - `atlasIdx=4` → `Hiragino Sans GB.ttc`
- 每个字体在 13px 下的 hinting（stem snapping）与笔画权重不同，且各自独立 raster，于是相邻字视觉亮度/粗细不一致；每字体 bitmap 在小字下也更易出现亚像素级边缘发虚。

## 根因

`GUIAppHost.cpp` 把 `findCjkFontCandidates()` 返回的**整个列表**逐个 `addFontFallback`，多个 CJK fallback 同时挂载。FreeType 按字符命中顺序在不同 face 间分散解析，同一段中文因此来自多个面孔，风格不统一。

## 修复（提交 ce6858ad / f4acc6a8）

1. `findCjkFontCandidates()` 重排为 **best-first 单一全覆封面孔优先**：
   `PingFang.ttc`(mac) / `msyh.ttc`(win) → Noto/SourceHan(打包兜底) → 其余子集系统字体。
   注释明确：**不得注册多个 CJK fallback**。
2. `GUIAppHost` 改为只注册**第一个存在的候选**（`break`），不再全量注册；不再强制 `SDF`，让 `attachFallbackToBase` 按 base flavor 决定（≤48px bitmap 清晰、>48px SDF）。
3. 配套：`QuadRender` 对 scaled glyph size 做 `glm::round` 像素对齐（消亚像素模糊）；`TextureLibrary` 解析 `FontAtlas_/FontGlyph_` → ClampNearest、`SDFFontAtlas_` → ClampLinear，与 premultiplied white 覆盖一致。

## 验证

- GUIWorkbench 新增 **"中文测试"** 页（只渲染中文，9–40px 固定字号 + 连续段落 + 标点混合 + 逐字对比行）作为验收界面。
- 修复后整段中文来自同一面孔，相邻字亮度/粗细一致，小字 bitmap 清晰不发虚。

## 预防

1. **CJK（及任何脚本）fallback 必须单一、全覆盖、风格统一**：多个同脚本 fallback 会按字符分散到不同 face，造成风格跳变。只有"主拉丁 + 一个中文 + 一个 emoji(色)"这种**不同脚本**才该并列。
2. 字体相关回归先加 CJK trace（`atlasIdx / fallback / page / scale`）而非靠截图猜；`scale=1.0 + 全 Bitmap + page=0` 可快速排除 DPI / 分页 / SDF 因素。
3. 小字（≤48px）走 bitmap（Nearest + 像素对齐）比 SDF 更锐利；SDF 仅用于大缩放场景。强制 SDF 给中文小字必然发虚。
4. DPI 当前由 `GUIAppHost` 用 `presentExtent/logicalExtent` 比值设置（非真机 DPR）；HiDPI 自适应需改为从系统 API 取真机 DPR——属后续架构改进，非本 bug 根因。
