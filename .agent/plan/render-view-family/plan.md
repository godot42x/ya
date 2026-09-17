# Render View Family 与 GUI/GameUI 渲染边界重构计划

> 建立日期：2026-09-12
> 状态：R2 架构未收口。功能已向 ViewFamily 靠近（typed View resources、`recordFamily` 入口、host live Scene 列表），但 `RenderDeviceState` + `RenderFrameCoordinator` 仍是一次不完整拆分，不是更清晰的唯一 orchestration。Checkpoint C/D/E 均为部分完成。下一刀是合并为公开 `Renderer` owner，不是产品双 viewport，也不是再向 Binding/Stage 加字段。

## 1. 主线选择

下一阶段选择 camera based render flow / SceneRenderScheduler / ViewFamily 作为主线。当前多窗口 RHI、GUI surface/present 和单 Camera 输入契约已经存在，但产品帧仍是一次 Surface acquire → 一次 world recording → 一次 present；高层编排分散在 `RenderDeviceState` 与 `RenderFrameCoordinator` 之间，读者要跨多层才能拼出主 loop。这里不引入 WorldInstance/WorldRegistry：Scene 仍属于 GameRuntime、GameEditor 或 preview 产品层；只有本帧需要显示的 Scene viewport 才向渲染调度器提交 offscreen render request。先收口公开 `Renderer` owner，再推进多相机 / 多 Surface；不要同时重写 Dock、动画、GameUI 和 N Camera。

推荐顺序：

1. R0：正确性基线与可观测性。
2. R1：SceneRenderRequest / SceneSnapshot / RenderViewInput 契约。
3. R2：SceneRenderScheduler 在 UI 之前聚合 viewport 离屏任务；公开 `Renderer` 按 ViewFamily 编译、录制并返回 `RecordedFrame`。
4. R3：Editor/preview/UI-only 三类产品路径接入。
5. R4：GameUI 按 View 复用、性能与扩展性收口。

GUI 动画属于 gui-invalidation-architecture 的独立小切片，可在 R0 完成后并行；GUI Dock/multi-OS-window 属于 gui-multi-os-window-editor，本计划只消费其 surface/present 合约。

## 2. 当前仓库事实

- Framework/Render/Render3D/Common/RenderFrameInputs.h 已有 CameraFrameInput、ViewComposeInput、DisplayComposeInput、PresentFrameInput。这些包与 SceneViewRecording / ViewFamilyRecordContext / RenderPipelineFrameContext 仍重复携带矩阵、extent、frameData、cmd 与 Scene*。
- 真实主 loop 只有一条：`AppKernel::run` → `GameRuntimeFrameOrchestrator::tickRender` → `RenderFrameCoordinator::record` → `pipeline.recordFamily` → compose → host submit/present。`RenderRuntime` 类已删除，但拆出的 `RenderDeviceState` 与 `RenderFrameCoordinator` 不是两个独立 owner：Coordinator 几乎无状态，经 friend 写 DeviceState 的 submissions / viewOutputs / presentation / published output。DeviceState 实际是 persistent renderer（cmd、submission pool、pipeline、presentation、environment、terrain、diagnostics），不是 RHI device（RHI 已是 `IRender`）。
- `RenderSubmission::finish()` 只置 `_finished`，不 queue submit。Host 拥有 world-enable、viewport rect 与 present。`activeSceneProvider` / `ViewportStateService` 已删除。本帧 Scene 经 `SceneViewRecording::derivedScene` 进入各 family，由 `HostSceneViewSubmit` 列表绑定。TAA 未使用，未引入空的 `ViewHistoryStore`。
- `beginFrameCommandBuffer()` 在没有 Surface/imageIndex 时拒绝录制，因此 Scene recording 仍与 present Surface 耦合；一个 View 供多个 Surface、headless/offscreen、先录全部 View 再分窗 present 均未实现。
- `recordFamily()` 已是 family graph 入口，但 Deferred/Forward 内部仍是 per-view `beginViewRecording` / `beginView` / `appendViewToGraph` 循环，并用 `familyPredecessor` 串行连接。LightStage::_frameInputs、BasicShadowMapTechnique::_preparedViewSlot 仍是隐式 current View。
- Forward 与 Deferred 保留各自的 FrameGraphOrchestrator 和 pass topology；不抽强制 BaseRenderPipeline。
- Applications/GameRuntime/Utility/RenderFrameExtractor.* 从 ECS 抽取 RenderFrameData。
- RenderFrameData 当前同时承载 camera、lights、draw buckets、skinning，混合了 Scene、View 和 frame-flight 语义；SceneSnapshot 现在是唯一场景快照名称。RenderFrameData 仍是 pipeline 消费的 per-view packet，后续继续拆出 View preparation 数据。
- GUI live 事实源是 WidgetTree，录制只消费 immutable UIFrameSnapshot。
- IRenderSurfaceContext、swapchain、acquire/present 已与 Camera 离屏目标分开；acquire/present 由 host/present coordinator 负责。

## 3. 目标对象模型

    Scene owners (GameRuntime / GameEditor / Preview)
      -> SceneRenderScheduler::submit(SceneRenderRequest)
           -> group by SceneId for this frame
           -> build SceneSnapshot once per Scene
           -> SceneViewportTask[viewport A, viewport B, ...]
                -> RenderViewInput + offscreen output
      -> host build RenderFramePlan (PreparedViewFamily[] + UI snapshot + compose)
      -> Renderer::recordFrame(plan, surfaceTarget) -> RecordedFrame
           -> Forward / Deferred recordFamily
      -> host submit / present RecordedFrame

Scene 是产品层的内容/编辑对象和 ECS 所有权边界，不是 GUI window，也不是 Renderer 的全局状态。Scene owner 只在本帧需要某个 viewport 时 submit request；未 submit 的 Scene 不会被渲染。

SceneRenderRequest 是本帧的窄请求，至少包含稳定 SceneId、viewport/view id、camera packet、offscreen target description、render flags、overlay/UI binding 和用于构建不可变 SceneSnapshot 的 source/callback。请求本身不创建 OS window、不 acquire/present，也不把 live Scene/ECS 查询留到 RenderGraph execute 阶段。

SceneRenderScheduler 是 UI 之前的 frame coordinator：收集请求、校验 Scene 生命周期、按 Scene 去重 Scene extraction、按 viewport 生成 camera-dependent preparation，并输出 immutable SceneRenderPlan。它不拥有 Scene，不替 Scene tick，不决定 Dock/tab 归属。

SceneSnapshot 是某个 Scene 在某一 frame 的不可变输出；同一帧可以有多个 Scene snapshot。snapshot 的所有 resource handles 必须保持到引用它的 viewport command submit 完成。不同 Scene 即使共享材质/mesh 资源，也不能共享实体、灯光、skinning 或排序结果。

ViewFamily 是同一 Scene、同一 frame、同一 pipeline/resource scheduling 语义下的一组 viewport View。一个 Scene 可以有多个 ViewFamily；不同 Scene 不能因为使用相同 pipeline 就合并 snapshot。UIOnly 不提交 SceneRenderRequest，直接走 WidgetTree/UIFrameSnapshot/Render2D。

### 3.1 与业界实现的对照与结论

| 参考实现 | 对应关系 | 采纳结论 | 不照搬的部分 |
| --- | --- | --- | --- |
| Unreal UWorld / FScene / FSceneRenderer / FSceneViewFamily | Scene 内容与渲染代理分离；一次 ViewFamily 可包含多个 View；编辑器和 preview 可以有不同 world/context | 采用 Scene owner 提供内容、renderer 按 ViewFamily 生成 frame plan 的方向；SceneRenderScheduler 对齐 FSceneRenderer 的 frame coordinator 角色 | 不把 Scene/ECS 或 editor world 生命周期下沉到 GUI Framework；不复制 UE 的全局 world context 层 |
| Unity Scene + Camera + ScriptableRenderContext / RenderPipeline | Camera/viewport 提交渲染请求，pipeline 按 camera/culling 组织；Scene 本身不直接录制 GPU 命令 | 采用 request/plan/record 三段式；Scene owner 只提交 request，Scheduler 负责去重 Scene extraction 和 per-view preparation | 不把每个 Camera 都当成独立完整 frame；同一 Scene 的多个 View 必须共享 snapshot |
| Godot SceneTree / World3D / Viewport / SubViewport | Viewport 绑定 world 和 render target；多个 SubViewport 可渲染不同内容，再由 UI/Canvas 合成 | 采纳 viewport 是显示/离屏请求边界、Scene 是内容边界的关系；Material preview、thumbnail、editor viewport 都是 request consumer | 不让每个 Viewport 隐式拥有一套 Renderer 或 device；OS window 仍由 GUI host 管理 |
| ImGui draw list + platform viewport | 没有 Scene renderer；每个 context/window 生成 draw data，后端只消费 draw data | 采纳后端只消费 immutable packet，不读 live tree/Scene 的原则；UI snapshot 与 SceneRenderPlan 同属 frame packet | 不用 ImGui 的 immediate draw list 代替 3D Scene extraction、culling 或 render graph |

