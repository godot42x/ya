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
| `PerFlightFrameResourceSetBase` | `SkinningLayoutProvider` | `Render3D/Common/PerFlightFrameResourceSetBase.h` | 现在只持 skinning DSL 布局，已无 per-flight 资源；转交 `rdg-cache-dx` P4 |
| `RenderSurfaceContext::getCurrentFrameIndex()` | `getPresentCycleIndex()` | `RHI/Core/RenderSurfaceContext.h:69`、`RHI/Backend/Vulkan/VulkanRenderSurfaceContext.h:77`、`RHI/Render.h:109`、`VulkanRender.cpp:1197,1270,1291` | 它同时被 GPU timing query ring 当作槽位使用，改名需一并核对 |
| `MAX_FLIGHTS_IN_FLIGHT` | 保持 | | flight 轴语义正确 |

#### M4 现状发现（2026-09-18，加 submit trace 时暴露）

`resolveFlightIndex` 从 primary surface 的 present 计数推导 flight，而 Vulkan surface 的
`flightFrameSize = 1`（`RHI/Backend/Vulkan/VulkanRenderSurfaceContext.h:27`），`advanceFrame()`
对它取模后 `currentFrameIdx` 恒为 0。实测证据：一帧一条 `Submit tick N: flight=0 token=N recorded=1`，
90 tick 全是 `flight=0`（runtime 与 editor 都是）。

后果：渲染侧那套双槽 flight 设计（`RenderSubmissionPool`、`RenderViewOutputTable`、
`AppRenderState::viewFrameDataPerFlight`、`FrameUploadArena`）在生产里**只会走到槽位 0**，
槽位 1 从未被真实执行过；而它之所以仍然安全，是因为 `VulkanRenderSurfaceContext::begin()`
在 acquire 前调 `waitAllGraphicsFences()`（等待**所有** frame fence，当前即那一个），
等于每帧 CPU 等上一帧 GPU 做完——「wait-idle-per-frame」策略，与 `flightFrameSize = 1` 自洽。

因此 M4 的结论要加一条前置判断：**flight 深度到底是 1 还是 2**。若要保持 1，则渲染侧的
双槽表、keepalive 与 `FrameFlightResources` 拆分都只是余量，不必为 overlap 记账；若要真正
overlap，需要同时 (a) 提高 `flightFrameSize`、(b) 把 `waitAllGraphicsFences()` 改成只等
`frameFences[currentFrameIdx]`、(c) 验证渲染侧 per-flight 表真的会轮转到槽位 1。这三件事
都不在本刀范围内（属 R2/R4 的性能决策，且要两个后端一起验），先记录为待决项。

#### M4 执行记录（P2 前置，2026-09-18 已提交）

