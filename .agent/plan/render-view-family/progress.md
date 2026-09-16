# Progress

## 当前状态

- 阶段：R0 基线审计已完成；R1 已完成 SceneRenderRequest/SceneRenderPlan 的最小 frame-local 调度切片、真实 extractor 的显式 Scene/View 分层，以及 GameRuntime 单 View 的 scheduler 接入；尚未让 RenderRuntime 接管多 View 录制。
- 已确认：RenderFrameInputs.h 已有四组输入；RenderRuntime 仍按单 View、单 active Scene 记录，但 RenderFrameData 的 Scene snapshot owner 已与 View-owned draw buckets 分离。
- 已确认前置：多 OS window 的 surface/present 改造属于 gui-multi-os-window-editor，不在本计划重复实现；本计划也不引入 WorldInstance/WorldRegistry。
- 当前 checkpoint：Forward FrameResourceSet 已按 submission/View 拆开 frame descriptor 与 upload slice；RenderRuntime 仍只录制一个 View，Deferred/Shadow 仍覆写 per-flight Binding。
- 本轮完成 RenderFrameData ownership 收口：RenderFrameData 不再继承 SceneFrameSnapshot，而是持有 shared snapshot 并独立保存 View-owned draw buckets；Forward/Deferred/Shadow/Debug/EntityId 消费者通过显式路径读取 View buckets、shared skinning palettes 和 light presence。
- R2 第一切片：RenderRuntime::FrameInput 已显式携带 SceneRenderPlanInput；GameRuntime 将 sealed plan/task 传入，Runtime 在 command recording 前验证 snapshot 索引和 SceneId/revision 归属。当前仍只录制首个单 View，未引入多 View output 或额外 submit。
- GPU lifetime guard：FrameUploadArena 现在按 `flightIndex + frameToken` 识别一次 submission；同一 token 的第二次 begin 已改为幂等 no-op。Forward 的 frame/light/skybox descriptor 已改为 View-owned；skinning 仍是 submission 共享（同 Scene 多 View 正确）。
- 架构审计结论：RenderSubmissionContext / RenderViewRecordingContext 已进入 tick 输入，Forward beginView 不再按 flightIndex 覆写唯一 Binding。下一步不是再给 Forward 打补丁，而是把同一规则迁到 Deferred/Shadow，并让 Runtime 把 submission 保活到 fence。

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
| R2 ViewFamily | 进行中（Forward View binding） | Forward/Deferred topology、当前单 View submit、Deferred/Shadow per-flight binding | 多 View record、独立 output、双 Surface 验收、Runtime submission 保活 |
| R3 GUI2D/GameUI | 未开始 | WidgetTree live source、UIFrameSnapshot 输入 | UI-only 与 GameUI[ViewId] |
| R4 性能收口 | 未开始 | 优化由 profile 触发 | cache、submit、第三 pipeline 决策 |

## 下一轮接力点

R0 已完成。Forward 已具备 beginSubmission/beginView。下一 checkpoint 把同一 View-owned binding 规则迁到 Deferred/Shadow，或让 RenderRuntime 把 submission 保活到 fence；不要在尚未隔离的 pipeline 上开始双 View GPU 录制。

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