评估结论：Scene owner 提交 offscreen request + 独立 SceneRenderScheduler + 公开 Renderer 录制 immutable plan，是合理且接近主流引擎的组合。需要修正两点：Scene 不应直接调用 RHI/Renderer，只由宿主调用 submit；“UI 之前”只约束 GPU compose 顺序，不强制 UI logic/snapshot 晚于 request collection。

Scheduler 必须是 frame-local coordinator，而不是常驻 Scene registry。beginFrame -> submit(request)* -> seal/build plan -> record(plan) -> clearFrame。SceneRenderRequest 是产品层到渲染层的窄协议；SceneRenderPlan 是 immutable 渲染输入，并拥有按 (SceneId, sceneRevision) 去重的 snapshot table；SceneViewportTask 只保存 snapshotIndex，读取时必须校验表项的 SceneId/revision。Renderer 不保存 request，不拥有 Scene，不调用 SceneManager。Scheduler 位于 Renderer 同层的 Render3D orchestration/service，GUI Framework 只消费 View output 和 UIFrameSnapshot。

每个 Scene 的 SceneSnapshot 只保存该 Scene 的 transforms、mesh/material 引用、lights、animation/skinning 结果、resource handles 和稳定 id。它不保存唯一 Camera 的矩阵、View 排序、culling 或 viewport rect，也不能隐含来自另一个 Scene 的资源/实体。

当前 `SceneSnapshot` 仍包含 directional light 的 shadow 字段存储位置，但真实语义已由 `prepareView()` 按 View 写入 per-view `RenderFrameData`；SceneRenderPlan 的共享 snapshot 不得被不同 View 原地修改。后续应把 shadow/cascade 字段从场景快照结构中彻底移除。

RenderViewInput 至少包含 ViewId、SceneId、owner 计算的 view/projection/viewProjection/camera position、离屏 extent/scale、culling mask、postprocess/debug/overlay flags 和该 Scene 的 SceneSnapshot 引用。UIOnly 不构造 RenderViewInput，也不伪造空 SceneId；它只消费 UIFrameSnapshot/Render2D 输入。View 输出是离屏 color/depth/辅助 attachment，不拥有 OS window、swapchain，也不 acquire/present。

对象语义必须分开：Camera/View 是一次 world 渲染；ViewportWidget 是显示 View 输出的 GUI 矩形；Surface 是 OS window 的 present 目标；Swapchain 是 Surface 的显示缓冲；ViewCompose 写 View RT；DisplayCompose 把一个或多个 View/preview/chrome image 排到 Surface；Present 提交 Surface。一个 View 可被多个 Surface 显示，一个 Surface 可显示多个 View。

### 3.2 同一 Scene 的 View / Frame 复用策略

同一帧、同一 SceneId + sceneRevision 的多个 View 必须共享同一个不可变
SceneSnapshot 身份，而不是把 snapshot 按值复制进每个 RenderFrameData。
早期 RenderFrameData::sceneSnapshot 是值成员，prepareView() 会复制后排序；该问题已迁移为 shared snapshot + View-owned order。稳定目标继续保持：

    SceneRenderPlan.snapshots[i]
      -> shared_ptr<const SceneSnapshot>
           -> ViewPreparation A: shared snapshot + A-only derived data
           -> ViewPreparation B: shared snapshot + B-only derived data

复用边界固定如下：

| 数据 | 同 Scene 多 View 策略 | 原因 |
| --- | --- | --- |
| transforms、mesh/material/entity 引用、原始灯光、skinning palette、资源句柄 | 直接共享 SceneSnapshot | 与相机无关，禁止 View 原地修改 |
| material/mesh 的稳定候选分桶 | 共享；必要时在 snapshot 构建阶段预计算 | 避免每个 View 重建相同候选集合 |
| frustum visibility、LOD、camera distance、透明排序、View draw order | 每个 View 生成 | 依赖 camera / viewport / render flags |
| directional cascade、point shadow view、shadow fitting | 每个 View 生成；结果只写入 View preparation | 阴影投影依赖 camera 和 shadow settings |
| shader light packet | 每个 View 生成 upload slice；源数据从 snapshot 读取 | GPU packet 含 View-specific shadow 矩阵 |
| Render target、GBuffer、postprocess、entity-id、overlay | 每个 View 独立 | attachment 和输出生命周期不同 |
| 同一个 View 在多个 Surface 显示 | 复用同一个 View output / UI snapshot | Surface 只是 display/present 投影 |

“共享 snapshot”与“View-owned draw order”必须作为一个原子迁移目标，不能
先把 RenderFrameData::sceneSnapshot 改成 shared_ptr、再让旧的
sortDrawItems() 继续修改 snapshot。当前 RenderFrameData 持有
shared_ptr<const SceneSnapshot>，同时持有 View-owned 的 index/order
ranges；pipeline 消费者通过 View order 访问 snapshot 中的候选项。
DrawCandidateView 同时支持 contiguous 与 indexed 两种只读访问，indexed
路径不物化 RenderDrawItem，也不提供伪装成连续内存的 data()。
SceneSnapshot 中的 sortKey 和可变 vector 顺序必须删除、冻结或彻底改成
候选数据语义。不得通过共享可变 vector、修改 snapshot 内的 sortKey 或复用
同一 View descriptor 来“节省拷贝”。

同一逻辑帧的多个 surface/window 必须在同一个 SceneRenderScheduler 中提交
并 seal，才能命中 SceneId + sceneRevision 去重。Scheduler 不能按每个 OS
window 或每次 `Renderer::recordFrame` 分别创建，否则同一 Scene 会被重复抽取。
只有独立帧时钟或明确的跨线程隔离场景，才允许使用不同 plan。

可选缓存只能在 profile 证明后加入：

- 相同 SceneId + sceneRevision + ViewPreparationKey 可复用完整 View preparation；
- 相同 frustum / render flags 可复用 visibility/order；
- 相同 shadow camera/settings 可复用 shadow preparation。

缓存 key 必须包含 scene revision、camera/view-projection、viewport extent、
render flags、shadow/settings revision 和 resource generation；不能只按 SceneId
缓存。当前 GameRuntime 提交的 sceneRevision = 0 仍是迁移期占位，接入多个
editor/preview View 前必须由 Scene owner 提供真实递增 revision，或提供等价的
frame-local content generation。

### 3.3 状态与逻辑如何拆：按生命周期和不变量，不按“有状态/无状态”机械切割

当前问题不是类里出现了 `begin/end`，而是同一个长寿命对象同时表示“渲染算法”“本帧当前值”和“刚录完的 View”。历史上 `RenderRuntime::renderFrame()` 既做安全点资源更新，又打开 command buffer、录 Scene View、做 UI/View/Display compose、发布调试输出并结束录制。Checkpoint E 把它拆成 `RenderDeviceState` + `RenderFrameCoordinator`，但 Coordinator 经 friend 写回 DeviceState，主 loop 仍是一次 acquire → 一次 recording → 一次 present。Deferred/Forward 虽有 `recordFamily`，内部仍用 per-view `beginView` 写局部 current 状态；第二个 View 仍可能覆盖隐式槽位（`_frameInputs`、`_preparedViewSlot`）。

状态和逻辑应在能维护不变量的 owner 内放在一起：

- `Renderer`：公开的持久 renderer（backend、pipeline selection、in-flight resources、ViewFamily recording、compose）。内部可有私有 recording/coordinator，产品层不需要认识 DeviceState/Coordinator 两个同级对象。`IRender` / `VulkanRender` 才是 RHI backend。
- `FrameRecording`：一次 command recording scope（cmd、allocateUpload、allocateDescriptor、retain、seal）。当前类名 `RenderSubmission` 暗示 GPU submit，但 `finish()` 并不 submit。
- `FrameFlightResources`：fence-safe arena、descriptors、keepalives。
- `SceneFamilyResources`：同一 Scene snapshot + family 内共享的 skinning upload、候选 packet、可共享 shadow work；由 submission 持有。不同 Scene 即使在同一 submission，也必须得到不同实例。
- `ViewResources`：该 View 的 UBO slices、descriptor sets、GBuffer、entity-id、postprocess/history 引用和 typed outputs；以值/handle 返回，不保存在 renderer 的 `_current*`。
- `ViewHistoryStore`：TAA/exposure 等跨帧历史，按稳定 ViewId + generation 持有；Bloom/SSAO 临时图不进入 history。

应拆成显式数据或纯构建逻辑的部分：

- `SceneSnapshot`、`PreparedView`、`SceneViewFamilyPlan`、`RenderViewOutput` 是 immutable/value packet。
- culling、draw ordering、cascade fitting 等 CPU 计算放进 `SceneViewPreparer`，输入 snapshot + view，输出 `PreparedView`，不访问 RHI。
- Deferred/Forward 的图构建放进 `DeferredViewFamilyRenderer` / `ForwardViewFamilyRenderer`；它们持有 device-lifetime pass recipes，但 `recordFamily(plan, submission)` 只通过返回值发布结果。
- 每个 pass recipe（GBuffer/SSAO/Lighting/ForwardOpaque/Postprocess 等）只持 PSO/layout/static resources，接口形如 `addPass(graph, inputs) -> outputs`。execute lambda 只捕获 pass data、RG handles 和稳定 GPU handles，不更新 recipe 自身。