- `RecordedFrame`（`Render3D/Common/RecordedFrame.h`）：一次录制的**结果值**——`commandBuffer` + `flightIndex` + `frameToken` + `valid()`。`RenderFrameCoordinator::record()` 返回它，取代裸 `ICommandBuffer*`；host 只提交它，不再靠「指针是否为空」表达成功/失败。
- 合同收紧：seal（`RenderSubmission::finish()`）失败时返回**无效** `RecordedFrame`，host 走空提交——acquired image 仍被 present 合法化。旧代码在同样的失败下会照旧提交那个 command buffer（只打一条 error），即「补丁之上再打补丁」。
- host 侧：`tickRender` 的 record→submit 边界现在有一条 trace（`Submit tick N: flight=F token=T recorded=B`），这正是发现 `flightFrameSize = 1` 的入口。
- M4 的改名（`RenderSubmission` → `FrameRecording`、`flightIndex` → `flightSlot`、`RenderSubmissionPool` 拆池等）仍未开始；本刀只把「录制结果」这一件事从裸指针变成值，并保留 `flightIndex` 现名以免与 P2 改名混批。

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
| `DeferredFrameResourceSet` | `DeferredGpuResourceLibrary`（转交 `rdg-cache-dx` P4） |
| `ForwardFrameResourceSet` | `ForwardGpuResourceLibrary`（转交 `rdg-cache-dx` P4） |
| `ShadowFrameResources` | `ShadowViewResources`（转交 `rdg-cache-dx` P4） |
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
| `AppRenderState::bWorldSceneRenderEnabled`（`AppRenderServices::set/isWorldSceneRenderEnabled`） | 本 tick 有没有世界视口要提交 | 声明方决定 | **已删（4d-1）**：替代物是「没有 producer 声明 view」；`SkeletonAnimationSystem::setTickPolicy` 改读渲染侧派生事实 `AppRenderState::renderedScenesLastTick`（上一 tick 是否为该 Scene 产出内容，对齐 UE `OnlyTickPoseWhenRendered`） |
| `AppRenderState::extensionHostView`（`setExtensionHostViewState` / `clearExtensionHostViewState`） | 编辑器作者视口的相机矩阵 | producer 自己填 `SceneViewDesc` | **已删（4d-1）**：`EditorAuthoringViewProducer` 直接声明；`prepareHostViewState` 收成宿主几何 + 时钟，主 view 的相机由声明回填进 `hostView` |
| `AppRenderState::bCameraPreviewHostOwned` + `cameraPreviewEntityUUID` | 预览视图用哪个相机 | 声明方决定 | **已删（4d-2）**：`EditorViewProducer` 按 `EditorLayer::getCameraPreviewEntity()`（用户选中项）声明预览 inset，view id 由编辑器自持；`resolvePreviewCamera` 删除，FOV 线框改由编辑器 world overlay pass 绘制 |
| `AppRenderServices::setViewportRect` / `getViewportRect` | 作者视口的离屏 rect | producer 自己填 `SceneViewDesc` | **已删（4d-3b）**：编辑器不再写它，作者视口的 rect 由 `EditorLayer::getViewportRect()` 直接进声明；这套访问器只剩宿主几何（automation 的 `viewportResize` 只改游戏视口）与 `get_world_view_state` 读数 |
| `AppRenderState::bShowEditorGizmos`（`App::set/isEditorGizmoShown`） | 编辑器视口这 tick 要不画 gizmo | 声明方按 view 声明 | **已删（4d-3a）**：开关是编辑器的 view option（`EditorLayer`），`EditorViewProducer` 读它声明 feature，`EditorSurface` 菜单写它；automation 的 `set_editor_gizmos_visible` 经 `IEditorAutomationControl` 打到编辑器（无编辑器即失败）；游戏视口只声明 `Game`，不再受这个开关影响 |
| ~~`kPrimarySceneViewId = 1` / `kHostOverlayPreviewViewId = 2`~~ | view 的持久身份 | **已迁（2026-09-19）**：owner-scoped `SceneViewKey{owner, local}`，owner 由 producer 自命名（`viewOwner()`），扁平 `viewId = owner << 32 | local` | view 1 曾经同时是两个产品（编辑器作者视口 / 独立游戏视口）的身份，view 2 由 host 铸造。已迁完；**偏离**：不是「注册时铸键」而是 producer 自命名——注册顺序派的 id 会在别人插入 producer 时漂移，与「稳定键」目标冲突 |
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

#### M9 执行记录（4d-1，2026-09-18 已提交）

