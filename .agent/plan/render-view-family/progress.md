# Progress

## 当前状态

- 阶段：R0/R1 契约已落地。R2 功能向 ViewFamily 靠近，架构未收口。真实 loop 是 `AppKernel` → `GameRuntimeTickOrchestrator::tickRender` → `RenderFrameCoordinator::record` → `recordFamily` → host present。`RenderRuntime` 类已删除。
- 已确认：RenderFrameInputs.h 已有四组输入；RenderFrameData 的 Scene snapshot owner 已与 View-owned draw buckets 分离。
- 已确认前置：多 OS window 的 surface/present 改造属于 gui-multi-os-window-editor，不在本计划重复实现；本计划也不引入 WorldInstance/WorldRegistry。
- 当前 checkpoint：Surface 持有 presentation blit 的 tone-map descriptor set。Checkpoint C 把 View tone-map DS 从 processor 挪走后，display compose 仍调用 `BasicPostprocessing::render` 且不传 set，MoltenVK 在 `vkQueueSubmit` encode 时对 `VkDescriptorSet 0x0` 空解引用。下一刀仍是 4.0.3 公开 `Renderer` owner。不要发明 PIE authoring PiP，也不要把产品双 viewport / 双 Surface 当成下一刀。
- 架构审计结论（2026-09-17）：DeviceState+Coordinator 是不完整拆分（friend 越界）；DeviceState 是 persistent renderer 不是 RHI device；`RenderSubmission::finish` 不 submit；输入/context 层过多；`recordFamily` 仍是 per-view 状态机外包装；Stage 仍有 `_frameInputs` / `_preparedViewSlot`；Scene recording 仍与 present Surface 耦合；`tickRender` 淹没 orchestration。Checkpoint C/D/E 均为部分完成。A/B 入口仍有效。host live Scene 列表已落地，默认产品帧仍提交当前 viewport Scene。
- 本轮完成 RenderFrameData ownership 收口：RenderFrameData 不再继承 SceneFrameSnapshot，而是持有 shared snapshot 并独立保存 View-owned draw buckets；Forward/Deferred/Shadow/Debug/EntityId 消费者通过显式路径读取 View buckets、shared skinning palettes 和 light presence。
- R2 第一切片：RenderRuntime::FrameInput 已显式携带 SceneRenderPlanInput；GameRuntime 将 sealed plan 与 parallel view recordings 传入，Runtime 在 command recording 前校验每个 task 的 snapshot 归属。
- GPU lifetime guard：FrameUploadArena 现在按 `flightIndex + frameToken` 识别一次 submission；同一 token 的第二次 begin 已改为幂等 no-op。Forward / Deferred / Shadow 的 frame descriptor 已改为 View-owned；skinning 已按 Scene family 持有，同 Scene 多 View 共享一份 SSBO，不同 Scene 不再以 flightIndex 为共享 key。

## 2026-09-17 checkpoint：View 声明 / 收集边界 review（无代码改动）

## 2026-09-18 checkpoint：shadow 的准备结果显式回传（4.0.2 C 收口 6b）

- 唯一目标：删掉计划点名的另一半隐式 current View——`BasicShadowMapTechnique` 记住「上一次 prepare 的是哪个 view slot、其中有多少盏点光」并在 append 时复用。与 6a 的差别是这一半不是死状态（它真的被读），所以要把 prepare 的结果显式传回去。
- token：新增 `ShadowPreparedView{viewSlot, pointLightCount, valid()}`（`Render3D/Shadow/IShadowTechnique.h`）。`IShadowTechnique::prepare()` 改为返回它；`pointLightCount` 随 token 走，是因为 append 会用自己的 `frameData` 重建 payload，而那份 `frameData` 未必是 prepare 看过的那个包（Forward 的 `stageCtx.frameData` 是相机包，prepare 用的是 `recording.frameData`），显式带回才能保证 pass 用的就是「为它准备过的」那份计数。
- 传递链：`ShadowStage::prepareView` 返回 token → Forward 的 `ForwardFamilyViewBranch.shadowPrepared` / Deferred 的 `DeferredFamilyViewBranch.shadowPrepared` → `appendViewportPassGraph(...)` / `appendDeferredViewToGraph(...)` → 两个 orchestrator 的 `BuildInputs.shadowPrepared` → `ShadowStage::appendGraphPasses(graph, ctx, prepared, dependency)` → technique。Stage 与 technique 上不再有 `_preparedViewSlot` / `_lastPreparedPointLightCount`。
- 顺手去掉两处：append 里对 `pointLightCount` 的二次 `std::min(..., MAX_POINT_LIGHTS)`（prepare 时已经 clamp 过）与无人使用的 `getLastPreparedPointLightCount()`。
- 行为修正（刻意）：prepare 被拒（无 `frameData`、或 submission 未在录制）的 View 现在以无效 token append，什么都不加。旧代码在这种情况下会复用上一个 View 的 slot 与计数，把别人的 shadow pass 加进这一帧的图里——这正是「Stage 记住当前 view」会造成的错误。
- 新测试：`Engine/Test/Source/ShadowPreparedViewTest.cpp`——token 默认无效；在 shadow 打开的前提下，未 prepare 的 View append 出来的是空输出且图里不多出 pass。`ForwardFrameGraphOrchestratorTest.BuildInputsDefaultsStayEmpty` 补一条 `shadowPrepared` 默认无效。
- 验收：`xmake b ya-render-3d ya-render-3d-test ya-game-editor ya-testing`；`ya-render-3d-test` 174/174（原 172 + 2）；runtime 与 editor 的 viewport 截图与 4d-3b 逐字节相同。
- 验证口径补强（这一刀顺手做了）：先确认 shadow 确实影响这两张图，再谈截图不变。用 `--automation-config` 把 `shadow.quality` 设为 0（注意该键在 automation 文档根下，与 `smoke.postprocess.*` 的前缀不同）：runtime 视口整幅变化（bbox 全图、单通道最大差 162），editor 视口同样整幅变化（最大差 96）。所以「开关 shadow 会改变像素、而本刀前后截图逐字节相同」才构成 shadow 路径未被改坏的证据。
- 保留未完成：无（Stage current-view 至此清空）。

