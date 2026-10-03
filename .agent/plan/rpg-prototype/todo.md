# TODO

## 评审待办（2026-10-02 重排，细节见 `review-2026-09-30.md` 文末）

已完成
- [x] 双面 quad：精灵 / 延迟光照全屏 pass / billboard 都 `cullMode None` 画两遍 → 改剔除背面（364ab87c）
- [x] `Render/Frame` self 2.9–5ms 归因：主体是帧栅栏等待（`kFramesInFlight = 1`，CPU/GPU 串行，归 render-view-family M4），见 `r4-measurements.md` §7（3070fafe）

步骤 1 资源线收尾（51f0d090 / 344b0456）
- [x] 删 Loading 槽计数：审计只按间隔，不计入稳态探针，只断言不自愈
- [x] 环境光照由处理器维护每个场景选中的来源，消费侧 O(1)，去掉每帧分配与排序
- [x] 结论进 resource-system skill；IBL 基线换同尺寸截图；归档 `resource-handle-events`，遗留项移到 `resource-leftovers.md`

步骤 2 治理与小项
- [x] 合并 rpg-prototype 与 scene-2d-world-and-game-ui 为 2D 世界线（scene-2d 归档，剩余项见 `plan.md` §9）；从 game-ui-script-framework / ui-behavior-capabilities 转出 B2、`input.inject_key`、`viewAspect` 的归属
- [x] 按键注入（`input.inject_key`）提前，补 R0–R3 行为的自动化（`Script/automation/2d-rpg/`）
- [x] 合并 `callNamed` 与 `invoke`（B2 的前置）：`callNamed(..., ENamedCallError)`
- [x] 清空 `AssetManager::setFrameTaskSink` 悬空 sink（App 关闭时不清空）
- [x] 脚本 `callMethod` 接入统一编辑漏斗（B2 的前置）
- [ ] 自动化 `scene.load` 会 `stopRuntime` 但不重新 `startRuntime`，加载后脚本不 tick；端到端脚本因此只能从默认场景起跑

步骤 3 P0 契约冻结（前置：步骤 2 的计划线合并）
- [x] 契约矩阵：混合语义、sprite 与 3D 深度的关系、资源责任、实例格式与排序键（layer → y → order）冻结在 `P0-contract-matrix.md` 文末（C1–C5）；精灵只用画家顺序、不写深度，y-sort 逐对象开关
- [ ] 纯 2D View 的 graph 策略未选（workload 跳 stage 还是单独 2D graph），随步骤 6 / `render-view-family` P3 定；混合场景的像素验收随步骤 5

步骤 4 2D 模块（可与步骤 3 并行）
- [ ] `Scene2D` target：Sprite2D / Tilemap / Tileset 移出 `ya-render-3d`，拆开 `TilemapComponent.cpp` 的数据 / 查询 / 编辑 / 渲染展开
- [ ] `SpriteAnimation` 组件（D-T2 决策门已触发），删 Player / Npc / Chest 三处 `uvRect`
- [ ] `Sprite2DComponent` pivot / anchor

步骤 5 2D 合批核心（前置：步骤 3、4）
- [ ] 精灵实例化，一开始就与 `ScreenDrawList` 共享 quad 合批核心（不在 `Sprite2DStage` 里单独做再推倒）
- [ ] 画家顺序 + y-sort 作为合批排序键，删 `z = base − y·ε`
- [ ] 纹理表每帧重建 + `slotFor` 线性查找 → 直接映射（按资产槽身份作键）
- [ ] tilemap 每图层 / 区块静态实例缓冲，只在编辑时重建

步骤 6 纯 2D View 家族（前置：`../render-view-family/` P3 的 `PreparedView` / ViewFamily compiler）
- [ ] 纯 2D View 不跑 GBuffer / 光照 / 天空盒 / Bloom

步骤 7 能力进引擎（前置：步骤 2、4）
- [ ] B2 脚本对外接口收口（含 `viewAspect` / `viewSize` 去重、脚本函数不经 `App::get()`）
- [ ] 相机 2D 字段（每单位像素数、像素吸附、整数缩放）+ 相机跟随组件
- [ ] InputMap（命名动作）
- [ ] GUI 文本 reveal（逐字显示）
- [ ] 按格移动做成引擎 Lua 库
- [ ] 去硬编码：`"Player"`、`kSpriteTexture`、`TilemapGround` / `"Dialogue"` 命名约定

并行
- [ ] 定 flight 深度：`kFramesInFlight` 保持 1 或真 overlap（`waitFrameFence` 约 2ms/帧；归 `../render-view-family/`，本线不做）

## 用户报告（2026-10-02，不在评审 7 步内）

- [x] Pixel-perfect camera：`CameraComponent` 整数缩放 + 提取期 texel 吸附（`plan.md` §10）

## 已拆出

- 资源层每帧 resolve → `../archive/resource-handle-events/`（H1–H5 已完成并归档）
