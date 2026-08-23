# 字体框架收敛进度记录

> 建立日期：2026-08-22
> 作用：记录字体子系统收口过程中的已完成切片、阶段证据、当前阻塞与下一轮接力点。

## 2026-08-22 — 计划建立（plan 模式，代码核对后落盘）

- 用户提出字体专项：当前文字模糊有锯齿，且要求一步到位收口（含中文/Unicode/emoji）。
- 讨论链：per-size 光栅（被否——回退 98aac34b flat memory 决策）→ SDF 先做（被否）→ **MSDF 直接接入（最终决策，Phase 2 落地）** → 首期按「方案 A 抽象 + Bitmap 后端」执行。
- 核对修正（对用户草案的 4 处关键修正已并入 plan.md §0/§2/§6）：
  1. 首期图集保持 RGBA8（Sprite2D.slang 是 RGBA×tint，R8 会黑字）；R8 降为可选子任务；
  2. TextureUploadService 只有整纹理上传，无子区域增量；
  3. 图集恒单纹理（TEXTURE_SET_SIZE 16 槽预算），扩容=重建+repack；
  4. 既有 Rule 6 违规（录制期 ensureGlyphs 建纹理）纳入本计划修正：pending/flush + GUIAppHost 帧边界安全点。
- 计划文件：plan.md / todo.md / feature_matrix.json / session_checklist.md 落盘。

## 下一轮直接接力点

1. Module 1：FontManager.h 数据结构重构（EFontRenderMode / Character 统一 / Font.renderMode）；
2. Module 2：DynamicFontAtlas（shelf packing + 单纹理 repack）。

## 2026-08-23 — Phase 1（Bitmap 后端基础设施）完成

### 完成模块
- M1 数据结构：`EFontRenderMode`（Bitmap/MSDF/SDF）、`Character` 统一（删 standaloneTexture，加 atlasSlot）、`Font.renderMode` + `atlas`/`rasterizer` 成员
- M2 `DynamicFontAtlas`（新）：shelf packing + 单纹理扩容 repack（onRepack 回调更新 UV）+ CPU 像素保留重传；RGBA8（Bitmap）
- M3 `IFontRasterizer` 抽象 + `BitmapFontRasterizer`（迁移 FT_LOAD_RENDER 灰度覆盖 → RGBA8）
- M4 FontManager 重写：loadFont 建 rasterizer+atlas 预光栅 ASCII95；`ensureGlyphs` → `requestGlyphs`（登记）+ `flushPendingGlyphs`（安全点捕获）；scaled view 保留（flat memory 决策不回退）；repack 用**裸指针回调**（修 Font→atlas→lambda→Font 引用循环泄漏）
- M6 QuadRender::drawText：去 bInAtlas/standalone 分支，全走动态图集；录制期不再建纹理
- M4c GUIAppHost：**双安全点 flush**——真实窗口路径（buildSnapshot 后、cmdBuf->begin 前）+ scenario-render snapshot-only 路径（无录制，同样安全）

### 验证
- 端到端：windowed host 加载 JetBrainsMono → 512x512 种子 atlas；em-dash U+2014 经 requestGlyphs→flush 捕获进图集（`flushed 1 glyphs`）；无 VMA 泄漏（修引用循环后 clean teardown）
- ya-gui-widgets-test 136/140（6 失败 = 5 预存 + DragOverDock[7570bcbc 引入的 stale 测试]）
- **基线/场景大面积失败为 7570bcbc 预存**（stash A/B 证实：无我的改动同样 rc=4；基线在 aa4660af 生成、7570bcbc 改布局未重生成）——不阻塞 font 主线，需另立事项

## 2026-08-23 — Phase 2 首刀：FreeType 原生 SDF 落地（模糊治本）

### 背景与决策修订
- msdfgen 无法获取（github 不可达；xmake 包 URL 也是 github）→ 计划决策 #7 的「MSDF 直接接入」受阻。
- **修订**：按计划接缝设计，先用 **FreeType 原生 `FT_RENDER_MODE_SDF`（2.14.1，零新依赖）** 实现 SDF flavor 治本模糊；MSDF 待网络可用后按「两接缝替换」（SDFFontRasterizer→MSDFFontRasterizer + shader median-of-3）升级，不动 atlas/cache/draw 路径。已在 progress/plan 记录。