## 2026-09-18 checkpoint：Stage 不再记住当前 view（4.0.2 C 收口 6a）

- 唯一目标：删掉 Stage 上「上一次准备的是哪个 view」这类隐式槽位里的死的那一半。计划的 4.0.2 C 收口点了两个名字，这一刀先处理 `LightStage`（另一半 `BasicShadowMapTechnique::_preparedViewSlot` 需要把 prepare 的结果显式回传，拆成 6b）。
- 死状态确认：`LightStage::_frameInputs` 的唯一写入方是 `setFrameInputs()`，而它在全仓库没有调用方（`rg -n 'setFrameInputs' Engine` 只有定义与声明）——真正在录 light pass 的是 `DeferredFrameGraphPasses`，它把 View-owned descriptor set 显式传给 `LightStage::execute(ctx, frameAndLight, environmentLighting, gBufferTextures, shadows)`。因此 `IRenderStage::execute(ctx)` 这个入口过去只会用空 handle 转调，必然早退。
- 落地：删 `LightStage::FrameInputs`、`_frameInputs`、`setFrameInputs()`，destroy 里的 `_frameInputs = {}` 一并去掉；`execute(const RenderStageContext&)` 保留为有注释的空实现（`IRenderStage` 要求它存在），注释写明「一个存下来的当前 View 输入对同一 graph 里的第二个 View 必然是过期的」。
- 验收：`xmake b ya-render-3d ya-render-3d-test ya-game-editor`；`ya-render-3d-test` 172/172；runtime 与 editor 的 viewport 截图与 4d-3b 逐字节相同（`1c6668976be1cdd5d755d1f1365700f7` / `1bfb16e7ca543abb7b517325df508b90`）——删掉的是从未被写入的状态，渲染结果不变。
- 保留未完成：6b（`BasicShadowMapTechnique::_preparedViewSlot` / `_lastPreparedPointLightCount` → prepare 返回显式 token，穿过 `ShadowStage` 与两个 orchestrator 的 BuildInputs 由 pipeline 在 append 时传回；顺带删掉 append 里对缓存计数的二次 clamp，`getEffectivePointLightCount()` 已经 clamp 到 `MAX_POINT_LIGHTS`）。

## 2026-09-18 checkpoint：作者视口 rect 由声明方给出（4.0.3 4d-3b）

- 唯一目标：把作者视口的 rect 从「编辑器经 `AppRenderServices::setViewportRect` 推给宿主、宿主再放进 collect context 让 producer 读回」这条往返，改成编辑器在声明里直接给出；顺带把编辑器里那套 pending-resize 同步机制（最后一个手写 sync）删掉。
- 声明权归编辑器：`EditorLayer::getViewportRect()` 给出面板几何（尚未布局时用编辑器自己的默认尺寸兜底，View 不会以空 rect 被声明）；`EditorViewProducer` 的作者视口与预览 inset 的 composeRect 都用它，不再读 `context.viewportRect`。
- 手写 sync 删除：`EditorLayer` 的 `_pendingViewportRect` / `_bViewportResizePending` / `_resizeTimerHandle` / `getPendingViewportResize()` / `queueViewportResize()` 与无人订阅的 `onViewportResized` 全部删除；`EditorModule::applyPendingViewportResize()` 及其在 onLogic 的调用删除（`setViewportRect` 与 `device->applyViewportResize` 都不再由编辑器调用）。
- device extent 跟随声明：`tickRender` 找到主 view 后，除了原有的相机回填，还把 `primaryView->viewportRect` 回填进 `hostView.viewportRect` 并调用 `device->applyViewportResize(...)`——设备「expect 哪个 viewport extent」现在是声明的派生结果，而不是别处推入的状态（该调用本身在未变化时 early-out）。
- automation：`smoke.viewportResize` 只写宿主 view rect（那个 rect 由游戏视口声明），不再直接 `device->applyViewportResize`。
- 刻意接受的行为差异：automation 的 viewportResize 改的是宿主视图几何（独立游戏视口 / 游戏态），编辑器作者视口的尺寸由它自己的面板决定，所以该键在编辑器里不再改 RT 尺寸。「让编辑器视口变成某个尺寸」的诚实做法是改窗口/布局，而不是从外部改一个 docked 面板的渲染尺寸。另一处：`_viewportSize` 现在始终跟随面板 rect，删掉了「鼠标捕获时不更新」的分支——那条分支的唯一用途是延迟 RT resize。
- 验收：`xmake b ya-game-editor`、`xmake b ya-testing`；`ya-testing` 相关滤镜 92/92（`EditorViewProducerTest` 扩到 3 例，新增「声明出来的 rect 就是面板 rect」与「未布局时用默认尺寸兜底」）；`rg -n 'getPendingViewportResize|queueViewportResize|_bViewportResizePending|_pendingViewportRect' Engine/Source` 为空，`EditorLayer::onViewportResized` 已删（`IRenderPipeline::onViewportResized` 是 device 级 hook，仍在，由 `RenderDeviceState::applyViewportResize` 调用）；runtime viewport 截图与 4d-3a 逐字节相同（MD5 `1c6668976be1cdd5d755d1f1365700f7`，1395200 字节）；编辑器 viewport 截图与 4d-3a 逐字节相同（MD5 `1bfb16e7ca543abb7b517325df508b90`，235x188、13649 色、98.7% 非黑，说明世界视图确实在画）。
- 基线口径修正（本刀发现）：编辑器 viewport 截图不再有跨会话稳定的字节基线——本环境里编辑器面板尺寸受持久化 dock 布局/窗口状态影响，本会话是 235x188，而 4b–4d-2 记录过的 679231 字节来自当时更大的面板。因此编辑器侧改看「同一会话内的前后对比」（本次两刀之间逐字节相同），runtime 侧（1024x768 / 1395200）仍可跨会话比对。
- 保留未完成：checkpoint 5（view 身份改 owner-scoped `SceneViewKey`，并按此建 `ViewHistoryStore` 稳定键）。

