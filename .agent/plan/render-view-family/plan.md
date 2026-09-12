# Render View Family 与 GUI/GameUI 渲染边界重构计划

> 建立日期：2026-09-12
> 状态：规划中，尚未开始本计划的代码 checkpoint

## 1. 主线选择

下一阶段选择 camera based render flow / SceneRenderScheduler / ViewFamily 作为主线。当前多窗口 RHI、GUI surface/present 和单 Camera 输入契约已经存在，但 RenderRuntime 仍按单 View、单 active Scene 编排。这里不引入 WorldInstance/WorldRegistry：Scene 仍属于 GameRuntime、GameEditor 或 preview 产品层；只有本帧需要显示的 Scene viewport 才向渲染调度器提交 offscreen render request。先稳定 Scene 请求、离屏任务和 UI 之前的调度边界，再推进多相机；不要同时重写 Dock、动画、GameUI 和 N Camera。

推荐顺序：

1. R0：正确性基线与可观测性。
2. R1：SceneRenderRequest / SceneFrameSnapshot / RenderViewInput 契约。
3. R2：SceneRenderScheduler 在 UI 之前聚合 viewport 离屏任务，RenderRuntime 只录制任务。
4. R3：Editor/preview/UI-only 三类产品路径接入。
5. R4：GameUI 按 View 复用、性能与扩展性收口。

GUI 动画属于 gui-invalidation-architecture 的独立小切片，可在 R0 完成后并行；GUI Dock/multi-OS-window 属于 gui-multi-os-window-editor，本计划只消费其 surface/present 合约。

## 2. 当前仓库事实

- Framework/Render/Render3D/Common/RenderFrameInputs.h 已有 CameraFrameInput、ViewComposeInput、DisplayComposeInput、PresentFrameInput。
- RenderRuntime 位于 Framework/Render/Render3D/RenderRuntime.*，已持有 PipelineCoordinator、PresentationGraphService、ViewportStateService，但仍按单 View 录制，并通过 activeSceneProvider 间接绑定一个 active Scene；这是迁移期事实，不应成为最终 Scene 所有权。
- Forward 与 Deferred 保留各自的 FrameGraphOrchestrator 和 pass topology；不抽强制 BaseRenderPipeline。
- Applications/GameRuntime/Utility/RenderFrameExtractor.* 从 ECS 抽取 RenderFrameData。
- RenderFrameData 当前同时承载 camera、lights、draw buckets、skinning，混合了 Scene、View 和 frame-flight 语义；上一小步引入的 WorldFrameSnapshot 名称已被判定过于贴近错误的 world 抽象，应在后续迁移中改为 SceneFrameSnapshot/SceneRenderSnapshot，当前类型仅作为兼容过渡。
- GUI live 事实源是 WidgetTree，录制只消费 immutable UIFrameSnapshot。
- IRenderSurfaceContext、swapchain、acquire/present 已与 Camera 离屏目标分开；acquire/present 由 host/present coordinator 负责。

## 3. 目标对象模型

    Scene owners (GameRuntime / GameEditor / Preview)
      -> SceneRenderScheduler::submit(SceneRenderRequest)
           -> group by SceneId for this frame
           -> build SceneFrameSnapshot once per Scene
           -> SceneViewportTask[viewport A, viewport B, ...]
                -> RenderViewInput + offscreen output
      -> RenderRuntime::recordSceneTasks(tasks)
           -> Forward / Deferred record each viewport
      -> UI hosts build UIFrameSnapshot
      -> ViewCompose (UI/gizmo onto scene output)
      -> DisplayCompose(surface, image)
      -> Present(surface)

Scene 是产品层的内容/编辑对象和 ECS 所有权边界，不是 GUI window，也不是 RenderRuntime 的全局状态。Scene owner 只在本帧需要某个 viewport 时 submit request；未 submit 的 Scene 不会被渲染。

SceneRenderRequest 是本帧的窄请求，至少包含稳定 SceneId、viewport/view id、camera packet、offscreen target description、render flags、overlay/UI binding 和用于构建不可变 SceneFrameSnapshot 的 source/callback。请求本身不创建 OS window、不 acquire/present，也不把 live Scene/ECS 查询留到 RenderGraph execute 阶段。

SceneRenderScheduler 是 UI 之前的 frame coordinator：收集请求、校验 Scene 生命周期、按 Scene 去重 Scene extraction、按 viewport 生成 camera-dependent preparation，并输出 immutable SceneRenderPlan。它不拥有 Scene，不替 Scene tick，不决定 Dock/tab 归属。

