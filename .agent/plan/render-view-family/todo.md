# TODO

## 逻辑→渲染链减法（2026-09-19，见 progress 同名 checkpoint）

判据只有两条：**没有生产者的东西删掉；每 tick 重复的展示工作移出 tick**。已落地：

- [x] 删不可达 demo：`AppMode` / `App::_appMode` / `App::clicked` / `dispatchInputFallbackEvent` 的绘图分支 / `buildScreenOverlaySprites`（保留 `_lastMousePos`）。
- [x] 窗口标题移出 per-tick：新增 `IRender::getDeviceName()` 取代 `render->as<VulkanRender>()` 降型；标题在 `RenderDeviceState::initRenderBackend` 里设一次。
- [x] 删死步骤 `syncViewportState(app)` 与它的 `Logic/ViewportSync` profile scope。
- [x] 删 `resolveViewportExtent` 与 `SceneViewCollectContext::viewportExtent`：host camera aspect 直接读 `hostView.viewportRect.extent`，logic 段不再读上一 tick 发布的 device extent。
- [x] 删主机 screen-overlay 通道（整条 pass）：`FramePacket::OverlayInput` / `overlay`、`RenderViewportOverlaySnapshot`、`recordRenderViewportOverlayPass`、`prepareRenderViewportOverlayPipeline`、`RenderOverlay.cpp`、Forward/Deferred 的 overlay pass 与 `perf::sample::renderViewportOverlay()`。
- [x] `tickRender` 每一步自己持有自己的存储：`declareViews` 收回 collector 并删掉没人用的返回指针；`buildGameRenderFrame -> TickFrame`（自带 UI snapshot，`boundFrame()` 在读取时绑定 packet 指针）；`hostViewDesc` 局部指针消失。顺带把头文件里已死的 `RenderOverlay.h` include 与三个过期前向声明删掉。
- [x] `HostViewState` 每个字段一个写者：`prepareHostViewState` 只写 `clock`（退化几何修补搬到 init 种子），identity 与生效几何的唯一每帧写者是 `declareViews`（含"没人声明 host viewport"分支），请求侧只有 `AppRenderServices::setViewportRect` 一条路径（init 也走它）；`HostViewState.h` 列出写者表。

下一批次（各自独立可验收，按此顺序）：

- [x] 公开 `Renderer` 合并（4.0.3 checkpoint 2/3）：`record` / `recordViewFamilies` / `prepareFrameRecord` 移入 `RenderDeviceState`，`friend` 与 5 处 `_device->_` 私有写入删除，`RenderFrameCoordinator.{h,cpp}` 整文件删除；App 侧 `AppRenderState::coordinator` 与 `AppRenderServices::getFrameCoordinator()` 删除。顺带修掉 unity build 掩盖的 `RenderStageContext` include 缺口。**未做**：`RenderSubmission` 拆 `FrameRecording` / `FrameFlightResources`、4.0.3 checkpoint 5（owner-scoped `SceneViewKey`）。

## V 系列（隐式驱动收口，见 plan 附.1–附.4）

按风险从低到高；V1–V3 一个 commit，V4+V5 一批，其余独立。

