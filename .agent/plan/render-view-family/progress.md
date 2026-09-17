# Progress

## 当前状态

- 阶段：R0/R1 契约已落地。R2 功能向 ViewFamily 靠近，架构未收口。真实 loop 是 `AppKernel` → `GameRuntimeTickOrchestrator::tickRender` → `RenderFrameCoordinator::record` → `recordFamily` → host present。`RenderRuntime` 类已删除。
- 已确认：RenderFrameInputs.h 已有四组输入；RenderFrameData 的 Scene snapshot owner 已与 View-owned draw buckets 分离。
- 已确认前置：多 OS window 的 surface/present 改造属于 gui-multi-os-window-editor，不在本计划重复实现；本计划也不引入 WorldInstance/WorldRegistry。
- 当前 checkpoint：4.0.3 计划状态已按 2026-09-17 review 修正。下一刀是合并 `RenderDeviceState` + `RenderFrameCoordinator` 为公开 `Renderer` owner。不要发明 PIE authoring PiP，也不要把产品双 viewport / 双 Surface 当成下一刀。
- 架构审计结论（2026-09-17）：DeviceState+Coordinator 是不完整拆分（friend 越界）；DeviceState 是 persistent renderer 不是 RHI device；`RenderSubmission::finish` 不 submit；输入/context 层过多；`recordFamily` 仍是 per-view 状态机外包装；Stage 仍有 `_frameInputs` / `_preparedViewSlot`；Scene recording 仍与 present Surface 耦合；`tickRender` 淹没 orchestration。Checkpoint C/D/E 均为部分完成。A/B 入口仍有效。host live Scene 列表已落地，默认产品帧仍提交当前 viewport Scene。
- 本轮完成 RenderFrameData ownership 收口：RenderFrameData 不再继承 SceneFrameSnapshot，而是持有 shared snapshot 并独立保存 View-owned draw buckets；Forward/Deferred/Shadow/Debug/EntityId 消费者通过显式路径读取 View buckets、shared skinning palettes 和 light presence。
- R2 第一切片：RenderRuntime::FrameInput 已显式携带 SceneRenderPlanInput；GameRuntime 将 sealed plan 与 parallel view recordings 传入，Runtime 在 command recording 前校验每个 task 的 snapshot 归属。
- GPU lifetime guard：FrameUploadArena 现在按 `flightIndex + frameToken` 识别一次 submission；同一 token 的第二次 begin 已改为幂等 no-op。Forward / Deferred / Shadow 的 frame descriptor 已改为 View-owned；skinning 已按 Scene family 持有，同 Scene 多 View 共享一份 SSBO，不同 Scene 不再以 flightIndex 为共享 key。

## 2026-09-17 checkpoint：按 tick 排期的字段（M1 P1b-2b）