### 实现
- `SDFFontRasterizer`（新）：`FT_LOAD_NO_BITMAP|FT_LOAD_NO_HINTING` 取 outline → `FT_RENDER_MODE_SDF` 距离场 → RGBA8（距离在 R，G/B/A=255）
- `Sprite2D.slang`：`FrameData.sdfSlotMask`（每纹理槽 1 bit）+ SDF 采样分支：`smoothstep(0.5 - fwidth(d)*0.5, 0.5 + fwidth(d)*0.5, d)`（**精确 1px 屏空间 AA**，尺度无关）
- `FrameUBO` 加 `sdfSlotMask`（Std140 对齐）+ 描述符 stageFlags 改 `Vertex|Fragment`（原仅 Vertex）
- `QuadRender`：`findOrAddTexture` 按 label `SDFFontAtlas_` 设 mask；`updateFrameUBO` 上传
- `DynamicFontAtlas` 加 label 参数（SDF 用 `SDFFontAtlas_`；sampler 解析同时认两种 label）
- `FontManager::loadFont` 加 `renderMode` 参数（默认 Bitmap 兼容）；GUIAppHost 默认 SDF + **96px 基分辨率**（2x 旧 48：spread 8px 在 13px 屏幕的渐变 ~1px → 小字锐利）

### 关键调试（记录防回归）
1. **首版 shader 用固定 `smoothstep(0.25,0.75)` 是错的**——对应整个 ±spread（8px 设计距离），13px 屏幕映射 ~2.2px 过渡，比旧路径还糊（GPU 实测拉普拉斯 42 vs 79）。改 fwidth 1px AA 后 69 vs 79。
2. **CPU `--dump-snapshot` BMP 不经 GPU shader**（快照光栅器直画位图），测不了 SDF——必须用 `--gpu-shot`（真实管线）。
3. **旧路径高拉普拉斯是混叠噪声**（边缘 164,46,229,180 锯齿）；SDF 边缘是干净渐变（106→229），"lap 略低"是好事不是退化。

### 验证
- windowed + `--gpu-shot`：SDF96 文本边缘 1-2px 干净渐变，与旧路径锐度相当且无锯齿；无 VMA 泄漏、rc=0
- 全 target 构建 0 error；ya-gui-widgets-test 136/140（6 = 5 预存 + DragOverDock[7570bcbc]）
- 对比图：/tmp/sdf_vs_bitmap.png（Bitmap vs SDF 同带并排）

### 待办
- MSDF 升级（网络可用后）：换 SDFFontRasterizer + shader median；CJK/emoji 字体栈（计划 Phase 3）
- 7570bcbc 的基线/场景过期问题另立事项（font 无新增回归，stash A/B 已证）

## 2026-08-23 — SDF 质量第二轮：baseline 对齐 + 模糊收敛（用户反馈驱动）

### 用户反馈
「字体的 baseline 没有对齐，还是有点模糊」

### 根因分析（GPU 截图逐像素取证）
1. **baseline 不齐**：逐字形垂直位置 ±1px 抖动（Bitmap vs SDF 的 dtop/dbot 有 -1/+1 波动）——亚像素 quad 位置导致笔画粗细不均、基线波浪。
2. **模糊观感**：非边缘过渡（实测 SDF 边缘 0-1px 已很锐），而是：
   - 小字号笔画亮度略低（222 vs 226，1px AA + 无 hinting 的细笔画变淡）；
   - 96px 基时距离场渐变 16px 设计 → 13px 屏幕 2.2px（偏宽）。

### 修复
1. **glyph quad 像素对齐**（QuadRender::drawText）：xpos/ypos `std::round` 到设备像素，advance 保持浮点——逐字形亚像素抖动消除（基线方差 1.99→1.75），Bitmap/SDF 路径共同受益。
2. **SDF 基分辨率 96→128px**：渐变 16px 设计 → 13px 屏幕 1.6px（锐利度提升；atlas 2048x1024 可接受）。
3. **shader 对比度增强**：`alpha = pow(alpha, 0.85)`——补偿小字号 SDF 细笔画偏淡。