目标生命周期不是三层，而是六个轴：

```text
Device            IRender / pass recipes / PSO-layout caches
  └─ Renderer     持久 renderer（pipelines、safe-point mutation、compose recipes）
      └─ FrameRecording / FrameFlightResources
          └─ SceneFamily  SceneFamilyResources + SceneViewFamilyPlan
              ├─ View     PreparedView + ViewResources + RenderViewOutput
              │   └─ Pass typed PassInputs / PassOutputs / PassParameters
              └─ View ...
Surface            DisplayComposePlan + Present（与 Scene/View 正交；当前仍耦合）
```

`FrameResourceSet::Binding` 不应继续膨胀成所有 pass 的总包。它应被替换为 `DeferredViewResources` / `ForwardViewResources` 聚合体，内部按 pass 使用 typed binding（如 `DeferredLightingBindings`、`EntityIdBindings`），而 descriptor/upload 的分配统一走 `FrameRecording`。Forward/Deferred 不共享大 Binding，只共享 allocator、upload slice、keepalive 等资源机制。

### 3.4 ViewFamily 才是 graph 编译单位

现计划“一 View 一 graph”会阻断同 Scene 多 View 的 shadow、scene packet 与潜在可见性复用；“整帧一个 mega graph”又会把不相关 Scene、Surface 和 pipeline 策略绑死。折中且更接近成熟 renderer 的单位是 `SceneViewFamilyPlan`：

```text
SceneRenderPlan
  ├─ SceneViewFamilyPlan(scene A, deferred, family settings)
  │    ├─ PreparedView A0
  │    └─ PreparedView A1
  └─ SceneViewFamilyPlan(scene B, forward, family settings)
       └─ PreparedView B0
```

每个 family 建一个 RenderGraph：先追加 family-shared work（例如符合复用条件的 shadow/scene uploads），再为每个 View 追加 GBuffer/SSAO/light/forward/postprocess 分支，最后返回 `RenderViewOutput[]`。不同 Scene、不同 pipeline、不同 shadow/history policy 默认是不同 family/graph，但可录进同一次 `FrameRecording`。

是否可共享不能只看 SceneId：family key 至少包含 scene snapshot generation、pipeline kind/config generation、shadow policy、view feature mask 和 multiview/stereo compatibility。Directional cascade 通常依赖 View；只有策略和矩阵一致时才共享。Point shadow、skinning 等也必须由明确 key 证明可共享，不能因为处于同一 flight 就共享。

### 3.5 `begin/end` 的保留与删除边界

`beginCommandBuffer/endCommandBuffer`、`beginRendering/endRendering`、debug label 等是 RHI 命令协议，保留在 `FrameRecording` / graph executor / pass recorder 中是合理的。应删除的是 persistent pipeline 上表达隐式 current state 的 `beginTick()`、`beginView()`、`getCurrent*()`：

- 顶层一次 `FrameRecording recording = renderer.beginRecording(...)`；seal 后不可再分配或录制。当前 `RenderSubmission::finish()` 只置标志，不 queue submit。
- 资源 API 使用 `allocateSceneFamily(...)`、`allocateView(...)` 并返回 owner/handle，不通过第二次 begin 改写内部 current slot。
- `recordFamily()` 返回 typed result；publish/compose 消费 result，不回头问 pipeline “当前输出是什么”。
- safe-point 资源重建由 `Renderer::applyPendingMutations(completedFlight)` 在录制前完成，不混进 View graph build。

### 3.6 业界架构可吸收点

- Unreal RDG 把 setup 与 execute 分开，pass parameter 显式声明资源依赖，execute lambda 只录命令；YA 应采用 typed pass data + graph result，避免 execute 修改 Stage。
- Unity RenderGraph 同样区分 recording 与 execution，并由 graph 管理临时资源生命周期；YA 的 attachment/postprocess 临时资源应留在 graph，descriptor/upload arena 仍由 submission owner 管理。
- Godot RenderingServer 用 opaque resource identity 分离 Scene 系统与渲染后端，并让 Viewport 绑定 Scenario；YA 可吸收 Scene/View request 与 GPU backend 解耦，但不采用全局 opaque singleton。
- Filament FrameGraph 把 pass 明确定义成资源读写计算，并以 setup lambda + execute lambda组织；YA 可吸收 typed PassData 和 family graph，不复制其具体 API。

明确拒绝：在 Stage/Processor 上继续添加 `viewId -> state` map；让 RenderGraph 拥有 device/submission allocator；抽万能 `BaseRenderPipeline`；用全局 current View；为了消除 `begin/end` 而把 command protocol 打散成无 owner 的自由函数。

### 3.7 目标调用流、类名与目录

最终主流程必须能按下面顺序直接阅读，CPU preparation 与 GPU recording 之间没有 service locator 或 current View：

```text
GameRuntimeFrameOrchestrator::tickRender
  -> buildGameRenderFrame (camera/preview/frustum/Scene request/UI snapshot)
  -> acquire PresentFrame
  -> Renderer::recordFrame(plan, surfaceTarget) -> RecordedFrame
       -> FrameRecording + FrameFlightResources
       -> ISceneViewFamilyRenderer::recordFamily(PreparedViewFamily)
            -> allocate SceneFamilyResources
            -> allocate ViewResources[]
            -> ViewFamilyGraphBuilder::build(...)
            -> RenderGraphExecutor::execute(...)
            -> ViewFamilyRenderResult
       -> record View/UI/Display compose from typed outputs
  -> host submit / present RecordedFrame
```

产品层只需要理解：`AppKernel`、`GameRuntimeFrameOrchestrator`、`Renderer`、`RenderFramePlan`、`RecordedFrame`、`PresentFrame`。Renderer 内部才出现 SceneViewPlanBuilder、PreparedViewFamily、FrameFlightResources、Forward/DeferredViewFamilyRenderer、RenderGraph。不要把 DeviceState、Coordinator、Submission、四种 Context 暴露为同级架构概念。

`RenderFramePlan` 目标形状：

```text
RenderFramePlan
  PreparedViewFamily[]
  SurfaceComposePlan[]

PreparedViewFamily
  SceneFamilyKey
  SceneSnapshot
  PreparedView[]     // task + prepared frame data + derived Scene

ViewRecordContext
  FrameRecording&
  PreparedView&
  ViewResources&
```

PreparedView 直接包含 View task、prepared frame data 和 derived Scene，不再需要 SceneViewRecording + CameraFrameInput + RenderPipelineFrameContext 三次转译。

建议的最终命名映射：

| 当前 | 目标 | 说明 |
| --- | --- | --- |
| `RenderRuntime` / `RenderDeviceState` + `RenderFrameCoordinator` | `Renderer` | 公开持久 renderer；Coordinator 可作私有实现，不经 friend 越界 |
| `RenderRuntime::FrameInput` | `RenderFramePlan` | sealed frame value：PreparedViewFamily[] + SurfaceComposePlan[] |
| `RenderSubmission` / `RenderSubmissionPool` | `FrameRecording` + `FrameFlightResources` | recording scope 与 fence-safe 资源；finish/seal 不是 queue submit |
| `SceneViewRecording` + `CameraFrameInput` + `RenderPipelineFrameContext` | `PreparedView` + `ViewRecordContext` | 删除重复转译层 |
| `RenderViewRecordingContext` | `ViewRecordContext` | FrameRecording + PreparedView + ViewResources |
| `DeferredRenderPipeline` | `DeferredViewFamilyRenderer` | 编译/录制一个 family，不是 per-view state machine 外包装 |
| `ForwardRenderPipeline` | `ForwardViewFamilyRenderer` | 同上，策略独立 |
| `DeferredFrameGraphOrchestrator` | `DeferredViewFamilyGraphBuilder` | 只 build graph，不发布 current 输出 |
| `ForwardFrameGraphOrchestrator` | `ForwardViewFamilyGraphBuilder` | 同上 |
| `DeferredFrameResourceSet` | `DeferredGpuResourceLibrary` | device-level layout/pool/static resources |
| `ForwardFrameResourceSet` | `ForwardGpuResourceLibrary` | 同上 |
| `GBufferStage` / `SSAOStage` / `LightStage` | `GBufferPass` / `SSAOPass` / `DeferredLightingPass` | pass recipe + typed inputs/outputs |
| `ViewportOverlayStage` | `ViewportOverlayPass` | 不保存 current frame inputs |

目标目录不需要为每个小类建一层；保持 4 个可读入口：

```text
Render3D/
  Runtime/       Renderer, FrameRecording, FrameFlightResources, ViewHistoryStore
  Scene/         SceneRenderScheduler, snapshots, preparer, ViewFamily plan
  Deferred/      DeferredViewFamilyRenderer, graph builder, resources, Passes/*
  Forward/       ForwardViewFamilyRenderer, graph builder, resources, Passes/*
  Common/        仅真正跨 Forward/Deferred 的小型 value/mechanism；不再作为杂物目录
```