- [x] V1 主 view 身份单一来源。权威 = `SceneViewDesc::ownsHostViewport()`（结构），`kPrimarySceneViewId` 退回"预览 compose 的目标槽位"。删 `declareViews` 的 id 搜索、`buildGameRenderFrame` 的 `viewFrames.front()`、`displayRootTask()` 的 `.front()` 回退、`sceneViewOwnsHostViewport(nullptr)` 视为 owns、Forward/Deferred 的 `recordings.empty()` 合成与 `bDisplayRoot || result.views.empty()`（后两条是 plan 附.1 漏登记的同类写法，已回填）。`primaryTask()` 与 `displayRootTask()` 两个名字合并。
- [x] V2 输出发布显式化。新增 `publishViewOutputIdentity(flightIndex, displayViewId)` 作 `_publishedOutputViewId` 唯一写入点（`0` 清空）；`publishFamilyResult` 只发 outputs；删 `beginFrameCommandBuffer` 的陈旧身份补偿与 `record()` 的 set/clear 两分支；顺带修掉"plan 非空但无 display root / viewId==0 时不清空、沿用上一帧身份"的 latent bug。
- [x] V3 删兜底链。`getActiveViewportImageShared` / `getViewportDisplayImageShared` 只认 published；`getViewportExtent()` 两段（published → `{}`）；`getViewOutput(viewId)` 只看本 flight；删死代码 `pipelineViewportDisplayImage()` 与随之无调用方的 `pipelineViewportColorImage()`；三个新用例钉住 "未发布返回 nullptr/{}" 与 "配对 slot ≠ 主 view"。
- [x] V4 删 `IRenderRuntimeServices`：时间改由 `RenderFrameData.timeSeconds`/`frameIndex` 携带；scene bindings 由 `RenderDeviceState::resolveViewSceneResources` 在录制前解析进 `RenderFrameData.sceneResources`；`DebugRenderSystem` 改构造注入到 Deferred/PipelineCoordinator InitDesc；接口文件、5 个 pass 的 `_runtimeServices`、死方法 `getGameplayResourceBinding()`（连同只转发它的 `App::getGameplayResourceBinding`）全删；`EnvironmentLightingSceneResources` 拆到独立头避免结果类型拉进整个 processor。
- [x] V5 `CameraFrameInput` → `FramePacket`：`cameraForViewRecording()` 与整条 per-view patching 删除；`RenderPipelineFrameContext.camera` → `const FramePacket* frame`，view 级数据一律读 `frame.view`；`recordCameraViewCompose` 改吃 `(uiFrameSnapshot, logicalViewportExtent)`；`RenderFramePlan.camera` → `.frame`。未做（留 P3 收尾）：`RenderFrameData`→`PreparedViewRenderData` 改名，以及把 `RenderViewRecordingContext` 并进 `RenderPipelineFrameContext`——两层此刻字段都有真实消费者，且 `derivedScene` 本来就是从该 View 的 task 读的，不是 patch。
- [x] V6 `RenderFramePlan` 去回调：新增 `IFrameRecordExtensions`（4 个具名阶段），plan 上只剩一个接口指针 → `std::function` 归零；`DisplayComposeInput` 与 `PresentationGraphService::Extensions` 两份平行描述删除；`App` 实现接口，host 侧不再装配闭包；`record()` 头部写明完整顺序。**未采用**贡献者 span（当前只有一个 host）与「缺 step 时 assert」（headless / UI-only 帧合法地什么都不录）。顺序无单测：`record()` 需要活 device，旧回调版本同样没有，已在 progress 记录。
- [x] V7 离屏 pump 显式化：提成 `GameRuntimeTickOrchestrator::pumpOffscreenTasks`，`tickRender` 与头文件都写明它是录制前的前置阶段（上一次提交在这里 fence+finalize，本 tick 排队的下一个 pump 才可读）。顺带删除零消费者的 `RenderDeviceState::isOffscreenPending()`。
- [x] V8 viewport debug catalog 移出 device：652 行整体搬到 `Render3D/Debug/ViewportDebugCatalogBuilder.{h,cpp}`，并改成**已解析句柄的纯函数**（`ViewportDebugCatalogInput`，含铺平的 36 个 point-shadow face，无回调）；`mutable` 缓存移成 `ViewportDebugCatalogCache`；device 只留唯一解析点 `makeViewportDebugCatalogInput()`。**偏离**：plan 原文的验收「rg ViewportDebug 在 RenderDeviceState* 为 0」不成立且不该成立（device 必须给出句柄来源），达标的是公开面不再有四个 catalog 方法、实现不在 device 文件里；「顺带断 Render3D→GUI/Compose」经查证属 compose prep 而非 debug，记在 `source-layout-subtraction` S2。

## R0

- [x] 画出 GameRuntimeTickOrchestrator、RenderFrameExtractor、RenderRuntime、Forward/Deferred、ViewCompose、DisplayCompose、Present 的真实调用图。
- [x] 核对 RenderGraph build/execute 的 snapshot 时序和 live-state 访问。
- [ ] 登记并修复影响 R0 的 GUI widget test include/target 配置。
- [ ] 添加单 Camera golden：矩阵、离屏 extent、output format、surface imageIndex。
- [ ] 添加 resize、surface recreate、zero-extent、关闭单窗场景。
- [x] 记录 Forward 与 Deferred 当前 pass 顺序，不改策略。

## R1

