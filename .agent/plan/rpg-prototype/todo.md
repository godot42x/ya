# TODO

## 评审待办（2026-10-01 按前置重排，细节见 `review-2026-09-30.md`）

已完成
- [x] 双面 quad：精灵 / 延迟光照全屏 pass / billboard 都 `cullMode None` 画两遍 → 改剔除背面（364ab87c）
- [x] `Render/Frame` self 2.9–5ms 归因：主体是帧栅栏等待（`kFramesInFlight = 1`，CPU/GPU 串行，归 render-view-family M4），见 `r4-measurements.md` §7（3070fafe）

阶段 0 治理
- [ ] 合并四条 2D 计划线（rpg-prototype / scene-2d-world-and-game-ui / game-ui-script-framework / ui-behavior-capabilities）
- [ ] 冻结 `scene-2d-world-and-game-ui` P0 契约（混合语义、深度 vs 画家顺序、资源责任、graph 策略、实例格式）
- [ ] 按键注入（`input.inject_key`）提前，补 R0–R3 行为的自动化

阶段 1 独立小项
- [ ] 合并 `callNamed` 与 `invoke`（B2 的前置）
- [ ] 清空 `AssetManager::setFrameTaskSink` 悬空 sink（App 关闭时不清空；见 `../resource-handle-events/todo.md`）

阶段 2 2D 模块
- [ ] `Scene2D` target：Sprite2D / Tilemap / Tileset 移出 `ya-render-3d`，拆开 `TilemapComponent.cpp` 的数据 / 查询 / 编辑 / 渲染展开（tilemap 静态实例缓冲与 `SpriteAnimation` 的前置）

阶段 3 2D 合批核心（前置：阶段 0 的 P0 契约）
- [ ] 精灵实例化，一开始就与 `ScreenDrawList` 共享 quad 合批核心（不在 `Sprite2DStage` 里单独做再推倒）
- [ ] 纹理表每帧重建 + `slotFor` 线性查找 → 直接映射（可按资产槽身份作键）
- [ ] tilemap 每图层 / 区块静态实例缓冲，只在编辑时重建（前置：阶段 2）

阶段 4 纯 2D View 家族
- [ ] 画家算法 + y-sort（sprite + compose，不写深度），之后删 `z = base − y·ε`（前置：`../render-view-family/` 的 `PreparedView` / ViewFamily compiler）

阶段 5 能力进引擎（前置：阶段 0、阶段 2）
- [ ] 脚本 `callMethod` 接入统一编辑漏斗（本阶段前置；见 `../resource-handle-events/todo.md`）
- [ ] B2 脚本对外接口收口（含 `viewAspect` / `viewSize` 去重；另前置 `callMethod` 漏斗。game-ui S4–S7 未完成，原排在 S7 之后）
- [ ] `SpriteAnimation` 组件（D-T2 决策门已触发；前置：阶段 2）
- [ ] `Sprite2DComponent` pivot / anchor
- [ ] 相机 2D 字段（每单位像素数、像素吸附、整数缩放）+ 相机跟随组件
- [ ] InputMap（命名动作）
- [ ] GUI 文本 reveal（逐字显示）
- [ ] 按格移动做成引擎 Lua 库
- [ ] 去硬编码：`"Player"`、`kSpriteTexture`、`TilemapGround` / `"Dialogue"` 命名约定、脚本函数里的 `App::get()`（`viewAspect` / `viewSize` 归 B2）

并行
- [ ] `kFramesInFlight = 1` 帧栅栏等待（R4 的 2.9–5ms 主体；归 `../render-view-family/` M4，本线不做）

## 已拆出

- 资源层每帧 resolve → `../resource-handle-events/`（H1–H5 已完成）