## 2026-09-18 checkpoint：gizmo 开关归编辑器（4.0.3 4d-3a）

- 唯一目标：把「编辑器视口要不要画编辑器家具」从 `AppRenderState::bShowEditorGizmos` 这个跨层格子，交回编辑器自己的 view option，并让 automation 的开关经编辑器而不是经 App。4d-3 原含两件事（feature 归位 / 作者视口 rect 归位），拆成两刀，这一刀只做 feature。
- 开关归编辑器：`EditorLayer` 增加 `isEditorGizmoShown()` / `setEditorGizmoShown(bool)`（view option，与 `_bShowViewportCameraOverlay` 同类，默认 false）。`EditorViewProducer` 的作者视口仍是「authoring 时一律画、之后按开关」，预览 inset 读同一个开关；`EditorSurface` 的 View 菜单直接读写 layer。
- 格子删除：`AppRenderState::bShowEditorGizmos` 与 `App::isEditorGizmoShown` / `App::setEditorGizmoShown` 删除（`AppLifecycle.cpp` 里那两个函数体是它的全部实现）。编辑器不再经 App 的渲染状态表达自己的视图诉求。
- 游戏视口不再受编辑器开关影响：`RuntimeGameViewProducer` 只声明 `features = Game`。刻意接受的行为差异——「Show Editor Gizmos」是编辑器的 view option，一个不由编辑器声明的视口不该被它改变；独立游戏本就没有这个开关可点。
- automation 改走编辑器：`IEditorAutomationControl` 增加 `setEditorGizmosVisible(bool)`，`EditorModule` 转给 `_layer`；`handleSetEditorGizmosVisible` 不再写 App，改为「有编辑器才成功，没有则报 `set_editor_gizmos_visible requires a loaded editor`」。
- 新测试：`Engine/Test/Source/EditorViewProducerTest.cpp`（2 例：authoring 视口恒画编辑器家具；预览 inset 的 gizmo feature 由编辑器的 view option 决定）与 `RuntimeGameViewProducerTest.cpp`（2 例：Runtime 态只声明 `Game`；authoring 态什么都不声明）。前者顺带补上了 4d-2 登记的空缺——预览 inset 第一次被「真实选中一台相机实体」驱动（`App app;` + `EditorLayer layer(&app)` 夹具，不需要 device）。
- 验收：`xmake b ya-game-editor`、`xmake b ya-testing`；`ya-testing` 相关滤镜 114/114（含新增 4 例）；`rg -n 'bShowEditorGizmos' Engine Example` 只剩 `EditorLayer` 自己的 `_bShowEditorGizmos`，`rg -n 'featuresForView|App::isEditorGizmoShown' Engine Example` 为空；经 control 入口起 editor 实例（pid/端口由 `control start` 给出），`set_editor_gizmos_visible {"visible": true}` 返回 `{"visible": true}`（新路由生效），同一调用打到 game 实例返回 `requires a loaded editor`（无编辑器时明确失败而不是静默成功）；game 实例 viewport 截图 1395200 字节，与 4b/4c/4d-1/4d-2 基线一致，说明 runtime 视口输出没变。
- 保留未完成：4d-3b（作者视口 rect 由声明方给出，`setViewportRect` 不再由编辑器写，automation 的 resize 改走声明）、checkpoint 5（owner-scoped `SceneViewKey`）。
- 记录的既有失败（非本刀）：`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots` 与 `InputRoutesByWindowId` 失败，原因是这两个源码守卫用例硬编码的源文件路径被 include/ 归并（`4c1c6e3c`）挪了位置；同批还有并发工作线的 `GameUIHostTest.BuildSnapshotComposesMountedWidgets`。

## 2026-09-18 checkpoint：抽取移出 seal（4.0.3 4a）