- [x] 设计 SceneFrameSnapshot 字段分类和所有权方向，确认 resource generation/lifetime 风险。
- [x] 设计 RenderViewInput 的现有来源与兼容迁移边界；RenderViewFamily/Output 留待真实多 View 切片。
- [x] 引入 SceneFrameSnapshot 的实际存储边界，RenderFrameData 仅作为现有 per-view pipeline packet。
- [x] 将 shadow/cascade 字段从 SceneFrameSnapshot 移到独立 per-view preparation。
- [x] 将 RenderFrameExtractor 拆为 Scene extraction 与 View preparation 两个显式阶段。
- [x] 通过显式 TerrainProcessor 输入移除 extractor 对全局 App 的依赖。
- [x] 将 directional shadow/cascade preparation 从共享 Scene snapshot 的可变字段迁移到 per-view preparation；Scene snapshot 只保存 view-independent light source data。
- [x] 定义 SceneId、SceneRenderRequest、SceneViewportTask、SceneRenderScheduler 的最小契约。
- [x] 实现 frame-local submit/seal/clear 调度和按 SceneId 去重 snapshot builder。
- [x] 将 snapshot table 提升为 SceneRenderPlan 所有者，以 snapshotIndex 供多个 viewport task 复用。
- [x] 用 sceneRevision 防止同一 Scene 内容变化后错误复用旧 snapshot。
- [x] 在 snapshotFor() 校验 snapshotIndex 对应的 SceneId/revision，拒绝错误 task 索引。
- [x] 将 SceneRenderScheduler 的 snapshot builder 接到真实 Scene extractor，并让 plan snapshot 直接进入当前单 View preparation。
- [x] 扩展 SceneRenderScheduler request collection 到 GameEditor/preview 的多个 View。
- [ ] 在 UI GPU compose 之前聚合 SceneRenderRequest，并禁止 UI paint/compose 期间临时抽取 Scene/ECS；不强制 UI widget tick 的相对顺序。
- [x] 迁移 shadow/entity-id/debug/Forward/Deferred 消费者到显式 Scene snapshot / per-view 字段。
- [x] 迁移 RenderFrameData 消费者到显式 `sceneSnapshot` + per-view 数据，并删除 SceneFrameSnapshot 继承关系。

## R2