SceneFrameSnapshot 是某个 Scene 在某一 frame 的不可变输出；同一帧可以有多个 Scene snapshot。snapshot 的所有 resource handles 必须保持到引用它的 viewport command submit 完成。不同 Scene 即使共享材质/mesh 资源，也不能共享实体、灯光、skinning 或排序结果。

ViewFamily 是同一 Scene、同一 frame、同一 pipeline/resource scheduling 语义下的一组 viewport View。一个 Scene 可以有多个 ViewFamily；不同 Scene 不能因为使用相同 pipeline 就合并 snapshot。UIOnly 不提交 SceneRenderRequest，直接走 WidgetTree/UIFrameSnapshot/Render2D。

### 3.1 与业界实现的对照与结论

| 参考实现 | 对应关系 | 采纳结论 | 不照搬的部分 |
| --- | --- | --- | --- |
| Unreal UWorld / FScene / FSceneRenderer / FSceneViewFamily | Scene 内容与渲染代理分离；一次 ViewFamily 可包含多个 View；编辑器和 preview 可以有不同 world/context | 采用 Scene owner 提供内容、renderer 按 ViewFamily 生成 frame plan 的方向；SceneRenderScheduler 对齐 FSceneRenderer 的 frame coordinator 角色 | 不把 Scene/ECS 或 editor world 生命周期下沉到 GUI Framework；不复制 UE 的全局 world context 层 |
| Unity Scene + Camera + ScriptableRenderContext / RenderPipeline | Camera/viewport 提交渲染请求，pipeline 按 camera/culling 组织；Scene 本身不直接录制 GPU 命令 | 采用 request/plan/record 三段式；Scene owner 只提交 request，Scheduler 负责去重 Scene extraction 和 per-view preparation | 不把每个 Camera 都当成独立完整 frame；同一 Scene 的多个 View 必须共享 snapshot |
| Godot SceneTree / World3D / Viewport / SubViewport | Viewport 绑定 world 和 render target；多个 SubViewport 可渲染不同内容，再由 UI/Canvas 合成 | 采纳 viewport 是显示/离屏请求边界、Scene 是内容边界的关系；Material preview、thumbnail、editor viewport 都是 request consumer | 不让每个 Viewport 隐式拥有一套 RenderRuntime 或 device；OS window 仍由 GUI host 管理 |
| ImGui draw list + platform viewport | 没有 Scene renderer；每个 context/window 生成 draw data，后端只消费 draw data | 采纳后端只消费 immutable packet，不读 live tree/Scene 的原则；UI snapshot 与 SceneRenderPlan 同属 frame packet | 不用 ImGui 的 immediate draw list 代替 3D Scene extraction、culling 或 render graph |

评估结论：Scene owner 提交 offscreen request + 独立 SceneRenderScheduler + RenderRuntime 录制 immutable plan，是合理且接近主流引擎的组合。需要修正两点：Scene 不应直接调用 RHI/RenderRuntime，只由宿主调用 submit；“UI 之前”只约束 GPU compose 顺序，不强制 UI logic/snapshot 晚于 request collection。

Scheduler 必须是 frame-local coordinator，而不是常驻 Scene registry。beginFrame -> submit(request)* -> seal/build plan -> record(plan) -> clearFrame。SceneRenderRequest 是产品层到渲染层的窄协议；SceneRenderPlan 是 immutable 渲染输入，并拥有按 (SceneId, sceneRevision) 去重的 snapshot table；SceneViewportTask 只保存 snapshotIndex，读取时必须校验表项的 SceneId/revision。RenderRuntime 不保存 request，不拥有 Scene，不调用 SceneManager。Scheduler 位于 RenderRuntime 同层的 Render3D orchestration/service，GUI Framework 只消费 View output 和 UIFrameSnapshot。

每个 Scene 的 SceneFrameSnapshot 只保存该 Scene 的 transforms、mesh/material 引用、lights、animation/skinning 结果、resource handles 和稳定 id。它不保存唯一 Camera 的矩阵、View 排序、culling 或 viewport rect，也不能隐含来自另一个 Scene 的资源/实体。

RenderViewInput 至少包含 ViewId、SceneId、owner 计算的 view/projection/viewProjection/camera position、离屏 extent/scale、culling mask、postprocess/debug/overlay flags 和该 Scene 的 SceneFrameSnapshot 引用。UIOnly 不构造 RenderViewInput，也不伪造空 SceneId；它只消费 UIFrameSnapshot/Render2D 输入。View 输出是离屏 color/depth/辅助 attachment，不拥有 OS window、swapchain，也不 acquire/present。

对象语义必须分开：Camera/View 是一次 world 渲染；ViewportWidget 是显示 View 输出的 GUI 矩形；Surface 是 OS window 的 present 目标；Swapchain 是 Surface 的显示缓冲；ViewCompose 写 View RT；DisplayCompose 把一个或多个 View/preview/chrome image 排到 Surface；Present 提交 Surface。一个 View 可被多个 Surface 显示，一个 Surface 可显示多个 View。