- 唯一目标：让 `SceneRenderScheduler::seal()` 只做分组，把 ECS 抽取变成调用方的显式一步。改前 `seal()` 在给请求分组的同时调用 `request.buildSnapshot()`，于是「抽 Scene」不是一步而是分组步骤的副作用，且请求队列里躺着捕获 `Scene*` / `TerrainProcessor*` 的闭包。
- 落地：`SceneRenderRequest` 删掉 `buildSnapshot`，成为纯声明（SceneId/revision、viewId、familyId、相机、rect、compose）；`seal()` 只建表与分组（快照表按 (sceneId, revision) 去重建好、内容为空），family 分组抽成文件内 `buildViewFamilies()` 供两步复用；新增显式第二步 `buildSceneSnapshots(SceneRenderPlan&, const SceneSnapshotResolver&)`，按每个表项向宿主要不可变快照。
- 行为保持：无法解析内容的 Scene 由第二步剔除其 view 并重新分组（回到「该 Scene 这一 tick 不产生 view、其余 Scene 照常录制」），旧的 `if (!snapshot) continue` 语义没有丢；`submit()` 的校验从「有 builder」改成「有 sceneId 与 viewId」。
- 宿主侧：`submitHostSceneViews(scheduler, views)` 不再收 `TerrainProcessor*`、不再建 builder 表；新增 `extractHostSceneSnapshots(plan, views, terrainProcessor)` 承担抽取（按 (sceneId, revision) 在提交列表里找 `Scene*`）。`tickRender` 现在是三步：declare → seal → extract。
- 测试：`HostSceneRenderSubmitTest` 明确断言「seal 之后快照表存在但内容为空」，再断言抽取成功；`RenderRuntimeSnapshotTest` 去掉所有 builder，改由 `sealWithEmptySnapshots` 或显式 resolver 驱动，并新增 `SceneSchedulerDropsViewsOfUnresolvedScene`（坏 Scene 被剔除、好 Scene 不受影响）；`SceneSchedulerDeduplicatesSnapshotPerScene` 与 `SceneSchedulerRebuildsSnapshotWhenSceneRevisionChanges` 现在同时断言「分组不抽取」（buildCalls==0）与「每唯一 Scene 抽一次」。
- 验证：`xmake b ya-render-3d-test ya-game-editor ya-testing`；`ya-render-3d-test` 174/174；`ya-testing` HostSceneRenderSubmit/RenderRuntimeSnapshot/ViewFamilyRenderer/AppKernel/AppAutomationConfig/Editor* 91/91；`HelloMaterial --exit-after-frame=90 --screenshot-target=viewport` 与 `run-editor` 同参数均 exit=0、日志 0 error，viewport 截图 1024x768 有 10625 种颜色 / 98.5% 非黑、编辑器截图 7733 种，说明世界抽取与录制确实在跑而不是退化成 UI-only 空帧。
- 保留未完成：4b（plan 携带 tick-local `Scene*`、删三个反查校验）、4c（合并 `SceneViewDesc`）、4d（`ISceneViewProducer` 与删全局格子）。`SceneSnapshotResolver` 是 4b 要消掉的东西：计划拿到 Scene 句柄后这一步不再需要 resolver。

## 2026-09-18 checkpoint：一份声明结构（4.0.3 4c）

- 唯一目标：让「一个 View 的声明」在全链路上只有一份。改前声明被写了两遍——宿主 `HostSceneViewSubmit`（10 字段）逐字段搬进 `SceneRenderRequest`（11 字段），`seal()` 再逐字段搬进 `SceneViewportTask`（14 字段）；字段没增加表达能力，只增加「该在哪一层读」的记忆负担。
- 一份 `SceneViewDesc`（新文件 `Render3D/Common/SceneViewDesc.h`）：Scene 句柄、viewId、sceneRevision、policyId、view/projection/cameraPos、viewportRect、composeOntoViewId/composeRect，加两个派生成员 `ownsHostViewport()` / `viewProjection()`（`makeCameraViewProjection` 也搬到这里，全仓库唯一一份实现）。`HostSceneViewSubmit` 与 `SceneRenderRequest` 两个类型删除，`SceneViewId` / `kPrimarySceneViewId` 随声明类型一起落到这个头文件。
- 计划条目内嵌声明：`SceneViewportTask` 变成 `{ SceneViewDesc desc; RenderViewOutputDesc output; snapshotIndex; familyIndex; }`，`seal()` 只写 `desc = desc` 加自己派生的 output 与索引，不再逐字段抄；读侧统一 `task.desc.*`（Forward/Deferred/coordinator/cameraForViewRecording/viewDisplayInsetsFromPlan 共约 20 处）。
- 键改用句柄：`SceneViewFamilyKey`、`SceneSnapshotEntry`、seal 内的 `SnapshotKey` 以及 `snapshotFor` 的校验都从派生整数 `sceneId` 换成 `Scene*`（`SceneId` 别名与 `scene->getInstanceId()` 调用从调度器里消失）。族键本来就是 tick-local（`RenderSubmission::_families` 在每次 submission 重用时清空），所以句柄身份足够，且比整数少一层派生。
- 顺手删掉没有写方的 `renderFlags`：它在声明、计划条目、族键三处都存在，全仓库无一处写非 0；一份「唯一声明」不该带一个没人写的字段，族键也同步去掉这个恒 0 分量。
- 宿主侧那层转发删除：`submitHostSceneViews`（只做 skip 空 Scene + `makeCameraViewProjection`）与 `HostSceneRenderSubmit.*` 一起消失；orchestrator 直接把 `SceneViewDesc` 交给 `scheduler.submit()`，文件只剩抽取这一步，改名 `HostSceneExtract.{h,cpp}`（`extractHostSceneSnapshots` 名字不变）。`pendingRequestCount()` 随「request」词汇一起改成 `declaredViewCount()`。
- 测试：`HostSceneRenderSubmitTest.cpp` → `HostSceneExtractTest.cpp`（3 个用例：双 Scene 隔离、同 Scene 两 View 共享 snapshot 且相机不同、tick 外声明被拒；原来那个只测转发层的 `ClosedSchedulerRejectsSubmit` 与 `SceneSchedulerRejectsRequestsOutsideFrame` 重复，删掉）。`SceneFamilyResourcesTest` 的族键构造改用真实 Scene；`RenderRuntimeSnapshotTest` / `ViewFamilyRendererTest` 改用 `SceneViewDesc`，新增 `static_assert` 钉住「计划条目内嵌 desc」与「快照表项带句柄」。
- 验证：`xmake b ya-render-3d ya-game-runtime ya-game-editor ya-testing`；`ya-render-3d-test` 172/172；`ya-testing` HostSceneExtract/RenderRuntimeSnapshot/ViewFamilyRenderer/SceneFamilyResources/AppKernel/AppAutomationConfig/Editor* 93/93；`HelloMaterial` 与 `run-editor` 各跑 `--exit-after-frame=90 --screenshot-target=viewport` 均 exit=0、日志 0 error，截图字节数与 4b 完全一致（1395200 / 679231），说明这一刀没有改变渲染结果。
- 保留未完成：4d（`ISceneViewProducer`、五个全局格子、declare 路径清零 ECS 查询）、checkpoint 5（`PreparedView` 收口 `CameraFrameInput` patch 与 owner-scoped `SceneViewKey`）。