目录移动必须跟随对应 owner/协议迁移，不单独作为 checkpoint。

### 3.8 时间语义与 `Frame` 命名迁移

`frame` 目前同时表示 host tick、Scene 内容版本、View 渲染采样、命令录制作用域、GPU submission、flight 槽与 Surface present 次数。单窗口单 View 时这些值恰好同步递增；一旦同一 editor UI 内存在多个 View（level viewport、material preview、thumbnail），它们各有独立渲染次数与 temporal 历史，`frameIndex` 就不能再同时代表它们。

判定规则：

- 可以叫 `Frame`：一次 host 调度批次，即 `AppKernel` 一次 iterate 决定的工作集合。
- 不能叫 `Frame`：Scene 内容版本、单个 View 的采样序号、命令录制作用域、GPU submission、fence 飞行槽、单个 Surface 的 present 次数。

逐符号迁移清单（M1 host tick / M2 Scene snapshot / M3 View / M4 recording-flight / M5 present / M6 pipeline 文件与类 / M7 GUI）、保留项与批次顺序见 `temporal_semantics.md`。该清单只消除同名异义，不改行为；与 4.0.3 分阶段合并执行，不单开一条并行重构线。

### 3.9 `IRenderRuntimeServices` 结论

`IRenderRuntimeServices` 作为抽象没有必要，作为数据有必要：它只有一个实现者 `RenderDeviceState`，全仓库没有任何替代实现或 mock；接口同时暴露时间、环境光照、调试绘制、gameplay 绑定四类无关能力，其中 `getGameplayResourceBinding()` 是死方法，三个 env lighting 方法与既有窄契约 `EnvironmentLightingResultProvider` 重复。

真正的问题是层级倒置：pass/stage 是 device-lifetime 配方，却经这个指针在录制期查 live ECS（`DeferredRenderPipeline::buildOverlayFrameInputs` 遍历 billboard / direction gizmo，`ForwardViewportStage` 遍历 skybox mesh），违反 3.1 与 4.0 的「ECS/Scene 查询必须在 graph build 前完成」。

处置：不单独开一刀，随 P3 `PreparedView` 收口一起删除——时间走 `HostClockState` 作为 pass 输入；env lighting 句柄在 graph build 前解析进 `PreparedView` 或并入 `EnvironmentLightingResultProvider`；`DebugRenderSystem` 由 overlay pass 构造注入；`getGameplayResourceBinding()` 直接删。删完后 `RenderDeviceState` 不再继承任何渲染接口，`PipelineCoordinator::InitDesc::runtimeServices` 随之消失，这是公开 `Renderer` owner 的净收益。

### 3.10 View 的声明、收集、抽取与录制边界（2026-09-17 review）

2026-09-17 review 的结论：**View 收集（collect）分层是对的，声明（declare）层缺失**。调度器「帧内聚合 + 按 (SceneId, sceneRevision) 去重 snapshot + 按 ViewFamily 分组」对应 UE 的 family/renderer 分层，保留；问题在于没有任何 owner 能声明自己的 view，于是编辑器的诉求只能经全局可变格子注入调度器。

#### 3.10.1 当前实际路径

（2026-09-17 review 时的现状；4d-1 已把「声明」从 tickRender 搬到各自 owner 的 producer，下表的前两行已删除，后三行仍待 4d-2 / 4d-3。）当时的「view 声明」只有 GameRuntime 一处（`tickRender` 内构造 `SceneViewDesc`）。GameEditor 不是提交方，它通过全局格子影响那一份声明：

| 编辑器想表达 | 实际通道 | 读取方 |
| --- | --- | --- |
| ~~我的视口相机矩阵~~ | ~~`setExtensionHostViewState` → `extensionHostView`~~ | **已删（4d-1）**：编辑器的 `EditorAuthoringViewProducer` 直接声明相机 |
| ~~我这 tick 没有世界视口~~ | ~~`setWorldSceneRenderEnabled` → `bWorldSceneRenderEnabled`~~ | **已删（4d-1）**：不声明即不渲染；动画策略改读 `renderedScenesLastTick` |
| ~~预览视图用哪个相机~~ | ~~`setCameraPreviewHostOwned` + `setCameraPreviewEntityUUID`~~ | **已删（4d-2）**：`EditorViewProducer` 据用户选中项声明预览 inset（view id 由编辑器自持） |
| 我的视口尺寸 | `setViewportRect`（仍是宿主几何，producer 以 context 读入） | `tickRender` → view 1 的 `viewportRect`；**4d-3b** 让声明方给出作者视口 rect，automation 的 resize 改走声明 |
| ~~我的视口要不要画 gizmo~~ | ~~`setEditorGizmoShown(bool)` → `AppRenderState::bShowEditorGizmos`~~ | **已删（4d-3a）**：开关是编辑器的 view option（`EditorLayer`），`EditorViewProducer` 读它声明 feature；automation 的 `set_editor_gizmos_visible` 经 `IEditorAutomationControl` 打到编辑器，无编辑器时报错；游戏视口只声明 `Game`，不再被这个开关影响 |

格子当时的写入方全在 `EditorLayer.cpp` / `EditorModule.cpp` / `EditorSurface.cpp`（gizmo 开关由 Window 菜单写），解析方全在 GameRuntime：编辑器无接口可声明自己的 view，只能「改状态再看运气」，且能成立全靠 hook 顺序（写在 `onLogic`，读在 `tickRender`）——与 GUI 侧已删除的「手写 tab sync」同类。4d-1 之后作者视口已经是声明，4d-2 收走预览、4d-3a 收走 gizmo，表里只剩「作者视口尺寸」一行等 4d-3b。

同一类症状还在 declare 阶段内以「直接读 ECS」的形式出现：`getPrimaryCamera`（`registry.view<CameraComponent>`）、`resolvePreviewCamera`（`scene.getEntityByUUID`）、`appendSceneCameraFrustumLines`（再遍历一遍 camera view）都在 `tickRender` 里对 live Scene 做查询。4d-1 已把「哪个是主相机」这一条交给声明方（`RuntimeGameViewProducer` 内部查询，`findPrimaryCamera` 落到 `Utility/SceneCameraQuery`）；剩下的预览相机选择与 frustum 线属于 4d-2。**4d 全部落地后，declare 路径上不再有 live-ECS 查询。**

UE 对照：`UGameViewportClient::bDisableWorldRendering` 与 world 选择同在 viewport client 上；Godot 是 `Viewport::world_3d`；Unity 由 `SceneView` 持有自己的 camera。三家都没有「中央提交者 + 全局开关」，`UWorld` / `World3D` 上不存在「我该被渲染吗」。因此该开关既不属于 Scene，也不属于 Renderer，而属于**声明方是否声明**。

#### 3.10.2 一个 View 被声明的层数（4c 后的现状）

| 层 | 结构 | 是声明本身还是派生 |
| --- | --- | --- |
| 声明（4c 起唯一一份） | `SceneViewDesc` | 是：Scene 句柄、view id、policy、相机、输出 rect、compose 目标 |
| 计划条目 | `SceneViewportTask` | **内嵌那个 desc**（逐字保留）+ `output` / `snapshotIndex` / `familyIndex` |
| 录制项 | `SceneViewRecording` | 引用计划条目 + `frameData` |
| pipeline 上下文 | `CameraFrameInput`（`cameraForViewRecording` 按 view patch 一次） | 派生：把 desc 的相机与输出展开成录制期要读的扁平结构 |
| 输出 | `RenderViewOutputDesc` | 派生：GPU 输出的身份与 extent |

4c 之前这里是六层、三份字段列表：`HostSceneViewSubmit` 12 字段搬进 `SceneRenderRequest`，`seal()` 再 14 字段搬进 `SceneViewportTask`。现在声明只写一次，计划条目内嵌它（`seal()` 不再逐字段抄），字段只在「派生物」两处存在：录制期的 `CameraFrameInput` 展开与输出的 `RenderViewOutputDesc`。剩余待收口的是这两处（checkpoint 5 的 `PreparedView`）。

#### 3.10.3 三处空转机制的处置

（登记于 2026-09-17 review，4a–4c 逐条处置；未完成项保留在表末。）

| 空转机制 | 处置 |
| --- | --- |
| `renderFlags` 全仓库无写方 | 已删（4c：「一份声明」不该带一个没人写的字段；`SceneViewFamilyKey` 同步去掉这个恒 0 分量） |
| `seal()` 内调用 `buildSnapshot()`，分组顺带抽取 | 已删（4a：`seal()` 只分组，抽取是 `buildSceneSnapshots()` 显式一步） |
| plan 只留 `sceneId`、丢弃 Scene 指针，宿主被迫反查 | 已删（4b：声明 / 计划条目 / 快照表项都带 tick-local `Scene*`，`derivedSceneForHostView` / `complete()` / `derivedScenesAgreeWithPlan()` 随之消失） |
| `sceneRevision` 恒为 0、`policyId` 恒为 1，`SceneViewFamilyKey` 仍含冗余的 `snapshotIndex` | **未处理**：三者都要等 4d 的 producer 真给出内容键（revision）与 policy，届时 key 才谈得上收口；checkpoint 5 的 owner-scoped view 身份是它的前置 |

