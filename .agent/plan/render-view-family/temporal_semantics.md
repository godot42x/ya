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
- 后续批次 P1d 落地（M1 同轴遗留，2026-09-17 扫到后补）：`AppAutomation::isFrameAutomationEnabled` / `hasFrameAutomationConfig` → `isTickAutomationEnabled` / `hasTickAutomationConfig`；`shouldRequestQuitAfterFrame` → `shouldRequestQuitAfterTick`；`EAppAutomationExitReason::ExitAfterFrame` → `ExitAfterTick`（`getAutomationExitReasonName` 仍返回外部名 `exit-after-frame`）；`isAutomationStableFrameReady` / `bStableFrameReady` → `isAutomationStableTickReady` / `bStableTickReady`（计数器本就是 `stableTicks`）；`TaskManager::registerFrameTask` / `hasFrameTasks` → `registerTickTask` / `hasTickTasks`（`taskManager.update()` 每 tick 调一次）；`AppAutomationTickContext` 参数名 `frameContext` → `tickContext`。保留：日志文案 `warmup frames` / `stable frames`、reason 字符串 `exit-after-frame`、CLI/config 键。

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
| `SceneFrameSnapshot` | `SceneSnapshot` | 定义 `Render3D/RenderFrameData.h:352`；消费方 `SceneRenderScheduler` / `SceneFamilyResources` / `RenderSubmission` / `RenderFrameExtractor` / `HostSceneRenderSubmit` 与三个测试 | 它是某 Scene 在一个内容版本上的不可变内容，与 host tick 无关；Scene 不变时被多个 View 共用 |
| `SceneSnapshotEntry`、`SceneRenderPlan::snapshots` | 保持 | `Render3D/Common/SceneRenderScheduler.h` | 已按 Scene 命名 |

#### M2 执行记录（2026-09-17，P1c 已提交）

- `SceneFrameSnapshot` → `SceneSnapshot`：定义与全部前置声明、`SceneRenderScheduler::SceneSnapshotEntry` / `SceneRenderPlan::snapshotFor` / `SceneRenderRequest::buildSnapshot`、`SceneFamilyResources`（成员、ctor、`snapshot()`、`bindSnapshot()`）、`RenderSubmission::allocateSceneFamily`、`RenderFrameExtractor::extractSceneSnapshot` / `prepareView` / `extractSceneLights`、`HostSceneRenderSubmit` 的 builder 类型、以及 `RenderRuntimeSnapshotTest` / `SceneFamilyResourcesTest` / `ViewFamilyRendererTest`。
- 声明列与续行缩进按原列补回（`RenderFrameExtractor`、`RenderSubmission`、`SceneFamilyResources`、`RenderFrameData`），diff 只剩标识符。
- 仍保留：测试文件名 `RenderRuntimeSnapshotTest.cpp`（与 `RenderFrameExtractor` 拆分一起处理，见 P3）。

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

### M9 — View 声明权的归属迁移（2026-09-17 review，见 plan §3.10）

M1–M8 消除的是同名异义；这一节处理**同一事实存了两份**：编辑器视口的几个诉求经 `AppRenderState` 的全局格子传进 GameRuntime，而编辑器侧本来就有直接来源（`EditorLayer::isViewportMode2D()`、`EditorLayer::getCamera()`、选中相机 UUID）。每个格子的去处如下，落地排在 4.0.3 第 4 刀（4d `ISceneViewProducer`）。

