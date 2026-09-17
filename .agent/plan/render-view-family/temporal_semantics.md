# 时间语义与 `Frame` 命名迁移清单

> 建立日期：2026-09-17
> 关联：`plan.md` 3.7 目标命名映射、4.0.3 Ownership 收口
> 状态：清单已建立；代码未迁移

## 0. 问题

`frame` 目前同时表示 host 调度批次、Scene 内容版本、单个 View 的渲染采样、命令录制作用域、GPU submission、swapchain 飞行槽和某个 Surface 的 present 次数。单窗口单 View 时这些值恰好同步递增，所以看不出问题；一旦一个 editor UI 里存在多个 View（level viewport、material preview、thumbnail、UI-only 预览），它们各自有独立的渲染次数与 temporal 历史，`frameIndex` 就不再能同时表示它们。

判定规则：

- 可以叫 `Frame`：一次 host 调度批次，即 `AppKernel` 一次 iterate 所决定的工作集合。
- 不能叫 `Frame`：Scene 内容版本、单个 View 的采样序号、命令录制作用域、GPU submission、fence 飞行槽、单个 Surface 的 present 次数。

## 1. 目标语义轴

| 语义 | 目标名称 | 递增条件 | 现状名称 |
| --- | --- | --- | --- |
| 一次 host 主循环 | `hostTick` | 每次 `AppKernel::iterate` | `App::_frameIndex`、`RenderRuntimeClockState::frameIndex`、`SceneRenderPlan::frameId` |
| Scene 内容版本 | `sceneRevision` | Scene 内容变化 | 已正确 |
| 某个 View 的渲染采样 | `viewSampleId` | 该 View 实际重新渲染 | `RenderFrameData::frameIndex` |
| 命令录制作用域 | `FrameRecording` + `recordingSerial` | 每次开始录制 | `RenderSubmission`、`RenderSubmission::frameToken` |
| GPU 提交 | host 持有 | 每次 queue submit | 当前无独立概念（`finish()` 不 submit） |
| fence 飞行槽 | `flightSlot` | GPU flight ring | `flightIndex` |
| Surface 显示次数 | `presentCycle` | 该 Surface 成功 present | `RenderSurfaceContext::getCurrentFrameIndex` |
| swapchain buffer | `imageIndex` | acquire 返回 | 已正确 |

两个同 host tick 的例子（这是当前结构无法表达的部分）：

```text
hostTick = 1200
  View 1 (level viewport)  : viewSampleId = 1200
  View 2 (material preview): viewSampleId = 87    // 只在需要时重画
  View 3 (thumbnail)       : 本轮未提交
  Surface A (main window)  : presentCycle = 1198
  Surface B (tool window)  : presentCycle = 36
```

## 2. 迁移清单

### M1 — Host tick 命名（纯重命名）

