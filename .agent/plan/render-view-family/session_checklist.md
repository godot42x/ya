# Session Checklist

## 开工前

- [ ] 阅读根 AGENTS.md、.agent/plan/AGENTS.md。
- [ ] 按任务读取 ya-build、render-arch；改 GUI compose 时再读取 gui-framework。
- [ ] 查看 git status、最近相关提交和 progress.md。
- [ ] 复述本轮唯一 checkpoint、边界、保留项和非目标。
- [ ] 用 rg 核对当前符号和调用方，不以旧计划路径替代源码事实。
- [ ] 确认不会重复实现 gui-multi-os-window-editor 已有的 surface/present 能力。

## 实施中

- [ ] 只修改当前 checkpoint 所需的抽象、调用点和测试。
- [ ] 不把 Forward/Deferred 策略差异抽成万能基类。
- [ ] graph execute 不查询 ECS、Scene、live WidgetTree 或 ResourceResolveSystem。
- [ ] 不在 command recording 中途重建 GPU 资源，检查 deferred deletion/lifetime。

## 收尾前

- [ ] 运行受影响的 XMake build/test，target 名称以 xmake l targets 为准。
- [ ] 运行对应 golden/trace；R2 后增加双 View/双 Surface。
- [ ] 检查 git diff --check、staged diff 和生成文件。
- [ ] 更新 progress.md、feature_matrix.json、todo.md。
- [ ] 明确记录保留项、未完成项和偏离项。
- [ ] 代码、测试、plan/progress 使用同一 checkpoint 提交。

## 最近一次 checkpoint

