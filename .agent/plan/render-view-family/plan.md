# Render View Family 与 GUI/GameUI 渲染边界重构计划

> 建立日期：2026-09-12
> 状态：规划中，尚未开始本计划的代码 checkpoint

## 1. 主线选择

下一阶段选择 camera based render flow / ViewFamily 作为主线。当前多窗口 RHI、GUI surface/present 和单 Camera 输入契约已经存在，但 RenderRuntime 仍按单 View 编排。先稳定渲染对象边界，再推进多相机；不要同时重写 Dock、动画、GameUI 和 N Camera。

推荐顺序：

1. R0：正确性基线与可观测性。
2. R1：WorldFrameSnapshot / RenderViewInput 分离。
3. R2：RenderRuntime 按 ViewFamily 编排。
4. R3：独立 GUI2D pipeline 与 GameUI 按 View 复用。
5. R4：性能与扩展性收口。

GUI 动画属于 gui-invalidation-architecture 的独立小切片，可在 R0 完成后并行；GUI Dock/multi-OS-window 属于 gui-multi-os-window-editor，本计划只消费其 surface/present 合约。

## 2. 当前仓库事实

- Framework/Render/Render3D/Common/RenderFrameInputs.h 已有 CameraFrameInput、ViewComposeInput、DisplayComposeInput、PresentFrameInput。
- RenderRuntime 位于 Framework/Render/Render3D/RenderRuntime.*，已持有 PipelineCoordinator、PresentationGraphService、ViewportStateService，但仍按单 View 录制。
- Forward 与 Deferred 保留各自的 FrameGraphOrchestrator 和 pass topology；不抽强制 BaseRenderPipeline。
- Applications/GameRuntime/Utility/RenderFrameExtractor.* 从 ECS 抽取 RenderFrameData。
- RenderFrameData 当前同时承载 camera、lights、draw buckets、skinning，混合了场景级和 View 级语义。
- GUI live 事实源是 WidgetTree，录制只消费 immutable UIFrameSnapshot。
- IRenderSurfaceContext、swapchain、acquire/present 已与 Camera 离屏目标分开；acquire/present 由 host/present coordinator 负责。

## 3. 目标对象模型

    FrameCoordinator
      -> WorldFrameSnapshot                 一帧一次，场景级
      -> RenderViewFamily                   每个 View 一份 camera/view 输入
      -> RenderRuntime::recordViews(family)
           -> Forward / Deferred record(View)
      -> ViewCompose                        写 View 离屏 RT
      -> DisplayCompose(surface, image)    写 Surface swapchain image
      -> Present(surface)

WorldFrameSnapshot 只保存 transforms、mesh/material 引用、lights、animation/skinning 结果、resource handles 和稳定 id。它不保存唯一 Camera 的矩阵、View 排序、culling 或 viewport rect。

RenderViewInput 至少包含 ViewId、owner 计算的 view/projection/viewProjection/camera position、离屏 extent/scale、culling mask、postprocess/debug/overlay flags 和 WorldFrameSnapshot 引用。View 输出是离屏 color/depth/辅助 attachment，不拥有 OS window、swapchain，也不 acquire/present。

对象语义必须分开：Camera/View 是一次 world 渲染；ViewportWidget 是显示 View 输出的 GUI 矩形；Surface 是 OS window 的 present 目标；Swapchain 是 Surface 的显示缓冲；ViewCompose 写 View RT；DisplayCompose 把一个或多个 View/preview/chrome image 排到 Surface；Present 提交 Surface。一个 View 可被多个 Surface 显示，一个 Surface 可显示多个 View。

## 4. 分阶段实施

每个 checkpoint 只有一个可验收目标；代码、测试、progress.md 与计划变更同一提交。禁止用目录移动、空 registry、兼容 facade 或只写文档冒充完成。

### R0 — 建立单 View 正确性基线

唯一目标：证明当前单 View 的 snapshot、graph、compose、present 和资源生命周期边界。

工作项：记录 GameRuntimeFrameOrchestrator → RenderFrameExtractor → RenderRuntime → pipeline → ViewCompose → DisplayCompose → Present 调用图；核对 graph build/execute 时序和 live-state 访问；增加单 Camera golden/trace；验证 resize、surface recreate、zero extent、关闭单窗；登记并修复影响基线的 GUI test target/include 问题。

验收：单 Camera golden 稳定；snapshot 在 graph build 前生成；trace 可区分 Camera graphics/UI/ViewCompose、DisplayCompose(surface)、Present(surface)；Forward/Deferred 当前 pass 顺序被记录且未被新抽象改写。

### R1 — 分离场景快照与 View 输入

唯一目标：场景抽取只做一次，View-specific 数据不污染共享场景快照。