| 现状 | 目标 | 主要位置 | 备注 |
| --- | --- | --- | --- |
| `GameRuntimeFrameOrchestrator`（类 + 文件名） | `GameRuntimeTickOrchestrator` | `Applications/GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.{h,cpp}`、`include/GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.h` | 它编排一次 host tick，不拥有任何 View 或 frame |
| `AppRenderFrameState` | `HostViewState` | `Applications/GameRuntime/AppRenderFrameState.h` + `include/` 转发 | 内容是 primary host 相机的矩阵/位置 + viewportRect + clock，不含 per-View 数据 |
| `RenderRuntimeClockState`（字段 `frameIndex`） | `HostClockState`（字段 `hostTick`） | `Render3D/Common/RenderRuntimeClockState.h` | `RenderRuntime` 已删除，属陈旧命名 |
| `App::_frameIndex` / `App::getFrameIndex()` | `App::_hostTick` / `App::getHostTick()` | `Applications/GameRuntime/App.{h,cpp}` | 全局静态计数，被 shader UBO、资源 GC、automation 共用 |
| `IRenderRuntimeServices::getFrameIndex()` | `getHostTick()` | `Render3D/Common/IRenderRuntimeServices.h` | |
| `EnvironmentLightingProcessor::_getFrameIndex` / `lastUsedFrame` / `currentFrame` | `_getHostTick` / `lastUsedTick` / `currentHostTick` | `Render3D/EnvironmentLighting/EnvironmentLightingProcessor.{h,cpp}` | 用途是 derived resource GC 计数 |
| `TerrainProcessor::_getFrameIndex` / `currentFrame()` / `setFrameIndexProvider` | `_getHostTick` / `currentHostTick()` / `setHostTickProvider` | `Render3D/Terrain/TerrainProcessor.{h,cpp}` | |
| `GameplayResourceBinding::_getFrameIndex` | `_getHostTick` | `Render3D/Services/GameplayResourceBinding.{h,cpp}` | |
| `SceneRenderScheduler::beginFrame/clearFrame/isFrameOpen/frameId` | `beginTick/clearTick/isTickOpen/hostTick` | `Render3D/Common/SceneRenderScheduler.{h,cpp}` | 它是 host-tick-local collector |
| `SceneRenderPlan::frameId` | `SceneRenderPlan::hostTick` | 同上；测试 `Engine/Test/Source/RenderRuntimeSnapshotTest.cpp:322` | |
| `RenderDeviceState::getFrameIndex()` | `getHostTick()` | `Render3D/RenderDeviceState.cpp:98` | |
| `AppAutomationFrameContext::frameIndex` | `hostTick` | `Applications/GameRuntime/Lifecycle/AppAutomation.h` | |
| `AppScreenshotCapture::recordedFrameIndex` / `earliestFrameIndex` | `recordedTick` / `earliestTick` | `Applications/GameRuntime/Utility/AppScreenshotCapture.{h,cpp}` | |
| `DebugPrimitives::updateFrameUBO` / `_frameData` | `updateViewUbo` / `_viewData` | `Render3D/Pipelines/DebugPrimitives.{h,cpp}` | 它是 per-view 常量，不是 per-host-tick |
| `AppKernel::_runController.markFrameCompleted()` | `markTickCompleted()` | `Framework/App/Kernel/AppKernel.cpp:54` | |
| `AppOptions` automation `frameIndex` 字段 | `hostTick` | `Applications/GameRuntime/AppOptions.h:37,43,71` | 外部 config 键保持不变，见第 3 节 |
| profile scope `Frame/FpsControl`、`Frame/Logic`、`Frame/Render`、`Frame/Automation`、`Frame/EventPump`、`Frame/MainThreadCallbacks` | `Tick/*` | `GameRuntimeTickOrchestrator.cpp:173` 起、`HostSdlEventSource.cpp:15` | 只影响 trace 可读性 |
| perf sample `Render/Frame`（整个 host tick 的 CPU/GPU 总量） | `Tick/Total` | `PerfKeys.h:70` | 不用 `Render/Tick`：它必须与 tick 内的 `Tick/Render` 一眼可分 |
| `RenderRuntimeSnapshotTest` | `RenderFramePlanningTest` | `Engine/Test/Source/RenderRuntimeSnapshotTest.cpp` | 类名 `RenderRuntime` 已删除 |
#### M1 执行记录（2026-09-17，已提交）

已落地（纯重命名，无行为改动）：

