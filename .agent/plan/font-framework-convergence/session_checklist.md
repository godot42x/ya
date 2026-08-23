# 字体框架收敛 Session Checklist

> 更新时间：2026-08-22

## 每轮开工前

1. 先读 plan.md / todo.md / feature_matrix.json；
2. 看工作区：git status --short；
3. 确认本轮只推进一个最小切片（数据结构 / 动态图集 / 光栅器 / FontManager 重写 / pending-flush / QuadRender）；
4. 涉及 GPU 资源时对照 Core Rule 6：任何纹理创建/重建都不得发生在帧录制中途；
5. 涉及缩放的改动先核对历史决策：98aac34b「single base atlas + scaled view = flat memory」，不得回退为 per-size atlas。

## 每轮进行中

1. 不改 `Sprite2D.slang` 的采样语义（首期 RGBA8）；R8 只能走「独立文本管线变体」子任务；
2. 图集恒单纹理（槽预算 TEXTURE_SET_SIZE 16），扩容=重建+repack，不新增多页；
3. 新字体纹理创建只允许出现在 `flushPendingGlyphs`（帧边界安全点）；
4. repack 后必须同步 scaled view uvRect + 重触发 FontAtlasTextureSink；
5. ImGui 字体栈（含 CJK/emoji merged）不在此计划内，迁移单独立项。

## 每轮收尾前

1. 运行证据：xmake b ya-render-resources / ya-gui-framework / GUIWorkbench + 相关闭包测试；
2. 视觉证据：--dump-snapshot-json 文本 item 抽样 + （涉及渲染变化时）重生成样式基线；
3. 更新 progress.md / todo.md / feature_matrix.json。

## 默认推进顺序

1. Module 1 数据结构 → 2 动态图集 → 3 光栅器 → 4 FontManager（含 pending/flush）→ 5 Shader（零改动确认）→ 6 QuadRender → 7 验收。

## 当前下一刀

1. Module 1：FontManager.h 数据结构（EFontRenderMode / Character 统一 / Font.renderMode）
2. Module 2：DynamicFontAtlas（shelf packing + repack）

## 已核对代码事实（2026-08-22）

- 图集：48px 单基 atlas RGBA8；`FontAtlas_*` 用 linear_clamp；16/行 shelf + 1px padding + pow2。
- `ensureGlyphs` 缺失码位走 `appendStandaloneGlyph`（每字符一张独立 Texture）→ CJK 资源爆炸。
- `Render2D::drawText` 在录制中调 `ensureGlyphs`（既有 Rule 6 违规，本计划修正）。
- `Sprite2D.slang::sampleTexturedSprite` = RGBA 采样 × tint、`.a<0.01` discard → 直接换 R8 图集会黑字。
- `EFormat::R8_UNORM` 枚举存在；`TextureUploadService` 只支持整纹理 staging+copy，无子区域增量。
- GUIAppHost 帧循环安全点：buildSnapshot（:1156）之后、cmdBuf->begin()（:1203）之前。
- UIFrameSnapshot text item 持 `shared_ptr<Font>`（不缓存 UV，repack 后 record 时重读 getCharacter 即可生效）。