- [x] 扩展 RenderRuntime::FrameInput 为 SceneRenderPlanInput，并在录制前校验 plan/task/snapshot 归属。
- [x] 为 FrameUploadArena 增加 frame token 防护，拒绝同一 submission 内重复 rewind flight backing。
- [x] 将 FrameUploadArena 同一 token 的 begin 改为幂等追加语义，避免多 View 准备时重置 cursor；descriptor binding 隔离仍留在后续切片。
- [x] 原子迁移 RenderFrameData：Scene snapshot 改为 shared_ptr<const SceneFrameSnapshot>，并使用 View-owned draw bucket 副本承载 camera-dependent sort；禁止旧 sortDrawItems() 修改共享 snapshot。
- [x] 将 View-owned draw bucket 副本替换为 index/order ranges，消除 RenderDrawItem 的重复拷贝；DrawCandidateView 提供 indexed read-only range。
- [x] 引入 RenderSubmissionContext / RenderViewRecordingContext；ForwardFrameResourceSet 提供 beginSubmission/beginView，同一 submission 的每个 View 拥有独立 frame descriptor set 与 upload slice。
- [x] 抽取 ViewDescriptorSetAllocator 与 beginFrameResourceSubmission/writeUploadSlice；Forward 与 Deferred 共用 pool 增长和 submission 开头，不合并 payload struct。
- [x] 将 Deferred 的 per-flight descriptor binding 改为 View-owned beginSubmission/beginView；SSAO/skybox 写入同一 View slot。
- [ ] 将 DrawPacket grouping/packet ranges 的生命周期纳入 submission/View context，确认录制期不缓存失效的候选 span。
- [x] 移除 Forward/Deferred pipeline 中跨 View 共享的 `_lastTickCtx` / `_lastFrameInput`，graph build 改用调用栈内 View-local post context。
- [ ] 明确并实现同 Scene 复用表：snapshot/candidates/skinning/light sources 共享；visibility/sort/shadow/targets 按 View 生成。
- [ ] 让同一逻辑帧的多个 surface/window 共用一个 SceneRenderScheduler/SceneRenderPlan，避免按窗口重复抽取同一 Scene。
- [x] 将 RenderRuntime 持久状态保活到 submit/fence。
- [x] 将 Shadow 的 per-flight descriptor binding 改为 View-owned beginSubmission/beginView。
- [x] 增加同一 submission 多 View 的 slot/slice identity 验证（RenderViewBindingTable + Forward/Deferred/Shadow writeViewPayloads）。Stage CIS 隔离见 4.0.2 C；family result publish 隔离见 4.0.2 D。
- [x] 为每个 View 建立独立 output/extent/format 句柄。
- [x] 将 Forward/Deferred viewport persistent key / RT 改为 View-keyed，避免多 View 共用一份 GBuffer/color。
- [x] 将 postprocess/bloom 输出与 CombinedImageSampler descriptor set 改为 View-owned，避免同一 cmdbuf 里两路 display 采样同一套 GPU 资源。Processor 上的 viewId map 已在 4.0.2 C 删除，descriptor 进入 typed `PostprocessPassBindings`。
- [x] 录制同一 Scene 的两个 View，共享一个 SceneFrameSnapshot。
- [x] 4.0.2 A：建立 `RenderSubmission` / pool owner，统一 command buffer、upload、transient descriptor、keepalive 与 finish 协议；删除 pipeline/resource-set 的分散 beginSubmission。
- [x] 4.0.2 B：引入 `SceneViewFamilyPlan` 与 `SceneFamilyResources`；skinning/scene packet 不再按 flight 全局共享；同 Scene 双 View复用 family、双 Scene隔离。
- [x] 4.0.2 C 入口：typed View/Pass resources；Bloom/BasicPost viewId map 与 Stage singleton CIS 删除；PointShadow packet 归 View Binding。
- [x] Surface presentation blit 持有自己的 tone-map CIS；不再把 display compose 当成 View-owned set 的缺省调用方。
- [x] 4.0.2 C 收口 6a：删 `LightStage::FrameInputs` / `_frameInputs` / `setFrameInputs()`（死状态：唯一写入方无调用方，真正的 light pass 由 frame-graph pass 显式传 View-owned descriptor set）。
- [x] 4.0.2 C 收口 6b：`ShadowPreparedView`（`viewSlot` + `pointLightCount`）取代 `BasicShadowMapTechnique::_preparedViewSlot` / `_lastPreparedPointLightCount`；`IShadowTechnique::prepare()` 返回该 token，经 `ShadowStage::prepareView` → pipeline 的 per-view 分支 → 两个 orchestrator 的 `BuildInputs.shadowPrepared` → `ShadowStage::appendGraphPasses(graph, ctx, prepared, dep)` 传回。附带删掉 append 里对缓存计数的二次 clamp 与无人使用的 `getLastPreparedPointLightCount()`。行为修正：prepare 被拒（无 frameData / 未在录制）的 View 现在不会 append 上一个 View 留下的 shadow pass。
- [x] 4.0.2 D 入口：`recordFamily` 一个 family graph；publish 不走 pipeline getter；删除 `tick`/`beginTick`。
- [ ] 4.0.2 D 收口：真正的 ViewFamily compiler，不再是 per-view `beginView` 循环外包装 + `familyPredecessor` 串行。
- [x] 4.0.2 E 入口：删除 `RenderRuntime` 类，拆成 `RenderDeviceState` + `RenderFrameCoordinator`；删除 ViewportState 与 active Scene locator。
- [ ] 4.0.3：合并 DeviceState+Coordinator 为公开 `Renderer`；关闭 friend 越界；`recordFrame(plan, surfaceTarget) -> RecordedFrame`。
- [x] 引入 `RecordedFrame`（command buffer + flight/token 身份 + `valid()`），`RenderFrameCoordinator::record()` 返回它，host 只提交该值；seal 失败返回无效值（host 空提交），取代「提交一个未封口的录制」。
- [ ] 将 `RenderSubmission` 拆成 `FrameRecording` 与 `FrameFlightResources`；host 提交真正的 command buffer。
- [ ] 引入 `PreparedView`，删除 CameraFrameInput patching / SceneViewRecording / RenderPipelineFrameContext 重复层。
- [x] 压缩 `tickRender` 为 `declareViews → extractScenes → prepareViews → buildGameRenderFrame → acquire → recordFrame → submitRecordedFrame`（步骤均为该类的私有静态函数，不引入新的全能 coordinator）；顺序与实现一致，`prepareModules` 仍在最前（计划原文的排后顺序属未验证的行为变更）。
- [x] family-scoped derived Scene：`SceneViewRecording::derivedScene` 按 family 绑定；删除 `RenderFramePlan::derivedScene`。
- [x] host 提交 live Scene 列表：`HostSceneViewSubmit` / `submitHostSceneViews`；两 live Scene 抽出隔离 snapshot 与两个 family。默认产品帧仍提交当前 viewport Scene。
- [ ] 产品帧同时显示两个 Scene viewport（排在 4.0.3 之后；不要发明 PIE authoring PiP）。
- [ ] 验证一个 View 到多个 Surface、多个 View 到一个 Surface。
- [ ] 验证 surface acquire/present/recreate 不进入 View pipeline。
- [ ] 只有在 trace 证明必要时再提出 submit 拆分。
- [ ] 判定 flight 深度：Vulkan surface 的 `flightFrameSize = 1` 使 `getCurrentFrameIndex()` 恒为 0（实测 90 tick 全部 `flight=0`），所以渲染侧双槽表（`RenderSubmissionPool` / `RenderViewOutputTable` / `viewFrameDataPerFlight` / `FrameUploadArena`）生产里只走槽位 0，而 `begin()` 的 `waitAllGraphicsFences()` 是每帧等齐 GPU 的 wait-idle 策略。要保持 1 就把余量当余量（不必为 overlap 记账）；要真 overlap 需同时提高 `flightFrameSize`、只等 `frameFences[currentFrameIdx]`、并验证槽位 1 真的被轮转到（两后端一起验）。属 R2/R4 性能决策，见 temporal_semantics.md M4。