- 接缝：新增 `ISceneViewProducer` / `SceneViewCollector` / `SceneViewCollectContext`（`Render3D/Common/SceneViewProducer.h`）。签名比计划原文多一个显式上下文参数（tick / dt / viewport 几何）：这些是帧事实，应当由生产者**读**；放进 collector 会把 sink 和输入混在一起。宿主注册列表在 `AppRenderState::viewProducers`，`App::add/removeSceneViewProducer` 增删。
- 世界视口归位：`RuntimeGameViewProducer`（Runtime 态、用游戏相机）与 `EditorAuthoringViewProducer`（非 Runtime 态且非 2D canvas、用编辑器相机）各自声明主 view。谁在显示视口就由谁声明，不再需要编辑器通知 runtime「我这 tick 没有世界视口」。
- 格子删除：`extensionHostView`（含 set/clear）与 `bWorldSceneRenderEnabled`（含 set/is）两个格子连同 `prepareHostViewState` 里的相机分支一起删除；`prepareHostViewState` 现在只写宿主几何（viewportRect / framebuffer scale / clock），主 view 的相机由声明回填进 `hostView`（相机包络与 offscreen resize 仍读它，行为不变）。
- 动画策略换诚实输入：`SkeletonAnimationSystem::setTickPolicy` 从「某个 viewport 的世界开关」改为「上一 tick 是否为该 Scene 产出了内容」（`AppRenderState::renderedScenesLastTick`，由 `renderedScenes(plan)` 写入；该 helper 从 coordinator 文件内提到 `SceneRenderScheduler.h` 与宿主共用）。一 tick 滞后是结构性的：system 跑在 view 声明之前。
- 场景查询归位：`getPrimaryCamera` 从 orchestrator 静态函数移入 `Utility/SceneCameraQuery`（`findPrimaryCamera` / `findSecondaryCamera`），`AppSceneServices::getPrimaryCamera` 与宣布段都改用它；orchestrator 里的 `findNonPrimarySceneCamera` 删除，`resolvePreviewCamera` 复用 `findSecondaryCamera`。
- 验收证据：`ya-render-3d-test` 172/172；`ya-testing` 相关滤镜 101/101（含 `AppLifecycleTest.*`，强制重建后复跑 5 次一致）；`rg -n 'extensionHostView|setWorldSceneRenderEnabled|isWorldSceneRenderEnabled|bWorldSceneRenderEnabled|findNonPrimarySceneCamera' Engine Example` 为空；两次 smoke（runtime / editor，`--exit-after-frame=90 --screenshot-target=viewport`）exit=0、日志 0 error、截图字节数与 4b/4c 完全一致（1395200 / 679231）。
- 已知行为差异（刻意）：Runtime 态且场景没有相机实体时，主 view 仍会被声明（与之前一致），但相机是单位矩阵而不是借用编辑器相机——「游戏视口借用编辑器相机」正是被删除的那条耦合。
- 保留未完成：4d-2（相机预览生产者 + 删 `bCameraPreviewHostOwned` / `cameraPreviewEntityUUID` / `kHostOverlayPreviewViewId`，`resolvePreviewCamera` / `cameraProjectionForOutput` / `appendSceneCameraFrustumLines` 移回编辑器）、4d-3（`SceneViewDesc.features` + 删 `bShowEditorGizmos` 与 `featuresForView`，作者视口 rect 由声明方给出）。


#### M9 执行记录（4d-3a，2026-09-18 已提交）

- 开关归编辑器：`EditorLayer` 增加 `isEditorGizmoShown()` / `setEditorGizmoShown(bool)`（编辑器自己的 view option，默认 false）。`EditorViewProducer` 的两个 view 都读它——authoring 视口在 authoring 态一律画编辑器家具，其余情况与预览 inset 一样只在这个选项打开时画。
- 格子删除：`AppRenderState::bShowEditorGizmos`、`App::isEditorGizmoShown` / `App::setEditorGizmoShown` 删除；`RuntimeGameViewProducer` 只声明 `features = Game`（编辑器不再能改变一个不由它声明的视口）。
- automation 改走编辑器：`IEditorAutomationControl` 增加 `setEditorGizmosVisible(bool)`，`EditorModule` 转给 `_layer`；`handleSetEditorGizmosVisible` 在有编辑器时成功、没有时返回错误，不再写 App。
- 验收证据：`ya-testing` 相关滤镜 114/114（新增 `EditorViewProducerTest` 2 例、`RuntimeGameViewProducerTest` 2 例）；`rg -n 'bShowEditorGizmos' Engine Example` 只剩 `EditorLayer` 的私有成员，`featuresForView` 与 `App::isEditorGizmoShown` 为空；经 `control start` 起的 editor 实例接受 `set_editor_gizmos_visible`，game 实例返回 `requires a loaded editor`，game viewport 截图 1395200 字节与 4b 起各刀基线一致。
- 保留未完成：4d-3b（作者视口 rect 由声明方给出；`setViewportRect` 不再由编辑器写，automation 的 resize 改走声明）。