#### 3.10.4 目标形态

一句话：**owner 声明自己的 view，collector 只做分组，抽取是显式一步，Renderer 只录计划。**

1. **一份声明结构** `SceneViewDesc`（取代 `HostSceneViewSubmit` 与 `SceneRenderRequest` 的重复）：Scene 句柄、稳定 view key、相机矩阵、输出 rect、feature mask、compose 目标。**4c 已完成**布局，**4d-2 已补上 `features` 与 `viewOwner`**（这两件事只有声明方知道：编辑器 view 画 gizmo、相机预览不画且要丢掉自己那台相机的机身体）。**未完成**：稳定 view key（owner-scoped `SceneViewKey`，checkpoint 5），`SceneViewportTask` / `SceneViewRecording` 也还没塌陷成「计划里的同一条 + 本帧的 `PreparedView`」。
2. **`ISceneViewProducer`**：`collectSceneViews(const SceneViewCollectContext&, SceneViewCollector&)`（比原计划多一个显式的 per-tick 上下文参数：tick / dt / viewport 几何是帧事实，应当读而不是写；把它们塞进 collector 会把 sink 和输入混在一起）。运行世界视口、编辑器作者视口、相机预览各自实现。**4d-1 已完成**：生产者接缝 + 世界视口由持有方声明，`extensionHostView`、`bWorldSceneRenderEnabled` 两个格子删除。**4d-2 已完成**：预览 inset 由编辑器按选中相机声明，`bCameraPreviewHostOwned` / `cameraPreviewEntityUUID` / `kHostOverlayPreviewViewId` 与 `resolvePreviewCamera` 删除。**4d-3a 已完成**：gizmo 开关归 `EditorLayer`，`bShowEditorGizmos` 删除，automation 经 `IEditorAutomationControl` 打编辑器。**未完成**：作者视口的 `viewportRect` 仍由宿主格子承载（4d-3b）。
3. **collect / extract / record 三步分离**：`collect()` 只收声明；`buildSceneSnapshots(plan, extractor)` 按唯一 Scene 显式抽一次；`Renderer::recordFrame(plan, surface)` 只消费。抽取不再是 `seal` 的副作用。4a 已落地这一形态，4b 把注入点从 `(SceneId, revision)` 收窄为 `SceneSnapshotExtractor(Scene&)`（M3 把 `RenderFrameExtractor` 改名为 `SceneSnapshotBuilder` 时把提取那一半并进去，不另立同名类型）。
4. **计划保留 Scene 句柄**（tick-local；Scene 生命周期更长）：**4b 已完成**——`derivedSceneForHostView` 与两个校验函数已删，改为 `ExtractedSceneRender` 的构造期不变量；4c 进一步把 `SceneViewFamilyKey` / `SceneSnapshotEntry` 的键从派生的 `sceneId` 换成句柄本身。
5. **View 身份 owner-scoped**：`kPrimarySceneViewId = 1` 现在同时承担「编辑器作者视口」与「独立游戏视口」两个产品的持久身份，`kHostOverlayPreviewViewId = 2` 由 host 铸造。改为 producer 注册时铸 `SceneViewKey{ownerId, localId}`。这是 `ViewHistoryStore`（TAA/exposure）与「同一 View 显示在两个 Surface」稳定键的前提。
6. **`plan.camera` 拆开**：`CameraFrameInput` 同时装 `flightIndex` / `frameIndex` / `deltaTime`（帧作用域）与一个 view 的相机矩阵（view 作用域）。前者进 `FrameContext`，后者进声明。与 M3 的 `CameraFrameInput` 删除同批。

#### 3.10.5 明确不做

- 不引入 WorldRegistry，不让 Scene 直接调 RHI/Renderer；仍由宿主调用 submit。
- 不删除 `SceneRenderScheduler` 的帧内聚合与 snapshot 去重；它是 family/renderer 分层的正确部分。
- 不让 GUI Framework 认识 Scene；Surface/present 与 View 正交的结论不变。
- 不为「一个 Scene 是否该渲染」引入 per-Scene 开关：该问题的答案是「没有 producer 声明这个 view」。

## 4. 分阶段实施

每个 checkpoint 只有一个可验收目标；代码、测试、progress.md 与计划变更同一提交。禁止用目录移动、空 registry、兼容 facade 或只写文档冒充完成。

### 4.0 GPU submission state split (R2 prerequisite)

本切片的真实迁移顺序固定为：

1. 以一个原子迁移改造 RenderFrameData：引用共享 SceneSnapshot，同时引入 View-owned draw buckets；不再按值复制或原地排序 Scene snapshot。该阶段已完成，View bucket 现在只保存 Scene 候选 vector 的借用指针和独立 order indices；后续只允许在此基础上继续拆 submission/View 生命周期。
2. Forward 的 resource set 提供 beginSubmission / beginView 语义：layout 和 pipeline 资源持久化，upload allocation、descriptor binding、skinning buffer 和 View output 由 submission/View 持有。Checkpoint A 已删除 resource-set 上的 `beginSubmission()`，改由 `RenderSubmission` 分配 upload/descriptor。
3. 当时由 RenderRuntime 保存 submission lifetime 到 submit/fence 完成。Checkpoint A 已把该阶段收敛为 `RenderSubmission` / `RenderSubmissionPool`：command buffer、frame token、upload arena、transient descriptor、keepalive 与 finish 由一次 recording 持有。`finish()` 仍不是 queue submit。RHI cmd begin/end 现由 DeviceState/Coordinator 打开。skinning 在 `SceneFamilyResources`；SSAO/Light/EntityId/Overlay/debug/post CIS 与 PointShadow instance/cull packet 在 typed View resources。
4. pipeline 的 recordView 只消费显式 View context，不再写 `_lastTickCtx` / `_lastFrameInput`。Forward/Deferred/Shadow **frame UBO** binding 已按 View 拆开；View pass CIS/UBO 由 typed `ViewResources` 持有。Checkpoint D 删除了 `tick()/beginTick()` 并以 `recordFamily` 为入口，但内部仍是 per-view 状态机。Stage `_frameInputs` / `_preparedViewSlot` 仍在。display compose 的 tone-map CIS 由 `PresentationGraphService` 按 Surface 持有，不复用 View `post.toneMap`。双 Scene 显示 / 双 Surface 排在 4.0.3 之后。
5. 通过 View A/B identity 测试确认：B 的 allocation、descriptor write、output publish 不改变 A；同一 Scene 的 A/B 仍指向同一个 snapshot owner。Checkpoint C 后这条对 SSAO/Light/EntityId/Overlay/debug/post pass binding 成立；Checkpoint D 后 table publish 也不再走 pipeline last-view getter。

这一切片不改变 Forward/Deferred 的 pass topology，也不引入新的 World 抽象；Shadow frame Binding 已按同一生命周期规则迁移。PointShadow indirect instance/cull packet 已按 View Binding 持有，不再按 flight 隐式共享。

`RenderSubmissionContext` 已删除。`RenderViewRecordingContext` 仍是 View 录制期的借用参数，只解决“不要从 pipeline 猜当前 View”，不是 GPU owner：

```text
RenderSubmission / RenderSubmissionPool
  command buffer / frame token / upload arena / transient descriptor / keepalive / finish

RenderViewRecordingContext
  SceneViewportTask / per-view RenderFrameData / viewSlot（borrowed parameter）
```

`FrameUploadArena` 不能让多个 View 通过第二次 begin 重置同一个 flight 的 cursor；`MAX_FLIGHTS_IN_FLIGHT` 只表示 GPU flight 并发，不等于 View 槽位。同一 `(flightIndex, frameToken)` 的 `beginFlight()` 仍幂等，不 rewind，后续 View 可以追加 allocation slice；不同 token 仍会在 fence-safe flight 上重新开始。Arena 现由 `RenderSubmissionPool` 持有，View 通过 `RenderSubmission::allocateUpload()` 取得 slice。Checkpoint B 再增加 SceneFamily 轴；禁止把 context bag 或 per-view mega Binding 当作最终生命周期。

### 4.0.1 FrameUploadArena 迁移期语义

同一 submission 的多个 View 可以依次调用 `beginFlight(flightIndex, frameToken)`：第一次调用初始化该 flight，后续相同 token 的调用必须是 no-op，并保留 cursor 与 backing buffer。每个 View 通过 `allocate()` 获得自己的 aligned slice，slice 的 buffer identity、offset 和 size 在 command buffer 录制到 queue submit 完成前保持有效。新的 frame token 仍必须等待旧 flight 安全后才能 rewind；扩容产生的旧 backing 继续交给 deferred deletion。

这只是 upload allocation 的正确性切片。frame UBO、persistent attachment、typed pass DS 与 family graph 已按 View/Family 拆开；不得因为 arena 可追加，就宣称 dual Scene 产品录制或 dual Surface GPU 已经正确。

### 4.0.2 Renderer 生命周期重构（A–E 入口）

这一轮允许改类名、文件名和调用协议，不保留 legacy facade。每个 checkpoint 仍只有一个可验收目标。迁移顺序从“继续填 Binding”改为“先建立 owner，再迁 pass，再删除旧状态”。