| 现状 | 语义 | 目标归属 | 处置 |
| --- | --- | --- | --- |
| `AppRenderState::bWorldSceneRenderEnabled`（`AppRenderServices::set/isWorldSceneRenderEnabled`） | 本 tick 有没有世界视口要提交 | 声明方决定 | 删除。替代物是「没有 producer 声明 view」，不是任何按 Scene 或按 host 的开关。同时解除 `SkeletonAnimationSystem::setTickPolicy` 对它的依赖（动画策略另找诚实输入：per-scene 可见性或 per-component 策略，对齐 UE `OnlyTickPoseWhenRendered`） |
| `AppRenderState::extensionHostView`（`setExtensionHostViewState` / `clearExtensionHostViewState`） | 编辑器作者视口的相机矩阵 | producer 自己填 `SceneViewDesc` | 删除。当前由 `prepareHostViewState` 回填进 `hostView`，再经 `HostSceneViewSubmit` 变成 view 1 的相机 |
| `AppRenderState::bCameraPreviewHostOwned` + `cameraPreviewEntityUUID` | 预览视图用哪个相机 | 声明方决定 | 删除。`resolvePreviewCamera` 与 `appendSceneCameraFrustumLines` 的相机选择前移回编辑器；producer 直接声明预览 view |
| `AppRenderServices::setViewportRect` / `getViewportRect` | 作者视口的离屏 rect | producer 自己填 `SceneViewDesc` | 降为 producer 输入；automation 的 resize 用例改走 producer 声明的 rect |
| `AppRenderState::bShowEditorGizmos`（`App::set/isEditorGizmoShown`） | 编辑器视口这 tick 要不画 gizmo | 声明方按 view 声明 | 删除。写入方是 EditorSurface 的 Window 菜单，读取方是 `featuresForView` 拼 feature mask；与 `viewportRect` 同批降为 producer 输入 |
| `kPrimarySceneViewId = 1` / `kHostOverlayPreviewViewId = 2` | view 的持久身份 | owner-scoped `SceneViewKey{ownerId, localId}` | 现在 view 1 同时是两个产品（编辑器作者视口 / 独立游戏视口）的身份，view 2 由 host 铸造。改为 producer 注册时铸键，作为 `ViewHistoryStore` 与跨 Surface 复用的稳定键 |
| `CameraFrameInput` 的 `flightIndex` / `frameIndex` / `deltaTime` | 帧作用域 | `FrameContext` | 与 M3 的 `CameraFrameInput` 删除同批；`flightIndex` 另见 M4 |
| `CameraFrameInput` 的 `view` / `projection` / `viewportRect` / `viewFeatures` | view 作用域 | `SceneViewDesc` → `PreparedView` | 与 M3 同批 |

判定规则（与 M1 同构）：一个值若由「某个视口的持有者」决定，它属于该 producer 的声明；若由「本帧的调度批次」决定，它属于 `FrameContext`；若由「Scene 内容」决定，它属于 `SceneSnapshot`。全局可变格子是这三者都没有归属时的症状。

#### M9 执行记录（4a，2026-09-18 已提交）

- `SceneRenderRequest` 去掉 `buildSnapshot`，成为纯声明：请求队列不再持有任何捕获 `Scene*` / `TerrainProcessor*` 的闭包。
- `SceneRenderScheduler::seal()` 只分组：按 (sceneId, sceneRevision) 去重建好快照表但内容为空，family 分组走文件内 `buildViewFamilies()`；不再触碰 Scene/ECS。
- 新增显式第二步 `buildSceneSnapshots(SceneRenderPlan&, const SceneSnapshotResolver&)`：按表项向宿主要不可变快照，未解析的表项剔除其 view 并重新分组（坏 Scene 不拖垮整帧）。`SceneSnapshotResolver` 是 4b 的过渡物——计划携带 tick-local `Scene*` 后即可删除。
- 宿主侧 `submitHostSceneViews` 收窄为「只声明」，抽取移到新的 `extractHostSceneSnapshots`；`tickRender` 变成 declare → seal → extract 三步。
- 下一步 4b 要消掉的东西：`derivedSceneForHostView`、`SceneRenderPlanInput::complete()`、`derivedScenesAgreeWithPlan()` 三个运行时反查，以及本阶段的 `SceneSnapshotResolver`。

#### M9 执行记录（4b，2026-09-18 已提交）