- 2026-09-19：窗口是呈现面，渲染分辨率是设置（用户选 B，"一般游戏都支持调整分辨率来提升性能"）。`HostViewState::viewportRect`(Rect2D) 改名为 `renderResolution`(Extent2D)，头注释写明窗口只决定怎么呈现、presentation pass 把渲染图拉伸上去；`AppRenderServices::setViewportRect/getViewportRect` → `setRenderResolution/getRenderResolution`；删 `App::_windowSize` 与 `getWindowSize()`（一个到不了渲染输入的窗口尺寸副本）；`SceneViewCollectContext::viewportRect` → `renderResolution`；`declareViews` 不再抄回 View 的 rect（要读实际渲染尺寸就读已发布输出 `getViewportExtent()`）；runtime camera aspect 与 game producer 的 rect 都读设置、不读窗口；Game UI 逻辑视口从 `sceneRender.displayRootTask()->desc.viewportRect` 派生（顺带修 PIE 的不精确与"没有 host viewport 时仍用旧 rect 做 setPresentation"）；automation `viewportResize` → `renderResolution`（含配置键与日志）；`get_world_view_state` 分开上报 `render_resolution` 与 `rendered_viewport_extent`，编辑器 smoke 改为断言实际产出 extent 非退化。验证：7 个目标 build ok；ya-render-3d-test 175/175；滤镜 282 passed / 3 failed（与基线同 3 个）；两张默认 smoke 截图逐字节相同；编辑器 smoke 六步全过；**B 的正面证据**：`smoke.renderResolution=640x360` 时 viewport 截图 640x360、presentation 截图仍 1024x768，目视确认被拉伸填满。下一刀：presentation fit/letterbox（需先决定多出来的像素画什么）或 `RenderSubmission` 拆分。
- 2026-09-19：公开 `Renderer` 合并（4.0.3 checkpoint 2/3）。`record` / `recordViewFamilies` / `prepareFrameRecord` 从 `RenderFrameCoordinator` 移入 `RenderDeviceState`（两者本就共用同一生命周期，coordinator 只持一个 device 指针），删 `friend struct RenderFrameCoordinator;` 与 5 处 `_device->_submissions` / `_device->_presentationGraphService` 私有写入，删 `RenderFrameCoordinator.{h,cpp}`。App 侧删 `AppRenderState::coordinator`、`AppRenderServices::getFrameCoordinator()`、`AppLifecycle` 的构造与 teardown 分支；`tickRender` / `recordFrame` 改收 `RenderDeviceState&`。`record()` 拆成 `prepareFrameRecord()`（录制前 7 步）+ `record()`（录制），把 I4 那条"没写下来的 safe point 约定"变成两个函数。附带修正 `ForwardViewportLitPasses.cpp` / `ForwardViewportUnlitPass.cpp` 缺 `Stage/IRenderStage.h`（过去靠 unity 批次掩盖）。验证：8 个目标 build ok；ya-render-3d-test 175/175；滤镜 282 passed / 3 failed（与基线同 3 个）；两张 smoke 截图逐字节相同。下一刀：`RenderSubmission` 拆 `FrameRecording` / `FrameFlightResources`。
- 2026-09-19：`HostViewState` 每个字段一个写者。`prepareHostViewState` 只写 `clock`（没有 renderer 时整体清空）；identity 与生效几何的唯一每帧写者变成 `declareViews`（"没人声明 host viewport"分支写 identity，代替原先每帧先 reset 再覆盖）；退化几何修补（`extent <= 0 ? _windowSize`）从每帧搬到 init 种子（CI 尺寸 > 0 ? CI : 窗口实际尺寸），因为它在下游 `applyViewportResize` 忽略退化 rect 的前提下永不可达；`AppLifecycle` 不再直接写 `hostView.viewportRect`，改走 `AppRenderServices::setViewportRect`，请求侧因此只有一条写入路径。`HostViewState.h` 顶部列出写者表。验证：四个目标 build ok；滤镜 282 passed / 3 failed（与基线同 3 个）；两张 smoke 截图逐字节相同；`run_widgettree_editor_smoke.py`（断言 viewport_rect 非退化）六步全过。下一刀：公开 `Renderer`（4.0.3 checkpoint 2/3）。
- 2026-09-19：`tickRender` 每一步自己持有自己的存储。`declareViews` 收回 `SceneViewCollector` 并删掉没人用的返回指针（`SceneRenderScheduler::submit` 按值接收，V1 之后 host camera 走 `hostFrameData()`）；`buildGameRenderFrame` 改为返回 `TickFrame`（自带 `UIFrameSnapshot`，`boundFrame()` 在读取时把 `frame.uiFrameSnapshot` 绑到自己身上，因此按值返回/移动后指针仍然正确）；`hostViewDesc` 消失。头文件显式 include `GUI/Widgets/UIFrameSnapshot.h` 与 `Render3D/Common/RenderFrameInputs.h`，删掉已死的 `RenderOverlay.h` include 与三个过期前向声明。验证：四个目标 build ok；渲染/编辑器滤镜 282 passed / 3 failed（与基线同 3 个）；两张 smoke 截图逐字节相同。下一刀：`HostViewState` 单一写者 → 公开 `Renderer`（4.0.3 checkpoint 2/3）。
- 2026-09-19：逻辑→渲染链减法（app 主流程 + 渲染主流程）。删主机 screen-overlay 通道——`FramePacket::overlay` 的唯一生产者是不可达 demo，四路输入全空时 snapshot 为 `nullptr` 而 Forward/Deferred 仍每帧 append，整条 `kTopologyPassOverlay` 在所有被测路径上空跑；连同 `RenderViewportOverlaySnapshot` / `recordRenderViewportOverlayPass` / `RenderOverlay.cpp` / `perf::sample::renderViewportOverlay()` 一起删除（保留 `RenderOverlayText2D` / `Line3D` 两个值给编辑器 HUD 与视锥）。删不可达 demo（`AppMode` / `_appMode` / `clicked` / `buildScreenOverlaySprites`）。窗口标题移出 tick：新增 `IRender::getDeviceName()` 取代 `render->as<VulkanRender>()`，标题在 `RenderDeviceState::initRenderBackend` 设一次。删死步骤 `syncViewportState`。删 `resolveViewportExtent` 与 `SceneViewCollectContext::viewportExtent`（零读者 + 两段死兜底），host camera aspect 改读 `hostView.viewportRect.extent`。验证：`ya-render-3d-test` **175/175**（−1 是钉住已删状态的用例）；渲染/编辑器滤镜 271 passed / 3 failed，3 个与 V1–V8 基线逐项相同；两张 smoke 截图逐字节相同（runtime `1c666897…`、editor `5b8f5dd8…`）。下一刀顺序：`tickRender` 三处生命周期去注释化 → `HostViewState` 单一写者 → 公开 `Renderer`（4.0.3 checkpoint 2/3）。
- 2026-09-17：Surface 持有 presentation blit tone-map DS。Checkpoint C 删除 processor 内部 set 后 display compose 仍 bind `VkDescriptorSet 0x0`，MoltenVK encode 崩溃。`PresentationGraphService` 分配 Surface 生命周期 CIS；`BasicPostprocessing::render` 拒绝空 set。PostProcessing/ViewPass/Deferred 10/10；HelloMaterial `--exit-after-frame=3` 退出码 0。下一刀仍是 4.0.3 Renderer。
- 2026-09-17：修正 C/D/E 计划状态为部分完成；目标收成公开 `Renderer`。不改引擎代码。下一刀是合并 DeviceState+Coordinator。
- 2026-09-17：host 提交 live Scene 列表：`HostSceneViewSubmit` 按 Scene* 去重 extract；recording derivedScene 从列表查找。HostSceneRenderSubmitTest 3/3，专项与回归 76/76。产品双 viewport 排在 4.0.3 之后。
- 2026-09-17：family-scoped derived Scene：删除 `RenderFramePlan::derivedScene`；recording 携带 host Scene；同 family 共享、跨 SceneId 隔离。Host 仍只提交一个 live Scene。专项与回归 76/76。下一刀是产品双 Scene 录制 / 双 Surface GPU。
- 2026-09-17：4.0.2 E：拆除 `RenderRuntime` 为 `RenderDeviceState` + `RenderFrameCoordinator`；删除 `ViewportStateService` 与 `getActiveScene` locator。Host 拥有 world-enable 与 viewport rect。空 `sceneRender` 是 UI-only。未引入空 ViewHistoryStore。专项与回归 72/72。下一刀是产品双 Scene / 双 Surface 验收。
- 2026-09-17：4.0.2 C：typed `ViewResources` 持有 SSAO/Light/EntityId/Overlay/debug/post DS/UBO；Bloom/BasicPost viewId map 与 Stage singleton CIS 删除；PointShadow packet 在 Shadow View Binding。ViewPassResourcesTest 5/5，专项与回归 66/66。下一刀曾是 Checkpoint D（family renderer），不再扩大 mega Binding。
- 2026-09-17：4.0.2 B：`SceneViewFamilyPlan` 在 seal 时物化；`SceneFamilyResources` 由 submission 持有 skinning SSBO。同 Scene 双 View owner 相同，双 Scene owner/buffer 不同。SceneFamilyResourcesTest 4/4，专项与回归 57/57。下一刀曾是 Checkpoint C（typed pass resources），不再扩大 mega Binding。
- 2026-09-17：4.0.2 A：`RenderSubmission` / `RenderSubmissionPool` 拥有 cmd/upload/transient DS/keepalive/finish；删除 `RenderSubmissionContext` / `RenderSubmissionTable` 与 resource-set `beginSubmission`。RenderSubmissionTest 7/7，专项与回归 53/53。下一刀曾是 Checkpoint B（SceneFamily），不再扩大 mega Binding。
- 2026-09-16：重审 4.0.2：补齐 SceneFamily 生命周期，ViewFamily 改为 graph 编译单位；执行顺序为 Submission owner → SceneFamily owner → typed pass resources → family renderer → 拆 RenderRuntime。下一刀曾是 Checkpoint A，不再扩大 mega Binding。
- 2026-09-16：postprocess/bloom 输出与 CombinedImageSampler set 按 View 持有；同一帧两个 View 不再共用 display GPU 资源。ViewPersistentResourceKeyTest 含 PostprocessAndBloomOutputsStayViewKeyed，RenderGraphCoreTest.ViewKeyedPostprocessTexturesStayIndependentAcrossSequentialGraphs，专项回归 56/56，ya-game-runtime / ya-game-editor 构建通过。Processor viewId map 仍是迁移期 hack。
- 2026-09-16：overlay View 的 composeRect 与 host viewport identity 分离；overlay 不得 resize host RT spec；camera frustum 为 compact gizmo。CameraFrustumOverlayTest 1/1，RenderRuntimeSnapshotTest 14/14，专项回归 52/52，ya-game-runtime / ya-game-editor 构建通过。
- 2026-09-16：产品路径为选中的 world Camera submit 第二个 SceneRenderRequest，ViewCompose PiP 到主 viewport 右下角，world Camera 用 overlay 3D 线画锥体。CameraFrustumOverlayTest 2/2，RenderRuntimeSnapshotTest 14/14，专项回归 53/53，ya-game-runtime / ya-game-editor 构建通过。
- 2026-09-16：RenderRuntime 按 SceneViewportTask 循环 tick/publish；同一 Scene 两个 View 共享 snapshot。RenderRuntimeSnapshotTest 11/11，专项与回归 48/48，ya-game-runtime 构建通过。
- 2026-09-16：Forward/Deferred viewport（含 GBuffer/SSAO）persistent key 按 ViewId 分名；同一 executor 上 View A/B 不再共用一张 GBuffer/color。ViewPersistentResourceKey 4/4，专项与 snapshot/binding/submission 回归 47/47，ya-game-runtime 构建通过。
- 2026-09-16：每个 View 拥有独立 output/extent/format 句柄；发布 B 不改写 A。RenderViewOutputTable 5/5，snapshot/binding/submission 回归 38/38，ya-game-runtime 构建通过。
- 2026-09-16：RenderRuntime 按 flight 持有 live submission；overlay 与 graph-exported image 保活到该 flight 下次 fence-safe 复用。RenderSubmissionTable 5/5，snapshot/binding/deferred/arena 回归 32/32，ya-game-runtime 构建通过。
- 2026-09-16：Shadow FrameResourceSet 提供 beginSubmission/beginView；cascade/face descriptor 与 upload slice 按 View 隔离。RenderViewBindingTable 8/8，snapshot/deferred/arena 回归 27/27，ya-game-runtime 构建通过。
- 2026-09-16：Deferred FrameResourceSet 提供 beginSubmission/beginView；SSAO/skybox 写入同一 View slot。抽出 ViewDescriptorSetAllocator 供 Forward/Deferred 共用。RenderViewBindingTable 7/7，snapshot/draw-candidate/deferred 回归通过，ya-game-runtime 构建通过。
- 2026-09-16：Forward FrameResourceSet 提供 beginSubmission/beginView；同一 submission 的 View 拥有独立 frame descriptor/slice。RenderViewBindingTable 6/6，snapshot/draw-candidate 回归通过，ya-game-runtime 构建通过。
- 2026-09-16：View-owned draw bucket 已完成 source pointer + order indices 迁移；14 个 RenderRuntime/DrawCandidateView 测试通过，ya-game-runtime 构建通过。
- 2026-09-16：Forward/Deferred 已移除跨 View 的 `_lastTickCtx` / `_lastFrameInput`，graph build 使用调用栈内 View-local context；渲染测试 14/14，ya-game-runtime 构建通过。
- 2026-09-16：FrameUploadArena 同 `(flightIndex, frameToken)` 的 begin 改为幂等追加语义；同 submission 的后续 allocation 不 rewind cursor，FrameUploadArena 专项测试通过。
- 保留未完成项：双 Scene/双 ViewFamily 产品录制、双 Surface GPU 验收、viewport click picking 仍需 mesh/billboard 写 entityId。
