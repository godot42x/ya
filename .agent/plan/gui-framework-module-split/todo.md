# GUI 框架 / 引擎模块化拆分 — TODO

> 对应 `plan.md`（2026-08-08 建立：RHI 命名、GUI 闭包独立、3D/gameplay 模块化）。
> 2026-08-08 review 迭代：修正 DeferredDeletionQueue 归属、补充
> Physics/IWindowProvider/RenderRuntime 耦合、固化 §9 决策。
> 2026-08-08 执行迭代（reset 到 a27b0034 重做）：目录随模块物理拆分、
> 每模块 glob 收集源码、导出宏单点化；闭包纯链接验证暴露跨层耦合，
> 列为后续解耦迭代（见 plan.md §10）。

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
- [x] 导出宏按库拆分：Api.h 单点定义 YA_*_API，xmake 统一注入
      （YA_SHARED + YA_MODULE_BUILD，模块零手写导出逻辑）
- [x] 单例归属固定（Render2D / FontManager / TextureLibrary → ya-ui）
- [x] 包依赖按库收敛（GUI 闭包：sdl3/glm/freetype/vulkansdk/vma/glad）
- [ ] GUI 闭包测试 target（Node2D/UISceneRenderer 测试只链闭包）
      —— 阻塞：纯链接验证暴露跨层耦合，见 plan.md §10；解耦完成前
      该 target 无法诚实链接（会级联拉入 resource/ecs/host/render-3d）

## Phase 3 —— 3D / gameplay 拆库

- [x] ya-resource / ya-ecs / ya-scene-core / ya-scene-3d
- [x] ya-render-graph（RDG 迁出到 RenderGraph/）
- [ ] ya-gameplay / ya-physics
- [x] ya-render-3d（RenderRuntime + pipeline + stage）
- [x] ya-physics（PhysicsDebugDraw 随目录归位到 Render3D/Debug，Physics
      不再触碰 Render2D；§9-E 以归位方式解决）
- [x] RenderFrameExtractor 随 Runtime/Application 归位到 Host
- [ ] ya-gameplay 拆分（Transform/Animation/Scripting/ResourceResolve）：
      ECS 组件/系统与 render-3d/host 深度耦合（ScriptApi 注册、离线
      cubemap 预处理），依赖图手术留待后续迭代；ya-ecs 暂为胖模块
- [ ] RenderRuntime.h 对 DeferredPipelineDebugViews 的 include 解耦
      （同模块内，优先级低）

## Phase 4 —— 宿主与验证

- [x] ya-host / ya-editor（各自独立 xmake.lua）
- [ ] 最小 GUI 宿主示例（仅链 GUI 闭包）
- [ ] shader 生成按消费方分组

## 执行迭代补充（2026-08-08）

- [x] Source 按模块目录物理拆分（Core/RHI/RHI+Backend/RenderGraph/UI/
      UI+Scene/Scene/Scene+3D/ECS/Resource/Render3D/Physics/Host/Editor），
      Render/Runtime/Platform/Bus 溶解
- [x] 每模块独立 xmake.lua + add_files("**.cpp") glob；Source/xmake.lua
      提供统一 ya_module() helper（导出宏/include 根/unity 分组单点化）
- [x] Shader 运行时（Slang/shaderc/spirv-cross）归入 RHI
- [x] 单头第三方实现归位：VMA/STB → RHI/Backend，TinyGLTF → Resource
- [x] ResourceStateTracker → RHI/Core；DeferredDeletionQueue → Core/Common；
      profiling 运行时状态/查询 → Core/Profiling
- [x] RenderViewportOverlayRecorder 拆分：compose pass → UI/Scene/
      Render2DComposePass，overlay pass → Render3D/Common/RenderOverlay
- [x] 删除全注释死代码 UIRender.* / UIComponentSytstem.* / SceneRenderer.h

## 验证命令

- `python3 Script/ya.py build --editor`
- `python3 Script/ya.py build --editor --build-arg=-r`（unity 缓存不刷新时）
- editor 运行：3D / 2D viewport、PIE、UI 面板
