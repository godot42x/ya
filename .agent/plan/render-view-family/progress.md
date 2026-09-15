# Progress

## 当前状态

- 阶段：R0 基线审计已完成；R1 已完成 SceneRenderRequest/SceneRenderPlan 的最小 frame-local 调度切片、真实 extractor 的显式 Scene/View 分层，以及 GameRuntime 单 View 的 scheduler 接入；尚未让 RenderRuntime 接管多 View 录制。
- 已确认：RenderFrameInputs.h 已有四组输入；RenderFrameData 与 RenderFrameExtractor 仍混合 Scene 级和 View 级数据；RenderRuntime 仍按单 View、单 active Scene 记录。
- 已确认前置：多 OS window 的 surface/present 改造属于 gui-multi-os-window-editor，不在本计划重复实现；本计划也不引入 WorldInstance/WorldRegistry。
- 本轮移除了 RenderRuntimeSnapshotTest 中依赖读取源码文本和 `find()` 的架构时序回归；保留运行时可执行的输入契约、scheduler 去重、revision 和 snapshot 索引校验。未修改运行时实现。

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
| R1 World/View 分离 | 进行中（契约小步完成） | RenderFrameData 仍兼容单 View；旧消费者未迁移 | 场景抽取、View sorting、shadow preparation 拆分 |
| R2 ViewFamily | 未开始 | Forward/Deferred topology、当前 submit 约束 | 多 View record、双 Surface 验收 |
| R3 GUI2D/GameUI | 未开始 | WidgetTree live source、UIFrameSnapshot 输入 | UI-only 与 GameUI[ViewId] |
| R4 性能收口 | 未开始 | 优化由 profile 触发 | cache、submit、第三 pipeline 决策 |

## 下一轮接力点

R0 已完成。SceneFrameSnapshot / RenderViewInput 的字段和所有权已明确，后续继续迁移 RenderFrameData 与 RenderRuntime::FrameInput。

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

当前边界：`RenderFrameData` 仍继承 `SceneFrameSnapshot` 以满足现有 pipeline 消费，但 `prepareView()` 先复制不可变 Scene snapshot，再只在 per-view packet 中写入 shadow/cascade 和 sortKey；共享 snapshot 不被 View 原地修改。

R1 第一小步已完成：SceneFrameSnapshot 显式承载当前可识别的 Scene lights、draw buckets 和 skinning palettes；RenderFrameData 继承它并继续保留 camera、viewport、frame metadata。该步没有改变 Forward/Deferred 消费者，只把 extractor 的 Scene 与 View 阶段显式化。

关键新增约束：UI GPU compose 前必须存在一个明确的 SceneRenderScheduler 边界。它收集 SceneRenderRequest，输出 immutable SceneRenderPlan；UI compose 只能消费 plan 产生的 viewport outputs，不应在 UI 过程中临时触发 Scene/ECS extraction。UI widget tick/buildSnapshot 的先后由 host/product 依据输入依赖决定，不被 Scheduler 强制锁死。

## 设计评估结论

- 设计方向合理：Scene owner 提交 offscreen request，调度器在一帧内按 Scene 去重并按 viewport 展开任务，RenderRuntime 只录制 immutable plan。
- 与 UE 对齐点：Scene/FScene 与 ViewFamily/FSceneRenderer 分离；同一 Scene 的多个 View 共享一次 scene extraction。
- 与 Unity 对齐点：Camera/request 进入 pipeline，ScriptableRenderContext 类似的 plan/record 边界隔离 Scene 领域对象。
- 与 Godot 对齐点：Viewport 是离屏输出和显示绑定点，SubViewport/preview 可对应多个 Scene request。
- 与 ImGui 对齐点：后端只消费 immutable UIFrameSnapshot/SceneRenderPlan，不读取 live WidgetTree/Scene。
- 必须坚持的修正：Scene 不直接依赖 RHI/RenderRuntime；SceneRenderScheduler 不属于 GUI Framework；UI 之前指 GPU compose 之前，不是强制 UI logic/snapshot 晚于 request collection。
- SceneFrameSnapshot 是当前唯一场景快照语义，不得重新引入 WorldFrameSnapshot、RenderFrameExtractor::extract() 或全局 world 抽象。