## 2026-09-18 checkpoint：相机预览与它的选择归编辑器（4.0.3 4d-2）

- 唯一目标：把「预览显示哪台相机」从宿主铸造的 view + 两个全局格子，交回持有选择的编辑器；顺带把声明里两件只有声明方知道的事（画什么 feature、从哪个实体渲染）从 orchestrator 的拼装挪进声明。
- 预览归位：`EditorViewProducer`（原 `EditorAuthoringViewProducer` 改名——它现在声明编辑器的两个 view）在声明作者视口之外，按 `EditorLayer::getCameraPreviewEntity()`（用户在 Hierarchy 选中的相机实体）声明预览 inset：`composeOntoViewId = kPrimarySceneViewId`、`composeRect = makeViewDisplayInsetRect(...)`、`viewOwner = 该相机实体`。view id `kEditorPreviewViewId = 2` 是编辑器自持常量，宿主不再铸造 `kHostOverlayPreviewViewId`；`cameraProjectionForOutput`（按输出 rect 的 aspect 出投影）随之落到编辑器。
- 两个格子删除：`AppRenderState::bCameraPreviewHostOwned` 与 `cameraPreviewEntityUUID`（含 `AppRenderServices` 的四个访问器）。`EditorLayer::onUpdate` 因此只剩 `_lastDeltaTime`——它的全部职责就是往这两个格子里写状态。`resolvePreviewCamera` 同时删除。
- FOV 线框换家：选中相机的线框原本经 `cameraFrame.overlay.worldLines`（宿主相机包络）画进世界图，选择信息来自 `cameraPreviewEntityUUID`。现在它由编辑器自己的 world overlay pass 画（`recordSelectedCameraFrustum`，与网格/gizmo/HUD 同批），用的是同一个 `appendCameraFrustumOverlayLines`，所以「选择」这条信息不再需要穿越宿主。
- 声明补两件事：`SceneViewDesc` 增加 `features`（`FRenderFeatureMask`）与 `viewOwner`（`entt::entity`）。前者让 orchestrator 的 `featuresForView` 与其「按 viewId 猜这是谁的 view」的逻辑消失；后者让 `prepareView` 的 viewOwner 直接来自声明，而不是「是不是预览 → 用预览相机，否则用 runtime 相机」的三分支猜法。RuntimeGameViewProducer 因此显式声明 `features = Game | (gizmo 开关 ? Gizmo : 0)`（保留 PIE 下的调试覆盖）与 `viewOwner = 游戏相机实体`（那台相机自己的机身体不该出现在它自己的视野里）。
- 验收：`ya-render-3d-test` 172/172；`ya-testing` 相关滤镜 101/101（`GameUIHostTest.BuildSnapshotComposesMountedWidgets` 是并发工作线那侧的既有失败，与本刀无关，已在 4d-1 记录过一次）；`HelloMaterial` 与 `run-editor` 各跑 `--exit-after-frame=90 --screenshot-target=viewport` exit=0、日志 0 error、截图字节数仍与 4b/4c/4d-1 完全一致（1395200 / 679231）。
- 刻意接受的行为差异（两条，都需要知会）：① 独立游戏（无编辑器）在 Runtime 态若有第二台相机，过去宿主会自动在角落声明一个预览 inset，现在没有——按计划「预览相机的选择策略回到持有选择的编辑器」，游戏二进制不该自己挑一台相机做画中画；若将来要，`RuntimeGameViewProducer` 显式声明即可（接缝已在）。② 选中相机的 FOV 线框从「世界图 overlay（可能被几何遮挡关系按原 pass 处理）」改为「编辑器 world overlay pass 的非深度测试段（与网格同批）」，即线框现在与网格同样始终可见。
- 未覆盖：预览 inset 与 FOV 线框需要「用户选中一台相机」才会出现，而 smoke 场景只有一台相机、也没有 UI 选择动作，所以两条路径只经过代码审查与构建验证，没有被自动化覆盖；补法要么给 `EditorViewProducer` 一个能构造 `EditorLayer` 的夹具（当前 `ya-testing` 没有构造 `EditorLayer` 的先例，需要带 device 的 App 夹具），要么经 automation `invoke` 走一次选择动作。已登记为待补。
- 保留未完成：4d-3（`bShowEditorGizmos` 格子归 `EditorLayer`；作者视口 rect 由声明方给出）、checkpoint 5（owner-scoped `SceneViewKey`）。