### R2 reuse gates

- [ ] GameRuntime 不再提交固定 sceneRevision = 0；改为真实 Scene content generation。
- [ ] 禁止 prepareView() 对共享 Scene snapshot 做按值复制或原地排序。
- [x] 为相同 Scene 的双 View 增加 snapshot pointer/index identity 验证。
- [ ] 为不同 camera 的双 View 增加 draw-order / shadow-preparation 非共享验证。

## 设计评估门禁

- [x] 对照 UE Scene/FSceneRenderer/ViewFamily、Unity Camera/ScriptableRenderContext、Godot Viewport/SubViewport、ImGui draw data。
- [x] 明确 Scene owner 不直接依赖 RHI；Scheduler 属于 Render3D orchestration，不属于 GUI Framework。
- [x] 明确 UI 之前只指 UI GPU compose 之前。
- [x] 明确 Stage 是 device 配方、View GPU 数据进 Binding / RDG persistent；禁止再叠 viewId map。`_frameInputs` 随 4.0.2 C 收口 6a 删除，`_preparedViewSlot` 随 6b 换成显式 token；Stage 上不再有隐式 current View。
- [x] 补齐 SceneFamily 轴并确认 submission 级 skinning 不能支持双 Scene。Device/Coordinator/Surface 正交尚未闭环。
- [x] 将 ViewFamily 定义为 graph 编译单位。`recordFamily` 入口已在；真正 compiler 仍待 4.0.3。
- [x] 明确 RHI begin/end 保留在 recording/graph executor；应删除的是 persistent pipeline 的隐式 current begin/tick。`tick` 已删，Stage current-view 未清。
- [x] 记录 View 声明 / 收集 / 抽取 / 录制边界（plan §3.10、temporal_semantics M9）：调度器帧内聚合正确，缺的是 producer 声明；五个全局格子（`bWorldSceneRenderEnabled` / `extensionHostView` / `cameraPreview*` / `viewportRect` / `bShowEditorGizmos`）、declare 路径上的 live-ECS 查询、六层重复声明、三处空转机制已登记。
- [x] 4.0.3 4a：抽取移出 `SceneRenderScheduler::seal()` → `buildSceneSnapshots(plan, resolver)`；`SceneRenderRequest` 不再携带任何闭包，无法解析内容的 Scene 由该步骤剔除并重新分组。
- [x] 4.0.3 4b：声明 / task / 快照表项保留 tick-local `Scene*`，`sceneId` 由 `seal()` 从句柄派生；删除 `derivedSceneForHostView`、`SceneRenderPlanInput::complete()`、`derivedScenesAgreeWithPlan()`、`derivedSceneForFamily` 与 `SceneSnapshotResolver`，改为 `ExtractedSceneRender` 的构造期不变量。
- [x] 4.0.3 4c：`HostSceneViewSubmit` 与 `SceneRenderRequest` 合并为一份 `SceneViewDesc`（`SceneViewportTask` 内嵌它，`submitHostSceneViews` 转发删除，`renderFlags` 与派生 `sceneId` 删除，键改用句柄；宿主文件改名 `HostSceneExtract.*`，只剩抽取）。
- [x] 4.0.3 4d-1：`ISceneViewProducer` / `SceneViewCollector` / `SceneViewCollectContext` 接缝 + `RuntimeGameViewProducer` / `EditorAuthoringViewProducer` 各自声明世界视口；删除 `bWorldSceneRenderEnabled` 与 `extensionHostView` 两个格子；`SkeletonAnimationSystem` 策略改读 `renderedScenesLastTick`；`getPrimaryCamera` 移入 `Utility/SceneCameraQuery`。
- [x] 4.0.3 4d-2：`EditorViewProducer` 声明编辑器两个 view（作者视口 + 选中相机的预览 inset）；删 `bCameraPreviewHostOwned` / `cameraPreviewEntityUUID` / 宿主铸造的 `kHostOverlayPreviewViewId`；`resolvePreviewCamera` / `cameraProjectionForOutput` 移入编辑器，FOV 线框移到编辑器 world overlay pass；`SceneViewDesc` 补 `features` 与 `viewOwner`。
- [x] 4.0.3 4d-3a：删 `bShowEditorGizmos` 格子（开关归 `EditorLayer`，声明方读它而不是 App）；automation 的 `set_editor_gizmos_visible` 经 `IEditorAutomationControl` 打到编辑器；游戏视口不再受编辑器开关影响。
- [x] 4.0.3 4d-3b：作者视口 rect 由声明方给出（编辑器不再写 `setViewportRect`，pending-resize 同步与 `onViewportResized` 删除）；device extent 跟随主 view 声明；automation 的 resize 只改宿主视图几何。
- [x] 4.0.3 4d-3 收口：一条 View 声明必须描述整像素——`Rect2D` 成员默认初始化（关闭未初始化几何这一类坑）；`EditorLayer::describesPixels()` 要求有限且至少一像素，未布局/折叠面板回落默认尺寸；`SceneRenderScheduler::submit()` 拒绝非有限或截断为 0×0 的声明。修复 4d-3b 引入的编辑器首帧 exit 255 崩溃。
- [ ] 4.0.3 checkpoint 5：view 身份改 owner-scoped `SceneViewKey`，并按此建立 `ViewHistoryStore` 稳定键（排在 4d 之后）。