- 计划字面持有 Scene：`SceneRenderRequest.scene`（取代 `sceneId`）、`SceneViewportTask.scene`、`SceneSnapshotEntry.scene`；`sceneId` 由 `seal()` 调 `scene->getInstanceId()` 派生，只作为分组键存在一次。
- 抽取接口收窄：`SceneSnapshotResolver(SceneId, uint64_t)` → `SceneSnapshotExtractor(Scene&)`；`extractHostSceneSnapshots(SceneRenderPlan, TerrainProcessor*)` 收 sealed plan，不再收提交列表，去掉了「每条快照表项扫一遍 view 列表」的反查。
- 三个运行时校验换成构造期不变量：新增 `ExtractedSceneRender`（`buildSceneSnapshots()` 是唯一能造出非空实例的地方，`pairViewFrames()` 是唯一能放进 recording 的地方）。`SceneRenderPlanInput`、`complete()`、`derivedScenesAgreeWithPlan()`、`derivedSceneForFamily()`、`SceneViewRecording::derivedScene`、`ViewFamilyRecordContext::derivedScene` 全部删除；`derivedScenesAgreeWithPlan` 的两种错误在只剩一份句柄后无法构造。
- 消费者改为读自己 task 上的句柄：Forward/Deferred 的 `RenderPipelineFrameContext.derivedScene` 取 `recording.task->scene`；`uniqueDerivedScenes` 变成 coordinator 文件内的 `renderedScenes(plan)`（直接扫已解析的快照表）。
- 验收证据：`ya-render-3d-test` 172/172、`ya-testing` 相关滤镜 89/89、两次 smoke（runtime / editor，`--exit-after-frame=90 --screenshot-target=viewport`）exit=0 且日志 0 error。

#### M9 执行记录（4c，2026-09-18 已提交）

- 一份声明结构：`SceneViewDesc`（新头文件 `Render3D/Common/SceneViewDesc.h`，带 `SceneViewId` / `kPrimarySceneViewId` / `makeCameraViewProjection`）取代 `HostSceneViewSubmit` 与 `SceneRenderRequest`。计划条目 `SceneViewportTask` 内嵌它（`desc`）+ `output` / `snapshotIndex` / `familyIndex`，`seal()` 不再逐字段搬运。
- 键改用句柄：`SceneViewFamilyKey` / `SceneSnapshotEntry` / `SnapshotKey` / `snapshotFor` 校验都从派生整数 `sceneId` 换成 `Scene*`；`using SceneId` 与该别名全线删除。族键本就 tick-local（submission 重用时清空），句柄身份足够。
- 删掉无写方的 `renderFlags`（含族键里恒 0 的分量）；`submitHostSceneViews` 转发层删除，宿主直接 `scheduler.submit(SceneViewDesc)`；`HostSceneRenderSubmit.*`（只剩抽取）改名 `HostSceneExtract.*`；`pendingRequestCount()` → `declaredViewCount()`。
- 验收证据：`ya-render-3d-test` 172/172、`ya-testing` 相关滤镜 93/93（新增 `SceneFamilyResourcesTest` 进滤镜）、两次 smoke exit=0 且日志 0 error、截图字节数与 4b 一致。


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
| P1c | M2 `SceneFrameSnapshot` → `SceneSnapshot` | 已提交，纯重命名 |
| P1d | M1 同轴遗留：automation / TaskManager 的 per-tick 命名 | 已提交，纯重命名 |
| P1e | M9 view 声明权迁移（4.0.3 的 4a–4d：抽取移出 seal、plan 保留 Scene 句柄、合并 `SceneViewDesc`、`ISceneViewProducer`） | 对应 4.0.3 checkpoint 4；4a–4c 纯结构，4d 删全局格子 |
| P2 | M4 + M5 + `Renderer` 合并 | 对应 4.0.3 checkpoint 2 / 3 |
| P3 | M3（C++ 部分） | 对应 4.0.3 checkpoint 4（PreparedView） |
| P4 | M3 的 Slang 部分：删 `frameIdx`、`FrameData/FrameUBO` → `ViewUbo/ViewData` | 需 `xmake ya-shader` 重新生成头，单独提交 |
| P5 | M6 | 对应 4.0.3 checkpoint 5 / 6（pipeline 瘦身与改名） |
| P6 | M7 | 独立小批，可与 GUI 计划线并行 |

禁止：