## 2026-09-18 checkpoint：世界视口由持有方声明（4.0.3 4d-1）

- 唯一目标：让「这个 tick 渲染哪个视口」由**持有该视口的 owner** 声明，而不是编辑器写全局格子、runtime 再去读。4d 按计划拆三刀，这一刀做接缝 + 世界视口。
- 接缝：`Render3D/Common/SceneViewProducer.h` 新增三件套——`SceneViewCollectContext`（activeScene / viewportRect / viewportExtent / hostTick / deltaTime）、`SceneViewCollector`（纯 sink，无策略：不想要就不要 declare）、`ISceneViewProducer::collectSceneViews(context, collector)`。比计划原文多一个显式上下文参数：帧事实应当被读，塞进 collector 会把 sink 和输入混在一起。宿主侧 `AppRenderState::viewProducers` + `App::add/removeSceneViewProducer`。
- 两个声明方：`RuntimeGameViewProducer`（GameRuntime，Runtime 态用游戏相机；无相机实体时仍声明、相机为单位矩阵）与 `EditorAuthoringViewProducer`（GameEditor，非 Runtime 态且非 2D canvas 时用编辑器相机；2D canvas 与 PIE 都不声明）。`EditorModule` 在 onAttach/onDetach 注册与注销。
- 删掉的两个格子：`AppRenderState::extensionHostView`（含 `set/clearExtensionHostViewState`）与 `AppRenderState::bWorldSceneRenderEnabled`（含 `set/isWorldSceneRenderEnabled`）。`prepareHostViewState` 相应收成「宿主几何 + 时钟」（viewportRect / framebuffer scale / clock），主 view 的相机由声明回填进 `hostView`——相机包络与 offscreen resize 仍读 `hostView`，渲染结果不变。
- 动画策略的诚实输入：`SkeletonAnimationSystem::setTickPolicy` 从「某个 viewport 的世界开关」改为「上一 tick 是否为该 Scene 产出了内容」——`AppRenderState::renderedScenesLastTick`，由 `renderedScenes(plan)` 在抽取后写入（该 helper 从 coordinator 文件内提到 `SceneRenderScheduler.h` 与宿主共用）。一 tick 滞后是结构性的（system 跑在 view 声明之前），正是 UE `bRecentlyRendered` 的形态。
- 场景查询归位：orchestrator 静态函数 `getPrimaryCamera` 移入 `Utility/SceneCameraQuery`（`findPrimaryCamera` / `findSecondaryCamera`），`AppSceneServices::getPrimaryCamera` 与 `syncRuntimeCameraAspect` 改用它；orchestrator 内的 `findNonPrimarySceneCamera` 删除（`resolvePreviewCamera` 复用 `findSecondaryCamera`）。预览相机选择与 frustum 线仍留在此文件，属 4d-2。
- 验收：`ya-render-3d-test` 172/172；`ya-testing` 相关滤镜 101/101（新增 `AppLifecycleTest.*`）；`rg -n 'extensionHostView|setWorldSceneRenderEnabled|isWorldSceneRenderEnabled|bWorldSceneRenderEnabled|findNonPrimarySceneCamera' Engine Example` 为空；`HelloMaterial` 与 `run-editor` 各跑 `--exit-after-frame=90 --screenshot-target=viewport` exit=0、日志 0 error、截图字节数与 4b/4c 完全一致（1395200 / 679231），说明 runtime 与 editor 两条线的主 view 都由 producer 正确声明。
- 过程中的一次误判记录：`AppLifecycleTest.SaveScenePersistsAndReloadsRoundTrip` 曾失败一次，用 `git stash` 隔离并强制重建后复跑 5 次全绿，确认与本刀无关；当时是可执行文件与 dylib 处于两次部分重建之间的不一致状态。
- 保留未完成：4d-2（相机预览生产者 + 删 `bCameraPreviewHostOwned` / `cameraPreviewEntityUUID` / 宿主铸造的 `kHostOverlayPreviewViewId`）、4d-3（`SceneViewDesc.features` + 删 `bShowEditorGizmos` 与 `featuresForView`）、checkpoint 5（owner-scoped `SceneViewKey`）。

## 2026-09-18 checkpoint：计划保留 Scene 句柄（4.0.3 4b）