## R3

- [ ] UI Editor 无 world render 路径。
- [ ] GameUIHost[ViewId] 的 tree/input/focus/snapshot 生命周期。
- [ ] 同一 View 多 Surface 复用 snapshot。
- [ ] 不同 View 不共享 live WidgetTree。
- [ ] 验证 UIFrameSnapshot 在 graph build 前生成，录制期不读 live tree。

## R4

- [ ] 汇总 CPU/GPU/frame trace 与资源峰值。
- [ ] 决定是否引入 culling/sort/cache。
- [ ] 决定是否拆 world/view 与 display/present submit。
- [ ] 评估第三种 pipeline 的接入契约，不新增强制 BaseRenderPipeline。
- [ ] 删除 `IRenderRuntimeServices`：时间改走 `HostClockState` 输入；env lighting 句柄在 graph build 前解析进 `PreparedView` 或并入 `EnvironmentLightingResultProvider`；`DebugRenderSystem` 由 overlay pass 构造注入；删除死方法 `getGameplayResourceBinding()`；随后删除 `PipelineCoordinator::InitDesc::runtimeServices`。随 P3 / 4.0.3 checkpoint 4 一起做。

## 时间语义迁移

清单见 `temporal_semantics.md`。每批只做一批，避免半改名状态；外部 automation config 键与脚本/UI 可见名不改。