## 4. 分阶段实施

每个 checkpoint 只有一个可验收目标；代码、测试、progress.md 与计划变更同一提交。禁止用目录移动、空 registry、兼容 facade 或只写文档冒充完成。

### R0 — 建立单 View 正确性基线

唯一目标：证明当前单 View 的 snapshot、graph、compose、present 和资源生命周期边界。

工作项：记录 GameRuntimeFrameOrchestrator → RenderFrameExtractor → RenderRuntime → pipeline → ViewCompose → DisplayCompose → Present 调用图；核对 graph build/execute 时序和 live-state 访问；增加单 Camera golden/trace；验证 resize、surface recreate、zero extent、关闭单窗；登记并修复影响基线的 GUI test target/include 问题。

验收：单 Camera golden 稳定；snapshot 在 graph build 前生成；trace 可区分 Camera graphics/UI/ViewCompose、DisplayCompose(surface)、Present(surface)；Forward/Deferred 当前 pass 顺序被记录且未被新抽象改写。

### R1 — 建立 SceneRenderRequest 与 Scene/View 契约

唯一目标：把 Scene 的身份、请求、所有权和 frame snapshot 边界定义清楚，允许同一帧存在多个互相隔离的 Scene request。

在 Framework/Render/Render3D/Common 增加 SceneId、SceneRenderRequest、SceneFrameSnapshot、RenderViewInput、RenderViewFamily 契约；增加不持有 Scene/ECS 的 SceneRenderScheduler 接口；GameRuntime、GameEditor、Material preview 各自提交 SceneRenderRequest。activeSceneProvider 只保留迁移期 compatibility adapter，不得成为新 View 的数据来源。

调度器在 UI 渲染之前工作：收集本帧请求，按 Scene 去重 extraction，再按 viewport 生成 culling、sort、shadow fitting 和 view-specific overlay。不得每个 View 重复遍历所属 Scene 的全部 ECS 资源，也不得跨 Scene 复用排序、shadow 或 entity-id 结果。

R1 字段分类不能按现有结构名整体搬迁，必须按语义拆分：

| 当前字段/数据 | 目标归属 | 迁移说明 |
| --- | --- | --- |
| RenderDrawItem.worldMatrix、mesh/material 引用、entity id | World snapshot candidate | 与 Camera 无关；先保留未排序 candidate |
| RenderDrawItem.sortKey、material/mesh bucket 顺序 | View preparation | 由 Camera distance 和 pipeline 策略决定，不能放进共享 snapshot |
| skinning palette 内容 | World snapshot | 一帧抽取一次；多个 View 只共享 palette，索引需保持稳定 |
| point/directional 原始光照参数 | World snapshot | 只保存灯光实体数据和稳定顺序 |
| directional cascade/shadow view-projection | View preparation | 当前由 camera view/projection、shadow settings 计算，不能随世界快照共享 |
| view/projection/viewProjection/cameraPos/viewportExtent/viewOwner | RenderViewInput | 当前已存在于 CameraFrameInput，迁移时保持值来源不变 |
| frame index / delta time | Frame/Scene/View metadata | 不参与场景资源抽取，不能通过 ECS 在 graph execute 阶段读取 |

因此 R1 的实现顺序固定为：先定义 SceneId/SceneRenderRequest 生命周期与 snapshot ownership；再为每个 Scene 引入未排序 candidates 和 View preparation；然后迁移 light/shadow；最后才替换旧 RenderFrameData adapter。禁止先把现有 RenderFrameData 机械拆成两个同构 struct，也禁止实现全局唯一 SceneFrameSnapshot。

验收：同一 Scene 的两个 View 共用一个 SceneFrameSnapshot；两个不同 Scene 产生两个 snapshot 且实体/灯光/资源生命周期不串；View A 的矩阵/extent 不修改 View B；UIOnly 不需要 Scene request；pipeline 不从 window/swapchain 反查矩阵或尺寸；单 View golden 不变。

### R2 — SceneRenderScheduler 编排离屏任务

唯一目标：在 UI 之前由 SceneRenderScheduler 聚合并执行本帧 Scene viewport 离屏任务，RenderRuntime 只消费不可变 SceneRenderPlan/SceneViewportTask。

将 RenderRuntime::FrameInput 扩展为 SceneRenderPlan/SceneViewportTask additive API，保留单 View adapter；Scheduler 负责 Scene snapshot 去重和任务排序，Runtime 负责 frame resources、pipeline record、ViewCompose 和输出句柄；两者都不创建 OS window、不 acquire/present；Forward/Deferred 只接收对应 Scene snapshot 和 RenderViewInput；每个 View 建立独立 output/format/extent 句柄，不用全局 ViewportStateService 隐式表示所有 View；当前保持一条 command buffer/submit，只有 trace 证明同步或资源压力后才讨论拆分。