- 唯一目标：把「按 tick 排期」的字段名从 frame 改成 tick，不改任何排期行为。这些值都是 host tick 序号（`App::_hostTick` / `EnvironmentLightingResultProvider` 的 tick provider），不是 Scene 版本、View 采样或 flight 槽。
- 落地：`TerrainProcessor::_nextResolveAuditFrame` 与 `EnvironmentLightingProcessor::_nextResolveAuditFrame`、`GameplayResourceBinding::_nextMaterialAuditFrame` → `_nextResolveAuditTick` / `_nextMaterialAuditTick`；`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`MATERIAL_AUDIT_INTERVAL_FRAMES` → `*_TICKS`（含 `AppAutomationConfigTest.ResourceResolveDerivedResourceGcDelayConstantIsStable` 的常量断言）；`TerrainDerivedResource::lastUsedFrame` → `lastUsedTick`；`TerrainComponent` 的 `getRebuildNotBeforeFrame` / `setRebuildNotBeforeFrame` / `_rebuildNotBeforeFrame` / `invalidate(rebuildNotBeforeFrame)` 与 `TerrainProcessor::markTerrainDirty(..., rebuildNotBeforeFrame)` → `*Tick`。
- 列对齐：改名缩短了成员名，`TerrainComponent`、`TerrainDerivedResource`、`TerrainProcessor`、`GameplayResourceBinding` 的声明列与 `TerrainProcessor.cpp` 里两处赋值块已按原列补回，diff 只剩标识符本身。
- 验证：`xmake b ya-render-3d-test`、`xmake b ya-testing`、`xmake b ya-game-editor`；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`git diff --check` 通过。
- 保留未完成：`DebugPrimitives::updateFrameUBO` / `_frameData`（按 `flightIndex` 索引，属 P2 flight 轴，随 `FrameFlightResources` 一起改）；P1c `SceneFrameSnapshot` → `SceneSnapshot`。

## 2026-09-17 checkpoint：perf 命名面收口（M1 P1b-2a）

- 唯一目标：把 perf 采样轴上残留的 `Frame` 改成 `Tick`，不改任何采样行为。host tick 的相位（logic / render / event pump / …）现在与代码其余部分说同一种话；trace 里再出现 `Frame/Logic` 就一定是别的语义。
- 落地：`perf::sample::renderFrame()` → `hostTick()`，key `Render/Frame` → `Tick/Total`（它就是整个 host tick 的 CPU/GPU 总量，也是 unaccounted 的基准）；`frameLogic` / `frameEventPump` / `frameFpsControl` / `frameRender` / `frameMainThreadCallbacks` / `frameAutomation` / `frameUnaccounted` / `frameRenderCallbacks` → `tick*`，key `Frame/*` → `Tick/*`；`YA_PERF_FRAME_SCOPE` → `YA_PERF_TICK_SCOPE`、`PerfFrameScopeTimerConditional` → `PerfTickScopeTimerConditional`（含 `frameSampleKey`、`ya_perf_frame_timer_`、局部 `frameValue`）。
- 名字选择：总量没按组名改成 `Render/Tick`，因为 tick 内还有一个 `Tick/Render`；`renderTick` 与 `tickRender` 只差词序，正是本轮要消除的阅读负担。总量用 `Tick/Total`，与 `Tick/*` 相位同组。
- profile 产物同步：`frameCycle` / `frameCpuMs` / `frameGpuMs` → `tickCycle` / `tickCpuMs` / `tickGpuMs`；`buildFrameCycleJson` → `buildTickCycleJson`。产物 Schema 在仓库内无消费方（`Script`、`Engine/Config`、`Example`、`.agent` 均无引用），因此随命名一起改，不保留别名。
- 明确的边界（不改）：用户可见文案 `Frame CPU:` / `Frame GPU:`（profiling 面板）与 `Frame {}`（stats 面板）保留，与 `--exit-after-frame` / `frame_index` 等外部键同类。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing`、`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`git diff --check` 通过。
- 保留未完成：tick 排期字段（`_nextResolveAuditFrame`、`MATERIAL_AUDIT_INTERVAL_FRAMES`、`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`getRebuildNotBeforeFrame()`）；`DebugPrimitives::updateFrameUBO`（随 P2 flight 轴）；P1c `SceneFrameSnapshot` → `SceneSnapshot`。
- 本轮扫到的同轴遗留（未做，登记备查）：`AppAutomation::isFrameAutomationEnabled` / `hasFrameAutomationConfig`、`shouldRequestQuitAfterFrame`、`AppAutomationTickContext` 参数名 `frameContext`、`AppTaskManager::registerFrameTask`/`hasFrameTasks` 仍是 host tick 语义。

## 2026-09-17 checkpoint：host view state 命名（M1 P1b-1）

- 唯一目标：把 `AppRenderFrameState` 改成 `HostViewState`，不改任何行为。它保存的是 host 自己那个 view 的 clock、viewport rect 与相机矩阵，既不是 per-View packet，也不是 present/swapchain 状态；`frameState` 这个名字会让读者以为它就是「这一帧的渲染状态」。
- 落地：`Applications/GameRuntime/AppRenderFrameState.h` → `HostViewState.h`（含 `include/GameRuntime/` 转发头）；`AppRenderState::frameState` → `hostView`、`extensionFrameState` → `extensionHostView`；`AppRenderServices::getRenderFrameState` / `setExtensionRenderFrameState` / `clearExtensionRenderFrameState` → `getHostViewState` / `setExtensionHostViewState` / `clearExtensionHostViewState`；`EditorViewportCompositor` 与 `EditorSurfaceContext` 的声明、定义与参数名同步（`makeEditorSurfaceContext` 的 `frame` → `hostView`）。消费方同步：`GameRuntimeTickOrchestrator`、`AppLifecycle`、`AppAutomationControlService`、`EditorModule`、`EditorLayer.Interaction`。
- 前置：阻塞这次改名的 GameEditor 在途改动已先单独提交（`[editor] own the authoring viewport compose in a compositor`、`[gui/layout] drop the applyArgs nodiscard callers intentionally ignore`），本轮才有干净、可编译、可独立提交的边界。
- 列对齐：类型名变短会打断声明列，`AppRenderState` / `AppRenderServices` / `EditorViewportCompositor` / `EditorSurfaceContext` 的成员与参数已按仓库风格重新对齐，没有留下错位。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing` 通过；`xmake r ya-testing --gtest_filter='EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppAutomationConfigTest.*:AppKernelTest.*'` 66/66；`git diff --check` 通过。
- 保留未完成：perf key / profile scope `Frame/*`（`PerfKeys.h` + 约 30 处调用）；tick 排期字段（`_nextResolveAuditFrame`、`MATERIAL_AUDIT_INTERVAL_FRAMES`、`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`getRebuildNotBeforeFrame()`）；`DebugPrimitives::updateFrameUBO`（按 `flightIndex` 索引，随 P2 flight 轴）；P1c `SceneFrameSnapshot` → `SceneSnapshot`。

## 2026-09-17 checkpoint：host tick 命名（M1 P1a）

- 唯一目标：把 host 调度批次的命名从 frame 改成 tick，不改任何行为。理由与逐符号清单见 `temporal_semantics.md`：`frame` 现在同时表示 host tick、Scene 内容版本、View 渲染采样、录制作用域、GPU submission、flight 槽和 Surface present 次数，单窗口单 View 时这些值恰好同步递增，多 View 后不再成立。
- 落地：`GameRuntimeFrameOrchestrator` → `GameRuntimeTickOrchestrator`（类 + `.h`/`.cpp` + `include/` 转发头；`prepareRenderFrameState` → `prepareHostViewState`）；`RenderRuntimeClockState` → `HostClockState` 且字段 `frameIndex` → `hostTick`；`App::_frameIndex`/`getFrameIndex()`/`currentFrameIndex()` → `_hostTick`/`getHostTick()`/`currentHostTick()`；`IRenderRuntimeServices` 与 `RenderDeviceState` 的 `getFrameIndex()` → `getHostTick()`；`setFrameIndexProvider`/`_getFrameIndex` → `setHostTickProvider`/`_getHostTick`；`lastUsedFrame` → `lastUsedTick`、`TerrainProcessor::currentFrame()` → `currentHostTick()`；`SceneRenderScheduler` 的 `beginFrame`/`clearFrame`/`isFrameOpen`/`frameId()` 与 `SceneRenderPlan::frameId` → tick 命名；automation 的 `markFrameCompleted`/`completedFrameCount`/`shouldAutomationExitAfterFrame`/`exitAfterFrame`/`AppAutomationFrameContext`/`onFrameCompleted`/`recordedFrameIndex`/`earliestFrameIndex`/`screenshotFrameIndex`/`screenshotWarmupFrames`/`screenshotSettleFrames` → tick 命名。
- 明确的边界（不改名）：CLI / config 键 `--exit-after-frame`、`exitAfterFrame`、`screenshot.frame`、`smoke.*.frame`；automation 协议键 `frame_index`、`warmup_frames`；Lua 脚本 API `time.getFrameIndex`；UI 文案 `Frame {}`；`DeferredDeletionQueue::currentFrame()`、`VulkanRender::_frameIndex`、`Instrumentor::_frameIndex`（fence 轴 / profile 事件索引，与本语义无关）。
- 刻意延后：`AppRenderFrameState` → `HostViewState`（当时消费方 `EditorModule.cpp` 与 `EditorViewportCompositor.{h,cpp}` 属于另一条在途改动；该改名已由后面的 M1 P1b-1 checkpoint 落地）；`DebugPrimitives::updateFrameUBO`（按 flightIndex 索引，属 P2 flight 轴）；perf key / profile scope `Frame/*`（`PerfKeys.h` + 约 30 处调用）；`RenderRuntimeSnapshotTest` → `RenderFramePlanningTest`；`_nextResolveAuditFrame` 等 tick 排期字段。
- 验证：`xmake b ya-render-3d-test`、`ya-game-runtime`、`ya-game-editor`、`ya-testing`、`ya-gui-closure-test`、`ya-gui-headless-host-test`、`ya-gui-minimal-host`、`GUIWorkbench` 全部通过；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*'` 25/25；`xmake r ya-gui-closure-test --gtest_filter='AppKernelTest.*'` 3/3；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:HostSceneRenderSubmitTest.*'` 20/20；`git diff --check` 通过。
- 与本轮无关的既有失败（已确认不在改动面内）：`ya-gui-widgets-test` 因 `Engine/Test/Source/GuiFrameInspectorTest.cpp` 找不到 `GUI/Compose/GuiFrameInspectorOverlay.h`（该 target 的 deps 不含 compose 模块）而编译失败；`ya-gui-headless-host-test` 有 3 个 GUI snapshot 断言失败（`lastItemCount` 期望 > 0，实际 0，日志显示 `draw=0 painted=6`）。两者都落在另一条进行中的 GUI 改动上，本轮未触碰任何 GUI widget / layout / snapshot 代码。

本轮同时写入计划结论：`IRenderRuntimeServices` 作为抽象没必要（唯一实现者、无 mock、死方法 `getGameplayResourceBinding()`、与 `EnvironmentLightingResultProvider` 重复、并被 pass 用来在录制期查 live ECS），处置见 `plan.md` 3.9 与 `temporal_semantics.md` M8，随 P3 `PreparedView` 收口一起删除。

## 2026-09-17 checkpoint：修正 C/D/E 计划状态

- 唯一目标：把计划改成与源码一致。C/D/E 从“完成”改为“部分完成”；删除把 DeviceState+Coordinator 当成闭环、把 RenderRuntime 当成现行 orchestrator 的叙述；下一刀改为公开 `Renderer` owner。
- 本刀不改引擎代码、不跑测试。
- 保留未完成：4.0.3 Renderer 合并；FrameRecording/FlightResources 拆名；PreparedView；Stage current-view；压缩 tickRender；Surface 与 View 正交；产品双 Scene 显示；双 Surface GPU。

## 2026-09-17 checkpoint：host 提交 live Scene 列表

- 唯一目标：host 不再把 `getActiveScene()` 当成唯一可提交的 Scene。`HostSceneViewSubmit` 列出本帧 live Scene viewport；同 Scene* 共享 extract；不同 Scene 隔离 snapshot/family；recording `derivedScene` 从列表查找。
- GameRuntime 默认仍把当前 viewport Scene（及同 Scene camera overlay）放进列表。未接 PIE authoring 第二 viewport，未做双 Surface。
- 验证：`xmake b ya-game-runtime`、`xmake b ya-testing`、`xmake r ya-testing --gtest_filter='HostSceneRenderSubmitTest.*'`（3/3）、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（76/76）、`git diff --check`。
- 保留未完成：产品帧同时显示两个 Scene viewport；display-root inspector getter fallback；双 Surface GPU 验收；ViewHistoryStore 待 TAA；sceneRevision 仍为 0。

## 2026-09-17 checkpoint：family-scoped derived Scene

- 唯一目标：coordinator 不再用一个 frame-wide Scene* 服务所有 family。每个 recording 携带产出该 snapshot 的 host Scene。
- 删除 `RenderFramePlan::derivedScene`。录制前对每个 unique derived Scene 调用 `prepareDerivedState`；family record context 从该 family 的 recording 取值。同 SceneId 必须共享指针，不同 SceneId 不得共享。Scheduler 仍不持有 Scene。Host 当前仍只提交一个 live Scene。
- 验证：`xmake b ya-render-3d`、`xmake b ya-game-runtime`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（76/76）、`git diff --check`。
- 保留未完成：host 提交两个 live Scene 的产品录制（随后由 live Scene 列表切片落地）；display-root inspector getter fallback；双 Surface GPU 验收；ViewHistoryStore 待 TAA。

## 2026-09-17 checkpoint：拆除 RenderRuntime facade

- 唯一目标：host frame 编排、device 持久状态、Scene view rendering、compose/present 不再由一个类同时拥有。
- `RenderDeviceState` 拥有 backend、persistent services 与 safe-point mutation（`applyViewportResize` / `applyPendingMutations` / `prepareDerivedState`）。`RenderFrameCoordinator::record(RenderFramePlan)` 创建 submission、按 family 调用 `recordFamily`、compose/present。`RenderFramePlan::derivedScene` 是本帧 Scene，不是 InitDesc locator。Host `AppRenderState` 拥有 `bWorldSceneRenderEnabled` 与 viewport rect；空 `sceneRender` 是 UI-only。删除 `RenderRuntime`、`ViewportStateService`、`getActiveScene`。未引入空的 `ViewHistoryStore`（TAA 无消费者）。
- 验证：`xmake b ya-render-3d`、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（72/72）、`git diff --check`。
- 保留未完成：display-root inspector getter fallback；双 Scene 产品录制；双 Surface GPU 验收；ViewHistoryStore 待 TAA。

## 2026-09-17 checkpoint：ViewFamily renderer

- 唯一目标：pipeline 不再以 `tick()/beginTick()/getCurrent*()` 表示一次 View；一个 family graph 可产生多个 typed `RenderViewOutput`。
- `IRenderPipeline::recordFamily` 取代 `tick`；coordinator 别名 `ISceneViewFamilyRenderer`。Deferred/Forward 对同一 family 建一个 graph：skinning prepare 一次，per-view 追加 shadow/GBuffer/forward/post；`familyPredecessor` 把门后续 View。export/pass 名走 `makeViewGraphName`。删除 `_currentGBufferResources` / `_publishedGraphOutputs` / `_currentPostprocessOutput` / `_currentOverlayFrameInputs` / `_currentEnvironmentLighting*` 作为 publish source。
- RenderRuntime 按 `plan.viewFamilies` 调用 `recordFamily`，`RenderViewOutputTable` 只 ingest `result.views`；debug catalog 优先 typed output。`_debugViews` / Forward `_viewportResources.publish` 只保留 display-root inspector fallback。未改文件名为 `*ViewFamilyRenderer` / `*GpuResourceLibrary`。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（71/71）、`git diff --check`。
- 保留未完成：Checkpoint E；display-root inspector getter fallback；双 Scene 产品录制；双 Surface GPU 验收。

## 2026-09-17 checkpoint：typed View/Pass resources

- 唯一目标：SSAO / Light / EntityId / Overlay / Forward-debug / postprocess 的 graph execute 不再更新 persistent Stage/Processor；每个 View 的 DS/UBO 由 typed `ViewResources` 子结构持有。
- `DeferredFrameResourceSet::ViewResources` / `ForwardFrameResourceSet::ViewResources` 组合 frame Binding 与 typed pass bindings（`SSAOPassBindings`、`DeferredLightingPassBindings`、`EntityIdPassBindings`、`OverlayPassBindings`、`ForwardDebugPassBindings`、`PostprocessPassBindings`）。Bloom/BasicPost 的 viewId map 删除。PointShadow instance/cull packet 进入 Shadow View Binding。
- graph-resolved CIS 仍可在 execute 写入 captured View DS；Stage 只保留 layout/PSO。未重命名 FrameResourceSet / Stage，也未改 `recordFamily`。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（66/66）、`git diff --check`。
- 保留未完成：Checkpoint E；display-root inspector getter fallback；双 Scene 产品录制；双 Surface GPU 验收。

## 2026-09-17 checkpoint：SceneFamily owner

- 唯一目标：同一 submission 里 Scene A/B 的 skinning GPU packet 互不覆盖；同 Scene 的多个 View 引用同一个 `SceneFamilyResources`。
- `SceneRenderScheduler::seal()` 物化 `SceneViewFamilyPlan[]`（按 sceneId/revision/snapshot/renderFlags/policyId 分组）。`RenderSubmission::allocateSceneFamily()` 按 key 返回稳定 owner。Forward/Deferred/Shadow `prepareSkinning` 写入 family SSBO，不再按 `flightIndex` 共用 `PerFlightFrameResourceSetBase` 槽位。
- 未改 Stage CIS、processor viewId map、PointShadow indirect、也未把 pipeline 改成 `recordFamily`。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*'`（57/57）、`git diff --check`。
- 保留未完成：Checkpoint D–E；pipeline last-view 图袋；双 Scene 产品录制；双 Surface GPU 验收。

## 2026-09-17 checkpoint：RenderSubmission owner

- 唯一目标：一次 command submission 的 command buffer、frame token、upload arena、transient descriptor、keepalive 与 finish 由 `RenderSubmission` / `RenderSubmissionPool` 维护；pipeline/resource set 不再各自 `beginSubmission()`。
- 删除 `RenderSubmissionContext` / `RenderSubmissionTable`。Forward/Deferred/Shadow `beginView(RenderSubmission&)` 从 submission 分配 upload slice 与 descriptor set；`writeViewPayloads(arena)` 只留给 identity 单测。RHI cmd begin/end 仍在 RenderRuntime coordinator。skinning 仍在 `PerFlightFrameResourceSetBase`。
- 验收：同 token 连续两 View slice 不覆写；finish 后 keepalive 存活到该 flight 新 token；二次 finish、finish 后 allocate/retain、同 token 再 acquire 均被拒绝。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*'`（53/53）、`git diff --check`。`xmake b ya-game-runtime` 因既有 `ModelComponent._childNodes` 编译失败，与本切片无关。
- 保留未完成：Checkpoint B–E；skinning 仍按 flight 共享；Stage CIS / last-view 图袋 / processor viewId map；双 Scene 录制；双 Surface GPU 验收。

## 2026-09-16 plan：Renderer 生命周期与 ViewFamily graph（4.0.2 重审）

- 唯一目标：review 当前计划和代码，并把可执行的大尺度重构写入 plan；本轮不改引擎代码。
- 修正遗漏：原三层 Device/Submission/View 漏了 SceneFamily；同 submission 双 Scene 下，per-flight skinning/scene GPU packet 仍会串。
- 修正图粒度：不再固定一 View 一 graph；`SceneViewFamilyPlan` 是 graph 编译单位，同 Scene/策略的 family-shared work 与 per-view branch 同图，不相关 family 分图但可共 submission。
- 状态/逻辑原则：allocator/submission/history 等维护不变量的 owner 保持状态+行为；snapshot/plan/prepared view/result 是值；pass recipe 只持 device-lifetime 配方。
- `begin/end` 结论：保留 RHI command protocol；删除 persistent pipeline 上表达隐式 current state 的 tick/beginTick/beginView/getCurrent。
- 执行顺序改为 A Submission owner → B SceneFamily owner → C typed pass resources/recipes → D family renderer → E 拆 RenderRuntime facade。
- 允许类名/文件名重构并删除 legacy API；每个 checkpoint 仍需单一验收目标。

## 2026-09-16 plan：配方与 View 数据分离（已被上述 4.0.2 重审收编）

- 唯一目标：把结论写进计划，不写代码。Stage/Pipeline 混有 last-view 数据，禁止再叠 viewId map。
- 目标三层：Device 配方（pipeline/layout/池）、Submission（arena/skinning）、View（Binding + RDG persistent）。录制 lambda 不改 Stage 成员。RDG 不接管 DS/UBO。不抽 BaseRenderPipeline。
- 原“全部 CIS 塞进 Binding”只保留为问题清单，不再作为目标形态；typed View/Pass resources 取代 mega Binding。
- 双 Scene / 双 Surface 排在 4.0.2 之后。Bloom/BasicPost 的 viewId map 视为迁移期 hack。
- 未改引擎代码；plan.md 3.3 / 4.0.2、todo、feature_matrix、session_checklist 同步。

## 2026-09-16 checkpoint：View-own postprocess / bloom display 资源

- 唯一目标：同一 submission 里两个 View 不再共用 postprocess/bloom 的 GPU 图和 descriptor set，避免两路画面相同并闪烁。
- 根因：tone mapping 默认开启。`BasicPostprocessing` / Bloom extract-composite 只有一套 CombinedImageSampler set，View B 在同一 cmdbuf 里原地 update 后，View A 已录制的 bind 在 submit 时也采样 View B。Bloom/Postprocess 输出曾是 transient，同 extent 时会被 registry 回收给下一个 graph。
- `ViewDescriptorSetAllocator` 支持 CombinedImageSampler。`BasicPostprocessing` 与 Bloom extract/blur/composite 按 viewId（blur 再加 passIndex）分配独立 set。`Postprocessing.Output` 与 `Bloom.CompositeOutput/Extract/BlurPing/BlurPong` 走 `createViewPersistentTexture`。Forward/Deferred 把 task viewId 传入 bloom/finalize。
- 未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴；SSAO/EntityId 仍有 maxSets=1 的辅助路径，不在本切片。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ViewKeyedPostprocessTexturesStayIndependentAcrossSequentialGraphs:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:PostProcessingStageTest.*:CameraFrustumOverlayTest.*:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（56/56）、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`git diff --check`。
- 保留未完成：双 Scene 录制；双 Surface；PointShadow indirect 仍是 flight 轴；viewport click picking 无 camera mesh。

## 2026-09-16 checkpoint：overlay View 与 host viewport identity 分离

- 唯一目标：修正 camera preview 的硬编码、过大 frustum、以及点击 camera 后主 viewport 缩小到角落并闪烁。
- `SceneRenderRequest` / `SceneViewportTask` 增加 `composeOntoViewId` + `composeRect`。`viewportRect` 只描述该 View 自己的离屏 RT；compose dest 不再进入 `cameraForViewRecording` 的 host rect。Runtime 从 plan 收集 insets，不再依赖 `kCameraPreviewViewId`。
- Forward/Deferred 仅在 `ownsHostViewport()` 时 `requestViewportResize`。Overlay graph 使用 View-local RT spec extent（`frame.view.viewportExtent`），host `_viewportRTSpec` 保持 WorldView[0] 尺寸。
- Camera frustum 改为 compact gizmo：沿 FOV ray 画 `visualDepth`，不 unproject clip far。`makeViewDisplayInsetRect` 是 host layout helper，不属于 frustum overlay。
- 未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴；material preview 仍未作为独立 request。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='CameraFrustumOverlayTest.*:ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`git diff --check`。
- 保留未完成：双 Scene 录制；双 Surface；PointShadow indirect 仍是 flight 轴；viewport click picking 无 camera mesh。

## 2026-09-16 checkpoint：Editor world Camera preview PiP

- 唯一目标：产品路径为选中的 world Camera 再 submit 一个同 Scene 的 SceneRenderRequest；录制后把该 View 的 output blit 到主 viewport 右下角；world Camera 在主 View 上画 3D frustum 线。
- GameRuntime 在 Editor 选中 `CameraComponent` 时 submit `kCameraPreviewViewId`；standalone 自动预览第一台非 primary camera。Preview 使用独立 output extent，与 primary 共享 Scene snapshot。
- `ViewComposeInput.insets` 描述要合成到 primary display RT 的 View；Runtime 从 `getViewOutput` 取样，不写 swapchain。这不是双 Surface。
- World Camera 可视化用 viewport overlay 的 `RenderOverlayLine3D` / `makeWorldLine`，不是 screen-space Line2D，也不是新 mesh。Hierarchy 点击即可选中；viewport 点击仍需要 mesh/billboard 写 entityId。
- `getViewportExtent` / `buildViewportSnapshot` 优先读 published primary output，避免 preview 的较小 RT 污染 editor picking 和 camera aspect。
- 未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴；material preview 仍未作为独立 request。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='CameraFrustumOverlayTest.*:ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（53/53）、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`git diff --check`。
- 保留未完成：双 Scene 录制；双 Surface；PointShadow indirect 仍是 flight 轴；viewport click picking 无 camera mesh。

## 2026-09-16 checkpoint：循环录制 SceneViewportTask

- 唯一目标：RenderRuntime 录制 sealed plan 里的每一个 SceneViewportTask；同一 Scene 的两个 View 共享一份 SceneFrameSnapshot，并各自发布 output。
- `SceneRenderPlanInput` 用与 `plan.viewportTasks` 平行的 `SceneViewRecording` 替换单一 task 指针；`complete()` 要求数量、指针和 snapshot 都对齐。`cameraForViewRecording` 把 task 的矩阵/extent/`frameData` 盖到 host camera 上。
- Runtime 对每个 recording tick pipeline、立即 publish；compose/present 仍用 primary（第一个 task）。GameRuntime 对每个 task `prepareView`，`viewFrameDataPerFlight` 按 flight 持有 View-owned `RenderFrameData`。
- 未让 GameRuntime submit 第二个 camera；未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（48/48）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：GameRuntime 仍单 camera submit；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：View-keyed persistent keys / RTs

- 唯一目标：Forward/Deferred viewport（含 GBuffer/SSAO）persistent key 按 View 分开，两个 View 不能共用一张 GBuffer/color。
- 新增 `makeViewPersistentTextureKey` / `createViewPersistentTexture`：key 为 `{base}.view{id}`；viewId 0 仍是 `.view0`，避免缺失 task 时回到未分名的全局 key。
- Forward `BuildInputs.viewId` 与 Deferred `BuildInputs`/`PassContext`/`SSAO` params 从 `frame.view.task->viewId` 传入；graph export 名保持单 graph 内唯一，不在本切片改成 mega-graph。
- 未循环 SceneViewportTask，因此 Runtime 仍只录制一个 View；PointShadow indirect 仍是 flight 轴。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（47/47）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：仍只录制单 View；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：View-keyed output handles

- 唯一目标：每个 View 拥有独立的 output/extent/format 句柄；发布 View B 不得改写 View A；ViewportStateService 不再表示 View 输出身份。
- 新增 `RenderViewOutput` / `RenderViewOutputTable`：按 flight 持有堆上独立 record，追加 View 不会因 vector 扩容让已发布句柄失效。
- `SceneViewportTask` 在 seal 时写入 `output.viewId` 与 `output.extent`；同一 Scene 的两个 task 可有不同 extent，仍共享 snapshot。
- Runtime 在 compose 后把当前 pipeline 的 color/display/depth/entityId 发布到该 task 的 ViewId；`getViewOutput(viewId)` / display getter 优先读表。
- 未循环 SceneViewportTask，未把 Forward/Deferred persistent key（`ForwardViewport.Color`）改成 View-keyed，因此真实 GPU 仍是一份 viewport RT。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（38/38）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：仍只录制单 View；pipeline 单一 persistent RT；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：RenderRuntime submission 保活到 fence-safe reuse

- 唯一目标：RenderRuntime 持有一次 submission 直到该 flight 的 GPU fence 安全复用；overlay 与 graph-exported image 不得只活到 `renderFrame()` 返回。
- 新增 `RenderSubmissionTable`：按 `MAX_FLIGHTS_IN_FLIGHT` 持有 `RenderSubmissionRecord`（context + keepalives）。同 token 幂等保留 keepalives；新 token 才释放上一轮 owner。`markRecordingComplete` 不 drop keepalives。
- `beginFrameCommandBuffer` 在 cmdBuf begin 后写入 live submission；tick 使用表内 context，不再构造临时 `RenderSubmissionContext`。overlay 在 tick 前 retain 到 table 与 cmdBuf。
- 录制结束后把 viewport display / viewport / postprocess 导出图 retain 到同一 flight；`renderFrame()` 返回后 `getLiveSubmission(flight)` 仍 occupied。
- 未把 FrameUploadArena / descriptor pool 搬进 Runtime，也未循环 SceneViewportTask，也未建立独立 View output。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderSubmissionTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（32/32）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：仍只录制单 View；无独立 View output；无双 Surface；PointShadow indirect 仍是 flight 轴；upload arena 仍由 pipeline resource set 持有。

## 2026-09-16 checkpoint：Shadow beginSubmission / beginView

- 唯一目标：Shadow resource set 为同一 submission 的每个 View 提供独立 Binding（cascade/face descriptor set + upload slice），View B 的准备不得改写 View A。
- `ShadowFrameResources` 接入 `PerFlightFrameResourceSetBase`（skinning）与 `ViewDescriptorSetAllocator` / `beginFrameResourceSubmission` / `writeUploadSlice`；删除 `prepare` / `getBinding(flightIndex)`。
- `IShadowTechnique::prepare` 改为接收 `RenderSubmissionContext` / `RenderViewRecordingContext`；Forward/Deferred pipeline 通过 `ShadowStage::prepareView` 传入与 viewport 相同的 submission/view。
- Directional/Point graph pass 从 `getViewBinding(flight, viewSlot)` 读取 Binding。PointShadow indirect instance/cull 缓冲仍按 flight 覆写，不在本切片展开。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewBindingTableTest.*'`（8/8，含 ShadowViewSlicesStayIndependent）、snapshot/deferred/arena 回归 27/27、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：RenderRuntime 不持有 submission 到 fence；仍只录制单 View；无独立 View output；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：Deferred beginSubmission / beginView

- 唯一目标：Deferred resource set 为同一 submission 的每个 View 提供独立 Binding（descriptor set + upload slice），View B 的准备不得改写 View A；抽取 Forward 已稳定的 pool 增长与 submission 开头，供 Forward/Deferred 共用。
- 新增 `ViewDescriptorSetAllocator`（chunked UniformBuffer pool，allocate 前增长）和 `beginFrameResourceSubmission` / `writeUploadSlice`；Forward 四条 pool 链与 Deferred 三条都改用该 allocator。
- `DeferredFrameResourceSet` 拆出 submission-scoped `SkinningBinding` 与 `RenderViewBindingTable<Binding>`；删除 `prepare` / `prepareSSAO` / `prepareSkybox` / `getBinding(flightIndex)`。`beginView` 在同一 View slot 写入 frame/light 以及可选 SSAO/skybox。
- `DeferredRenderPipeline::executeDeferredMainGraph` 按 Forward 同序 `beginSubmission` → `prepareSkinning` → `beginView`，graph 与 Light/SSAO/overlay 消费 View Binding，不再按 flight 覆写。
- 未统一 Forward/Deferred payload struct，也未抽万能 pipeline 基类。Shadow 仍按 flight 覆写 Binding。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewBindingTableTest.*'`（7/7，含 DeferredViewSlicesStayIndependent）、`DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*` 回归通过、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：Shadow View binding；RenderRuntime 不持有 submission 到 fence；仍只录制单 View；无独立 View output；无双 Surface。

## 2026-09-16 checkpoint：Forward beginSubmission / beginView

- 唯一目标：Forward resource set 为同一 submission 的每个 View 提供独立 Binding（descriptor set + upload slice），View B 的准备不得改写 View A。
- 新增 `RenderSubmissionContext` / `RenderViewRecordingContext`，`RenderPipelineFrameContext` 携带它们；RenderRuntime 在 tick 时填入 flight/token/cmdBuf/task。
- `RenderViewBindingTable` 按 flight 持有 View slot：同 token 追加，新 token rewind live count 并复用已有 slot。
- `ForwardFrameResourceSet::beginSubmission` 打开 arena flight；`beginView` 分配/复用该 View 的 frame descriptor sets，写入独立 slices，再更新那一组 set。`getBinding(flightIndex)` 已删除。
- Skinning 仍走 per-flight CRTP buffer，beginView 把 flight 的 skinning handle 抄进 View Binding；同 Scene 多 View 共享 palette，不同 Scene 的 skinning 隔离留到多 Scene 录制。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewBindingTableTest.*'`（6/6）、`RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*` 回归通过、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：Deferred/Shadow 仍按 flight 覆写 Binding；RenderRuntime 不持有 submission 到 fence；仍只录制单 View；无独立 View output；无双 Surface。

## 2026-09-16 checkpoint：pipeline View-local graph context

- Forward/Deferred pipeline 删除 `_lastTickCtx` 与 `_lastFrameInput` 两个跨调用保存槽位。
- Forward 的 postprocess/overlay graph build 改为接收 `executeViewportPass()` 栈内构造的 `FrameContext`，当前 `frame.viewportOverlaySnapshot` 也直接从本次 View 输入传入。
- Deferred 的 graph build 改为使用 `executeDeferredMainGraph()` 栈内构造的 `FrameContext`，不再把当前 frame input 存入 pipeline 成员。
- 该切片只消除 pipeline 层的跨 View 临时状态，不代表 upload arena、descriptor binding 或 output 资源已经完成 View 隔离。
- 验证：`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*'`（14/14）、`xmake b ya-game-runtime`。

## 2026-09-16 checkpoint：submission upload cursor

- `FrameUploadArena::beginFlight(flightIndex, frameToken)` 对同一 token 改为幂等调用，不再因为第二个 View 进入而 rewind cursor。
- 同一 submission 内的后续 View 可以继续 `allocate()` aligned slice；测试确认首个 slice offset 为 `0`，追加 slice offset 为 `16`，cursor 从 `4` 增长到 `20`，且 backing buffer identity 保持不变。
- 新 frame token 的 begin 仍保留原有 fence-safe flight 重置语义；本切片没有改变 descriptor pool/set、pipeline binding 或 output image 的所有权。
- 验证：`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='RenderGraphCoreTest.FrameUploadArena*'`；`git diff --check`。
- 保留未完成：`RenderSubmissionContext` / `RenderViewRecordingContext`、per-view descriptor/slice table、双 View GPU 录制和双 Surface 验收。允许同 token 追加 allocation 不等于 descriptor binding 已安全隔离。

## 2026-09-16 checkpoint：View order ranges

- RenderFrameData 的 View-owned draw bucket 已从 vector<RenderDrawItem> 副本迁移为 source + order：source 借用不可变 SceneFrameSnapshot 候选 vector，order 由每个 View 独立拥有。
- DrawCandidateView 支持 contiguous/indexed 两种只读 range、operator[]、range-for 和 subview()；indexed view 不物化候选，也不暴露连续 data()。
- RenderFrameExtractor::prepareView() 只初始化和排序 View-owned order，比较器从 shared candidates 读取 camera distance，不再修改共享 RenderDrawItem::sortKey。
- Forward、Deferred、Shadow、EntityId、Debug 和 auxiliary pass 消费者已迁移到 View bucket / DrawCandidateView；Scene-owned RenderShadingDrawBuckets 仅保留候选数据定义和 extractor source 输入。
- 验证：xmake b ya-render-3d-test、xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*'（14/14），xmake b ya-game-runtime。
- 本 checkpoint 未处理：RenderRuntime 多 View command recording、submission/View descriptor 与 upload lifetime 拆分、双 Surface 验收；下一 checkpoint 仍是 RenderSubmissionContext / RenderViewRecordingContext 生命周期隔离。

## 同 Scene View 复用审计

SceneRenderPlan 已按 SceneId + sceneRevision 去重并持有 shared_ptr<const SceneFrameSnapshot>。本轮已将 RenderFrameData 改为共享 snapshot owner，并把 View-owned draw bucket 进一步迁移为 source pointer + order indices；RenderFrameExtractor::prepareView() 不再按值复制 snapshot 或 RenderDrawItem，也不修改共享候选的 sortKey。

已确认的复用边界：transforms、mesh/material/entity 引用、原始灯光、skinning palette、候选 vector 和资源句柄可跨同 Scene 多 View 共享；camera 矩阵、viewport、visibility、LOD、sort order、shadow/cascade、View targets 和 GPU bindings 必须按 View 独立。shared snapshot、共享候选数据与 View-owned order 已作为一个原子迁移完成。另一个边界是 scheduler scope：同一逻辑帧的多个 surface/window 必须共用一个 SceneRenderScheduler/SceneRenderPlan，否则会重复抽取同一 Scene。GameRuntime 当前 sceneRevision = 0 仍为迁移期占位，不能作为长期缓存 key。

## R0 真实调用链

~~~text
GameRuntimeFrameOrchestrator::tickRender
  -> resolve frameState / viewport rect / camera matrices
  -> RenderFrameExtractor::extractSceneSnapshot(scene) + prepareView(camera, scene snapshot)
       -> extractCamera
       -> extractLights (directional/point/shadow fit)
       -> extractDrawItems (mesh/material/skinning)
       -> sortDrawItems
  -> GameUIHost::buildSnapshot()                 [runtime/simulation only]
  -> acquirePresentFrame(primary surface)
  -> RenderRuntime::renderFrame(FrameInput)
       -> apply pending pipeline changes
       -> prepare Render2D overlay/UI pipelines
       -> prepareFrame / begin command buffer
       -> renderWorldFrame
            -> active Forward/Deferred pipeline tick
                 -> pipeline-specific graph build/record
       -> recordCameraViewCompose
            -> Runtime UI snapshot compose to camera display RT
            -> editor/module view compose callback
       -> PresentationGraphService::recordDisplayCompose
            -> display compose to surface swapchain image
       -> end command buffer
  -> submitPresentFrame(surface, command buffer)
~~~

R0 结论：world snapshot 与 UI snapshot 都在 renderFrame 前生成；RenderRuntime 只接收 FrameInput 和 immutable snapshot 指针；ViewCompose 先于 DisplayCompose；acquire/present 仍由 host coordinator 负责；当前实现仍是单 Camera、单 command buffer/submit。

## Checkpoint

| Checkpoint | 状态 | 保留项 | 未完成 |
| --- | --- | --- | --- |
| R0 单 View 基线 | 已完成 | 单 View、现有 pass、单 submit、Forward/Deferred topology | 真实 GPU golden 仍依赖可运行窗口环境 |
| R1 World/View 分离 | 已完成（单 View 契约） | RenderFrameData 组合 Scene snapshot；现有单 View pipeline topology | GameEditor/preview 多 request |
| R2 ViewFamily | 进行中（A/B 入口落地；C/D/E 部分完成；host live Scene 列表落地） | Forward/Deferred topology；display-root inspector getter fallback；DeviceState+Coordinator 不完整拆分 | 4.0.3 Renderer owner，随后才是产品双 viewport / 双 Surface |
| R3 GUI2D/GameUI | 未开始 | WidgetTree live source、UIFrameSnapshot 输入 | UI-only 与 GameUI[ViewId] |
| R4 性能收口 | 未开始 | 优化由 profile 触发 | cache、submit、第三 pipeline 决策 |

## 下一轮接力点

R0/R1 已完成。A/B 入口有效。C：typed pass 入口在，Stage current-view 未清。D：`recordFamily` 入口在，内部仍是 per-view 状态机。E：RenderRuntime 类已删，DeviceState+Coordinator 未闭环。下一刀是公开 `Renderer` owner，不是产品双 viewport。

## R1 审计结论

用户模型修正：这里不需要 world 抽象。Scene owner 在本帧需要渲染某个 viewport 时，向 SceneRenderScheduler 提交一个 offscreen request；调度器在 UI 之前聚合这些 request，按 Scene 去重抽取，再按 viewport 生成任务。RenderRuntime 不拥有 Scene，也不通过全局 active Scene 决定所有渲染。

RenderFrameData 当前不是一个可以整体搬家的 world snapshot：

- RenderDrawItem 的 world transform、mesh/material 引用和 entity id 可成为共享 candidate；sortKey 与 bucket 顺序依赖 Camera/pipeline，应下沉到 View preparation。
- skinning palette 是可共享的一帧结果，但 palette index 必须在多个 View 间稳定。
- 原始灯光参数可共享；directional cascade/shadow view-projection 当前依赖 Camera，属于 View preparation。
- RenderFrameData 的 view/projection/viewProjection/cameraPos/viewportExtent/viewOwner 应迁移到 View 输入；目前仍由 CameraFrameInput 同时携带，不能删除旧字段直到所有 stage 迁移。
- 现有消费者覆盖 Forward、Deferred、shadow、entity-id、debug overlay 和 AppRenderState per-flight storage，直接拆 struct 会同时改变资源生命周期和 pass 输入。

R1 尚未完成代码迁移。下一步应将现有 RenderFrameExtractor 接到 request 的 snapshot builder，并把 camera-dependent sort/shadow preparation 放入 SceneViewportTask 生成阶段。

R1 调度切片已完成：SceneRenderScheduler 是不持有 Scene/ECS 的 frame-local collector；submit 只接受带有效 SceneId/ViewId 和 snapshot builder 的 request；seal 按 (SceneId, sceneRevision) 去重 builder，SceneRenderPlan 拥有唯一 snapshot table，再为每个 viewport 展开带 snapshotIndex 的 SceneViewportTask；clearFrame 清理本帧状态。plan 的 snapshot table 负责跨 task 保活，snapshotFor() 还会校验 task 的 SceneId/revision 与表项元数据，避免错误索引串用。尚未接入真正 SceneFrameSnapshot extractor 和 RenderRuntime record。

R1 抽取分层切片已完成：RenderFrameExtractor 现在只有 `extractSceneSnapshot(SceneExtractInput, SceneFrameSnapshot&)` 与 `prepareView(ViewPrepareInput, SceneFrameSnapshot, RenderFrameData&)`，分别负责 Scene/ECS 数据和 camera-dependent shadow/sort；旧 `extract()` 接口已删除。GameRuntime 当前单 View 路径已改为显式调用这两个阶段，TerrainProcessor 通过显式输入注入，extractor 不再通过 `App::get()` 取得全局状态。SceneRenderScheduler 仍待下一切片接管真实 builder；RenderRuntime 仍是单 View record。

R1 scheduler 接入切片已完成：GameRuntime 每帧通过 `beginFrame -> submit(active Scene request) -> seal(SceneRenderPlan)` 生成 plan，再从 plan 的 snapshot table 取出 SceneFrameSnapshot 调用 `prepareView()`；Scene 通过运行期唯一 instance id 提供 SceneId，scheduler 在 guard 退出时清理。本切片只接入当前单 View，不伪造 RenderRuntime 多 View API。

当前边界：`RenderFrameData` 已组合不可变 Scene snapshot，并在 `prepareView()` 中复制 snapshot 后写入 per-view shadow/cascade 和 sortKey；共享 snapshot 不被 View 原地修改。

R1 第一小步已完成：SceneFrameSnapshot 显式承载当前可识别的 Scene lights、draw buckets 和 skinning palettes；RenderFrameData 组合它并继续保留 camera、viewport、frame metadata。随后所有现有 Forward/Deferred/Shadow/Debug/EntityId 消费者已改为显式读取 `sceneSnapshot`。

关键新增约束：UI GPU compose 前必须存在一个明确的 SceneRenderScheduler 边界。它收集 SceneRenderRequest，输出 immutable SceneRenderPlan；UI compose 只能消费 plan 产生的 viewport outputs，不应在 UI 过程中临时触发 Scene/ECS extraction。UI widget tick/buildSnapshot 的先后由 host/product 依据输入依赖决定，不被 Scheduler 强制锁死。

## 设计评估结论

- 设计方向合理：Scene owner 提交 offscreen request，调度器在一帧内按 Scene 去重并按 viewport 展开任务，RenderRuntime 只录制 immutable plan。
- 与 UE 对齐点：Scene/FScene 与 ViewFamily/FSceneRenderer 分离；同一 Scene 的多个 View 共享一次 scene extraction。
- 与 Unity 对齐点：Camera/request 进入 pipeline，ScriptableRenderContext 类似的 plan/record 边界隔离 Scene 领域对象。
- 与 Godot 对齐点：Viewport 是离屏输出和显示绑定点，SubViewport/preview 可对应多个 Scene request。
- 与 ImGui 对齐点：后端只消费 immutable UIFrameSnapshot/SceneRenderPlan，不读取 live WidgetTree/Scene。
- 必须坚持的修正：Scene 不直接依赖 RHI/RenderRuntime；SceneRenderScheduler 不属于 GUI Framework；UI 之前指 GPU compose 之前，不是强制 UI logic/snapshot 晚于 request collection。
- SceneFrameSnapshot 是当前唯一场景快照语义，不得重新引入 WorldFrameSnapshot、RenderFrameExtractor::extract() 或全局 world 抽象。