- `GameRuntimeFrameOrchestrator` → `GameRuntimeTickOrchestrator`，含 `.h` / `.cpp` / `include/` 转发头；方法 `prepareRenderFrameState` → `prepareHostViewState`。
- `RenderRuntimeClockState` → `HostClockState`，字段 `frameIndex` → `hostTick`，含 `include/` 转发头。`RenderDeviceState::InitDesc::clockState` 名字保留，类型已换。
- `App::_frameIndex` → `App::_hostTick`；`App::getFrameIndex()` → `getHostTick()`；`App::currentFrameIndex()` → `currentHostTick()`。
- `IRenderRuntimeServices::getFrameIndex()` → `getHostTick()`；`RenderDeviceState::getFrameIndex()` → `getHostTick()`。
- `setFrameIndexProvider` → `setHostTickProvider`；`_getFrameIndex` → `_getHostTick`（EnvironmentLightingProcessor / TerrainProcessor / GameplayResourceBinding）。
- `EnvironmentLightingProcessor::lastUsedFrame` → `lastUsedTick`；`TerrainProcessor::currentFrame()` → `currentHostTick()`；三个 processor 的 `currentFrame` 局部量 → `currentTick`。`DeferredDeletionQueue::currentFrame()` 故意保留（fence 轴）。
- `SceneRenderScheduler` 的 `beginFrame` / `clearFrame` / `isFrameOpen` / `frameId()` / `_frameId` / `_frameOpen` → `beginTick` / `clearTick` / `isTickOpen` / `hostTick()` / `_hostTick` / `_tickOpen`；`SceneRenderPlan::frameId` → `hostTick`。
- `AppAutomationRunController::markFrameCompleted()` → `markTickCompleted()`；`completedFrameCount` → `completedTickCount`；`shouldAutomationExitAfterFrame` → `shouldAutomationExitAfterTick`；`AppAutomationRunOptions::exitAfterFrame` → `exitAfterTick`。CLI 键 `--exit-after-frame` 与 config 键 `exitAfterFrame` 不变。
- `AppAutomationFrameContext` → `AppAutomationTickContext`，字段 `frameIndex` → `hostTick`；`onFrameCompleted` → `onTickCompleted`（AppAutomation / AppAutomationControlService）。
- `AppScreenshotCaptureState::recordedFrameIndex` / `earliestFrameIndex` → `recordedTick` / `earliestTick`；`tryFinalize(currentFrameIndex)` → `(currentHostTick)`；`appendPresentationCapture(frameIndex)` → `(hostTick)`。
- `AppOptions`：`viewportResize.frameIndex`、`pipelineSwitch.frameIndex`、`screenshotFrameIndex`、`screenshotWarmupFrames`、`screenshotSettleFrames` → `hostTick` / `screenshotTick` / `screenshotWarmupTicks` / `screenshotSettleTicks`；run-state 的 `warmupFrames` / `settleFrames` / `stableFrames` → `*Ticks`。JSON 键 `frame_index`、`warmup_frames`、`smoke.*.frame` 不变。
- 调用方与测试同步：`App.cpp`、`AppSceneServices.cpp`、`AppLifecycle.cpp`、`HostSceneRenderSubmit.cpp`、`EditorStatsTab.cpp`、`EditorRuntimeToolsTab.cpp`、`AppKernelTest`、`AppAutomationConfigTest`、`EditorWindowSessionTest`、`GUIHeadlessHostTest`、`GUIWindowManagerTest`、`RenderRuntimeSnapshotTest`、`HostSceneRenderSubmitTest`、`ViewFamilyRendererTest`、两个 GUI Example。
- P1b-1（后续提交）：`AppRenderFrameState` → `HostViewState`（定义文件与 `include/` 转发头一起改名）；`AppRenderState::frameState` → `hostView`、`extensionFrameState` → `extensionHostView`；`AppRenderServices::getRenderFrameState` / `setExtensionRenderFrameState` / `clearExtensionRenderFrameState` → `getHostViewState` / `setExtensionHostViewState` / `clearExtensionHostViewState`；`EditorViewportCompositor` 与 `EditorSurfaceContext` 的声明、定义与参数名同步。
- 后续批次 P1b-2a 落地（perf 命名面）：`perf::sample::renderFrame()` → `hostTick()`（key `Render/Frame` → `Tick/Total`）；`frameLogic` / `frameEventPump` / `frameFpsControl` / `frameRender` / `frameMainThreadCallbacks` / `frameAutomation` / `frameUnaccounted` / `frameRenderCallbacks` → `tick*`，key `Frame/*` → `Tick/*`；`YA_PERF_FRAME_SCOPE` → `YA_PERF_TICK_SCOPE`、`PerfFrameScopeTimerConditional` → `PerfTickScopeTimerConditional`（含 `frameSampleKey` → `tickSampleKey`、`ya_perf_frame_timer_` → `ya_perf_tick_timer_`、局部 `frameValue` → `tickValue`）；profile 产物 `frameCycle` / `frameCpuMs` / `frameGpuMs` → `tickCycle` / `tickCpuMs` / `tickGpuMs`。UI 文案 `Frame CPU:` / `Frame GPU:` 与 `Frame {}` 保留。
- 后续批次 P1b-2b 落地（按 tick 排期的二级命名）：`TerrainProcessor` / `EnvironmentLightingProcessor` 的 `_nextResolveAuditFrame` 与 `GameplayResourceBinding::_nextMaterialAuditFrame` → `*AuditTick`；`DERIVED_RESOURCE_GC_DELAY_FRAMES` / `MATERIAL_AUDIT_INTERVAL_FRAMES` → `*_TICKS`（连同 `AppAutomationConfigTest` 的常量断言）；`TerrainDerivedResource::lastUsedFrame` → `lastUsedTick`；`TerrainComponent::getRebuildNotBeforeFrame` / `setRebuildNotBeforeFrame` / `_rebuildNotBeforeFrame` / `invalidate(rebuildNotBeforeFrame)` 与 `TerrainProcessor::markTerrainDirty(..., rebuildNotBeforeFrame)` → `*Tick`。