### 验证（--gpu-shot 像素取证）
- tabbar 13px：stroke luma Bitmap 226 vs SDF-final 223（接近）；基线方差 1.75（优于 Bitmap 1.99）
- bigtitle 47px：SDF 221 vs Bitmap 216（**大字号 SDF 反超**——尺度无关优势显现）
- 全 target 构建 0 error；136/140（6 预存）
- 对比图：/tmp/final_sdf_top.png / /tmp/final_bmp_top.png

## 2026-08-23 — Phase 3：字体栈（fallback 链）+ CJK + Emoji 端到端打通

### 实现
- **Font/Character 结构扩展**：`FFontStackEntry`（fallback face：path + renderMode + baseSize + 独立 atlas/rasterizer）；`Character` 加 `atlasIndex`（0=primary，1..=fallback）、`designSize`（光栅化尺寸）、`bColor`（emoji 白 tint）
- **`EFontRenderMode::Color`** + **`ColorFontRasterizer`**（FT_LOAD_COLOR → BGRA→RGBA，CBDT/COLR emoji）
- **`FontManager::addFontFallback(render, name, path, mode, baseSize)`**：每个 fallback 独立 atlas（emoji ColorFontAtlas / CJK SDFFontAtlas_Fallback @ 256 初始）
- **flush 按链解析**：primary 先查 `FT_Get_Char_Index`（**修 .notdef 坑**——FT_Load_Char 对缺失码位加载 .notdef，不查 index 会捕获豆腐块而不是走 fallback）→ fallback 依次（index 探针 + rasterize）
- **scaled view 缩放修正**：`rescaleCharacter` 用 `viewFontSize / character.designSize`（fallback 字形按自己的 baseSize 缩放，否则 64px 捕获的 CJK 在 13px 视图被按 primary 128px 缩成 6.5px）
- **drawText 栈感知**：`atlasTextureFor(character)` 取对应 face 的 atlas；`bColor` 字形白 tint
- **makeScaledView 共享 fallback 链**（修 fallback UV 用错纹理）
- **捕获后失效修正**：`requestGlyphs` 返回 bool；**GUIAppHost flush 后若新捕获 → tree.invalidateLayout() + 全树 invalidateSubtree(Paint)**——不能在 paint 中 markLayoutDirty（UIElement::paint 结束时清 _bPaintDirty，且 stretch 布局 rect 不变时 paint 缓存命中旧 items/font 指针）
- **共享候选**：`findCjkFontCandidates()`（Noto→系统 PingFang/msyh）+ `findEmojiFontPath()`（seguiemj）
- **GUIAppHost 默认栈**：JetBrainsMono(SDF@128) + Hiragino/PingFang CJK(SDF@64) + seguiemj emoji(Color@32)
- **Unicode 验收页**（append 最后，不移动既有场景坐标）：简中/日/韩 + emoji + 混排 + 大字号中文

### 验证
- snapshot：CJK 行宽 273px（21 字符×13px，之前 '?' 是 164px）；GPU 截图 CJK 笔画 166 列均亮 201（真实字形）+ emoji 568 彩色像素
- 136/140 测试（6 预存）；dock 场景 ✓；gallery_drop 预存（stash A/B）
- **调试关键坑**：① FT_Load_Char .notdef 陷阱；② fallback 字形缩放分母；③ paint 缓存持有旧 font 指针 + paint 内失效被清 → 捕获后 host 级失效

## 2026-08-23 — Phase 3 收尾：共享候选 helper + 字体栈闭包回归

- **三处 CJK 候选列表去重**：AppLifecycle（game 主字体选择）、ImGuiSystem（merged CJK font）、GUIAppHost 全部改用 `FontManager::findCjkFontCandidates()`（bundled Noto → 系统 PingFang/msyh）；AppLifecycle 保留 JetBrainsMono 作最终 Latin 兜底
- **闭包回归测试**（WidgetLayoutTest，链接 ya-gui-closure-test）：
  - `ScaledViewScalesFallbackGlyphsByOwnDesignSize`：fallback 字形按自身 designSize 缩放（64px 捕获的 CJK 在 13px view = 13px，而非 primary 分母的 6.5px）——保护 Phase 3 关键坑 #2
  - `MeasureTextUsesResolvedFallbackGlyphAdvances`：捕获后 measureText 用真实 CJK advance（13px/字）而非 '?' fallback（8.125px）
