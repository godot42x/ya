# GUI 框架 / 引擎模块化拆分 — TODO

> 对应 `plan.md`（2026-08-08 建立：RHI 命名、GUI 闭包独立、3D/gameplay 模块化）。
> 2026-08-08 review 迭代：修正 DeferredDeletionQueue 归属、补充
> Physics/IWindowProvider/RenderRuntime 耦合、固化 §9 决策。

## 已完成的前置（2026-08-08 提交）

- [x] `929c4489 [render/2d]` Render2D pass pipeline 收敛 + 录制会话实例化
- [x] `3870b568 [render/ui]` 共享 2D compose pass（三场景统一入口）
- [x] `003142ce [render/runtime]` viewport 编排收口（display target / compose 钩子 / world 开关）
- [x] `10c9b109 [editor]` editor compose 迁移 + 2D 模式裁剪 world 渲染

## Phase 0 —— 依赖收敛

- [x] 删除 `Render2D.h` 死 include：`RenderOverlay.h`、`IRenderStage.h`
- [x] `Render2D.h` 瘦身：FontManager/TextureLibrary/Render.h 移出或前置声明；
      findOrAddTexture 移入 cpp
- [x] 目录归位（用户指示：物理目录随模块走）——Phase 2 一并完成
- [ ] GUI public 头 include 卫生检查（禁 3D/physics/ECS 头）

## Phase 1 —— scene-core 剥离

- [x] `Node3D` 移出 `Node.h` 到独立 `Node3D.h`
- [x] Node2D 依赖闭包验证（只触达 Node 基类 + UIBase）
- [x] UISceneRenderer / Node2D 头归入 UI/（Scene/ 子目录）

## Phase 2 —— GUI 闭包拆库（xmake）

- [x] 新增基础设施库 ya-core / ya-rhi / ya-rhi-backend（共享，非 GUI 组成）
- [x] 新增 GUI 自有库 ya-ui（含 Font/Texture）/ ya-ui-scene
- [x] ya-engine 保持 shared 聚合导出（§9-B），内部拆 static
- [x] unity_group 按 target 分组
- [x] 导出宏按库拆分（BUILD_SHARED_YA 传递）
- [x] 单例归属固定（Render2D / FontManager / TextureLibrary → ya-ui）
- [x] 包依赖按库收敛（GUI 闭包：sdl3/glm/freetype/vulkansdk/vma/glad）
- [ ] GUI 闭包测试 target（Node2D/UISceneRenderer 测试只链闭包）

## Phase 3 —— 3D / gameplay 拆库

- [ ] ya-resource / ya-ecs / ya-scene-core / ya-scene-3d
- [ ] ya-render-graph（RDG 迁出 Render/Core/Graph）
- [ ] ya-gameplay / ya-physics
- [ ] ya-render-3d（先解耦 RenderRuntime.h 对 DeferredPipelineDebugViews 的 include）
- [ ] RenderFrameExtractor 数据桥归位
- [ ] PhysicsDebugDraw 改注入 line 收集器（§9-E）

## Phase 4 —— 宿主与验证

- [ ] ya-host / ya-editor
- [ ] 最小 GUI 宿主示例（仅链 GUI 闭包）
- [ ] shader 生成按消费方分组

## 验证命令

- `python3 Script/ya.py build --editor`
- `python3 Script/ya.py build --editor --build-arg=-r`（unity 缓存不刷新时）
- editor 运行：3D / 2D viewport、PIE、UI 面板
