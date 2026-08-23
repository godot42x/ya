# 字体框架收敛 TODO

> 更新时间：2026-08-22

## 当前主线

- [ ] Phase 1 — Rasterizer 抽象 + 动态图集（Bitmap 后端）
- [~] Phase 2 — SDF 首刀已落地（FreeType 原生，治本模糊）；MSDF 待网络（两接缝替换）
- [~] Phase 3 — 字体栈（CJK/emoji）已打通；SDF 特效（描边/发光）仍预留
- [ ] Phase 4 — 自动模式选择（预留）

## Phase 1 — Bitmap 后端

- [x] Module 1：`FontManager.h` 数据结构重构（EFontRenderMode / Character 统一 / Font.renderMode）
- [x] Module 2：`DynamicFontAtlas` 新增（shelf packing + 单纹理 repack 扩容）
- [x] Module 3：`IFontRasterizer` 抽象 + `BitmapFontRasterizer`（迁移 FT_LOAD_RENDER 逻辑）
- [x] Module 4a：`loadFont`/`ensureGlyphs` 重写（进动态图集，删 standaloneTexture）
- [x] Module 4b：pending glyph 队列 + `flushPendingGlyphs(render)`（Rule 6 修正）
- [x] Module 4c：GUIAppHost 帧循环 flush 安全点（buildSnapshot 之后、cmdBuf->begin() 之前）
- [x] Module 4d：repack 后 scaled view uvRect 同步 + FontAtlasTextureSink 重触发
- [x] Module 5：Shader 首期零改动确认（RGBA8）；R8 可选子任务立项（文本管线变体，独立验收）
- [x] Module 6：`QuadRender::drawText` 去 bInAtlas 分支 + 去录制期 ensureGlyphs
- [ ] Module 7：Compose/Snapshot 零改动确认

## 验收

- [ ] ASCII 渲染不劣于现状
- [ ] CJK 混排进同一动态图集（无 per-字符纹理）
- [ ] 生僻字触发扩容后 UV 正确、无渲染错误
- [ ] 帧录制期零字体纹理创建（Rule 6）
- [ ] Vulkan + OpenGL 双后端编译；MSVC 门禁走查
- [ ] 全量 21 场景 + 137 闭包测试 + 样式基线门（基线重生成）
- [ ] IFontRasterizer 扩展点验证（新后端不触上层）
