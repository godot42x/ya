# 渲染管线去重 + RenderRuntime 服务化拆分 — 进度

> 建立日期：2026-08-22
> 最近更新：2026-08-22

## 当前状态

- Phase 1（Pipeline 无状态工具抽取）**已完成**。
- Phase 2（PerFlightFrameResourceSetBase 资源机制基类抽取）**已完成**。
- Phase 3.1（PipelineCoordinator Service）**已完成**。
- Phase 3.2（PresentationGraphService）**已完成**。
- Phase 3.3（Viewport 状态收敛）**已完成**。
- **三条主线全部完成。**

## 本轮已完成

### Phase 1 — makeShadowDebugResource 去重（上轮）

- `Common/PipelineCommon.h/.cpp`：`makeShadowDebugResource` 唯一实现 + 转发头 `include/Render3D/Common/PipelineCommon.h`。
- Forward / Deferred 删除本地副本，4 处调用点原样保留。
- 构建 + `RenderGraphCoreTest` 91/91 通过。

### Phase 2 — PerFlightFrameResourceSetBase（本轮）

- 新增 CRTP 基类 `Engine/Source/Framework/Render/Render3D/Common/PerFlightFrameResourceSetBase.h`（+ 转发头），收纳：
  - `_uploadArena` 创建/持有（label 参数化）
  - skinning descriptor layout/pool 创建/持有（label + set 号参数化）
  - `calculateSkinningCapacity`（统一为 Deferred 版容量策略，Forward 内联版收敛，日志文本用 `_resourceTag` 保留原有 "Forward"/"Deferred" 前缀）
  - `ensureSkinningCapacity`（含 fence-safe retire 语义原样保留）
  - `prepareSkinning`
  - `getSkinningDSL()`
- `DeferredFrameResourceSet` / `ForwardFrameResourceSet` 各自派生：
  - 只保留自己的 descriptor 集合（frame/light/SSAO/skybox DSL+DSP）、`Binding` 数组与 pass 状态
  - 通过 CRTP friend + `bindings()` accessor 向基类暴露每-flight skinning 槽
  - `DeferredFrameResourceSet::calculateSkinningCapacity` 保留为 private static 一行转发（`DeferredFrameResourceSetTestAccess` 测试入口不变）
  - Forward 的 `prepareSkinning` 保持 public 包装（`ForwardRenderPipeline.cpp:574` 外部调用）；Deferred 保持 private（内部 `prepare()` 调用）
- 验收：
  - `xmake b ya-render-3d` / `ya-render-graph` / `ya-game-runtime` ✅
  - render 相关测试全绿：`RenderGraphCoreTest` + `DeferredRenderPipelineTest` + `DeferredFrameResourceSetTest` + `*Shadow*` 98/98 ✅
  - 全量测试仅剩 GUI 布局失败（`ToolControlsTest.SplitPaneDividerDragChangesRatioAndEndsSession` 等 5 个）——stash 验证为基线既有问题，与本改动无关
  - 静态审计：`ensureSkinningCapacity` / `calculateSkinningCapacity` / `prepareSkinning` 实现唯一（除 ShadowFrameResources 独立副本，未动）

### Phase 3.1 — PipelineCoordinator Service（本轮）

- 新增 `Render3D/Services/PipelineCoordinator.h/.cpp`（+ 转发头）：
  - 收纳状态：`_renderPipeline` / `_pendingRenderPipeline` / `_forwardPipeline` / `_deferredPipeline` / `_pendingActivePipelineReload` / `_pendingRenderTargetFormatCommands`
  - 收纳方法：initActivePipeline / initForward / initDeferred / shutdownActivePipeline / applyPendingRenderPipelineSwitch / applyPendingRenderTargetFormatCommands / getSelectedForward / getSelectedDeferred
  - `ERenderPipeline` enum 移入 Coordinator；`RenderRuntime` 用 `using ERenderPipeline = PipelineCoordinator::ERenderPipeline;` 保持外部引用不变
  - 注入：render / hostServices / sharedResourceProvider / runtimeServices / reapplyViewportSink（viewport rect 仍归 RenderRuntime，Phase 3.3 才收敛）
- RenderRuntime 变纯转发：对外签名（getActivePipeline / getRenderPipeline / getPendingRenderPipeline / setPendingRenderPipeline / requestActivePipelineReload / isDeferredPipelineActive / requestRenderTargetFormat）不变；renderFrame 内 `applyPendingChanges()`
- 适配直接访问私有成员的外部/内部代码：AppAutomation.cpp、RenderRuntimeViewportDebug.cpp、RenderRuntimeViewportSnapshot.cpp
- 验收：`ya-render-3d` / `ya-game-runtime` / `ya-runtime`（含 Editor）构建通过；render 相关测试 98/98；运行时冒烟受环境限制（execv failed，未进入引擎代码）