在 Framework/Render/Render3D/Common 增加 WorldFrameSnapshot、RenderViewInput、RenderViewFamily 契约；在 Applications/GameRuntime/Utility 将 RenderFrameExtractor 的 world extraction 与 view preparation 分成稳定块；迁移 Forward、Deferred、shadow、entity-id、debug overlay 消费者；旧 RenderFrameData 只保留短期 adapter，并记录删除条件。

View 级工作包括 culling、sort、shadow fitting、view-specific overlay；不得每个 View 重复遍历并抽取全部 ECS 资源。

R1 字段分类不能按现有结构名整体搬迁，必须按语义拆分：

| 当前字段/数据 | 目标归属 | 迁移说明 |
| --- | --- | --- |
| RenderDrawItem.worldMatrix、mesh/material 引用、entity id | World snapshot candidate | 与 Camera 无关；先保留未排序 candidate |
| RenderDrawItem.sortKey、material/mesh bucket 顺序 | View preparation | 由 Camera distance 和 pipeline 策略决定，不能放进共享 snapshot |
| skinning palette 内容 | World snapshot | 一帧抽取一次；多个 View 只共享 palette，索引需保持稳定 |
| point/directional 原始光照参数 | World snapshot | 只保存灯光实体数据和稳定顺序 |
| directional cascade/shadow view-projection | View preparation | 当前由 camera view/projection、shadow settings 计算，不能随世界快照共享 |
| view/projection/viewProjection/cameraPos/viewportExtent/viewOwner | RenderViewInput | 当前已存在于 CameraFrameInput，迁移时保持值来源不变 |
| frame index / delta time | Frame/View metadata | 不参与场景资源抽取，不能通过 ECS 在 graph execute 阶段读取 |

因此 R1 的实现顺序固定为：先引入未排序 world candidates 和 view preparation 的内部契约；再迁移 light/shadow；最后才替换旧 RenderFrameData adapter。禁止先把现有 RenderFrameData 机械拆成两个同构 struct。

验收：同一帧两个 View 共用一个 world snapshot；View A 的矩阵/extent 不修改 View B；pipeline 不从 window/swapchain 反查矩阵或尺寸；单 View golden 不变。

### R2 — RenderRuntime 编排 RenderViewFamily

唯一目标：一个 RenderRuntime 能记录多个 View，同时保持 PresentSurface 生命周期独立。

将 RenderRuntime::FrameInput 扩展为 family 级 additive API，保留单 View adapter；Runtime 只协调 frame resources、View record、ViewCompose 和输出句柄，不创建 OS window、不 acquire/present；Forward/Deferred 只接收统一 View 输入和 immutable world snapshot；每个 View 建立独立 output/format/extent 句柄，不用全局 ViewportStateService 隐式表示所有 View；当前保持一条 command buffer/submit，只有 trace 证明同步或资源压力后才讨论拆分。

验收：同一 snapshot 渲染两个 Camera；一个 View 输出被两个 Surface display compose；一个 Surface display compose 两个 View；关闭/最小化一个 Surface 不影响另一 Surface 和 View；GPU 资源在 submit 完成前存活。

### R3 — 独立 GUI2D pipeline 与 GameUI 按 View 复用

唯一目标：通过显式 View/compose 契约连接 UI Editor、GameUI、world render，不复制 RenderRuntime，也不把 UI 挂回 Scene。

GUI Framework 保留 WidgetTree、UIFrameSnapshot、Render2D compose、GUIRenderSurface，以及每 native window 的 tree/snapshot/focus/input。GameRuntime/GameEditor 负责 GameUIHost[ViewId] 生命周期、input rect、focus/capture、UI scale、snapshot 与 ViewCompose 绑定。UI Editor 默认走 WidgetTree → UIFrameSnapshot → Render2D → PresentSurface；3D 预览必须显式嵌入 WorldView。

规则：每个可交互 Game View 默认独立 WidgetTree；同一 View 被多个 Surface 显示时复用 snapshot；不同 View 不共享 live widget tree。

验收：UI Editor 无 world render 仍可运行；两个 Game View 的 input/focus/snapshot 不串扰；同一 View 在两个 Surface 显示时只生成一份 UI snapshot；录制期只读 immutable UIFrameSnapshot。

### R4 — 性能、诊断与扩展性收口

唯一目标：用 trace/profile 决定优化是否进入稳定架构。候选包括 world extraction 复用、culling/sort cache、View 输出复用、compose 批量调度、submit 拆分和第三种 pipeline 接入方式。

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

- WorldFrameSnapshot 与 RenderViewInput 责任边界稳定。
- RenderRuntime 能调度多个 View，Forward/Deferred 保留策略差异。
- Surface/swapchain/present 与 Camera/Viewport/Compose 不再混名或互相反查。
- GUI2D、GameUI、WorldView 依赖方向清楚。
- 至少有单 View、双 View、双 Surface、UI-only 四类可重复验证场景。
- 后续性能优化由 trace/profile 选择，而不是继续增加隐式状态。