**Checkpoint A — 建立 `RenderSubmission` owner，替代分散的 beginSubmission（已完成）**

唯一目标：一次 command submission 的 command buffer、frame token、upload arena、transient descriptor allocation、keepalive 与 finish 状态由一个对象维护。

- 将 `RenderSubmissionContext` + `RenderSubmissionTable` + `FrameUploadArena` 的可变协议收敛为 `RenderSubmission` / `RenderSubmissionPool`；pipeline/resource set 不再各自 `beginSubmission()`。
- `RenderSubmission::allocateUpload()`、`allocateDescriptorSet()`、`retain()`、`finish()` 维护录制期不变量；finish 后拒绝分配。
- RHI command buffer begin/end 只在该 owner 或 frame coordinator 出现；本切片留在 RenderRuntime coordinator，测试使用 dummy cmd 指针。
- 验收：同一 submission 连续分配两个 View 的 slice/set 不覆写；flight 复用前旧 keepalive 存活；非法二次 finish/finish 后分配被拒绝。

**Checkpoint B — 引入 Scene-family 资源轴，修正“skinning 是 submission 全局”的错误假设（已完成）**

唯一目标：同一 submission 中 Scene A/B 的 scene packet、skinning upload/binding 与可共享 shadow work 互不覆盖；同 Scene family 的多个 View 显式引用同一 family owner。

- `SceneRenderPlan` 物化 `SceneViewFamilyPlan[]`；family 由 scene snapshot generation + pipeline/config + feature/shadow policy 分组。
- 引入 `SceneFamilyResources` / `SceneFamilyGpuPacket`，由 submission 持有，View 只保存引用。
- `PerFlightFrameResourceSetBase` 的 skinning 单例迁出；不同 Scene 不以 flightIndex 为共享 key。
- 验收：同 Scene 双 View family owner identity 相同；双 Scene identity 不同；View B 不改变 View A/Scene A 的 skinning descriptor/range。
- 本切片未共享 PointShadow indirect buffer（Checkpoint C），也未把 pipeline 改成 `recordFamily`（Checkpoint D）。

**Checkpoint C — typed View/Pass resources，Stage 改为 pass recipe（部分完成）**

入口已落地：SSAO / Light / EntityId / Overlay / Forward-debug / postprocess 的 DS/UBO 由 `ViewResources` typed 子结构持有；Bloom/BasicPost viewId map 与 Stage singleton CIS 已删除；PointShadow packet 在 Shadow View Binding。

未收口：Stage 仍有隐式 current View。源码仍有 `LightStage::_frameInputs` / `setFrameInputs()`、`BasicShadowMapTechnique::_preparedViewSlot`。部分 `_debugViews` / `_frameShadowSettings` 是调试或 persistent settings，可以保留；`_preparedViewSlot`、`_frameInputs` 必须清掉。

已完成的入口工作：

- 在 `DeferredFrameResourceSet` / `ForwardFrameResourceSet` 内引入 `ViewResources`（frame Binding + typed pass bindings）；不要扩展 mega `FrameResourceSet::Binding`。完整类重命名为 `*GpuResourceLibrary` 未改文件名。
- SSAO/Light/EntityId/Overlay/debug/post 的 CIS/UBO 从 Stage 成员迁到 View pass bindings。Bloom/BasicPost 的 `_viewBindings` / `_viewSets` map 删除。
- PointShadow instance 列表来自 View draw buckets，packet 放进 Shadow View Binding 的 `PointShadowIndirectResources`。
- 入口验收：两 View 的 pass binding identity 不同；execute 前后 recipe 半径/layout 不变。current-view 槽位未清，故 Checkpoint C 不能标完成。

**Checkpoint D — `ISceneViewFamilyRenderer::recordFamily` 编译 family graph（部分完成）**

family graph 入口已落地：`recordFamily` 对同一 family 建一个 graph，publish 只 ingest `ViewFamilyRenderResult::views`，`IRenderPipeline` 无 `tick`。

未收口：Deferred/Forward 内部仍是旧 per-view 状态机的外包装（`beginViewRecording` / `syncFrameSettings` / `prepareShadowPass` / `beginView` / `appendViewToGraph`），并用 `familyPredecessor` 把各 View 串行连接。当前更准确的描述是：多个 View 被放进了一个 graph，但仍由旧 per-view 状态机逐个构建。还不是真正的 ViewFamily compiler。验收测试证明了 family plan 分组与 output publish 路径，不证明 compiler 已替换 state machine。

- `IRenderPipeline` 增加 coordinator 别名 `ISceneViewFamilyRenderer`；具体 Forward/Deferred 仍保持 concrete 类型与现文件名，未改名为 `*ViewFamilyRenderer` / `*GpuResourceLibrary`。
- `recordFamily(ViewFamilyRecordContext) -> ViewFamilyRenderResult`：同一 family 建一个 graph，family-shared skinning 只 prepare 一次，再为每个 View 追加 shadow/GBuffer/forward/post 分支；`familyPredecessor` 把门后续 View 接到前一 View 最后一 pass。export/pass 名使用 `makeViewGraphName`，避免同一 graph 内重名覆盖。
- 删除 `_currentGBufferResources` / `_currentViewportResources` / `_publishedGraphOutputs` / `_currentPostprocessOutput` / `_currentOverlayFrameInputs` / `_currentEnvironmentLighting*` 作为 publish source。`_debugViews` 与 Forward `_viewportResources.publish` 只保留 display-root inspector/getter fallback。
- Coordinator 按 `plan.viewFamilies` 调用 `recordFamily`；`RenderViewOutputTable` 只 ingest `result.views`；debug catalog 优先 typed output。
- 验收（入口）：`ViewFamilyRendererTest` 同 Scene 双 View 一个 family plan、双 Scene 两个 family plan、table 不经 pipeline getter publish、`IRenderPipeline` 无 `tick`。未验收真正的 family compiler。

**Checkpoint E — 拆除 `RenderRuntime` god facade（部分完成）**

类拆分已落地：删除 `RenderRuntime`、`renderFrame(FrameInput)`、`getCurrent*()`；host 不再用 ViewportState / active Scene locator。

ownership 尚未闭环：`RenderFrameCoordinator` 几乎无状态，经 friend 访问 DeviceState 的 `_submissions` / `_viewOutputs` / `_presentationGraphService` / `_publishedOutputFlight` / `_publishedOutputViewId`。二者不是两个真正独立的 owner，只是把旧 RenderRuntime 的方法和字段拆到两个文件。`RenderDeviceState` 名称与职责不符，它是 persistent renderer，不是 device。`RenderFramePlan` 仍同时包含 Scene plan 与 `PresentFrameInput`；`beginFrameCommandBuffer()` 无 Surface 则拒绝录制。E 的验收“多个 Surface 共享同一 frame outputs / 关闭某 Surface 不影响 Scene family record”未实现。

已完成的入口工作：删除 `ViewportStateService`；extent/format 来自 View plan 与 host `HostViewState`；未引入空的 `ViewHistoryStore`。UI-only 由 host 提交空 `sceneRender`。入口验收：UI-only frame 可不创建 Scene family；资源 mutation 只发生在 recording 前。

**产品双 Scene 第一刀 — family-scoped derived Scene（已完成）**

唯一目标：coordinator 不再用一个 frame-wide Scene* 服务所有 family。每个 `SceneViewRecording` 携带产出该 snapshot 的 host Scene；同 family / SceneId 必须共享指针，不同 SceneId 不得共享。

- 删除 `RenderFramePlan::derivedScene`。
- 录制前对 `uniqueDerivedScenes` 各调用一次 `prepareDerivedState`（UI-only 仍 prepare null）。
- `ViewFamilyRecordContext::derivedScene` 来自该 family 的 recording，不是 plan 字段。
- 验收：双 Scene 两个 family 得到两个独立 derived Scene；同 Scene 双 View 共享一个；family 内混用或跨 SceneId 别名指针被拒绝。Scheduler 仍不拥有 Scene。Host 当前仍只提交一个 live Scene；不要宣称产品双 Scene GPU 录制完成。

**产品双 Scene 第二刀 — host 提交 live Scene 列表（已完成）**

唯一目标：host 不再把 `getActiveScene()` 当成唯一可提交的 Scene。`HostSceneViewSubmit` 列出本帧每个 live Scene viewport；同 Scene* 共享一次 extract；不同 Scene 产生隔离 snapshot / family；recording 的 `derivedScene` 从该列表查找，不回退成单一 active Scene。

- GameRuntime 经 `submitHostSceneViews` 提交列表；默认产品帧仍把当前 viewport Scene（及同 Scene 的 camera overlay）放进列表。
- 验收：两个 live `Scene` 对象抽出不同 snapshot 与两个 family，灯光数据不串；同 Scene 双 View 共享 snapshot。未宣称编辑器 PIE 同时显示 authoring+play，也未做双 Surface GPU。

Checkpoint A/B 的 owner 入口已落地。C/D/E 只有入口，ownership 与 family compiler 未闭环。不要把新增 pipeline feature、双 Surface 视觉效果、产品双 viewport UX 或目录搬迁当作下一刀。