- [x] P1a：M1 host tick 主体——`GameRuntimeFrameOrchestrator`→`GameRuntimeTickOrchestrator`（含文件与转发头）、`RenderRuntimeClockState`→`HostClockState`（字段 `hostTick`）、`App::_frameIndex`/`getFrameIndex`/`currentFrameIndex`→`_hostTick`/`getHostTick`/`currentHostTick`、`IRenderRuntimeServices` 与 `RenderDeviceState` 的 `getHostTick`、`setHostTickProvider`/`_getHostTick`、`lastUsedTick`/`currentHostTick`、`SceneRenderScheduler.beginTick`/`clearTick`/`isTickOpen`/`hostTick`、`SceneRenderPlan::hostTick`、automation 的 `markTickCompleted`/`completedTickCount`/`exitAfterTick`/`AppAutomationTickContext`/`recordedTick`/`earliestTick`/`screenshotTick`。构建与专项测试全通过。
- [x] P1b-1：`AppRenderFrameState`→`HostViewState`（文件/转发头、`AppRenderState` 字段、`AppRenderServices` 访问器、`EditorViewportCompositor`/`EditorSurfaceContext` 参数）。
- [x] P1b-2a：perf 命名面——sample `Frame/*`→`Tick/*`、`renderFrame()`→`hostTick()`（key `Render/Frame`→`Tick/Total`）、`YA_PERF_FRAME_SCOPE`→`YA_PERF_TICK_SCOPE`、`PerfFrameScopeTimerConditional`→`PerfTickScopeTimerConditional`、profile 产物 `frameCycle`/`frameCpuMs`/`frameGpuMs`→`tickCycle`/`tickCpuMs`/`tickGpuMs`。
- [x] P1b-2b：tick 排期字段——`_nextResolveAuditFrame`/`_nextMaterialAuditFrame`→`*AuditTick`、`MATERIAL_AUDIT_INTERVAL_FRAMES`/`DERIVED_RESOURCE_GC_DELAY_FRAMES`→`*_TICKS`、`TerrainDerivedResource::lastUsedFrame`→`lastUsedTick`、`TerrainComponent::get/setRebuildNotBeforeFrame`/`_rebuildNotBeforeFrame`→`*Tick`。
- [ ] P1b-2c：`DebugPrimitives::updateFrameUBO`/`_frameData`（按 `flightIndex` 索引，随 P2 flight 轴）。
- [x] P1c：M2 `SceneFrameSnapshot`→`SceneSnapshot`（定义、转发声明、`SceneRenderScheduler`/`SceneFamilyResources`/`RenderSubmission`/extractor 及测试）。
- [x] P1d：M1 同轴遗留——automation 与 TaskManager 的 per-tick 命名（`isTickAutomationEnabled`、`hasTickAutomationConfig`、`shouldRequestQuitAfterTick`、`ExitAfterTick`、`isAutomationStableTickReady`、`bStableTickReady`、`registerTickTask`/`hasTickTasks`、`tickContext` 参数）。
- [ ] P2：M4 recording / flight（`RenderSubmission`→`FrameRecording`、`frameToken`→`recordingSerial`、`flightIndex`→`flightSlot`、`FrameUploadArena`→`UploadArena`、`PerFlightFrameResourceSetBase`→`SkinningLayoutProvider`）+ M5 present（`PresentFrameInput`、`FPresentFrame`）+ 公开 `Renderer` 合并（4.0.3 checkpoint 2 / 3）。
- [ ] P3：M3 C++ 部分（`RenderFrameData`→`PreparedViewRenderData`、删除 `CameraFrameInput` / `RenderPipelineFrameContext` / `RenderViewRecordingContext` / `SceneViewRecording` 四层转译、`RenderStageContext`→`PassRecordContext`、`FrameContext`→`ViewPassContext`、`RenderFrameExtractor` 拆为 SceneSnapshotBuilder + ViewPreparer）+ 4.0.3 checkpoint 4。
- [ ] P4：M3 Slang 部分（删除无消费方的 `frameIdx`；`FrameData`/`FrameUBO`→`ViewUbo`/`ViewData` 与 C++ alias 同批），需 `xmake ya-shader` 重新生成头。
- [ ] P5：M6 pipeline 文件与类改名（`*FrameGraphOrchestrator`、`*FrameGraphPasses`、`*FrameGraphResources`、`*FrameResourceSet`、`ShadowFrameResources`、`RenderDeviceState.Frame.cpp`）。
- [ ] P6：M7 GUI 命名（`UIFrameSnapshot`→`UISnapshot`、`UIFrameBuildContext`、`UIFrameComposeReplay`、`GuiFrameInspectorOverlay`、dump/digest 函数）。