验收：同一 Scene snapshot 渲染两个 Camera；两个 Scene 各自提交并渲染一个 viewport；一个 View 输出被两个 Surface display compose；一个 Surface display compose 多个 View；未提交 request 的 Scene 不产生 render task；关闭/最小化一个 Surface 不影响另一 Surface、其它 Scene request 和 View；GPU 资源在 submit 完成前存活。

### R3 — 独立 GUI2D pipeline 与 GameUI 按 View 复用

唯一目标：通过显式 SceneRenderRequest/View/compose 契约连接 Level Editor、Material Preview、UI Editor 与 Scene render，不复制 RenderRuntime，也不把 UI 挂回 Scene。

GUI Framework 保留 WidgetTree、UIFrameSnapshot、Render2D compose、GUIRenderSurface，以及每 native window 的 tree/snapshot/focus/input。GameRuntime/GameEditor 负责 GameUIHost[ViewId] 生命周期、input rect、focus/capture、UI scale、snapshot 与 ViewCompose 绑定；Level Editor、Material Preview 等功能各自提交 SceneRenderRequest。UI Editor 默认走 WidgetTree → UIFrameSnapshot → Render2D → PresentSurface；3D 预览必须显式提交对应 Scene request，不能偷用 active runtime Scene。

规则：每个可交互 Game View 默认独立 WidgetTree；同一 View 被多个 Surface 显示时复用 snapshot；不同 View 不共享 live widget tree；UIOnly 不提交 Scene request。

验收：UI Editor 无 world render 仍可运行；两个 Game View 的 input/focus/snapshot 不串扰；同一 View 在两个 Surface 显示时只生成一份 UI snapshot；录制期只读 immutable UIFrameSnapshot。

### R4 — GameUI 复用、性能与扩展性收口

唯一目标：用 trace/profile 决定优化是否进入稳定架构。候选包括 Scene extraction 复用、culling/sort cache、View 输出复用、compose 批量调度、submit 拆分和第三种 pipeline 接入方式。

不做：强制 BaseRenderPipeline；每窗复制 IRender/RenderRuntime；GUI Framework 依赖 Scene/ECS/Render3D；一个 WidgetTree 管多个 OS window；把相机矩阵、swapchain imageIndex、NativeWindow 句柄塞进 shader-facing 结构。

## 5. 与已有计划的边界

| 主题 | 所属计划 | 本计划处理方式 |
| --- | --- | --- |
| GUI invalidation / animation | gui-invalidation-architecture | R0 后可并行；动画 tick 必须在 snapshot 前 |
| GUI Dock / multi-OS-window | gui-multi-os-window-editor | 消费 IRenderSurfaceContext 与 per-window present，不重写 Dock |
| Forward/Deferred 去重 | render-pipeline-dedup-runtime-split | R1/R2 契约稳定后再推进，不抽万能 pipeline 基类 |
| GameEditor tab/session | gui-editor plans | 只要求 editor 提供 View/Surface 绑定 |

## 6. 验证与提交门禁

每轮先读根 AGENTS.md、plan/AGENTS.md、ya-build、render-arch；检查工作区和前一 checkpoint；复述唯一目标、边界、保留项、非目标；用 rg 核对真实符号和调用方。

每个 checkpoint 至少运行 git diff --check、受影响的 XMake build/test、单 View golden/trace，并在 R2 后加入双 View/双 Surface。生成文件只读，资源不能在录制中途重建，graph execute 不得查询 ECS/Scene/live WidgetTree。代码、测试、plan/progress 同一 checkpoint 提交。

提交格式：[render/view] ...、[render/runtime] ... 或 [gui/compose] ...。

## 7. 完成定义

- SceneRenderRequest、SceneFrameSnapshot、SceneRenderPlan 与 RenderViewInput 责任边界稳定；旧 WorldFrameSnapshot 兼容名被移除或限定为过渡 adapter。
- SceneRenderScheduler 能调度多个 Scene、多个 View，RenderRuntime 只录制 plan，Forward/Deferred 保留策略差异。
- Surface/swapchain/present 与 Camera/Viewport/Compose 不再混名或互相反查。
- GUI2D、GameUI、SceneViewport 依赖方向清楚。
- 至少有单 Scene 单 View、单 Scene 双 View、双 Scene 双 View、双 Surface、UI-only 五类可重复验证场景。
- 后续性能优化由 trace/profile 选择，而不是继续增加隐式状态。