#### M9 执行记录（4d-3b，2026-09-18 已提交）

- 声明里给 rect：`EditorLayer::getViewportRect()` 返回面板几何（未布局时用编辑器默认尺寸兜底）；`EditorViewProducer` 的作者视口与预览 inset 的 `composeRect` 都用它，不再读 `context.viewportRect`。`context.viewportRect` 现在只描述宿主视图几何（游戏视口）。
- 手写 sync 删除：`EditorLayer` 的 pending-resize 三件套与 `getPendingViewportResize` / `queueViewportResize`、无人订阅的 `onViewportResized`、`EditorModule::applyPendingViewportResize` 全部删除；编辑器不再调用 `setViewportRect` / `device->applyViewportResize`。
- device extent 变成派生：`tickRender` 拿到主 view 后把 `primaryView->viewportRect` 回填 `hostView.viewportRect` 并 `device->applyViewportResize(...)`；「设备期望哪个 viewport extent」不再由别的层推入。automation 的 `smoke.viewportResize` 只写宿主 view rect。
- 行为差异（刻意）：automation 的 viewportResize 在编辑器里不再改变作者视口的 RT 尺寸（面板尺寸是面板自己的事实；要固定编辑器视口尺寸应该改窗口/布局）；`_viewportSize` 始终跟随面板 rect（删掉「鼠标捕获时不更新」的延迟分支）。
- 验收证据：`ya-testing` 相关滤镜 92/92（`EditorViewProducerTest` 3 例，含「声明的 rect 就是面板 rect」「未布局用默认尺寸」）；`rg -n 'getPendingViewportResize|queueViewportResize|_bViewportResizePending|_pendingViewportRect' Engine/Source` 为空，`EditorLayer::onViewportResized` 删除（`IRenderPipeline::onViewportResized` 是 device 级 hook，保留）；runtime 与 editor 的 viewport 截图都与 4d-3a 逐字节相同（`1c6668976be1cdd5d755d1f1365700f7` / `1bfb16e7ca543abb7b517325df508b90`）。
- 基线口径修正：编辑器 viewport 的字节基线受持久化 dock 布局影响（本会话 235x188），跨会话比不可靠；编辑器侧改看同会话前后对比，runtime 侧仍可跨会话比。

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
- 4d-2 已满足：`rg -n 'bCameraPreviewHostOwned|cameraPreviewEntityUUID|resolvePreviewCamera|kHostOverlayPreviewViewId' Engine Example` 为空（gizmo 格子留待 4d-3）。`SceneViewDesc` 现在自带 `features` / `viewOwner`，orchestrator 里不再有 `featuresForView` 或 viewOwner 三分支。
- 4d-3a 已满足：`rg -n 'bShowEditorGizmos' Engine Example` 只剩 `EditorLayer` 自己的私有成员（编辑器 view option），`rg -n 'App::isEditorGizmoShown|featuresForView' Engine Example` 为空；`EditorViewProducerTest` / `RuntimeGameViewProducerTest` 钉住「authoring 视口恒画编辑器家具、预览 inset 按编辑器的 view option 声明 feature、游戏视口只画 authored 内容」。
- 4d-3b 已满足：作者视口的 rect 由 `EditorViewProducer` 从 `EditorLayer` 声明（`EditorViewProducerTest` 断言声明的 rect 就是面板 rect，未布局时用默认尺寸）；`rg -n 'getPendingViewportResize|queueViewportResize|_bViewportResizePending|_pendingViewportRect' Engine/Source` 为空，`EditorLayer::onViewportResized` 删除（pipeline 侧的 `IRenderPipeline::onViewportResized` 保留）；device 的预期 viewport extent 与 `hostView.viewportRect` 都由主 view 的声明派生。4d 的五个格子至此全部删除。
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