- 唯一目标：把「这一 tick 每个 view 渲染哪个 Scene」从反查变成声明本身的事实，从而删掉 `derivedSceneForHostView`、`SceneRenderPlanInput::complete()`、`derivedScenesAgreeWithPlan()` 三个运行时校验和 4a 的过渡物 `SceneSnapshotResolver`。
- 声明携带句柄：`SceneRenderRequest.sceneId` → `SceneRenderRequest.scene`（宿主本就持有 live Scene，句柄整 tick 有效）；`SceneViewportTask` 与 `SceneSnapshotEntry` 各带 `Scene*`，`sceneId` 由 `seal()` 从 `scene->getInstanceId()` 派生，同一事实不再存两份。
- 抽取不再反查：`SceneSnapshotResolver(SceneId, revision)` → `SceneSnapshotExtractor(Scene&)`，宿主侧 `extractHostSceneSnapshots(SceneRenderPlan, TerrainProcessor*)` 收 sealed plan（by value），不再需要 `span<HostSceneViewSubmit>` 去按 id 找 Scene，也去掉了「每条表项扫一遍提交列表」的二次查找。
- 构造期不变量取代运行时校验：新增 `ExtractedSceneRender`（私有 `plan` + `views`，只有 `buildSceneSnapshots()` 是 friend）。只有它进入 `RenderFramePlan::sceneRender`，于是「忘了抽取」从运行时日志变成编译错误；view 与 task 只能经 `pairViewFrames()` 配对，`views.size() == viewportTasks.size()`、`views[i].task == &viewportTasks[i]`、`frameData != nullptr` 由构造保证，`complete()` 与其错误分支消失。`pairViewFrames()` 同时接管宿主原本分散在 `tickRender` 里的 frame-data 策略（每存活 view 一槽、UI-only tick 只留相机那一槽并丢弃陈旧 snapshot）。
- 每 view 的 Scene 只来自自己的 task：`SceneViewRecording::derivedScene`、`ViewFamilyRecordContext::derivedScene`、`derivedSceneForFamily` 删除，Forward/Deferred 改为 `.derivedScene = recording.task ? recording.task->scene : nullptr`；`uniqueDerivedScenes` 变成 `RenderFrameCoordinator.cpp` 的文件内 `renderedScenes(plan)`，直接读已解析的快照表（表项按 (sceneId, revision) 去重，故 derived state 也按 `Scene*` 去重）。`derivedScenesAgreeWithPlan` 校验的两种错误（同 sceneId 两份 Scene、不同 sceneId 共用一份）在只剩一份句柄后不可能构造出来。
- 附带：`ya-render-3d-test` 增加 `ya-scene-core` 依赖，plan 测试改用真实 `Scene`（不再是 `reinterpret_cast<Scene*>(0xA000)` 这种无法解引用的假句柄）；删除 `MixedDerivedSceneInOneFamilyIsRejected`、`SharedDerivedSceneAcrossSceneIdsIsRejected`、`SceneRenderPlanInputRequiresPlanAndMatchingViewRecordings` 三个只测已删机制的用例，新增 `EveryTaskCarriesTheSceneItsDeclarationNamed`、`SameSceneViewsShareOneSnapshotAndScene`、`UiOnlyTickKeepsOneFrameSlotAndPairsNoView`、`ExtractedSceneRenderPairsEveryTaskWithItsOwnFrameData`。
- 验证：`xmake b ya-render-3d ya-engine ya-testing`；`ya-render-3d-test` 172/172（净减 2：删 3 加 1 个新用例）；`ya-testing` HostSceneRenderSubmit/RenderRuntimeSnapshot/ViewFamilyRenderer/AppKernel/AppAutomationConfig/Editor* 89/89；`HelloMaterial --exit-after-frame=90 --screenshot-target=viewport` 与 `run-editor` 同参数均 exit=0、日志 0 error，截图 1024x768 非黑（运行时 1395200B / 编辑器 679231B），世界抽取与录制仍在跑。
- 保留未完成：4c（合并 `HostSceneViewSubmit` 与 `SceneRenderRequest` 为 `SceneViewDesc`，消掉 12/14 字段两次机械搬运；也是 4d 的前置）、4d（`ISceneViewProducer`、删五个全局格子、declare 路径清零 ECS 查询）、checkpoint 5（owner-scoped `SceneViewKey`）。
- 已登记不做的偏离：`ForwardRenderPipeline::appendViewportPassGraph` 与 `DeferredRenderPipeline` 仍在录制期遍历 live ECS（direction gizmo / billboard / skybox），属于 §3.10.4 里随 P3 `PreparedView` 一起删的同一类问题，本刀不顺手改。


- 触发：核查 `isWorldSceneRenderEnabled()` 该属于 Scene 还是 viewport client。结论：它既不属于 Scene 也不属于 Renderer，而属于**声明方是否声明**；该格子的存在是 view 收集链缺少 producer 接口的症状。
- 盘点的现状（登记进 plan §3.10 / temporal_semantics M9）：真正的 view 声明只有 GameRuntime 一处；GameEditor 经四个全局格子（`bWorldSceneRenderEnabled`、`extensionHostView`、`bCameraPreviewHostOwned`+`cameraPreviewEntityUUID`、`viewportRect`）影响它，且全靠 hook 顺序成立（写在 `onLogic`、读在 `tickRender`）。一个 view 被声明六次（`HostSceneViewSubmit` → `SceneRenderRequest` → `SceneViewportTask` → `SceneViewRecording` → `CameraFrameInput` → `RenderViewOutputDesc`），字段只被机械搬运。
- 三处空转：① `renderFlags` 无写方、`sceneRevision` 恒 0、`familyId` 恒 1，于是 `SceneViewFamilyKey` 退化为 `snapshotIndex` 单键；② `seal()` 内直接调 `buildSnapshot()`，ECS 抽取是分组步骤的副作用；③ plan 丢弃 Scene 指针，导致宿主必须用 `derivedSceneForHostView` / `complete()` / `derivedScenesAgreeWithPlan()` 三个函数反查校验。
- 保留的判断：调度器的「帧内聚合 + 按 (SceneId, sceneRevision) 去重 snapshot + 按 ViewFamily 分组」对应 UE family/renderer 分层，是对的，不删；不引入 WorldRegistry；GUI Framework 仍不认识 Scene。
- 落地：写入 plan §3.10（现状、六层重复、三处空转、目标形态、明确不做）、4.0.3 执行顺序第 4 刀（4a 抽取移出 seal / 4b plan 保留 Scene 句柄 / 4c 合并 `SceneViewDesc` / 4d `ISceneViewProducer`）、R1/R3 交叉引用、temporal_semantics M9 与批次 P1e、todo 六个工作项、feature_matrix `view_declaration_contract`。
- 本 checkpoint 无代码改动、无测试执行；上面登记的是下一步待实现项，不当作已完成。