### 4.0.3 Ownership 收口（当前主线）

2026-09-17 只读 review：功能向 ViewFamily 靠近，架构没有真正收口。旧 RenderRuntime 被拆成多个名称，没有形成更清晰的唯一 orchestration，反而增加了概念跳转。

真实 loop：

```text
AppKernel::run
  -> GameRuntimeFrameOrchestrator::iterate
       -> tickLogic
       -> tickRender
            -> 收集 Scene/View、extract、prepareView、UI snapshot
            -> acquire Surface
            -> RenderFrameCoordinator::record
                 -> DeviceState begin command buffer
                 -> pipeline.recordFamily
                 -> View/Display compose
                 -> end command buffer
            -> submit/present
```

问题不是没有 loop，而是读者要跨越约 7 个概念层才能拼出它。`tickRender` 同时塞了 camera policy、preview、frustum、Scene request、extract、prepare、GameUI、acquire、plan assembly、module compose、submit/present。

执行顺序（每个 checkpoint 一个可验收目标）：

1. **修计划状态（已完成）**：C/D/E 改为部分完成；删除把 DeviceState+Coordinator 当成闭环、把 RenderRuntime 当成现行 orchestrator 的叙述。
2. **公开 `Renderer` owner**：合并 `RenderDeviceState` + `RenderFrameCoordinator`；关闭 friend 越界。产品层只调用 `Renderer::recordFrame(plan, surfaceTarget) -> RecordedFrame`。不引入第三个全能 coordinator。
3. **recording 与 flight 拆名**：`RenderSubmission` 拆成 `FrameRecording`（cmd/allocate/retain/seal）与 `FrameFlightResources`（fence-safe arena/descriptors/keepalives）。`RecordedFrame` 带 command buffer 与 flightIndex，由 host submit。
4. **view 声明与收集收口（2026-09-17 review 新增，见 §3.10）**：补齐「谁声明 view」，再让抽取成为显式一步。四刀，每刀可独立验收：
   - 4a **抽取移出 seal**（已完成）：`SceneRenderScheduler::seal()` 不再调用 `request.buildSnapshot()`；改为 `seal()` 只分组（快照表建好但内容为空）、`buildSceneSnapshots(plan, resolver)` 显式抽取。请求结构不再携带任何闭包；无法解析内容的 Scene 由该步骤剔除并重新分组，其余 Scene 仍照常录制。
   - 4b **计划保留 Scene 句柄**（已完成）：声明 / task / 快照表项携带 tick-local `Scene*`，`sceneId` 由 `seal()` 从句柄派生；抽取接口收窄为 `SceneSnapshotExtractor(Scene&)`。`derivedSceneForHostView`、`SceneRenderPlanInput`（含 `complete()`）、`derivedScenesAgreeWithPlan()`、`derivedSceneForFamily()`、`SceneViewRecording::derivedScene` 全部删除，改建为 `ExtractedSceneRender`：只有 `buildSceneSnapshots()` 能造出非空 plan、只有 `pairViewFrames()` 能放入 recording，于是「忘了抽取」与两列错位都变成编译错误而不是运行时日志。
   - 4c **合并声明结构**（已完成）：`HostSceneViewSubmit` 与 `SceneRenderRequest` 合成一份 `SceneViewDesc`（`Render3D/Common/SceneViewDesc.h`）。`SceneViewportTask` 内嵌它，`seal()` 不再逐字段搬运；`submitHostSceneViews` 这层转发删除，宿主直接 `scheduler.submit()`；无写方的 `renderFlags`、派生的 `sceneId`（键改为句柄本身）、`HostSceneRenderSubmit.*` 也一并消失（该文件只剩抽取，改名 `HostSceneExtract.*`）。
   - 4d **`ISceneViewProducer` 与编辑器提交自己的视口**：运行世界视口、编辑器作者视口、相机预览各自 `collectSceneViews`。删除 `bWorldSceneRenderEnabled`、`extensionHostView` 注入、`bCameraPreviewHostOwned`、`cameraPreviewEntityUUID`、`bShowEditorGizmos`；预览相机选择回到编辑器；`viewportRect` 降为 producer 输入；gizmo 由声明方按 view 声明；orchestrator 不再铸造 `kHostOverlayPreviewViewId`。验收含第二条：declare 路径上不再有 `registry.view` / `getEntityByUUID` / `getPrimaryCamera` 这类 live-ECS 查询（见 §3.10.1）。三刀：
     - 4d-1 **producer 接缝 + 世界视口归位**（已完成）：新增 `ISceneViewProducer` / `SceneViewCollector` / `SceneViewCollectContext`（Render3D）；宿主注册列表在 `AppRenderState::viewProducers`；`RuntimeGameViewProducer`（Runtime 态，用游戏相机）与 `EditorAuthoringViewProducer`（非 Runtime 态且非 2D canvas，用编辑器相机）各自声明主 view。删除 `extensionHostView` 与 `bWorldSceneRenderEnabled` 两个格子；`prepareHostViewState` 收成「宿主几何 + 时钟」，相机由声明回填；`getPrimaryCamera` 从 orchestrator 静态函数移入 `Utility/SceneCameraQuery`（Scene 查询不该是 orchestrator 私有）。`SkeletonAnimationSystem` 的策略改为「上一 tick 是否为该 Scene 产出了内容」（`AppRenderState::renderedScenesLastTick`，渲染侧派生事实，替代按 viewport 的开关）。
     - 4d-2 **相机预览归位**（已完成）：`EditorViewProducer`（原 `EditorAuthoringViewProducer` 改名，因为现在声明的是编辑器的两个 view）同时声明作者视口与预览 inset；删 `bCameraPreviewHostOwned` / `cameraPreviewEntityUUID` 与宿主铸造的 `kHostOverlayPreviewViewId`（view id 归编辑器）；`resolvePreviewCamera` / `cameraProjectionForOutput` 移入编辑器，选中相机直接从 `EditorLayer::getCameraPreviewEntity()` 来；选中相机的 FOV 线框从宿主相机包络移到编辑器自己的 world overlay pass（`recordSelectedCameraFrustum`）。同时给 `SceneViewDesc` 补上 `features` 与 `viewOwner`——这两件事都只有声明方知道。
     - 4d-3a **feature 归位**（已完成）：gizmo 开关归 `EditorLayer`（编辑器自己的 view option），删 `AppRenderState::bShowEditorGizmos` 与 `App::is/setEditorGizmoShown`；`EditorViewProducer` 的两个 view 都读它；automation 的 `set_editor_gizmos_visible` 经 `IEditorAutomationControl` 打到编辑器（无编辑器即失败）；`RuntimeGameViewProducer` 只声明 `features = Game`——编辑器不再能改变不由它声明的视口（刻意接受的行为差异）。新增 `EditorViewProducerTest` / `RuntimeGameViewProducerTest`。
     - 4d-3b **作者视口 rect 归位**（未开始）：作者视口的 rect 由编辑器按面板声明，`setViewportRect` 不再由编辑器写，automation 的 resize 用例改走声明。
5. **PreparedView**：删除 CameraFrameInput patching、SceneViewRecording、RenderPipelineFrameContext 之间的重复层。view 身份改为 owner-scoped `SceneViewKey`（4d 之后），并按此建立 `ViewHistoryStore` 的稳定键。
6. **清除 Stage current-view**：删除 `_frameInputs` / `_preparedViewSlot` 等隐式槽位。
7. **压缩 `tickRender`**：保留该入口，收成 collectSceneViews → extractScenes → prepareViews → prepareModules → buildGameRenderFrame → acquire → recordFrame → submitPresent → presentModuleExtras。camera preview、Scene request、UI snapshot 下沉到普通 builder；`tickRender` 里不再有「某个 view 要不要渲染」的判断。

Surface 与 View 正交、产品帧同时显示两个 Scene viewport、双 Surface GPU 排在 4.0.3 之后。不要为了“继续”发明 PIE authoring PiP。

### R0 — 建立单 View 正确性基线

唯一目标：证明当前单 View 的 snapshot、graph、compose、present 和资源生命周期边界。

工作项：记录 GameRuntimeFrameOrchestrator → RenderFrameExtractor → RenderRuntime → pipeline → ViewCompose → DisplayCompose → Present 调用图；核对 graph build/execute 时序和 live-state 访问；增加单 Camera golden/trace；验证 resize、surface recreate、zero extent、关闭单窗；登记并修复影响基线的 GUI test target/include 问题。

验收：单 Camera golden 稳定；snapshot 在 graph build 前生成；trace 可区分 Camera graphics/UI/ViewCompose、DisplayCompose(surface)、Present(surface)；Forward/Deferred 当前 pass 顺序被记录且未被新抽象改写。

### R1 — 建立 SceneRenderRequest 与 Scene/View 契约

唯一目标：把 Scene 的身份、请求、所有权和 frame snapshot 边界定义清楚，允许同一帧存在多个互相隔离的 Scene request。