### Phase 3.2 — PresentationGraphService（本轮）

- 新增 `Render3D/Services/PresentationGraphService.h/.cpp`（+ 转发头）：
  - 收纳：`_presentationGraphExecutors` / `_presentationImages` / `_presentationPostProcessor` / `_presentationPostProcessState`
  - 收纳方法：rebuildPresentationImages -> rebuildImages / renderPresentationPass -> render / getCurrentPresentationImageShared / getCurrentPresentationImageIndex；`createPresentationRenderTexture` / `makePresentationImportedTextureDesc` 移入实现
  - `PresentationExtensions` 扩展点类型移入 service（`Extensions`），`RenderRuntime::FrameInput::PresentationExtensions` 用 using 保留兼容
  - 注入：render + viewportDisplayImageProvider（取最终显示图，`getViewportDisplayImageShared` 仍归 RenderRuntime）；swapchain onRecreate 监听 token 改为 service
  - 清理时机不变：`_deleter.push("ScreenRT")` 仍在 RenderRuntime，lambda 改调 `service.shutdown()`
- RenderRuntime：删除 4 成员 + 4 方法；`getPresentationImageShared` 保留 public 转发；renderFrame 改调 `_presentationGraphService.render(...)`
- 验收：`ya-render-3d` / `ya-game-runtime` / `ya-runtime` 构建通过；render 相关测试 99/99

### Phase 3.3 — Viewport 状态收敛（本轮）

- 新增 `Render3D/Services/ViewportStateService.h`（纯状态、无渲染依赖）：收纳 `_viewportRect` / `_viewportFrameBufferScale` / `_bWorldSceneRenderEnabled` + 初始化判定
- RenderRuntime：删除 3 成员；public API（onViewportResized / getViewportRect / getViewportFrameBufferScale / getViewportExtent / setWorldSceneRenderEnabled / isWorldSceneRenderEnabled）签名不变，内部转发/读 service；ensureViewportRectInitialized 与 resize 通知管线保留在编排层
- 验收：`ya-render-3d` / `ya-game-runtime` / `ya-runtime` 构建通过；render 相关测试 98/98

## 计划收尾审计

- `RenderRuntime` 成员仅剩：基础注入 + 3 个 owned processor + `_deleter` + render/shader/commandBuffers/currentRenderAPI + 5 个 Service + viewport debug 缓存；无管线/presentation/viewport 直接状态 ✅
- `makeShadowDebugResource` / `ensureSkinningCapacity` / `calculateSkinningCapacity` 实现唯一（Shadow 独立副本经研判属不同资源策略，维持原状）✅
- 策略方法（shouldSkipTick / buildShadowState / captureShadowSettings / syncShadowSettings / viewport RT spec）未合并，各自独立 ✅
- 无新增 RenderImage / raw IImageView 公开泄漏；新 Service 仅持有管线指针与 RenderTexture ✅
- 对外签名：`IRenderPipeline` / `IRenderRuntimeServices` / `RenderRuntime` public API / `RenderRuntime::ERenderPipeline` 全部不变，App 层无感 ✅

- **ShadowFrameResources 不抽基类（决策更新）**：其 skinning 资源策略与 Forward/Deferred 不是同构——skinning DS 从共享 `_descriptorPool` 分配、扩容时不重建 pool/DS 只换 buffer+retire 旧 buffer；而基类是「扩容建新 pool+DS+retire 旧资源」。强行复用会改变资源创建/释放时机，违反铁律 A。表面重复（同名方法）实为不同资源策略，维持各自实现。
- Phase 3（Service 化）已开始侦察。

## 关键决策记录

- **不做 `IBasePipeline`**。
- 抽象边界 =「抽资源/内存机制，不抽渲染策略」。
- `PipelineCoordinator` 只做协调状态，不定义策略接口。
- `Common/` 共享代码模式：内部实现放 `Render3D/Common/`，公共头用 `include/Render3D/Common/` 转发。
- **资源机制基类用 CRTP**（`PerFlightFrameResourceSetBase<Derived>`）：头文件模板、无虚表/导出负担；派生类只需 friend + `bindings()` accessor；Binding 字段名契约编译期验证。容量策略以 Deferred 版为权威实现。
- Forward / Deferred 的 `prepareSkinning` 可见性差异（public vs private）通过派生层包装保留。