- 一次提交里只改一半符号，留下 `RenderFrameData` 与 `PreparedView` 并存。
- 用 `using` / `typedef` 做长期兼容别名（违反根 `AGENTS.md` 规则 0、5）。
- 把 `flightIndex` 机械改名与 flight 归属修正混成一批：改名是纯机械，归属修正需要 `FrameFlightResources` 设计。
- M9 不属于改名：它删格子、加接口，必须按 4a–4d 逐刀验收，不能与 P2 的 `Renderer` 合并混批。

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
- P1d 已满足：`rg -n 'isFrameAutomationEnabled|hasFrameAutomationConfig|shouldRequestQuitAfterFrame|ExitAfterFrame|StableFrameReady|registerFrameTask|hasFrameTasks|frameContext' Engine Example` 为空。
- P1d 构建/测试证据：`xmake b ya-game-editor`、`xmake b ya-testing`；`xmake r ya-testing --gtest_filter='AppKernelTest.*:AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*'` 66/66（含改名后的 `AppKernelTest.HeadlessLoopHonorsExitAfterTick`）。
- M9 / P1e 的目标（4d 落地后应可逐条验证）：`rg -n 'bWorldSceneRenderEnabled|setWorldSceneRenderEnabled|isWorldSceneRenderEnabled' Engine Example` 为空；`rg -n 'extensionHostView|setExtensionHostViewState|clearExtensionHostViewState' Engine` 为空；`rg -n 'bCameraPreviewHostOwned|cameraPreviewEntityUUID|resolvePreviewCamera|kHostOverlayPreviewViewId|bShowEditorGizmos' Engine` 为空；`rg -n 'derivedSceneForHostView|derivedScenesAgreeWithPlan|derivedSceneForFamily' Engine` 为空（4b 已满足）。
- M9 4a–4c 的验收：4a `SceneRenderScheduler::seal()` 内不再出现 `buildSnapshot` 调用；4b plan/task/快照表项携带 Scene 句柄、`sceneId` 由句柄派生、无运行时反查校验（`rg -n 'derivedSceneForHostView|derivedScenesAgreeWithPlan|derivedSceneForFamily|complete\(\)' Engine/Source/Framework/Render/Render3D/Common/RenderFrameInputs.h` 为空）；4c 声明结构只剩一份（`rg -n 'HostSceneViewSubmit|SceneRenderRequest|submitHostSceneViews|renderFlags|\bSceneId\b' Engine Example` 为空，`SceneViewportTask` 只内嵌 `SceneViewDesc`），且 `HostSceneExtractTest` 的隔离语义不变（3/3）。
- M9 4d 的验收：编辑器 2D 画布模式与 3D 模式的 view 集合差异由 producer 声明表达，`tickRender` 内不再有「某个 view 要不要渲染」的判断；`SkeletonAnimationSystem` 不再依赖世界渲染开关。
- M9 4d 的第二条验收（2026-09-18 补充）：declare 路径上不再有 live-ECS 查询——`getPrimaryCamera`（`registry.view<CameraComponent>`）、`resolvePreviewCamera`（`getEntityByUUID`）、`appendSceneCameraFrustumLines`（再遍历 camera view）都要移出 `tickRender` 的声明段，由 producer 收集时给出或在 `SceneSnapshot` 里体现。`bShowEditorGizmos` 是第五个全局格子（写入方 `EditorSurface.cpp` Window 菜单，读取方 `featuresForView`），与 `setViewportRect` 同批降为 producer 输入。
- M9 相关构建/测试基线（落地时逐刀执行）：`xmake b ya-game-editor ya-testing ya-render-3d-test`；`ya-testing` EditorDockWorkspace/EditorWindowSession/EditorRootSession/HostSceneExtract（4c 前的名字是 HostSceneRenderSubmit）；`ya-render-3d-test` RenderRuntimeSnapshot/ViewFamilyRenderer/ViewPassResources/SceneFamilyResources。
- 仍待处理：`DebugPrimitives::updateFrameUBO`（随 P2 flight 轴）。
- `rg -n '\bframeIndex\b|\bframeId\b|\bframeToken\b' Engine/Source` 只剩第 3 节保留项与 automation 外部键。
- `rg -n 'flightIndex' Engine/Source` 为空。
- P1c 已满足：`rg -n 'SceneFrameSnapshot' Engine Example` 为空。
- `rg -n 'RenderFrameData|CameraFrameInput|RenderPipelineFrameContext|RenderViewRecordingContext|SceneViewRecording' Engine/Source` 为空。
- `rg -n 'UIFrameSnapshot' Engine/Source/Framework/GUI` 为空。
- 构建与专项：`xmake b ya-render-3d-test`、`ya-game-runtime`、`ya-game-editor`、GUI 目标；`xmake r ya-render-3d-test --gtest_filter='RenderFramePlanningTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'`。
- `git diff --check`。