在 Framework/Render/Render3D/Common 增加 SceneId、SceneRenderRequest、SceneSnapshot、RenderViewInput、RenderViewFamily 契约；增加不持有 Scene/ECS 的 SceneRenderScheduler 接口；GameRuntime、GameEditor、Material preview 各自提交 SceneRenderRequest。active Scene 查询只属于产品层，不参与新的 Scene request 数据来源。

调度器在 UI 渲染之前工作：收集本帧请求，按 Scene 去重 extraction，再按 viewport 生成 culling、sort、shadow fitting 和 view-specific overlay。不得每个 View 重复遍历所属 Scene 的全部 ECS 资源，也不得跨 Scene 复用排序、shadow 或 entity-id 结果。

R1 字段分类不能按现有结构名整体搬迁，必须按语义拆分：

| 当前字段/数据 | 目标归属 | 迁移说明 |
| --- | --- | --- |
| RenderDrawItem.worldMatrix、mesh/material 引用、entity id | Scene snapshot candidate | 与 Camera 无关；先保留未排序 candidate |
| RenderDrawItem.sortKey、material/mesh bucket 顺序 | View preparation | 由 Camera distance 和 pipeline 策略决定，不能放进共享 snapshot |
| skinning palette 内容 | Scene snapshot | 一帧抽取一次；同 Scene family 多个 View 共享 palette，索引需保持稳定 |
| point/directional 原始光照参数 | Scene snapshot | 只保存灯光实体数据和稳定顺序 |
| directional cascade/shadow view-projection | View preparation | 当前由 camera view/projection、shadow settings 计算，不能随世界快照共享 |
| view/projection/viewProjection/cameraPos/viewportExtent/viewOwner | RenderViewInput | 当前已存在于 CameraFrameInput，迁移时保持值来源不变 |
| frame index / delta time | Frame/Scene/View metadata | 不参与场景资源抽取，不能通过 ECS 在 graph execute 阶段读取 |

因此 R1 的实现顺序固定为：先定义 SceneId/SceneRenderRequest 生命周期与 snapshot ownership；再为每个 Scene 引入未排序 candidates 和 View preparation；然后迁移 light/shadow；最后才替换旧 RenderFrameData adapter。当前 extractor 已形成显式 Scene extraction / View preparation 两阶段，但 scheduler 尚未接入真实 Scene extractor；下一切片才连接这两者。禁止先把现有 RenderFrameData 机械拆成两个同构 struct，也禁止实现全局唯一 SceneSnapshot。

验收：同一 Scene 的两个 View 共用一个 SceneSnapshot；两个不同 Scene 产生两个 snapshot 且实体/灯光/资源生命周期不串；View A 的矩阵/extent 不修改 View B；UIOnly 不需要 Scene request；pipeline 不从 window/swapchain 反查矩阵或尺寸；单 View golden 不变。

「谁声明 view」尚未落地：当前只有 GameRuntime 一处声明点，编辑器通过 `extensionHostView` / `bWorldSceneRenderEnabled` / `bCameraPreview*` 几个全局格子影响它。契约、代价与目标形态见 §3.10，落地排在 4.0.3 第 4 刀（4d）。本条的「UIOnly 不需要 Scene 声明」在 4d 之后应表现为「没有 producer 声明 view」，而不是声明一个空的 view。

### R2 — SceneRenderScheduler 编排离屏任务

唯一目标：在 UI 之前由 SceneRenderScheduler 聚合本帧 Scene viewport 离屏任务，并由公开 `Renderer` 只消费 sealed SceneRenderPlan / PreparedViewFamily。

Scheduler 负责 Scene snapshot 去重和任务排序；Renderer 负责 frame recording、pipeline record、ViewCompose 和输出句柄。两者都不创建 OS window、不 acquire/present。当前实现仍把 Scene recording 绑在一次 Surface acquire 上，这是 4.0.3 要解开的耦合，不是已完成事实。

R2 当前执行顺序：4.0.2 A/B 入口已落地；C/D/E 部分完成；family-scoped derived Scene 与 host live Scene 列表已落地。下一刀是 4.0.3：合并 DeviceState+Coordinator 为公开 `Renderer`。产品双 viewport 与双 Surface GPU 排在 ownership 收口之后。

验收：同一 Scene snapshot 渲染两个 Camera；两个 Scene 各自提交并渲染一个 viewport；一个 View 输出被两个 Surface display compose；一个 Surface display compose 多个 View；未提交 request 的 Scene 不产生 render task；关闭/最小化一个 Surface 不影响另一 Surface、其它 Scene request 和 View；GPU 资源在 submit 完成前存活。

### R3 — 独立 GUI2D pipeline 与 GameUI 按 View 复用

唯一目标：通过显式 SceneRenderRequest/View/compose 契约连接 Level Editor、Material Preview、UI Editor 与 Scene render，不复制 Renderer，也不把 UI 挂回 Scene。

GUI Framework 保留 WidgetTree、UIFrameSnapshot、Render2D compose、GUIRenderSurface，以及每 native window 的 tree/snapshot/focus/input。GameRuntime/GameEditor 负责 GameUIHost[ViewId] 生命周期、input rect、focus/capture、UI scale、snapshot 与 ViewCompose 绑定；Level Editor、Material Preview 等功能各自提交 SceneRenderRequest。UI Editor 默认走 WidgetTree → UIFrameSnapshot → Render2D → PresentSurface；3D 预览必须显式提交对应 Scene request，不能偷用 active runtime Scene。

规则：每个可交互 Game View 默认独立 WidgetTree；同一 View 被多个 Surface 显示时复用 snapshot；不同 View 不共享 live widget tree；UIOnly 不提交 Scene request。

R3 的「各自提交 SceneRenderRequest」与 §3.10 是同一件事：`ISceneViewProducer` / `collectSceneViews` 是它的接口形态。Material Preview、UI Editor 3D 预览、相机预览都必须成为 producer，而不是经由全局格子寄生在 runtime 视口声明上。

验收：UI Editor 无 world render 仍可运行；两个 Game View 的 input/focus/snapshot 不串扰；同一 View 在两个 Surface 显示时只生成一份 UI snapshot；录制期只读 immutable UIFrameSnapshot。

### R4 — GameUI 复用、性能与扩展性收口

唯一目标：用 trace/profile 决定优化是否进入稳定架构。候选包括 Scene extraction 复用、culling/sort cache、View 输出复用、compose 批量调度、submit 拆分和第三种 pipeline 接入方式。

不做：强制 BaseRenderPipeline；每窗复制 IRender/Renderer；GUI Framework 依赖 Scene/ECS/Render3D；一个 WidgetTree 管多个 OS window；把相机矩阵、swapchain imageIndex、NativeWindow 句柄塞进 shader-facing 结构。

## 5. 与已有计划的边界

| 主题 | 所属计划 | 本计划处理方式 |
| --- | --- | --- |
| GUI invalidation / animation | gui-invalidation-architecture | R0 后可并行；动画 tick 必须在 snapshot 前 |
| GUI Dock / multi-OS-window | gui-multi-os-window-editor | 消费 IRenderSurfaceContext 与 per-window present，不重写 Dock |
| Forward/Deferred 去重 | render-pipeline-dedup-runtime-split | 本计划 4.0.2 建立共享 submission/resource 机制与独立 family renderer；只共享机制和窄接口，不合并 pass topology |
| GameEditor tab/session | gui-editor plans | 只要求 editor 提供 View/Surface 绑定 |

## 6. 验证与提交门禁

每轮先读根 AGENTS.md、plan/AGENTS.md、ya-build、render-arch；检查工作区和前一 checkpoint；复述唯一目标、边界、保留项、非目标；用 rg 核对真实符号和调用方。

每个 checkpoint 至少运行 git diff --check、受影响的 XMake build/test、单 View golden/trace，并在 R2 后加入双 View/双 Surface。生成文件只读，资源不能在录制中途重建，graph execute 不得查询 ECS/Scene/live WidgetTree。代码、测试、plan/progress 同一 checkpoint 提交。

提交格式：[render/view] ...、[render/runtime] ... 或 [gui/compose] ...。

## 7. 完成定义

- SceneRenderRequest、SceneSnapshot、SceneRenderPlan 与 RenderViewInput 责任边界稳定；代码中不再存在 WorldFrameSnapshot 或 RenderFrameExtractor::extract() 兼容接口。
- SceneRenderScheduler 能调度多个 Scene、多个 View；公开 `Renderer` 只消费 sealed plan 并返回 `RecordedFrame`；`RenderRuntime` 已删除，但 DeviceState+Coordinator 的不完整拆分必须收成单一 Renderer；Forward/Deferred family renderer 保留策略差异，且不再靠 per-view state machine 外包装。
- Surface/swapchain/present 与 Camera/Viewport/Compose 不再混名或互相反查；Scene recording 不再要求本帧已经 acquire 一个 present Surface。
- GUI2D、GameUI、SceneViewport 依赖方向清楚。
- 至少有单 Scene 单 View、单 Scene 双 View、双 Scene 双 View、双 Surface、UI-only 五类可重复验证场景。
- 后续性能优化由 trace/profile 选择，而不是继续增加隐式状态。