刻意延后（仍在 M1 范围内，需要独立批次）：

| 未做项 | 原因 |
| --- | --- |
| `AppRenderFrameState` → `HostViewState` | 曾延后（消费方 `EditorModule.cpp` 与 `EditorViewportCompositor.{h,cpp}` 属另一条在途改动）；前置提交落地后已由 P1b-1 完成 |
| `DebugPrimitives::updateFrameUBO` / `_frameData` | 它按 `flightIndex` 索引，是 flight 轴而不是 host tick；等 P2 `FrameFlightResources` 落地后一并改名 |
| perf key / profile scope `Frame/*`、`frameLogic()`、`Render/Frame` | 曾延后（`PerfKeys.h` 定义 + 约 30 处调用属独立命名面）；已由 P1b-2a 完成 |
| `RenderRuntimeSnapshotTest` → `RenderFramePlanningTest` | 与 `RenderRuntime` 遗留命名一起处理，避免和 `Test.xmake.lua` 显式文件列表混批 |
| `_nextResolveAuditFrame`、`MATERIAL_AUDIT_INTERVAL_FRAMES`、`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`getRebuildNotBeforeFrame()` | 曾延后（「按 tick 排期」的二级命名，含组件 API）；已由 P1b-2b 完成 |

### M2 — Scene snapshot 命名

| 现状 | 目标 | 主要位置 | 备注 |
| --- | --- | --- | --- |
| `SceneFrameSnapshot` | `SceneSnapshot` | 定义 `Render3D/RenderFrameData.h:344`；消费方 Forward/Deferred/Shadow/EntityId/Debug/`RenderFrameExtractor`/Scheduler | 它是某 Scene 在一个内容版本上的不可变内容，与 host tick 无关；Scene 不变时被多个 View 共用 |
| `SceneSnapshotEntry`、`SceneRenderPlan::snapshots` | 保持 | `Render3D/Common/SceneRenderScheduler.h` | 已按 Scene 命名 |

### M3 — View 语义（核心）

| 现状 | 目标 | 主要位置 | 备注 |
| --- | --- | --- | --- |
| `RenderFrameData` | `PreparedViewRenderData` | `Render3D/RenderFrameData.h:368` | 它是「一个 View 的渲染数据包」，`Frame` 遮蔽了 per-view 本质 |
| `RenderFrameData::frameIndex` | `viewSampleId` | 唯一消费方 `Common/Shadow/BasicShadowMap/BasicShadowMapTechnique.cpp:207` | 语义是「该 View 第几次被渲染」 |
| `RenderFrameData::{view,projection,viewProjection,cameraPos,viewportExtent,viewOwner}` | 迁入 `PreparedView` | 同上 | 与 `CameraFrameInput` 重复 |
| `CameraFrameInput` | 删除；host 部分进 `HostViewState`，per-view 部分进 `PreparedView` | `Render3D/Common/RenderFrameInputs.h:157` | 当前同时携带 flightIndex/frameIndex/矩阵/viewport/overlay/uiFrameSnapshot/shadowSettings/frameData，是单 Camera 假设 |
| `RenderPipelineFrameContext` | `ViewRecordContext` | 同上 :311 | |
| `RenderViewRecordingContext` | 合并进 `ViewRecordContext` | `Render3D/Common/RenderRecordingContext.h` | 与上一条重复携带 task/frameData/viewportExtent |
| `SceneViewRecording` | 合并进 `PreparedView` | `Render3D/Common/RenderFrameInputs.h:30` | 与 task/frameData/derivedScene 重复 |
| `RenderStageContext` | `PassRecordContext`；删除其 `frameIndex` | `Render3D/Stage/IRenderStage.h:19` | `flightIndex` 见 M4 |
| `FrameContext` | `ViewPassContext` | `Render3D/Common/RenderOverlay.h:16` | overlay/post pass 的 view 级常量 |
| `RenderFrameExtractor` | `SceneSnapshotBuilder` + `ViewPreparer` | `Applications/GameRuntime/Utility/RenderFrameExtractor.{h,cpp}` | 两个阶段已分离，类名仍隐含「frame extractor」 |
| `RenderFrameExtractor::ViewPrepareInput::frameIndex` | `viewSampleId` | 同上 | |
| slang `frameIdx`（`Unlit.slang:16`、`PhongLit.slang:50`） | 删除字段 | C++ 写入点 `Forward/ForwardViewportLitPasses.cpp:414`、`Forward/ForwardViewportUnlitPass.cpp:188` | 两处声明均无 shader 消费方，属未使用字段；删除可省一次 UBO 写入 |
| slang `FrameData` / `FrameUBO` 结构名 | `ViewUbo` / `ViewData` | `Unlit`、`PhongLit`、`PBRForward`、`Skybox`、`EntityId`、`DeferredRender/*`、`CombineShadowMappingGenerate`、`Misc/BillboardWorld`、`Sprite2D*` | 它们是 per-view 常量缓冲；跨语言改名需 shader + regen + C++ 同批 |
| C++ alias `PBRFrameUBO`/`PhongFrameUBO`/`UnlitFrameUBO`/`SkyboxFrameUBO`/`BillboardFrameUBO`/`EntityIdViewportPass::FrameUBO`/`BasicShadowPayload::FrameUBO` | `*ViewUbo` | `Forward/ForwardFrameResourceSet.h:37`、`Forward/ForwardViewportLitPasses.h:34`、`Deferred/ViewportOverlayStage.h:64,74`、`Common/EntityIdViewportPass.h:38`、`Common/Shadow/BasicShadowMap/BasicShadowPayload.h:24` | |
| Forward pass 参数 `outFrame` | `outViewUbo` | `Forward/*Pass*.cpp` | |

### M4 — Recording / submission / flight

| 现状 | 目标 | 主要位置 | 备注 |
| --- | --- | --- | --- |
| `RenderSubmission` | `FrameRecording` | `Render3D/Common/RenderSubmission.{h,cpp}` | `finish()` 只置位，不提交 queue，名字暗示 GPU submit |
| `RenderSubmissionPool` | `FrameRecordingPool` + `FrameFlightResources` | 同上 | 池中混了 recording 作用域与 fence-safe 资源 |
| `RenderSubmission::frameToken()` | `recordingSerial()` | 同上 | 当前 token = host frameIndex；多 Surface 在同一 host tick 产生多个 submission 时会冲突 |
| `flightIndex` | `flightSlot` | `CameraFrameInput`、`RenderStageContext`、`RenderViewRecordingContext`、`FrameUploadArena`、`RenderViewBindingTable`、`RenderViewOutputTable`、`ShadowFrameResources`、Deferred/Forward `FrameResourceSet`、`DebugPrimitives`、`RenderSubmission*` | 它是 fence ring 槽位，不是时间 |
| `GameRuntimeFrameOrchestrator::resolveFlightIndex(app)` | 由 `FrameFlightResources` 提供 | `GameRuntimeFrameOrchestrator.cpp:426` | 当前从 primary surface present 计数推导；多 Surface 下 owner 错误 |
| `FrameUploadArena` | `UploadArena` | `RHI/Core/FrameUploadArena.{h,cpp}` + `include/` 转发 | |
| `FrameResourceSubmission.h`（`beginViewBindingTable` / `writeUploadSlice`） | `ViewRecordingSupport.h` | `Render3D/Common/FrameResourceSubmission.h` | |
| `PerFlightFrameResourceSetBase` | `SkinningLayoutProvider` | `Render3D/Common/PerFlightFrameResourceSetBase.h` | 现在只持 skinning DSL 布局，已无 per-flight 资源 |
| `RenderSurfaceContext::getCurrentFrameIndex()` | `getPresentCycleIndex()` | `RHI/Core/RenderSurfaceContext.h:69`、`RHI/Backend/Vulkan/VulkanRenderSurfaceContext.h:77`、`RHI/Render.h:109`、`VulkanRender.cpp:1197,1270,1291` | 它同时被 GPU timing query ring 当作槽位使用，改名需一并核对 |
| `MAX_FLIGHTS_IN_FLIGHT` | 保持 | | flight 轴语义正确 |

### M5 — Present / surface

| 现状 | 目标 | 主要位置 | 备注 |
| --- | --- | --- | --- |
| `PresentFrameInput` | `SurfacePresentInput` | `Render3D/Common/RenderFrameInputs.h:275` | |
| `FPresentFrame` | `PresentTicket` | `RHI/Core/PresentFrame.h` | acquire → record → present 的配对票据 |
| `acquirePresentFrame` / `submitPresentFrame` | `acquirePresentTicket` / `submitPresentTicket` | 同上 | |
| `RenderFramePlan`（名字保留） | 结构重组为 `hostTick + PreparedViewFamily[] + SurfaceComposePlan[]` | `RenderFrameInputs.h:284` | 现在同时挂 `sceneRender` 与 `present`，仍是一 Surface 一录制 |

### M6 — Pipeline 文件与类改名（对齐 `plan.md` 3.7）

| 现状 | 目标 |
| --- | --- |
| `RenderDeviceState` + `RenderFrameCoordinator` | `Renderer` |
| `RenderDeviceState.Frame.cpp`（文件名） | `Renderer.Record.cpp` |
| `DeferredFrameGraphOrchestrator` | `DeferredViewFamilyGraphBuilder` |
| `ForwardFrameGraphOrchestrator` | `ForwardViewFamilyGraphBuilder` |
| `DeferredFrameGraphPasses.{h,cpp}` | `DeferredViewFamilyGraphPasses.*` |
| `ForwardFrameGraphPasses.{h,cpp}` | `ForwardViewFamilyGraphPasses.*` |
| `DeferredFrameGraphResources.h` | `DeferredViewGraphResources.h` |
| `ForwardFrameGraphResources.h` | `ForwardViewGraphResources.h` |
| `DeferredFrameResourceSet` | `DeferredGpuResourceLibrary` |
| `ForwardFrameResourceSet` | `ForwardGpuResourceLibrary` |
| `ShadowFrameResources` | `ShadowViewResources` |
| 测试名 `DeferredFrameResourceSetTest`、`DeferredFrameGraphResourcesTest`、`ForwardFrameGraphOrchestratorTest`、`RenderGraphCoreTest.FrameUploadArena*` | 跟随新名 |

### M7 — GUI framework 命名

| 现状 | 目标 | 主要位置 | 备注 |
| --- | --- | --- | --- |
| `UIFrameSnapshot` | `UISnapshot` | `GUI/Runtime/Widgets/UIFrameSnapshot.{h,cpp}` + `include/GUI/Widgets/UIFrameSnapshot.h` 转发 + 各控件 include | 它是「一次 tree build 的不可变 draw data」，与 host tick 无关 |
| `UIFrameBuildContext` | `UISnapshotBuildContext` | `WidgetTree.h`、`UIFrameSnapshot.h` | |
| `UIFrameComposeReplay.{h,cpp}` / `measureUIFrameComposeReplay` | `UIComposeReplay.*` / `measureUIComposeReplay` | `GUI/Runtime/Compose/` | |
| `GuiFrameInspectorOverlay.{h,cpp}` | `GuiOverlayInspector.*` | `GUI/Runtime/Compose/` | |
| `dumpUIFrameSnapshot` / `digestUIFrameSnapshot` / `semanticDigestUIFrameSnapshot` | `dumpUISnapshot` / `digestUISnapshot` / `semanticDigestUISnapshot` | `GUI/Runtime/Widgets/UIFrameSnapshotDump.*` | |
| `GUIAppHost` 调试字段 `dumpFrame` / `gpuShotFrame` / `offscreenShotFrame` / `guiFrameInspector` | `dumpTick` / `gpuShotTick` / `offscreenShotTick` / `guiOverlayInspector` | `GUI/Host/GUIAppHost.h:88-130` | 调试旋钮，最低优先 |

### M8 — `IRenderRuntimeServices` 去留

结论：作为抽象没必要，作为数据有必要。现状盘点（2026-09-17）：

- 只有一个实现者 `RenderDeviceState`；`Engine/Test`、`Example` 里没有任何替代实现或 mock，所以这个 virtual 从未被抽象化使用。
- 它同时暴露四个互不相关的关注点：时间（`getHostTick` / `getElapsedTimeSeconds`）、环境光照（`getEnvironmentLightingProcessor` / `getSceneSkyboxDescriptorSet` / `getSceneEnvironmentLightingDescriptorSet` / `resolveSceneEnvironmentLightingResources`）、调试（`getDebugRenderSystem`）、gameplay 绑定（`getGameplayResourceBinding`）。
- `getGameplayResourceBinding()` 是接口上的死方法：`App::getGameplayResourceBinding()` 走的是 `RenderDeviceState` 具体类型，没有任何调用方经由这个 virtual。
- 与既有窄契约重复：`EnvironmentLightingResultProvider` 已经是 Host 注入的只读函数式窄契约，`IRenderRuntimeServices` 里的三个 env lighting 方法做的是同一件事，只是换成了宽 virtual 并额外递出一个可继续下钻的 `EnvironmentLightingProcessor*`。
- 层级倒置：pass/stage 作为 device-lifetime 配方却持有顶层 runtime 指针，于是能在录制期查 live ECS。见 `DeferredRenderPipeline::buildOverlayFrameInputs`（遍历 `BillboardComponent, TransformComponent` 与 `TransformComponent, DirectionComponent`）与 `ForwardViewportStage`（遍历 `SkyboxComponent, StaticMeshComponent`）。这违反 plan 3.1 / 4.0 的「ECS/Scene 查询必须在 graph build 前完成」。

目标形态（落在 P3 / 4.0.3 checkpoint 4，因为它和 `PreparedView` 收口是同一件事）：

1. 时间：走 `HostClockState` / `hostTick`，作为 pass 输入数据传入，不再从全局拉。
2. 环境光照：把 `EnvironmentLightingResultProvider` 扩成携带 descriptor set 的版本，或在 graph build 前把句柄解析进 `PreparedView`；两个机制只保留一个。
3. 调试绘制：只有 overlay pass 需要，构造时注入 `DebugRenderSystem&`（或更窄的 `DebugDrawSink`），不经 runtime 转发。
4. `getGameplayResourceBinding()`：直接从接口删除。

删除 `IRenderRuntimeServices` 之后 `RenderDeviceState` 不再继承任何渲染接口，`PipelineCoordinator::InitDesc::runtimeServices` 随之消失，这是公开 `Renderer` owner 那一刀的净收益。

## 3. 保留项（这些 `frame` 是正确的）

- `FrameBuffer` / `IFrameBuffer` / `VulkanFrameBuffer`：真实 GPU framebuffer。
- `DeferredDeletionQueue::currentFrame()` / `Entry::frameIndex`、`VulkanRender::_frameIndex`：fence / 延迟删除计数，不是 host tick。
- `Instrumentor::_frameIndex`、`_frameIndexMap`：profile 事件索引，与 frame/tick 无关。
- Lua 脚本 API `time.getFrameIndex`（`LuaScriptingApi::LuaTimeApi`）：脚本可见面，改名会破坏用户脚本；待单独决定，当前内部 lambda 已改为 `app.getHostTick()`。
- UI 文案 `Frame {}`（`EditorStatsTab` / `EditorRuntimeToolsTab`）：用户可见标签，只改 API 不改文案。
- perf key `Frame/Logic` 等：不在这里的保留项内——host tick 相位的样例名已由 P1b-2a 改成 `Tick/*`。
- `RenderGraph` / `RenderGraphExecutor` / `RGPassHandle`：graph 抽象与 frame 语义无关。
- `MAX_FLIGHTS_IN_FLIGHT`：flight 轴。
- `WidgetTree::buildSnapshot`：返回值改名后语义不变。
- `SceneRevision` / `SceneSnapshotEntry`：已正确。
- automation 外部键 `exitAfterFrame`、`screenshot.frame`、`smoke.viewportResize.frame`、`smoke.renderPipeline.frame`：属于 CLI / config 兼容面，内部分母改名但键名不改。

## 4. 批次与顺序

每批一个可验收目标，避免半改名状态。

| 批次 | 内容 | 与 4.0.3 的关系 |
| --- | --- | --- |
| P1a | M1 的 host tick 主体（orchestrator 文件与类、HostClockState、App tick、provider、scheduler、automation 计数） | 已提交，不减任何功能 |
| P1b-1 | M1 剩余之一：`AppRenderFrameState` → `HostViewState` | 已提交，纯重命名 |
| P1b-2a | M1 剩余之二：perf 命名面 `Frame/*` → `Tick/*`（含 profile 产物键与 perf scope 宏） | 已提交，纯重命名 |
| P1b-2b | M1 剩余之三：按 tick 排期的字段（audit 间隔、derived resource GC 延迟、terrain rebuild 门槛） | 已提交，纯重命名 |
| P1b-2c | M1 剩余之四：DebugPrimitives flight UBO | 随 P2 flight 轴 |
| P1c | M2 `SceneFrameSnapshot` → `SceneSnapshot` | 独立：纯重命名 |
| P2 | M4 + M5 + `Renderer` 合并 | 对应 4.0.3 checkpoint 2 / 3 |
| P3 | M3（C++ 部分） | 对应 4.0.3 checkpoint 4（PreparedView） |
| P4 | M3 的 Slang 部分：删 `frameIdx`、`FrameData/FrameUBO` → `ViewUbo/ViewData` | 需 `xmake ya-shader` 重新生成头，单独提交 |
| P5 | M6 | 对应 4.0.3 checkpoint 5 / 6（pipeline 瘦身与改名） |
| P6 | M7 | 独立小批，可与 GUI 计划线并行 |

禁止：

- 一次提交里只改一半符号，留下 `RenderFrameData` 与 `PreparedView` 并存。
- 用 `using` / `typedef` 做长期兼容别名（违反根 `AGENTS.md` 规则 0、5）。
- 把 `flightIndex` 机械改名与 flight 归属修正混成一批：改名是纯机械，归属修正需要 `FrameFlightResources` 设计。

## 5. 验收

改名完成后应当可以逐条验证：

- P1a 已满足：`rg -n 'GameRuntimeFrameOrchestrator|RenderRuntimeClockState|getFrameIndex|_getFrameIndex|setFrameIndexProvider|markFrameCompleted|onFrameCompleted|exitAfterFrame|completedFrameCount|screenshotFrameIndex' Engine Example` 为空。
- P1a 已满足：`rg -n 'scheduler.beginFrame|scheduler.clearFrame|scheduler.isFrameOpen|plan.frameId' Engine` 为空。
- P1a 构建证据：`xmake b ya-render-3d-test ya-game-runtime ya-game-editor ya-testing ya-gui-closure-test ya-gui-headless-host-test ya-gui-minimal-host GUIWorkbench` 全部通过；`ya-gui-widgets-test` 因既有的 `GUI/Compose` include 缺口失败，与本次改名无关。
- P1a 测试证据：`ya-render-3d-test` 25/25、`ya-gui-closure-test --gtest_filter=AppKernelTest.*` 3/3、`ya-testing --gtest_filter=AppAutomationConfigTest.*:EditorWindowSessionTest.*:HostSceneRenderSubmitTest.*` 20/20。
- P1b-1 已满足：`rg -n 'AppRenderFrameState|getRenderFrameState|setExtensionRenderFrameState|clearExtensionRenderFrameState|extensionFrameState|frameState' Engine` 为空。
- P1b-1 构建/测试证据：`xmake b ya-game-editor`、`xmake b ya-testing`；`xmake r ya-testing --gtest_filter='EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppAutomationConfigTest.*:AppKernelTest.*'` 66/66。
- P1b-2a 已满足：`rg -n 'perf::sample::(renderFrame|frame)[A-Za-z]*|YA_PERF_FRAME_SCOPE|PerfFrameScopeTimerConditional|frameCycle|frameCpuMs|frameGpuMs|"Frame/' Engine` 为空。
- P1b-2a 构建/测试证据：`xmake b ya-game-editor`、`xmake b ya-testing`、`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66。
- P1b-2b 已满足：`rg -n 'AuditFrame|RebuildNotBeforeFrame|MATERIAL_AUDIT_INTERVAL_FRAMES|DERIVED_RESOURCE_GC_DELAY_FRAMES|lastUsedFrame' Engine` 为空。
- P1b-2b 构建/测试证据：`xmake b ya-render-3d-test`、`xmake b ya-testing`、`xmake b ya-game-editor`；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41。
- 仍待处理：`DebugPrimitives::updateFrameUBO`（随 P2 flight 轴）。
- `rg -n '\bframeIndex\b|\bframeId\b|\bframeToken\b' Engine/Source` 只剩第 3 节保留项与 automation 外部键。
- `rg -n 'flightIndex' Engine/Source` 为空。
- `rg -n 'SceneFrameSnapshot|RenderFrameData|CameraFrameInput|RenderPipelineFrameContext|RenderViewRecordingContext|SceneViewRecording' Engine/Source` 为空。
- `rg -n 'UIFrameSnapshot' Engine/Source/Framework/GUI` 为空。
- 构建与专项：`xmake b ya-render-3d-test`、`ya-game-runtime`、`ya-game-editor`、GUI 目标；`xmake r ya-render-3d-test --gtest_filter='RenderFramePlanningTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'`。
- `git diff --check`。