## 2026-09-17 checkpoint：Surface 持有 presentation blit descriptor set

- 唯一目标：display compose 不再绑定空的 `BasicPostprocessing` input set。View tone-map DS 仍在 `ViewResources::post.toneMap`；swapchain blit 是 Surface 轴，由 `PresentationGraphService` 在 `init` 分配并传入 `render()`。
- 落地：`PresentationGraphService` 持有 `_presentationInputPool` / `_presentationToneMap`；`recordDisplayCompose` 传入 `.toneMap`；`BasicPostprocessing::render` 在 set 为空时拒绝 bind/draw。
- 验证：`xmake b ya-game-runtime`、`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='PostProcessingStageTest.*:ViewPassResourcesTest.*:DeferredPassParamsTest.*:DeferredRenderPipelineTest.*'` 10/10；`python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=3 --log-level=warn --log-detail-level=error` 退出码 0，无 `VkDescriptorSet 0x0` / MoltenVK `bindDescriptorSets` 崩溃；`git diff --check` 通过。
- 保留未完成：4.0.3 Renderer 合并；FrameRecording/FlightResources 拆名；PreparedView；Stage current-view；压缩 tickRender；产品双 Scene 显示；双 Surface GPU。C 仍未清除 Stage current-view。

## 2026-09-17 checkpoint：automation / TaskManager 的 per-tick 命名（M1 P1d）

- 来源：P1b-2a 扫 perf 命名面时发现同一根轴上还有一批 host tick 语义的符号仍叫 frame。它们不在原 M1 清单里，属于「同轴遗留」，单独补一批，避免 M1 收尾后 automation 内部同时存在 tick 与 frame 两套叫法。
- 落地：`AppAutomation::isFrameAutomationEnabled` / `hasFrameAutomationConfig` → `isTickAutomationEnabled` / `hasTickAutomationConfig`；`shouldRequestQuitAfterFrame` → `shouldRequestQuitAfterTick`；`EAppAutomationExitReason::ExitAfterFrame` → `ExitAfterTick`；`isAutomationStableFrameReady` / `bStableFrameReady` → `isAutomationStableTickReady` / `bStableTickReady`（它们比较的本来就是 `stableTicks` / `settleTicks`）；`TaskManager::registerFrameTask` / `hasFrameTasks` → `registerTickTask` / `hasTickTasks`（`taskManager.update()` 在 `tickLogic` 每 tick 调一次）；`AppAutomationTickContext` 的 `frameContext` 参数 → `tickContext`。调用方同步：`GameRuntimeTickOrchestrator`、`AppAutomationControlService`、`AppLifecycle`、`EditorLayer`、`EditorActionCatalog`、`RuntimeRenderSettingsSection`、`AppKernelTest`。
- 保留的边界：日志文案 `warmup frames` / `stable frames`、`getAutomationExitReasonName` 返回的 `exit-after-frame`、以及 `--exit-after-frame` / `frame_index` 等 CLI、config、协议键都不改——它们是外部可见面。
- 列对齐：`onTickCompleted` 里的 `bStableTickReady` 赋值列已补回，diff 只剩标识符。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing`；`xmake r ya-testing --gtest_filter='AppKernelTest.*:AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*'` 66/66（含改名后的 `AppKernelTest.HeadlessLoopHonorsExitAfterTick`）；`git diff --check` 通过。

## 2026-09-17 checkpoint：Scene snapshot 命名（M2 P1c）

- 唯一目标：把 `SceneFrameSnapshot` 改成 `SceneSnapshot`，不改任何行为。它是「某个 Scene 在某个内容版本上的不可变内容」，Scene 不变时被同一帧的多个 View 共用；名字里的 Frame 会让它看起来像按渲染帧产出的数据，而它恰恰是跨 View 共享、与 host tick 无关的那部分。
- 落地：定义（`RenderFrameData.h`）与全部前置声明；`SceneRenderScheduler` 的 `SceneSnapshotEntry` / `SceneRenderPlan::snapshotFor` / `SceneRenderRequest::buildSnapshot`；`SceneFamilyResources`（成员、ctor、`snapshot()`、`bindSnapshot()`）；`RenderSubmission::allocateSceneFamily`；`RenderFrameExtractor::extractSceneSnapshot` / `prepareView` / `extractSceneLights`；`HostSceneRenderSubmit` 的 builder 类型；`RenderRuntimeSnapshotTest` / `SceneFamilyResourcesTest` / `ViewFamilyRendererTest`。
- 列对齐：`RenderFrameExtractor`（成员块与续行参数）、`RenderSubmission`、`SceneFamilyResources`、`RenderFrameData` 的声明列按原列补回；测试里两处 `static_assert` 的续行缩进随锚点一起左移，diff 只剩标识符。
- 计划同步：`plan.md` 里描述现行类型的 `SceneFrameSnapshot` 一并改成 `SceneSnapshot`（旧名只保留在 M1/M2 的 old→new 映射记录里）。
- 验证：`xmake b ya-render-3d-test`、`xmake b ya-testing`、`xmake b ya-game-editor`；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`git diff --check` 通过。
- 保留未完成：测试文件名 `RenderRuntimeSnapshotTest.cpp` 仍是旧命名（它的内容也已不是「runtime snapshot」，与 `RenderFrameExtractor` 拆分一起处理，属 P3）。

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