- 验证：closure-test 166 通过（+2 新）、6 预存；ya-game-runtime 构建 ✓

## 2026-08-23 — 乱码 + Latin 失真修复（用户反馈轮）

### 现象
1. 中文有概率出现乱码
2. 字母有概率某些地方失真（毛刺或模糊）

### 根因（取证）
1. **乱码**：`DynamicFontAtlas` 每次 `upload()`/repack 创建**新纹理**（同 label）；`QuadRender::findOrAddTexture` 按 label 去重返回旧槽位 → descriptor 绑定旧图集 + 新 UV → 每次 repack/upload 后乱码帧。flush 只要有 pending glyph 就 upload → 概率性乱码。
2. **Latin 失真（模糊）**：SDF 细笔画（'l'/'i' 竖笔，13px 屏幕 ~1px 宽）在 **1px fwidth AA** 下无全亮核心（笔画宽 ≈ AA 宽，全程过渡）→ 核心亮度仅 154-180；另有半像素相位（161+139 分摊两列）。多次运行 0 diff 证实是系统性内容相关（非竞态）。
3. **毛刺**：扫描几乎为零（1 孤立点）——用户观感主要来自笔画淡 + 半像素相位。

### 修复
1. **findOrAddTexture repack-aware**：label 命中但 texture 指针变化 → 更新 binding + `++_resourceVersion`（flushScreen 的 updateResources 在批次后检查 version → **当帧生效**，乱码帧消除）
2. **0.25×fwidth AA**（0.5px 过渡）：1px 笔画恢复核心（tabbar 不回归，细笔画 154→169+）
3. **pow 0.85→0.72** 对比度：CJK 均亮 201→216、细笔画 184→213

### 验证
- 逐帧 CJK 检查：4 帧全部 166 列均亮 216（无乱码帧）
- 多次运行 0 diff（确定性）
- tabbar 224 / CJK 216 / 细笔画 169-213 / 孤立毛刺 1
- 136/140 测试（6 预存）；全 target 构建 0 error

## 2026-08-23 — 切页乱码 + 每帧纹理 churn 修复（用户复现反馈轮）

### 现象（用户复现）
「第一次看是 OK 的，切了 page，再回去，整个就乱码了」

### 根因（两个叠加）
1. **view 的 atlas 纹理指针陈旧（乱码主因）**：`drawText` 对 primary 字形用 `font->atlasTexture` **字段**（view 创建时的复制值）。切页触发新字形捕获 → flush `upload()` 换新纹理 → **base 的字段更新了，但 scaled view 的字段是旧复制**（refreshScaledView 只刷 characters）→ 旧纹理 + 新 UV → 整页乱码。fallback 路径早已用活句柄（`atlas->texture()`），primary 漏了。
2. **每帧纹理 churn（perf + 放大问题）**：某些码位（emoji 变体选择符 U+FE0F 等）整个字体栈都 rasterize 失败 → captureInto 失败不写 characters → **每帧重新请求 → 每帧 flush upload 整批 atlas（4096²+1024²+512²）** → 每帧换纹理（修复前每帧新纹理加剧绑定陈旧问题）。

### 修复
1. `Font::atlasTextureFor` primary 分支改用**活句柄**（`atlas->texture()`，与 fallback 一致）——切页/upload 后 view 立即拿到新纹理
2. `Font::missing` 集合：整个栈都失败的码位记入 missing，requestGlyphs 跳过、flush 不再重试（渲染回退 '?'）——消除每帧 churn

### 验证（automation 端口真机复现）
- 脚本：start Unicode → 点 Render 页签 → 点 Unicode 页签 → capture_screenshot → CJK 行 166 列均亮 217（**无乱码**）
- 纹理创建从「每帧」降到「2 个批次」（初始化 + 首次捕获），后续帧零创建
- 136/140 测试（6 预存）；全 target 构建 0 error
