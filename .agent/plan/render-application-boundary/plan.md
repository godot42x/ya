# Framework 只放可复用管线，应用拥有排布与状态

> 建立日期：2026-09-22
> 状态：AB1 / AB2 已落地；AB7-step1 已落地（整帧录制顺序搬到应用侧）；AB3-step2、AB4-step2/3、
> AB5 / AB6、AB7-step2 待做。

## 1. 原则

> **Framework/Render 只放可以复用的管线；这一帧渲染哪些 View、它们属于哪个 Scene、
> 输出到哪里、什么时候 acquire / submit / present，都是具体应用（GameRuntime /
> GameEditor）的事。**

精确一点的读法：

> Framework/Render 负责「如何渲染一个已经准备好的 View」；
> 应用负责「这一帧要渲染哪些 View」。

不能把公共渲染机制也删出 Framework，否则 Forward/Deferred、RenderGraph、
View/Display compose 会被两个应用各写一遍。

## 2. 边界表

| 层 | 拥有 | 不得拥有 |
| --- | --- | --- |
| RHI (`Framework/RHI`) | device / queue / command buffer / swapchain / surface / acquire-present 抽象 | 应用策略、Scene/ECS |
| `Framework/Render` | Forward / Deferred / PostProcess 管线、RenderGraph、View/Display compose **pass**、PSO 与 descriptor 缓存、immutable 输入契约（`SceneViewDesc` / `RenderFrameData` 等） | 当前 active Scene、当前主 View、哪个 Tab 可见、哪个窗口要 present、是否 Editor/Runtime、Game UI 是否叠加、是否截图/RenderDoc |
| `Applications/GameRuntime` | app loop、本 tick 的 View 声明与收集、Scene snapshot 抽取、View preparation、frame-flight / submission 状态、Surface 与 View 的排布、GameUI 绑定、acquire → record → submit → present 编排 | 管线内部算法 |
| `Applications/GameEditor` | 编辑器声明哪些 View、面板 rect、编辑器 overlay / gizmo、工作区呈现策略 | 世界管线实现 |
| `Framework/GUI` | WidgetTree tick、`UIFrameSnapshot`、input/focus、window manager、控件绘制 | Scene / ECS / Render3D |

`Framework/Render` 可以提供一个较小的执行接口（`IRenderPipeline::recordFamily`），
但**不再制造一个知道整帧排布的公开 `Renderer`**——那等于把应用编排搬回框架里。
这与 `render-view-family` 的 4.0.3 原方向（合并成公开 `Renderer`）有出入，取舍理由见 §5。

## 3. 判定口径

一个文件/类型属于哪一层，只问三个问题：

1. 它是否只依赖 GPU 抽象与 immutable 输入？（是 → Framework/Render）
2. 它是否要知道「当前应用有哪些 Scene / 哪些 View / 哪个窗口」？（是 → 应用层）
3. 它是否只是为某个面板/工具服务的查询面？（是 → 消费方那一层，不是 renderer）

## 3.1 目标语义：Window、Surface、View、Pipeline 各自独立

这四者不能形成继承链，也不能用“primary/host/current”让其中一个替另一个做身份。
它们只在应用构造的本 tick render plan 中通过稳定 ID 发生关系。

| 语义 | 唯一事实 / owner | 它不表示什么 | 稳定关联 |
| --- | --- | --- | --- |
| `Window` / `WindowSession` | GUI host；原生窗口、WidgetTree、事件与焦点生命周期 | swapchain、相机、渲染分辨率 | `WindowId`；可关联 0 或 1 个呈现 surface（headless 合法） |
| `Surface` | RHI；一个 window 的呈现能力与 swapchain 生命周期 | Scene、View、WidgetTree、device frame | `SurfaceId`；通常由 app 把 `WindowId → SurfaceId` 绑定 |
| `SurfaceImage` | 一次 surface acquire 的结果；image index / acquire token | 逻辑帧号、View 输出身份 | `(SurfaceId, acquire serial/image index)`；只在本次提交有效 |
| `View` | View producer；Scene、camera、projection、输出尺寸、features 与 temporal history key | Window、swapchain、GUI 面板、present 顺序 | owner-scoped `SceneViewKey` / `SceneViewId` |
| `SceneSnapshot` | 应用 extraction；按 `(Scene, revision)` 去重 | camera、surface、窗口顺序 | View request 引用共享快照 |
| `RenderPipeline` | Framework/Render；用 immutable view input 执行 Forward/Deferred 等策略 | active window、surface、swapchain、当前 View 的隐式全局状态 | 应用 render configuration 选择策略；View request 可引用策略，不把 pipeline 绑定到 window |
| `DisplayComposition` | 应用/GUI host；为一个 surface 声明有序图层（View output、UI snapshot、chrome、clear/overlay） | 新相机、新 Scene、渲染管线选择 | `SurfaceId` + `ViewId` / UI snapshot 引用 |
| `FrameRecording` | Render recording；本次生成的 command work 与资源租约，未必已经提交 | queue submission、present、app tick | recording token；资源 lease 至 completion |
| `SubmissionGraph` | 应用给出的 work 依赖 + RHI backend 执行的 queue submit/sync | 一个 surface 或一个 View | graph/batch serial；command buffer 只能进入一次 submit |

目标数据关系：

```text
GUI host: WindowSession[] ──WindowId──┐
                                     ├─ app-owned binding: WindowId ↔ SurfaceId
RHI device: SurfaceContext[] ─SurfaceId┘

app tick → SceneSnapshot[(Scene, revision)] → ViewRequest[ViewId]
                                      └────→ DisplayComposition[SurfaceId]
ViewRequest → selected RenderPipeline → ViewOutput[ViewId]
ViewOutput[] → DisplayComposition + acquired SurfaceImage → compose command work
           → SubmissionGraph → present each acquired SurfaceId on a compatible queue
```

应用入口应能读完唯一的 orchestration 顺序；但 acquire 不应成为整帧 View 录制的门槛：

```text
poll OS events once; dispatch to all WindowSessions
  → tick each live WidgetTree and build its immutable UI snapshot
  → collect Scene/View requests and Surface display policies
  → deduplicate Scene extraction; prepare and record requested offscreen Views
  → acquire each eligible Surface before recording its display composition
  → compose each acquired Surface's ordered layers; failed/minimized surfaces skip only this step
  → build/submit the device queue dependency graph with acquire waits and resource leases
  → present each acquired Surface after its producing submission is queued
  → retire closed WindowSessions/Surfaces only after GPU and present use is complete
```

Surface acquire 只必须发生在依赖其 swapchain image 的 display-compose recording 之前；它不是
offscreen View recording 的前置条件。所有 OS event 由一个 host pump 收集一次，再按 WindowId 分发，
不能让每个 WindowSession 各自 poll 全局事件队列。acquire 失败、最小化、swapchain out-of-date 只取消
该 Surface 本 tick 的 display/present，不丢弃仍被请求的 offscreen View work。device-lost 等 device-wide
错误则使本 tick 全体提交失效，不能伪装成单 Surface 跳过。各队列的 semaphore/fence 关联以实际
submission dependency graph 为准，不能从 window 数量推导 submission 数量。

硬边界：

- Window 的 logical client extent、drawable/framebuffer pixel extent 与 DPI scale 属于 WindowSession；
  Surface recreate 更新 swapchain extent / format / color space / present sync；View 的 target extent /
  render scale 属于 View request。它们由明确的 host/producer 适配规则关联，但不能互相冒充。窗口 DPI
  变化应更新该窗口的 WidgetTree/snapshot 映射，不应污染其他窗口的 View target。
- 一个 View 可以不呈现、呈现到一个或多个 Surface；一个 Surface 的 composition 可以组合多个 View
  和多个 UI 层。View 不再带 `composeOntoViewId`，也没有 `displayRootTask()` / “第一个 root”语义。
- View 的 3D 管线在 offscreen target 上运行；2D GUI 编辑器可以只声明 GUI display layer，不必制造
  Scene/View。Game UI 是 GUI snapshot 的一种 layer，不能成为全局唯一的 `FramePacket::uiFrameSnapshot`。
- SceneSnapshot 按 `(SceneId, contentRevision)` 每 tick 至多抽取一次；revision 必须来自 owner 或
  等价的 frame-local generation，当前 `sceneRevision = 0` 占位不能作为跨 View 去重键。每个 View 独立提供
  camera、extent、render scale 与 temporal
  history。多个 surface 引用同一 View 时复用其 published output；多个不同 View 可复用同一 SceneSnapshot。
- 现有 `RenderFrameData` 同时装了 SceneSnapshot / per-view draw buckets 与 camera，也装了
  frameIndex / deltaTime / timeSeconds。闭环时拆为共享 Scene snapshot、per-view prepared input、
  tick constants 三种数据；不得仅把整块改名为 `PreparedView` 后继续混装。
- App tick、device flight slot、queue submission serial、各 Surface 的 acquire/image index、
  View history serial 是不同身份/计数。一个 tick 可无 submission、一个或多个 queue submission；
  多个 Surface 可共享同一个 graphics submission；任何计数都不得充当另一个的替身。flight slot 只有在
  对应 GPU completion fence 已满足后才可复用；Surface image index 只在这次 acquired token 的
  submit/present 生命周期内有效；View history 按 View identity + generation 独立推进。
- Surface 的 acquire 与 present 是 per-surface；queue submission/completion 是 device/queue 工作。
  一个 command buffer 只能提交一次；多 Surface 共享 recording 时，应由 submission 明确等待所有关联的
  image-available sync、为各 acquired image 产生可等待的完成 sync，再分别调用各 Surface 的 present。
  若 queue family 需要 ownership transfer，也必须成为 graph dependency，而非 Surface::end 的隐藏副作用。
  不能让 `SurfaceContext::end()` 同时暗含任意 command submit 与 present，也不能假设一个 window 对应
  一次 GPU submit。
- Pipeline 是“如何处理一份 View 输入”的执行策略；应用 render configuration 决定采用的策略，View request
  可明确引用该配置。当前产品可以对所有 View 选同一 Forward/Deferred 策略，架构不把策略绑定到 window。
  pipeline 不 `begin/end` surface，也不控制 acquire、
  present 或窗口循环。graph/pass 的执行状态属于本次 recording/submission，pipeline 对象只保留明确可复用
  的 immutable config / device cache，不留“上次 active View”的隐式状态。

当前代码中必须随闭环删除的混合语义：`SceneViewDesc::composeOntoViewId`、
`SceneRenderPlan::displayRootTask()` 的首项回退、`HostViewportView` 单一 View 缓存、
`RenderFramePlan::present` 单值、`FramePacket::uiFrameSnapshot` 单值，以及 RHI / GUI host 的
primary surface/window 所有权。保留同 Scene snapshot 去重、owner-scoped View key、
`ViewTargetStore` 生命周期和 app-owned record 顺序。

### 3.2 当前计划遗漏的生命周期与隐藏全局状态

下列不是可选“以后优化”，而是多窗口/多 View 闭环的正确性条件。实现时要纳入对应 checkpoint 的
验收，不额外制造只改名的批次。

| 事实 | 当前耦合/证据 | 目标 owner 与收口要求 |
| --- | --- | --- |
| 窗口身份与资源代次 | GUI 用 `GUIWindowId`，RHI/render 传裸 `IRenderSurfaceContext*`；present target/cache 可能跨 surface 重建 | WindowId、SurfaceId、ViewId 各自类型化；surface/window recreate 或 ID reuse 带 generation。异步读回、诊断、catalog、输出查询都带身份，禁止缓存裸指针作为永久 key |
| acquire 事务与 submission | `IRenderSurfaceContext::begin/end` 将 acquire 与 submit+present 包在 surface；`RenderSubmission::finish()` 只结束 recording，却保存单一 `_hostSurface`；`RecordedFrame` 仅一个 command buffer | acquired image 是一次性 token；recording 产出 command work + resource leases；device 建立一次性 submission dependency graph；surface 只 acquire/present；失败路径要消费/取消 acquired token，不能遗留 semaphore/fence 处于不可复用状态 |
| partial failure | 当前 `RecordedFrame` 无效时向已 acquire surface 提交空命令；多个 Surface 尚无 acquire 集合模型 | 明确 acquire failed / out-of-date / minimized / record failed / submit failed / device lost 的逐级处理。只做 per-surface skip 的错误不得吞成 device-wide 错误；acquire 成功后任何早退必须仍合法地消费 image-available semaphore 并释放 image |
| 帧内与跨帧状态 | `RenderDeviceState` 聚合 `_submissions`、`ViewTargetStore`、pipeline controller、processor/resource caches、offscreen jobs、diagnostics、surface presentation | 按 device lifetime cache、tick-local input/plan、View lifetime/history、Surface lifetime、flight-slot resources、submission lifetime leases 分类；只有跨帧不变量需要常驻。`beginSubmission()` 语义应改为一次 recording/family 明确调用，不可用 pipeline 的隐式 current frame |
| Scene 身份与“当前世界” | `GameRuntimeTickOrchestrator` / `EditorViewProducer` 仍从 `SceneManager::getActiveScene()` / `collectContext.activeScene` 推导请求；编辑器注释和自动化接口仍使用 world/viewport 词汇 | `SceneId` 必须由 View producer 明确提交；SceneManager 的 active scene 只是产品选择，不能成为 renderer 或 scheduler 的隐式查询。Level scene、PIE scene、材质/缩略图 preview scene、独立 editor document 都可各自提交 Scene-backed request；没有 Scene 的 preview 走 standalone offscreen request |
| Pipeline 活状态 | `PipelineCoordinator` 保存 active/pending strategy，Forward/Deferred 仍有 submission/view begin hooks | active 策略及 pending settings 属应用/产品配置；pipeline 实例只持共享不可变 recipe/device cache；每个 call 显式接收 `PreparedView`、Scene-family data、recording context 并返回输出。safe-point reload 是应用 orchestration 的显式步骤 |
| 尺寸、色彩空间 | 当前每 surface `SurfaceWritePass` 知 format，但 Display layer 没完整列 color space/alpha/HDR/tone-map contract；`FramePacket::renderScale` 全局 | Window logical/drawable/DPI、Surface extent/format/color space、View target extent/scale 分别持有；display layer 显式给采样编码/alpha/blend，Surface policy 选 tone-map/encoding。移除全局 `FramePacket::renderScale`，缩放归 View |
| GUI 全局状态 | `GUIApp` 的 primary 与 manager extras 分治；每 session 有 tree/snapshot；`FontManager::setActiveDpiScale()` 是进程级可变值 | 一个 OS event pump + session registry；每 tree 独立 tick/snapshot/input/focus/DPI。字体 atlas/字体实例的 DPI 身份需是显式 per-window/per-scale 资源或明确的共享逻辑像素策略，不能依赖“最后 tick 的窗口”全局 active DPI |
| 退出 / 重建 | session close 会 wait surface flight；surface cache `SurfacePresentation` 随 renderer shutdown 清理 | 先停止产生新 plan，再撤销 session 的 View/UI 引用，等待相关 submission 与 present completion，释放 swapchain-dependent presentation resources，销毁 Surface，再销毁 native window；device shutdown 最后。resize/recreate 同样在该 Surface 的 GPU 使用完成后替换 generation |
| GUI-only 与异步 offscreen | GUI Framework 不依赖 Render3D；material/thumbnail/preview 可没有 Scene | 将 RenderRequest 分为 Scene-backed View 与 standalone image/offscreen task；UI-only composition 是独立 display layer，不伪造空 Scene/View。offscreen task 的输出/同步进入同一 submission graph，但不被窗口 present 必需性门控 |
| 模块扩展行为 | `RenderFramePlan::recordExtensions` 保存 `IFrameRecordExtensions*`，extension 接口从 data plan 回调行为 | plan 只含数据；GameEditor/GameUI 的 overlay、capture 和 chrome 阶段由应用 loop 在明确阶段调用，并将录制结果加入同一 recording graph。Framework 不持有应用对象指针，不靠 callback 把顺序藏回 renderer |
| 输出查询与截图 | 自动化和编辑器通过 `getHostViewportOutput()`、`getHostViewportView()` 等单宿主 facade 取图；截图同时有 viewport/presentation/offscreen 三类目标 | 所有输出查询显式带 `ViewId`、`SurfaceId` 或 standalone target id，以及 frame/generation；“host viewport”只能是 GameEditor 的产品 binding，不能成为 Render/GUI 的公共身份。截图服务消费已发布 immutable output，不触发额外渲染或隐式 acquire |

这里的 “一次 queue submission”不是架构硬限制；硬约束是每份 command buffer 只提交一次，跨 queue 的 wait/signal、ownership transfer、resource keepalive 和 completion 必须显式建图。先用当前单 graphics queue 完成一个 device submission + 多 surface presents；只有 RHI backend 确实要求多 queue 时才拆多个 submission node，不为抽象未来并行而先建复杂调度器。

### 3.3 `RenderDeviceState` 现有成员的生命周期盘点

这类成员不能一起“下沉到 Renderer”或一起搬到 `RuntimeRenderContext`。迁移时按下面的生命周期分组，
每组只允许有一个 owner：

| 生命周期 | 当前代表 | 目标处理 |
| --- | --- | --- |
| device lifetime | `IRender*`、shader storage、pipeline recipes/PSO、resource factory、共享 descriptor/layout cache | 留在 Framework/Render 或 RHI device owner；只提供线程安全/显式的 resource API |
| scene-derived lifetime | `EnvironmentLightingProcessor`、`TerrainProcessor`、`GameplayResourceBinding`、按 Scene 的 derived resource cache | 由应用在 tick 前提供 Scene 集合；processor 只按 `(SceneId, revision)` 维护派生缓存，不读取 active/current Scene |
| surface lifetime | `SurfacePresentation`、swapchain imported images、SurfaceWritePass、Surface format/color-space policy | 由 RHI surface registry / display composer 按 `SurfaceId + generation` 持有；Surface 销毁先等待所有引用它的 submission |
| View lifetime | `ViewTargetStore`、published `RenderViewOutput`、View history/temporal attachments、ViewResourceTable | 由 View identity + target generation 管理；View 输出不绑定窗口，多个 Surface 只引用同一 published output |
| flight/submission lifetime | command buffers、upload arena、descriptor lanes、`RenderSubmission`/未来 `FrameRecording`、retained resources | 每个 flight slot 只在 GPU completion 后复用；recording seal、queue submit、completion、present 分开表达 |
| tick-local | `SceneRenderScheduler`、`RenderFramePlan`、SceneSnapshotSet、ViewRequest、SurfaceDisplayPlan、acquire tokens | 由应用 tick 入口创建/封存；不写回长寿命 host state，不被 renderer 保存为 current plan |
| application policy | `HostRenderSettings`、GameUI/Editor UI snapshot、host viewport binding、module callbacks、截图请求 | 只在 GameRuntime/GameEditor/GUI host；Framework 只消费 immutable layer/input，不反向查询应用对象 |

任何同时跨越两行的类型都必须拆成“长寿命 owner + 本次调用的 immutable input”，不能以一个
`RenderDeviceState` 字段继续承载两种生命周期。

## 4. Checkpoints

### 4.0 执行依赖（本次复核后收敛）

后续实现按下面的依赖推进，避免先改数据结构再把旧的窗口等级和提交语义搬进新结构：

1. **契约冻结**：先引入/统一 `WindowId + generation`、`SurfaceId + generation`、`ViewId`、
   `SceneId + contentRevision`、`FrameRecording` / acquire token 的身份规则；同时把 logical extent、
   drawable extent、Surface extent、View target extent 的来源写清。此阶段只清理类型和不变量，不宣称多窗口已完成。
2. **RHI presentation backend**（**step 1 已落地 2026-09-24，见 AB4-2b**）：设备 bootstrap 接收
   startup surface requirements 集合；renderer 的 primary surface 所有权与 `getPrimarySurfaceContext()`
   /`primaryWindow()` 已删除，全部 surface 走一个按 `SurfaceId + generation` 的注册表。
   未完成部分：把 acquire、queue submission、present、completion 拆成可表达失败状态的接口
   （AB4-2f）；以及“一个 present family 覆盖不了全部窗口”时的多 family queue plan（今天明确拒绝）。
3. **GUI host session**：把 GUIApp 的首窗口和 extras 纳入同一 session registry、event pump、tick、
   snapshot、close/recreate 生命周期；移除 `presentGuiSnapshot` 的独立提交循环。GUI Framework 只输出
   每个 session 的 UI snapshot 和 display layer，不知道 Scene。
4. **应用 RenderPlan**：一次 tick 先收集所有 Scene/View/offscreen requests，再按 Scene revision 去重，
   记录每个 View 一次，最后对 acquired Surface 做 display compose；无 Surface 仍允许发布 offscreen output。
   这一步同时替换单值 `present`、`uiFrameSnapshot`、`displayRootTask()` 和 `recordExtensions` 行为回调。
5. **查询与清理**：截图、debug catalog、编辑器 host binding 全部改成显式 View/Surface/target 身份；
   最后才做 `RenderSubmission → FrameRecording`、`PresentationGraphService → DisplayComposer` 等命名/目录
   收口。若前一步仍存在旧身份，禁止用改名掩盖。
6. **规范同步**：闭环实现后同步 `.agent/skills/render-arch/SKILL.md` 与活跃的
   `render-view-family` 计划，把本计划确定的 Window/Surface/View/Frame/Submission 语义写成唯一当前规范；
   archive 文档保留历史，但必须标明其方案已冻结，不能继续作为执行依据。

每个步骤的验收必须同时覆盖：单窗口、双窗口、无 Surface 的 offscreen、Surface minimized/resize、
部分 acquire 失败、record/submit 失败后的下一 tick，以及关闭/重建时 GPU 资源保活。

### AB1 — 应用拥有本帧的 view 排布（已落地）

唯一目标：让「本帧渲染哪些 View」这件事的生命周期出现在读代码的地方。

- `SceneRenderScheduler` 从 `AppRenderState` 的长寿命字段变成 `tickRender` 的局部对象：
  `beginTick → submit → seal` 描述的是一个 tick 的排布，因此它的生命周期就是这个作用域。
  此前它藏在 host state 里，读者要跨文件才能确认「谁每帧清理它」。
- `declareViews` 因此收 `SceneRenderScheduler&`：scheduler 是 tick 自己的排布，
  而 `declareViews` 是写它的两个步骤之一。
- 删掉 `SceneSchedulerGuard`（局部对象析构即 `clearTick` 的语义）。

### AB2 — 应用的渲染排布有自己的位置（已落地）

唯一目标：`GameRuntime/Lifecycle/` 不再兼收渲染排布。

```
Applications/GameRuntime/
├── App.cpp / AppRenderServices.cpp / AppSceneServices.cpp ...   # 组合根与对外 facade
├── Lifecycle/      # app 生命周期：AppLifecycle / AppEventRouter / FPSCtrl / AppAutomation / HostSdlEventSource / GameRuntimeTickOrchestrator
├── Render/         # 本帧的排布：RenderFrameExtractor / HostSceneExtract / RuntimeGameViewProducer / SceneCameraQuery
├── GUI/GameUI/ Automation/ Bootstrap/ Settings/
```

纯搬迁，无行为变化；公开路径随之变为 `GameRuntime/Render/<Name>.h`。

### AB3 — 编辑器通过应用读渲染器，而不是通过 device 内部（step 1 已落地）

唯一目标：`RenderDeviceState` 的公开面上不再有「为编辑器面板服务」的查询。

唯一目标：**GameEditor 不再认识 `RenderDeviceState`**，也不再用 `dynamic_cast`
去问渲染器「你是什么管线」。

AB3-step1（已落地）：

- **策略身份、设置、编译后的图变成 typed 契约**，不再是「先 downcast 到 concrete
  pipeline 再读」。`IRenderPipeline` 增加 `kind()`（`ERenderPipelineKind`）、
  `getLastFrameGraphTopology()`，以及 `IRenderPipelineSettings` facet 的
  `resolveSettings()` / `requestSettings()`。`DeferredRenderPipeline::SettingsSnapshot`
  上移为 `Render3D/Common/RenderPipelineSettings.h` 的 `RenderPipelineSettings`（带 `kind`），
  Forward 也实现同一 facet：读 `shadow` / `postProcessing`，原样携带只属于 deferred 的块，
  由 `kind` 说明。`PipelineCoordinator::ERenderPipeline` 改为 `ERenderPipelineKind` 的别名，
  `toString(kind)` 只有一处。
- **`AppRenderServices` 成为应用侧唯一缝**：`getRenderPipelineKind` / `getPendingRenderPipelineKind` /
  `setPendingRenderPipelineKind` / `requestRenderPipelineReload` / `getRenderPipelineSettings` /
  `setRenderPipelineSettings` / `getFrameGraphTopology` / `getViewExtent` / `getViewDepthFormat` /
  `getViewOutput` / `buildViewportSnapshot` / `buildRenderTargetCatalog` / `getDebugRenderSystem` /
  `getDiagnosticsService` / `hasRenderer`。新增 `RenderDeviceState::resolveActivePipelineKind/Settings`、
  `requestActivePipelineSettings`、`getActiveFrameGraphTopology`、`getViewDepthFormat` 作为转发落点。
- **删除没有消费者的公开方法**：`buildPipelineDebugOutputCatalog` /
  `getDeferredPipelineDebugViews` 只有 `makeViewportDebugCatalogInput` 一个调用方，改为 private；
  `AppRenderServices::getRenderPipeline()`（返回 `IRenderPipeline*`，零调用方）删除。
- **验收证据**：`grep RenderDeviceState Engine/Source/Applications/GameEditor` 为空；
  `grep 'dynamic_cast<.*RenderPipeline' GameEditor` 为空；`getDeviceState()` 的剩余调用者全部在
  GameRuntime（app 自己）内。
- 顺带：`RuntimeDebugPrimitivesSection` / `RuntimeRenderTargetSection` 的 `.cpp` 与 `.h` 此前被压成
  单行（自 `d9de4739` 起），这轮必须改它们，因此一并展开成正常可读形式（无行为变化）。

AB3-step2（待做）：剩下的三个仍是 renderer 自己的事实，只是目前以服务引用形式穿过 facade：
`DebugRenderSystem&`、`RenderDiagnosticsService&`、`buildRenderTargetCatalog` /
`buildViewportSnapshot` 的返回体。收口方向是 typed command（`setRenderDocCaptureEnabled` 等）
与「由数据构造 catalog」的纯函数，而不是继续扩大 facade 的引用面。

### AB4 — presentation 只搬运，present 由应用编排（进行中）

唯一目标：presentation 不再属于「主窗口」，present 的编排由应用持有。

> **架构复核修正（2026-09-24）：AB4-step1 与 AB4-2a 只移走了帧推进、诊断查询和 per-surface 策略，
> 没有移除单窗口等级。** `RenderCreateInfo` 仍带 `nativeWindow + swapchainCI`；Vulkan 用它创建
> `_surface`、据它选择 present queue，并把它单独存进 `_primarySurface`。`IRender` 仍提供
> `getPrimarySurfaceContext()/primaryWindow()`；GUIApp 也把 `_primaryWindow` 和 extra-window manager
> 分开持有、分开 tick/present。因此“bootstrap fact, not a rank”只是注释约束，底层对象模型仍是
> 一主多次。AB4 后续不得以 `getHostSurface()` 作为永久抽象把这个模型保留下来。
>
> **目标对象模型：** 一个应用创建一个 render device；每个可呈现的 OS window 都由同一条显式
> `createSurfaceContext(window, swapchainDesc)` 注册路径获得 surface context。Renderer 不拥有
> “创建 device 时附带的那个窗口”，也不通过无参 getter 暴露默认 surface。GUI host 的全部窗口
> 是同级 session：统一注册、事件路由、tick、compose 与 present。应用可以有“启动时打开的窗口”或
> “游戏内容默认显示的 display root”等产品策略，但这些策略由 app 持有明确的 window/surface ID，
> 不成为 RHI 的窗口等级。
>
> **Vulkan 前置设计必须先闭环：** present support 是 physical device + queue family + `VkSurfaceKHR`
> 的关系，logical device 创建时必须声明启用的 queue families。初次创建设备前，由应用一次性提供所有
> 已知 startup window 的 surface requirements；backend 创建这些 VkSurface、按整个集合选择 physical
> device 和 queue plan，再创建 logical device。队列选择优先让 graphics queue 支持全部初始 surfaces；
> 若做不到，启用满足初始集合所需的最小额外 present queue families。运行时 tear-off 创建的 surface
> 只能使用已启用且经查询支持它的 family；不支持时，明确拒绝该 surface 并报告能力限制。本阶段不
> “启用每个 queue family”也不隐式重建 device；若产品要求任意运行时 surface 必成功，再单独设计
> device rebuild/migration。初始窗口没有主次，但 device bootstrap 必须知道它们的集合。Surface context
> 创建、swapchain 创建及销毁顺序需和 device teardown 一起验收。

AB4-step1（已落地）：**present target 变成 per-surface**。

- 新增 `SurfacePresentation`：一个 OS 窗口的 present 目标，拥有该 surface 的导入图 + 
  每张图的 executor，以及**按该 surface 的 swapchain format** 构建的 `SurfaceWritePass`。
- `RenderDeviceState` 由「一个 `_presentationGraphService` + 一个 `_surfaceWritePass`」
  改为 `_surfacePresentations` 表：按 `plan.present.surface` 惰性创建、随 device 销毁。
  第二个窗口是这张表里的第二项，而不是第二个 renderer。
- `initPresentationResources` → `initSurfacePresentations`：init 期不再做任何 GPU 工作，
  也不再有「主 surface 特权」；这里只登记 teardown，保证每个 surface 的图与 write pass 
  都在 render backend 之前销毁。
- `getPresentationImageShared()` 收 `IRenderSurfaceContext&`：图像只对「具名的窗口」有意义，
  匿名 getter 在多 surface 下只能挑一个再叫它 current。查询是非创建的（没呈现过的 surface 
  返回空，不为回答查询而建图）。
- `record()` 在**录制前**解析/构建本 surface 的 present target（与 `prepareComposePipelines` 
  同处 safe point）。

AB4-step2（待做）不是一个改动，而是以下有依赖关系的闭环；不得先把 `RenderFramePlan` 改成 vector
就宣称支持多窗口，因为当前 RHI 和 GUI host 仍把第一个窗口单独持有。

1. **AB4-2b：RHI surface 注册不分首窗/后续窗（step 1 已落地 2026-09-24）。**

   已落地：`RenderCreateInfo.nativeWindow + swapchainCI` → `startupSurfaces`（设备被创建时必须能
   呈现的窗口集合）；`IRender::getPrimarySurfaceContext()` / `primaryWindow()` 删除；Vulkan 的
   单值 `_surface`、`_primarySurface`、`createPrimarySurface()`、`primaryVulkanSwapchain()` 删除。
   设备持有 `SurfaceId{index, generation}` 注册表：`createSurfaceContext(window, desc)` 是
   startup 窗口与后续窗口唯一的注册路径，`findSurface(id)` / `findSurface(window)` /
   `findSurfaceId(window)` / `destroySurfaceContext(id)` 是唯一的解析与释放路径。释放后的 id
   不会解析到占用同一 slot 的下一个窗口（generation 判据）。present family 由整组 startup
   requirements 选出：一个 family 必须能呈现全部 startup 窗口；后续窗口只在其上可呈现时被接受，
   否则注册失败并给出原因（`VulkanRenderSurfaceContext::queryPresentSupport`）。

   app 侧的“我呈现哪个窗口”随之变成 app 自己的绑定：`AppRenderState::hostSurfaceId`（init 时从
   host 创建的窗口求得）是 `AppRenderServices::getHostSurface()` 的唯一来源；GUI host 用
   `findSurface(window)` 命名自己的窗口；`GUIWindowSession` 持有 `SurfaceId` + 解析出的指针，
   surface 生命周期归设备（`destroySurfaceContext`），不再由 session 的 `unique_ptr` 决定。

   未完成（本 step 明确不做，登记在案）：
   - 多 present family queue plan：今天一个 family 必须覆盖整组 startup 窗口，否则该设备被跳过；
     “满足初始集合所需的最小额外 present family”尚未实现。
   - 无窗口（offscreen-only）设备：`startupSurfaces` 为空会被明确拒绝（queue plan 需要 present
     family），不是已实现的模式。
   - acquire/submit/present 仍在 `IRenderSurfaceContext::begin/end` 里（AB4-2f）。

   AB4-2b step 1b（已落地 2026-09-24）：per-surface 的 GPU 状态也按身份归位。
   `SurfacePresentation` 记录 `SurfaceId`；`RenderDeviceState::acquireSurfacePresentation(id, surface)`
   是唯一入口，`PresentFrameInput.surfaceId`（app 用自己的 `AppRenderState::hostSurfaceId` 填）把它
   带过应用边界，因此窗口关闭再打开即使复用同一地址也不会命中上一扇窗的导入图。
   `prepareFrameRecord` 在录制前的 safe point 调 `reconcileSurfacePresentations()`：surface 已从设备
   注册表消失的 present target 被丢弃（它的导入图属于一个不存在的 swapchain）。
   仍以指针为入参的两个查询（`getPresentationImageShared`、`buildRenderTargetCatalog`）改成先解析 id
   再查表，设备无法为其命名（未初始化 / 测试 stand-in）时返回空而不是猜。

   证据：`ya-rhi-vulkan-smoke` 8 passed / 1 skipped（platform minimize guard），新增
   `RHISurfaceContext.AReleasedSurfaceIdDoesNotResolveToTheNextTenantOfItsSlot` 与
   `RHISurfaceContext.StartupWindowsAndLaterWindowsAreRegisteredAlike`；`ya-testing` 1319 tests /
   1318 passed / 1 skipped（同一 platform guard）/ 0 failed；GUIWorkbench `--smoke-actions` PASS；
   parity 两张图 md5 仍 `c775245ae636f15b41da8485319a2267`；editor smoke exit=0。
2. **AB4-2c：GUI 所有窗口成为同级 session。** GUIApp 不再把 `_primaryWindow` 与
   `GUIWindowManager` extras 分开持有；注册、事件路由、tick、关闭和拖拽查找统一走 session registry。
   应用启动窗口只是一个普通 session 的创建时机，退出策略由应用明确指定。
3. **AB4-2d：一个逻辑 tick 构造多个 SurfaceDisplayPlan。** render plan 分开携带
   View requests 与每个 surface 的有序 display layers；acquire result 是录制期间的一次性 runtime token，
   不属于长期 display policy；record/compose/present 对每个
   surface 执行同一顺序，绝不按 vector 下标解释默认窗口。UI snapshot 按 WindowId/session 关联，
   同一 View output 可被多个 display layer 引用。
4. **AB4-2e：tear-off viewport 绑定自己的 View 与 surface。** Editor 把面板产生的 View request
   和 display layer 显式关联到目标 session；chrome 采样该 View 的 display image，不借用别的窗口的图。

当前默认窗口由 GUIApp 的 `_primaryWindow` 单独持有，extras 才由 GUI host 的
`presentGuiSnapshot` 自建 acquire/submit/present，且只呈现 GUI chrome；整帧录制完全没参与，
所以被拖出去的 viewport 面板看不到世界画面。

#### AB4-step2 的执行口径（沿用 `render-view-family` R2 已登记的决定，不再单独拍板）

**一个逻辑 tick、一份 SceneSnapshot 集合、多份 View request、N 份 surface composition。** R2 的待办
（“让同一逻辑 tick 的多个 surface/window 共用一个 SceneRenderScheduler/SceneRenderPlan，避免按窗口
重复抽取同一 Scene”）已经确定此口径；今天 `RenderFramePlan` 仍把 `PresentFrameInput`、UI snapshot
与唯一 display root 混在一起。目标 plan 拆出 frame facts、Scene snapshots、View requests、各 surface 的
display layer list 和本次 acquired image。先统一无等级 surface/session 的生命周期，再一次替换这个混合
plan；不要在旧结构上逐个加 vector 字段。

当前产品的真实顺序（`EditorModule`，与 step2 要改的正是这两处）：

```
当前默认窗口 tickRender → record（世界 RT → chrome UIImage → compose → present）
当前 extras      onAfterPresent → sweepAndPresentExtraWindows
                → GUIWindowManager::tickTrees + renderAll
                → presentGuiSnapshot（自建 acquire/submit/present，只有 GUI）
```

因此 step2 不只是“多一个 present target”，而是**合并默认窗口与 extras 的 session 生命周期，
再把每个窗口的 tick / compose / present 放进同一个 app 录制顺序**——这是主要时序改动，也是最容易踩
“chrome tick 在 present 之后”这类半状态的地方。

##### AB4-2a-1：帧生命周期离开 surface（前置，不动 plan）

今天 RHI 的 device 级簿记挂在**主 surface 的 present**上，而“主 surface”这个身份
就是这里要删的东西：

| 写法 | 位置 | 问题 |
| --- | --- | --- |
| `_bDeviceFrameOwner` / `onPrimaryPresentFenceWaited()` | `VulkanRenderSurfaceContext.cpp:279/494`、`VulkanRender.cpp:1252` | 帧号推进、`DeferredDeletionQueue::flush`、GPU 计时读回都由**主 surface** 的 `begin()` 触发。第二 surface begin 时不推进（对），主 surface 缺席（最小化/拒绝帧）时也不推进（错：这一帧的回收没人管） |
| `_activeFlightIndex = _render->primaryFrameIndex() % MAX_FLIGHTS_IN_FLIGHT` | `Render2D/QuadRender.cpp:571`、`LineRender.cpp:251` | GUI 2D 的 flight 槽位取自主 swapchain 的帧号，而不是本帧 recording 的 flight slot |
| `_render->primarySwapchain()` | `RenderDiagnosticsService.cpp:148/251` | 诊断读回只认主 surface 的 swapchain（**已落地**，见下） |

##### AB4-2a-1 收尾：最后两个匿名“那个窗口”查询（已落地 2026-09-23）

- `RenderDiagnosticsService::init(...)` 增加 `IRenderSurfaceContext* captureSurface`：服务不再自己
  `primarySwapchain()` 挑窗口，而是被**告知**它诊断哪个窗口（onRecreate 订阅与 RenderDoc
  render context 都跟着这个 surface）。`RenderDeviceState::initDiagnostics` 传的是“device
  创建时用的那个 surface”（init 期事实，非每帧路径）。
- `RenderDeviceState::buildRenderTargetCatalog()` → `buildRenderTargetCatalog(IRenderSurfaceContext&)`：
  catalog 的 surface 条目描述**调用方点名的**那个窗口；`AppRenderServices` 侧解析“本 app 呈现的
  窗口”（AB4-2b 变多 display root 时按 root 取）。
- `IRender::primarySwapchain()` 删除（零消费者）。

**顺带发现（登记，未做）**：`IRenderPass::create()` 在整个仓库里**零调用方**，所以
`VulkanRenderPass`（含 OpenGL 那份）是死代码——它正是 `VulkanRender::primaryVulkanSwapchain()`
的唯一使用者，而它的 `createDefaultRenderPass()` 还会拿**主 swapchain 的 format** 当默认附件。
删掉这套 render-pass 抽象要连带清理 Forward 各 pass desc 里恒为 nullptr 的 `renderPass` 字段与
`RenderDefines.h` 的 `RenderPassCreateInfo`，属独立批次（“删死抽象”），不与本批的“窗口等级”混。

##### AB4-2a-1 收尾之二：调用点不再各自去问 renderer（已落地 2026-09-23）

这次复核推翻了当时“保留 `getPrimarySurfaceContext()`，只集中调用到
`AppRenderServices::getHostSurface()`”的处理：它没有消除等级，只把查询集中到一个 facade。
该 facade 只能作为迁移中的临时调用点，不是最终设计。AB4 的验收必须覆盖删除
`IRender::getPrimarySurfaceContext()/primaryWindow()`、Vulkan 的 `_primarySurface` 与
`createPrimarySurface()`，并让 app 对每个 display root 显式提供对应 surface；GUI host 同时删除
`_primaryWindow` 与 primary/extras 双路径。命名为“initial/bootstrap/default”但仍由 renderer 单独
拥有的 surface，同样不通过验收。

证据：`make test` 2705 passed / 0 failed；parity md5 与基线相同；编辑器 smoke 三轮全过
（本轮共 4 次里 1 次 `{0,0}` 首帧竞态，与既有登记同形）。

UE 的同位概念是**帧级**的：device 的帧号、延迟删除、GPU 计时读回都挂在“这一帧”上，
由渲染线程每 tick 推进一次，跟“哪个窗口 present 了”无关；present 是 per-viewport 的
`RHIEndDrawingViewport`。所以这一批做的是：把帧号推进 / 延迟删除 flush / GPU 计时读回
挂到**本帧 recording**（`RecordedFrame` / `RenderSubmission`）上，Render2D 的 flight 槽位
改读本帧 recording，`primarySwapchain()` 这类匿名查询改为显式给 surface。

- 验收：现有双 surface 用例（`RHISurfaceContext.ExtraWindowPresentResizeCloseSoak` /
  `ExtraWindowResizeAndCloseDoesNotDeviceWaitIdlePrimary`）全绿 —— 它们本来就每帧
  present 主 + 额外两个 surface，是双 advance / 漏 advance 的现成探针；`make test` 全绿；
  parity md5 逐字节不变。

##### AB4-2a-1 已落地（2026-09-23）

三条写入点全部移走，`_bDeviceFrameOwner` 删除：

| 之前 | 之后 |
| --- | --- |
| `VulkanRenderSurfaceContext::begin()` 里 `if (_bDeviceFrameOwner) onPrimaryPresentFenceWaited()` | 删除；surface 不再触发任何帧级动作 |
| `VulkanRender::_frameIndex` 由主 surface 的 acquire 推进；GPU 计时环槽位取自主 surface 的 `getCurrentFrameIndex()` | `IRender::beginRecordedFrame()` / `recordedFrameIndex()` / `framesInFlight()`；计时环槽位 = `_frameIndex % kFramesInFlight`（`VulkanRender::frameTimingSlot()`） |
| `Render2D` 的 flight 槽位 `primaryFrameIndex() % MAX_FLIGHTS_IN_FLIGHT`（两处） | `Render2D::begin` 从 `recordedFrameIndex() % framesInFlight()` 解析一次，作为 `flightSlot` 形参传给 `FQuadRender::begin` / `FLineRender::begin` |
| `GameRuntimeTickOrchestrator::resolveFlightIndex` 读 `primarySurface->getCurrentFrameIndex()` | 读 `recordedFrameIndex() % framesInFlight()` |
| `IRender::primaryFrameIndex()`、`IRenderSurfaceContext::getCurrentFrameIndex()` | 删除（零消费者；surface 的槽位回到 private） |

新增 `kFramesInFlight`（`RHI/RenderDefines.h`，值仍是 1）：它是 device 级的“几帧在飞”，
同时是每个每帧环的深度（surface 的 acquire/flight 环与 GPU 计时环）与“一个录制最多能用哪个槽位”
的上界。**值没变**——抬高它是 CPU/GPU overlap 决策（temporal_semantics M4），本轮只把
“谁是那个数”从窗口改成 device。

调用点：`tickRender` 在 acquire 之后、录制之前调一次 `render->beginRecordedFrame()`，两条
早退路径（acquire 失败 / 不可呈现）也走它——那正是“没有任何 surface 参与的一帧也要回收”的情形。

证据：新用例 `RHISurfaceContext.FrameBookkeepingBelongsToTheFrameNotAWindow`（正：无 surface
即可推进；反：present 一个窗口**不**推进）。负向对照（真做）：把 `beginRecordedFrame()` 加回
主 surface 的 `begin()` → 该用例 FAIL（`recordedFrameIndex` 3 vs 2），移除后 PASS。
`make test` 13 target / **2705 passed / 0 failed**；parity 两张图 md5 仍 `c775245ae…`；
编辑器 smoke exit=0（一次 `{0,0}` 首帧竞态，重跑三次全过，与已登记的首帧竞态同形）。

##### AB4-2a-2 第一步：backdrop 是 per surface 的政策（已落地 2026-09-23）

`fillsPrimarySurface()`（无参数、只能有一个 surface 回答）→ `fillsSurface(const IRenderSurfaceContext&)`，
`App::presentsViewDisplayImage(const IRenderSurfaceContext&)`（旧的 `nullptr` 退化分支不能作为最终协议）。
Surface display compose 只对 acquire 成功的 Surface 录制；offscreen View recording 可以没有 Surface。编辑器答“每个我托管的窗口都由 chrome 填满”
（tear-off 窗口是同一套 chrome 在第二张 surface 上，不是另一种窗口）；`PresentFrameInput::backdrop`
本来就在 present 项上，现在它的来源也是 per surface 的。这就是 UE 里“游戏视口填满窗口”与
“视口是窗口里的一块面板”的区别，与窗口等级无关。

证据：`AppLifecycleTest.TheSurfaceBackdropIsWhatTheLoadedModulesSayItIs` 改为对一个 stand-in
surface 提问（不再能靠 `nullptr` 让断言变空转）。`make test` 2705 passed / 0 failed；parity、
编辑器 smoke 与基线一致。

##### AB4-2c：所有 window session 以同一生命周期运行

- 全体 session 的 tree tick 与 snapshot 构建发生在 **record 之前**（app 帧循环内）；GUI snapshot
  以所属 WindowId/session 进入对应 surface display plan，compose 落到该 session 自己的 swapchain。
- 删除 `presentGuiSnapshot` 的独立 acquire/submit/present loop；GUIApp 与 GameEditor 共用同一条
  app-owned record/present 顺序。
- session 的关闭语义保持在帧边界收口；不允许录制中途销毁 session 或 surface
  （`app_teardown_order_and_instance_lock.md` 的边界）。
- 验收：`GUIAppCrossWindowDragTest.*` / `GUIWindowManagerTest.*` / `EditorNativeTearOffTest.*`
  全绿；各 session 都由同一 registry 查找、分发事件、tick、关闭。

##### AB4-2d：单帧 plan 拆分并支持多个 surface composition（依赖 AB4-2b/2c）

- 彻底替换 `RenderFramePlan` 的混合输入：`FramePacket` 只留 tick/flight/clock 等帧事实；View 的
  render scale/extent 进入各自的 `ViewRequest`，不得再由单一 frame-wide 设置代替；
  `SceneSnapshotSet` 共享抽取结果；`ViewRequest[]` 携带 scene key、camera、extent、features、pipeline
  policy；`SurfaceDisplayPlan[]` 描述 surface ID 与有序 display layers。UI snapshot 随其所属
  `WindowId`/session 进入 display layer。删除 `SceneViewDesc::composeOntoViewId` 和全局
  `FramePacket::uiFrameSnapshot`，不留 `displayRootTask()`。
- 每个 `SurfaceDisplayPlan` 描述一个 surface 的 composition（surface ID、有序 layer 列表、
  backdrop/clear policy）。acquire 返回值独立保存在该 surface 本次 `AcquiredSurfaceImage` 中；
  不把 transient image index 塞进长期的 display policy。
  **无序集合，没有“第一项”特权**：plan 不区分主次；应用启动时选择哪个窗口作为默认展示策略，
  必须通过明确的 window/surface ID 表达，不能映射回 renderer 的隐式默认对象。今天读
  `.present.surface` 的单值消费点全部改读“自己那一项”，不留“数组第 0 项就是默认窗口”
  这类隐式排序。
- `RuntimeRenderContext` 先为每个不同 View 记录一次并发布 `ViewOutput[ViewId]`；之后只对 acquire
  成功的 Surface 记录 display compose。空 surface 列表仍可录制 Scene/View 的 offscreen 工作；纯 UI app
  可不产生任何 View。Application 决定 work 的逻辑依赖顺序；RHI 将显式 queue work / acquire wait /
  present signal / completion 要求映射到 backend，同一 command buffer 不可因多个 Surface 被重复提交。
- `SurfacePresentation` 已经是 per-surface（step1 落地，按该窗口自己的 swapchain format
  建 write pass），这一批不改它。
- 删除基于 `fillsPrimarySurface()` / `presentsViewDisplayImage()` 的隐式组合判断。每个 surface 的
  display layer 明确声明 image、UI/chrome、clear 与 overlay 顺序；游戏窗口可以是单个 View layer，
  Editor window 可以是 viewport image + GUI chrome 的多 layer composition。
- 验收：`ya-render-3d-test` 全绿；`run_display_compose_parity.py --skip-build` 两张图
  md5 逐字节不变（`c775245ae…`）；`make test` 全绿。

##### AB4-2e：某个窗口里的 viewport 面板渲世界

- viewport tab 由 `EditorViewProducer` 声明为独立 View request；所在 window session 的 display layer
  引用这个 View 的 output（owner-scoped `SceneViewKey` 已就位；同 Scene 双 View 的 snapshot 复用表
  在 R2 已有验证，这里直接接上）。chrome 的 `UIImage` 采样该 View 的 display RT——与任何窗口同一
  机制，不是把别的窗口的图复制过来。
- 验收：一条自动化：把 viewport 面板 tear-off 成独立 OS 窗口 → 断言第二张 swapchain
  的图非空、extent 非退化，且其内容来自**它自己**那个 View（不同相机位姿下该图必然
  不同于其它窗口的 presentation 图，防“把别处一张现成图拷给了第二张 surface”）；
  世界内容正确性留给目视 + 现有 smoke。

##### AB4-2f：录制、提交、呈现三段闭环

- `FrameRecording` 只表示 command work、GPU 资源租约和 recording token；它的 `seal` 不得被命名或实现成
  queue submit。`SubmissionGraph` 由应用把 View work、display-composition work 和各 Surface 的
  acquire/present dependency 连接起来，再交给 RHI backend 执行。
- 一个 command buffer 只能进入一次 queue submit；多个 Surface 共享 View work 时共享 recording 或
  output，不复制提交同一 command buffer。每个 acquired Surface 都有自己的 image-available /
  render-finished / present-complete 生命周期；present 只能等待产生该 image 的 submission signal。
- 需要覆盖四类早退：无 surface 但有 offscreen work、部分 surface acquire 失败、record 失败、
  submit/present 失败。前两类不应丢弃可独立完成的 View work；acquire 成功后发生 record/submit 失败，
  backend 必须提供合法的 image 消费/取消路径，确保下一个 acquire 不被悬挂 semaphore/fence 卡住。
- 当前 `IRenderSurfaceContext::end(imageIndex, commandBuffers)` 作为迁移态必须拆成 surface acquire token、
  backend submission、surface present 三个明确责任；不能把“空 command list 也可 present”继续当作通用
  错误处理协议。
- 验收：单 graphics queue 下两 Surface 可共享一份 View recording 并分别 present；一个 Surface minimized
  不影响另一个 Surface；无可呈现 Surface 时 offscreen View 仍能发布 output；失败后下一 tick 可重新 acquire，
  且所有 submission keepalive 在 completion 后才释放。

##### 非目标（本条不做）

- `PresentFrameInput::backdrop` 迁移为每个 `SurfaceDisplayPlan` 的显式 display policy；不再由全局或
  `fillsPrimarySurface()` 推导。
- 不在本条里决定 flight 深度（R2/temporal_semantics M4，独立决策）。
- 不新增“第二个 renderer”：AB4-step1 已经把第二窗口变成 `_surfacePresentations` 表的第二项。

AB4-step3（待做）：`PresentationGraphService` 只保留「把 ready image 写进 surface」+ 宿主
display stage 的顺序；acquire / submit / present 的编排留在应用。

### AB8 — 同一事实只有一个来源：帧的 View 事实（step 1 + step 2 已落地）

> AB8 的 `HostViewportView` 是当时单宿主窗口模型下的迁移态。AB4-2d 完成后，多个 window/session
> 可以各自显示不同 View，因此该单值不能继续作为最终模型；应由 `WindowId/SurfaceId →
> SurfaceDisplayPlan → ViewId` 显式求得窗口内容，工具查询也必须点名 View/Window。

唯一目标：**「这一帧的 View 有多大、宿主窗口显示哪个 View」只从计划里读一次**，
不再有「设置里抄一份、设备里记一份、应用再推一份」。

判据来自两层结论的共同点：`getViewExtent()` 不是「放错层」，而是它的语义缺一个身份；
问题也不只是命名，而是**同一个事实存在多个写入者**。本 checkpoint 只动两个已被确认重复的事实：

AB8-step1（已落地）：

- **pipeline 的 view rect 改为输入，不再由 renderer 保存。**
  `RenderDeviceState::_pipelineViewRect` 与公开的 `applyViewResize()` 删除；
  `PipelineCoordinator::applyPendingChanges(Rect2D viewRect)` 收本帧的 View rect（来自 plan
  的 display root），自己持有 `_appliedViewRect`（**它自己的已应用状态**，不是 View 声明的副本）。
  `InitDesc.reapplyViewRectSink` 这个 std::function 随之删除——「重建后要重新套用 rect」
  现在由参数表达。init 期的窗口尺寸种子改名为 `_initialViewExtent`，并注明它只服务第一次 build。
  同时删掉 `declareViews` 里那句 `device->applyViewResize(view.outputRect)`：同一个 rect 过去由
  **两处**推给 renderer，这正是「两个意见」的来源。
- **renderer 不再决定哪个 View 是宿主的。**
  删除 `_publishedOutputViewId` / `_publishedOutputFlight` / `publishViewOutputIdentity()` /
  `publishedViewOutput()`。应用侧新增 `HostViewportBinding{viewId, flightIndex}`（见
  `AppRenderState.h`），由 `tickRender` 从 plan 的 display root 写一次；`record()` 内部改用局部
  `displayOutput = getViewOutput(flight, displayRoot->viewId)`。
- **查询一律带身份。** 删除无身份的 `getViewExtent()`、`getActiveViewImageShared()`、
  `getViewDisplayImageShared()`、`getPostprocessOutputImageShared()`、`getViewDisplayImage()`、
  `getViewDisplayImageFormat()`；改为 `getViewOutput(flightIndex, viewId)` 与
  `surfaceImageFor(const RenderViewOutput*)`。`buildViewportSnapshot(flightIndex, viewId, Scene*)`
  同样带身份。应用侧 `AppRenderServices` 用自己保存的 binding 解析：`getHostViewportOutput()`、
  `getHostViewportViewId()`、`getViewOutput(viewId)`。自动化截图的三张图（postprocess /
  viewport / presentation）改由应用**指名**取值，不再问 device「当前 viewport 是哪张」。
- 顺带删掉两处已死的重复工作：`prepareFrameRecord` 里用**上一帧**已发布 display image 的格式
  去 prepare UI compose pipeline（同一函数上方 `prepareComposePipelines()` 已经用 pipeline 自己的
  postprocess format 做过同一件事）；以及 `record()` 结尾三个 `retain(...)`——
  `retainPublishedViewOutputs()` 已经保活了每个 live view 的 display/color/depth/entityId。

= 游戏相机；直接用编辑器相机是行为变更），所以它是一个需要拍板的设计点，不是机械搬迁。
AB8-step2（已落地）：**`HostViewState` 拆成设置与排布**。

- `HostViewState.h` → `HostRenderSettings.h`，struct 只剩 `clock` / `renderResolution` /
  `renderScale`（设置），三个相机字段删除。
- 新增 `GameRuntime/HostViewportView.h`：`HostViewportView{viewId, flightIndex, view,
  projection, cameraPos}`，即「宿主窗口显示的那个 View 及其相机」，与原 `HostViewportBinding`
  合并成一个值。写在 `tickRender`，**一个写者、一次写入**，来源是 plan 的 display root；
  `declareViews` 因此不再写任何 host state（它只声明与提交）。
- 编辑器与 automation 改读这个排布值：`EditorViewportCompositor::compose`、
  `makeEditorSurfaceContext`、`EditorLayer::pickEntity` 的入参从 `const HostViewState&` 变成
  `const HostViewportView&`；`get_world_view_state` 的 `camera_pos` 读它。

**修正上一版的一处判断**：这里原本写着「必须先决定 PIE 下 overlay/picking 用哪个相机」。
不需要——保持今天是宿主 display root 的相机（PIE 下即游戏相机）就是逐字节等价的行为，
而本轮的目标是消除「设置里抄一份 View 声明」，不是改变用哪个相机。剩下的产品问题是另一个
问题：**PIE 下编辑器视口的 overlay/picking 该不该跟着游戏相机**（今天跟，另一种答案是跟
编辑器相机）。它在 `HostViewportView` 落地后才是一个可以单独讨论的选择，而不是这次搬迁的前提。
= 游戏相机；直接用编辑器相机是行为变更），所以它是一个需要拍板的设计点，不是机械搬迁。

### AB5 — 命名对齐语义（待做，必须在 AB3/AB4 之后）

| 当前 | 目标 | 理由 |
| --- | --- | --- |
| `RenderFrameData` | 拆为共享 `SceneSnapshot`、View 级 `PreparedView`、帧级 tick constants | 现类型同时混装 Scene 共享数据、View 数据与 tick 数据，不能整体换名 |
| `RenderSubmission` | `FrameRecording` | `finish()` 只封录制，不 queue submit |
| `RenderFrameInputs.h` 内的 plan 类型 | `RenderPlan` | 内容是本次渲染输入 |
| `PipelineCoordinator` | 收为 renderer 私有，或改名 `RenderPipelineController` | 它管 active/pending pipeline 与切换，不是通用 coordinator |
| `PresentationGraphService` | `DisplayComposer` | 它把 ready image 写进 surface |
| `RenderDeviceState` | `Renderer` | 见 AB7 |

改名不得单独作为进度提交；先完成语义迁移再改。

### AB6 — `Render3D/{Common,Services}` 按关切归类（待做）

唯一目标：`Common/` 与 `Services/` 这两个语义桶消失。

只迁移能一句话回答职责的文件（Scene / View / Compose / Debug / Resource / Pipeline），
答不上的留在原地并在本文件记一笔，不为了消灭平铺而硬塞。

### AB7 — `RenderDeviceState` 拆成应用级 RenderContext 与 Framework 管线对象（step 1 已落地）

前置：AB3（查询面收窄）与 AB4（presentation 解开）。

**这一条已经从「改名成 `Renderer`」改成结构性拆分。** 修订理由：名字不是问题，职责才是。
`RenderDeviceState` 同时是 RHI/device 生命周期、持久 GPU 资源、以及**产品级的整帧录制执行器**
（prepare → begin → recordFamily → publish → view compose → UI compose → display compose → capture）。
只把它改名成 `Renderer` 会让「Framework 拥有整帧排布」这件事换一个更好看的名字继续存在。

AB7-step1（已落地，2026-09-23 第五批）：**整帧录制顺序搬到应用侧，`RenderDeviceState::record` 删除。**

- `Applications/GameRuntime/Render/RuntimeRenderContext.{h,cpp}`（公开头
  `include/GameRuntime/Render/RuntimeRenderContext.h`）持有 `RenderDeviceState*`，公开
  `record(const RenderFramePlan&) -> RecordedFrame`；**函数体就是那条顺序**：prepare → begin recording
  → graphics/View work → inset 合并 / inset 图构建 → UI compose → 应用显式 overlay/capture 阶段
  → 对已 acquire 的 Surface 做 display compose → retain → seal recording。display root 的解析、inset 的
  合并与每个 Surface 的 display policy 都在应用 plan 中，Framework 不选择窗口。
- `RenderDeviceState` 只剩**机制步骤**，且都是真实函数体：`prepareFrameRecord` / `beginFrameCommandBuffer`
  / `recordViewFamilies` / `retainPublishedViewOutputs` / `endFrameCommandBuffer` /
  `sealFrame(flightIndex, cmdBuf) -> RecordedFrame`（新，合并原 finish 段）/ `acquireSurfacePresentation`
  转为 public；`getLiveSubmission(uint32_t)` 增加非 const 重载（`SurfacePresentation::recordDisplayCompose`
  的公开签名本就要求 `RenderSubmission&`）。`publishFamilyResult` / `findSurfacePresentation` 仍私有。
  **没有保留任何转发到 `record` 的方法**，也没有 `Coordinator2` / `RenderServiceHub` 之类的皮。
- 归属：`AppRenderState::runtimeRender`（`std::unique_ptr<RuntimeRenderContext>`），在 `AppLifecycle`
  里紧跟 `device->init(...)` 创建、在 `device` 之前销毁；调用点是
  `GameRuntimeTickOrchestrator::recordFrame`（它仍只负责「本帧的事实」）。
- 反向验收（入口可读性）：`App::run` → `iterate` → `declareViews → extractScenes → prepareViews →
  buildGameRenderFrame → acquire → record → submit` 不变；`tickRender` 只是多了一行
  `recordFrame(app, *renderContext, ...)`，没有变难读。
- 证据：`Engine/Test/Source/RuntimeRenderContextTest.cpp`（4 个用例，落在 `ya-testing`）。
- 顺带消掉：`RenderDeviceState.Frame.cpp` 的 `#include "GUI/Compose/Render2DComposePass.h"`
  （本批前它已无使用者，真正的使用者在 `Render3D/Common/ViewCompose.cpp`）。
  `RenderDeviceState.cpp` 的那个 include **仍在**——它服务 `prepareComposePipelines()` 的
  `prepareRender2DComposePassPipeline`。

AB7-step2（待做）：让 `RuntimeRenderContext` 继续收下「frame flight、scene/view plan、submission、
surface present 目标、Game UI 绑定、present 前后策略」这些今天仍散在应用侧或 device 上的事实；
`recordExtensions` 的阶段变成应用侧显式调用（plan §4b 同一条）。

目标形态：

```
Applications/GameRuntime/Render/
  RuntimeRenderContext      frame flight、scene/view plan、submission、surface present 目标、
                            acquire 之后的 record 顺序、Game UI 绑定、present 前后策略
Framework/Render/
  ForwardRenderPipeline / DeferredRenderPipeline / ViewComposePass / DisplayComposePass /
  PostProcess stages / RenderGraph / FrameRecording
```

它不是新增一个万能 coordinator，而是把 `RenderDeviceState` 里已经存在的应用职责放回应用侧。
Framework 侧的公开入口限定在 device/resource lifetime、View-family recording、DisplayComposer
和已发布 View output 等窄接口；它不公开“整帧 record(plan, surface)”这种应用级入口，不能看见
`_pipelineCoordinator` / `_submissions` / `_viewOutputs` / `_surfacePresentations` 的产品排布。
取名用 `RuntimeRenderContext`（当前编辑器仍跑在 `GameRuntime::App` 上，叫 `GameEditorRenderer` 不准确）。

## 5. 与 `render-view-family` 4.0.3 的取舍

### 5.1 已确认但未完成的所有权偏差（2026-09-22 review）

以下每一条都已在源码里核对过，不是推测。它们**不是本 checkpoint 的目标**，列在这里是为了让下一步
不再从「猜哪里有问题」开始。每条都标了当前证据与目标归属。

| 偏差 | 当前证据 | 目标归属 |
| --- | --- | --- |
| ~~整帧录制编排仍在 Framework~~ **已修（2026-09-23 第五批）** | ~~`RenderDeviceState::record()` 负责 acquire 之后的 submission / recordFamily / view compose / display compose / surface presentation / finish~~ | 已落地：顺序在应用侧 `RuntimeRenderContext::record`，`RenderDeviceState::record` 已删除；Framework 只留机制步骤（AB7-step1） |
| 本 tick 的 View 准备数据由 App 长期持有 | ~~`AppRenderState::viewFrameDataPerFlight`~~ **已删除（2026-09-22 review batch 2）**；`AppLifecycle` 的 quit / `handleSceneDestroy` 两处清空循环同步删除 | `ExtractedSceneRender` 持有本 tick 的 `_frameData`，`SceneViewRecording` 只借用该对象内的 plan/data；保活审计确认 `RenderFrameData::sceneResources` 只含录制期消费的句柄/processor 指针，GPU 生命周期由 `RenderSubmission` 与 `retainPublishedViewOutputs` 负责，因此不存在把 packet 留在 App 才能保活的约束。零生产消费者的 `hostFrameData()` 也一并删除 |
| 无身份的 View 尺寸语义仍在 pipeline 接口上 | ~~`IRenderPipeline::getViewExtent()`~~ **已删除（2026-09-22）**；~~pipeline 内仍保存单套 View 资源（`_viewResources` / `_viewRI` / `_viewRTSpec` / `_pendingViewExtent` / `_debugViews`）~~ **已修（2026-09-23 第三批）** | 已落地：View 资源按 `ViewResourceKey = identity + extent + format + feature policy` 分键（`ViewResourceTable`）；"单套尺寸"的接口（`onViewResized`、`_pendingViewExtent`）已删除 |
| ~~`IRenderRuntimeServices` 删了，但「当前 Scene」仍是隐式全局~~ **已修（2026-09-23 第四批）** | ~~`setActiveSceneProvider` ×3 + `prepareDerivedState(Scene*, dt)` 每帧注入「刚处理的那个 Scene」；`_pendingStateScene != scene` 那一支会 `clearSceneResolveWork()`，所以处理 B 会丢掉 A 的 resolve 状态~~ | 已落地：三个 processor 各持**按 Scene 一份**的 `SceneWork`，入口是 `prepareScenes(std::span<Scene* const>, float)`；本 tick 不点名的 Scene 在那里失去 work（与 `reconcilePublishedViews` 同一判据）。实体级查询 / 失效入口都带 Scene |
| plan 仍携带行为 | `RenderFramePlan::recordExtensions`（`IFrameRecordExtensions*`），renderer 在固定阶段回调它 | 比 `std::function` 清晰，但「plan 是 immutable data」仍未达成；方向是把那些阶段变成应用侧显式调用（与 AB7 同批） |
| renderer 仍有编辑器查询面 + 反向依赖 GUI | `buildViewportSnapshot` / `buildRenderTargetCatalog` / `getDebugRenderSystem` / `getDiagnosticsService`；~~`RenderDeviceState.cpp` 与 `RenderDeviceState.Frame.cpp` include `GUI/Compose/Render2DComposePass.h`~~ **一半已修（2026-09-23 第五批）**：`RenderDeviceState.Frame.cpp` 那个（本批前已无使用者）随 `record()` 一起删除；`RenderDeviceState.cpp` 的仍在，它服务 `prepareComposePipelines()` 的 `prepareRender2DComposePassPipeline` | AB3-step2（typed command + 由数据构造 catalog）；剩下那个 include 需要 compose 准备改由宿主调用（已在 `source-layout-subtraction` S2 记录） |

### 报告点出的死代码（2026-09-22 已修）

### 第三批：View 资源按身份分键（2026-09-23 已落地）

唯一目标：Forward / Deferred pipeline 不得再用**一套**成员表示「当前 View 的资源 / 尺寸 / 输出」。

- 新增 `Render3D/Common/ViewResourceKey.h`：`ViewResourceKey{viewId, extent, colorFormat, depthFormat,
  featureMask}` 说明「哪些资源属于同一个 View」；`ViewResourceTable<Resources>` 是 pipeline 的 per-View
  发布表（一个 View 一个 live entry —— 同一 View 换 extent/format 是替换，不是第二条；另一个身份的 View
  是另一条，B 不覆盖 A）。
- `ForwardRenderPipeline`：`_viewResources` 从单个 `ForwardViewResources` 变为
  `ViewResourceTable<ForwardViewResources>`；`recordFamily` 对**每个**记录的 View 发布（不再只发 display
  root），键 = identity + extent + 格式 + feature policy。删除死成员 `_viewRI`、`_pendingViewExtent`、
  `requestViewResize`、`EForwardPendingResourceRefresh::ViewResize`，以及无身份的查询
  `getCurrentViewportResources` / `getViewOutputImageShared` / `getPostprocessOutputImageShared` /
  `getBloom*ImageShared`。`getViewDepthImageShared` / `getEntityIdImageShared` 改为**带身份**。
- `DeferredRenderPipeline`：`_debugViews` 从单个 `DeferredPipelineDebugViews` 变为同一 keyed 表
  `_publishedViews`；`buildDebugViews(viewId)` 取代无身份版本；同样删除 `_pendingViewExtent` / `ViewResize` /
  无身份的 `getCurrentGBufferResources` / `getCurrentViewportResources` / `getViewOutputImageShared` /
  `getBloom*ImageShared`。`appendRenderTargetEntries` 改为**按 View** 输出条目（此前只有一个
  "当前 View" 行，两个不同 extent 的 View 无法同时表达）。
- **删除 `IRenderPipeline::onViewResized`**：它唯一的作用是让 pipeline 记住「那个 View 的尺寸」。随之
  `PipelineCoordinator::applyPendingChanges(Rect2D)` 的 rect 参数与 `_appliedViewRect` 一并删除。这是
  AB8-step1 的下一步：那次把 rect 从 renderer 状态改成输入，这一步发现「输入给谁」本身不再需要——每个
  View 声明自己的 extent。（`prepareFrameRecord` 也随之不再收 `displayRoot` 只为取 rect。）
- 顺带删除因上述改动变成死代码的 `SSAOStage::setup(DeferredGBufferResources)` + `_gBufferResources`
  （只写不读）。`ForwardViewResources.h` / `DeferredViewResources.h` / `ViewportDebugCatalogBuilder.cpp`
  属并发写者的在飞 WIP，本批未改。
- 证据：`Engine/Test/Source/ViewResourceKeyTest.cpp`（key 分区 + 表语义 + Forward pipeline 在同一 tick
  记录三个 View 后各自持有独立 attachment/extent，且其中一个 resize 不打断其他 View）与
  `DeferredRenderPipelineTest.TwoViewsKeepTheirOwnPublishedResources`。

**收尾（2026-09-23 同一批的补丁）**：第三批只做了「按身份分键」，没有回答「一个 View 不再被声明时
它的条目去哪」。于是选中相机→声明 preview、取消选中→不再声明，那条 entry 与它唯一的
`shared_ptr<RenderTexture>` 附件永久留在表里，查询仍返回上一帧的图——正是
`render-arch` 契约「未发布就返回 `nullptr`/`{}`，要回落的调用方自己回落」禁止的兜底。

- 判据是**本 tick 的声明集合**（`SceneRenderPlan::viewTasks` 的 viewId），不是「距上次 publish
  多少帧」这类启发式；`ViewResourceTable` 上不引入定时器/纪元计数器。
- 落点是**录制前的 safe point**：`RenderDeviceState::prepareFrameRecord` 在
  `beginFrameCommandBuffer` 之前调用一次 `IRenderPipeline::reconcilePublishedViews(plan)`，
  pipeline 用 `retainIf([&plan](id){ return planDeclaresView(plan, id); })` 丢掉本 tick 不声明的
  View。**整 tick 一次、整份 plan 为输入**，所以同一 tick 内多个 family 的记录不会互相误杀；
  而且 tick 声明为空时也照样清空——`recordViewFamilies` 只在 plan 有 View 时才被调用，
  「按单次 family 淘汰」会漏掉「视口标签页关掉、本 tick 一个 View 都不声明」这一条真实路径
  （`EditorViewProducer` 的注释里写着这就是普通情况，不是错误状态）。
- 释放安全性已核实：表里的 `shared_ptr` 不是唯一保活——`retainPublishedViewOutputs` 把同一批
  color/depth/entityId owner 以 `RetainedResource`（内部 `shared_ptr<void>`）放进
  `RenderSubmission::_keepalives` 并 `retireResource`，flight 的 keepalive 只在该 flight 换 token
  复用时清空（那时 fence 已过）。**keepalive 没有只持裸 handle 的缺口，因此没有动 keepalive 设计。**
- 顺带：`RenderTargetCatalog::Entry` 增加 `SceneViewId viewId`（0 = 不属于任何 View），
  `appendRenderTargetEntries` 按 View 写入，`RuntimeRenderTargetSection` 显示它——两行同尺寸不同
  身份的 View 从此可区分（此前每行 label 都是字面量 "Forward View"）。
- 证据：`ViewResourceTableTest.RetainIfDropsOnlyTheViewsTheCriterionRejects`、
  `ForwardRenderPipelineTest.AViewTheNextTickDoesNotDeclareIsEvictedAndTheOtherIsKept`（用 `weak_ptr`
  证明附件真的被释放）、`ForwardRenderPipelineTest.ATickThatDeclaresNoViewLeavesNothingPublished`、
  `DeferredRenderPipelineTest.AViewTheNextTickDoesNotDeclareIsEvictedAndTheOtherIsKept`；
  `ya-render-3d-test` 185/185。

**覆盖补丁（2026-09-23 同日）**：这一批的淘汰调用点当时**没有自动化证据**——4 个新用例全部经
`*TestAccess` 直调 `pipeline.reconcilePublishedViews(plan)`，删掉 `RenderDeviceState.Frame.cpp` 里那一行
调用，185 个测试仍然全绿。补法是路径 1：两处 friend（`RenderDeviceStateTestAccess` /
`PipelineCoordinatorTestAccess`）+ 注入 pipeline + 一个新用例
`RenderDeviceStateTest.PrepareFrameRecordDropsTheViewsTheTickStopsDeclaring`（落在
`Engine/Test/Source/ViewResourceKeyTest.cpp`）。它钉的是「`prepareFrameRecord` 会按这份 plan 淘汰」与
「那一行存在且被调用」；**不**钉 `record()` 里 `prepareFrameRecord` / `beginFrameCommandBuffer` 的先后
（那要真 command buffer）。负向对照做过：注释掉那一行 → 新用例 FAIL，恢复后 PASS。
`ya-render-3d-test` 186/186。产品级证据见下一节。

### 淘汰的产品级证据：路径 3（已评估、暂不做）

「本 tick 不再声明的 View 在**产品面**上不再出现」目前没有任何自动化证据（面板不再列出那一行、
`viewResourcesFor` 之外的查询不再返回它）。三种取证路径评估如下：

- 路径 1（**本批采用**）：`RenderDeviceStateTestAccess` 注入 pipeline 调 `prepareFrameRecord`。生产改动
  最小（两处 friend，无行为/ABI 变化），证据钉调用点本身；不覆盖产品面。
- 路径 2（**明确不做**）：让 `PipelineCoordinator` 接受外部 pipeline 指针作为**生产**接口。为测试注入
  把「谁来建 pipeline」变成可注入的生产契约，是把测试需求写进产品接口。
- 路径 3（**已评估、暂不做**）：从编辑器**触发**真实路径、再从**自动化查询面**观察淘汰。触发侧本身
  可自动化：`EditorViewProducer.cpp:52` 的 `isViewportShown()`（视口标签页被换掉）与 `:60` 的
  `isViewportMode2D()`（切到 2D 画布）就是这两条真实路径的入口，而 `viewport.set_mode` 已是注册的
  script API（`EditorModule.cpp:347`），所以「声明消失」可以用脚本造出来。**卡点在查询通路**：
  `AppAutomationControlService` 没有 render target catalog / view resources 查询，script API 也没有
  `render.*`，要做必须**新增一个自动化 method**——那是产品面新功能，不是测试基建，因此本批不做。

**陷阱（已复核代码后落笔）**：现成的 `get_world_view_state.rendered_viewport_extent` **不能**当路径 3 的
观察点。它经 `AppRenderServices::getHostViewportOutput()` 读的是 `_viewOutputs`
（`RenderViewOutputTable`），而该表在 `beginSubmission` 遇到新 token 时把 `liveViewCount` 清零、随后由
**本 tick** 的 publish 重新填满；`find` / `get` 都以 `liveViewCount` 为边界。所以它每个 tick 呈现的都是
**当 tick 的重写结果**，从来不携带上一 tick 的 entry——「某 View 不再被声明」这件事在它上面**在
`81088ea1` 之前同样成立**（旧条目留在 pipeline 的 `ViewResourceTable` 里、查询返回上一帧的图，这个
可观察差异**只存在于 pipeline 的表**）。拿它写测试会得到一个**两个版本都绿**的无牙测试。
（精度说明：它「为空」是**查询边界**的事实，不是存储的事实——非 live slot 里仍留着一份上一 token 的
`shared_ptr`，只是不对外可查，与本目录已记的「`RenderViewOutputTable` 保活略长于必要」是同一处。）


`GameRuntimeTickOrchestrator::pumpOffscreenTasks` 是一个**只有自我递归、没有任何调用者**的函数：
663e0f82 想把它命名成 tickRender 的一个步骤，但 `tickRender` 实际直接调
`device->getOffscreenTaskService().tick(app.getTaskManager())`，命名的那一步丢了，函数体留在那里
自我调用。修法是**把步骤接回去**（`tickRender` 调 `pumpOffscreenTasks`，函数体做实际工作），不是
把名字删掉——命名本身是那次提交的正确意图。同时删除 `declareViews` 已不再使用的 `device` 形参。

`render-view-family` 曾把「合并成公开 `Renderer`」当作收口方向。本线保留「合并」
（它确实是一个 owner，不该再拆 Coordinator），但**拒绝**让它成为整帧排布的入口：
公开入口限定在

```cpp
init(...); shutdown(...);
record(const RenderPlan&, const SurfaceTarget&) -> RecordedFrame;
publishedViewOutput(viewId);
```

看不见 `_pipelineCoordinator` / `_submissions` / `_viewOutputs` / `_surfacePresentations`。

## 6. 非目标
### 第四批：Scene 是参数，不是查找（2026-09-23 已落地）

唯一目标：删掉「全局当前 Scene」。`EnvironmentLightingProcessor` / `TerrainProcessor` /
`GameplayResourceBinding` 各有一个 `setActiveSceneProvider(std::function<Scene*()>)`，由
`RenderDeviceState::prepareDerivedState(Scene*, dt)` 每帧把「刚处理的那个 Scene」塞进去，三个 processor
再在自己的 `onUpdate` 里反查回来。多 Scene 时不只「谁是当前的」含糊：`_pendingStateScene != scene` 那
一支会 `clearSceneResolveWork()`，所以准备 B 会把 A 刚建好的 resolve 状态整片丢掉。

- 每个 processor 现在有 `SceneWork`（**按 Scene 一份**）：per-entity 状态表、dirty 队列 / 集合、active 集合、
  audit 时钟、`bSeeded`。processor 上只剩跨 Scene 共享的一样东西——derived-resource 缓存，因为它的键是
  「资源由什么构建出来的」，不是「谁问的」。
- 入口从 `onUpdate(float)` 变成 `prepareScenes(std::span<Scene* const> scenes, float dt)`：本 tick 渲染哪些
  Scene 就是哪些 Scene 被准备，**没被点名的 Scene 的 work 在这里丢掉**——判据是本 tick 的声明集合
  （与 `IRenderPipeline::reconcilePublishedViews` 同一条），不是计时器或启发式；tick 一个 Scene 都不声明时
  同样清空，即旧的 `prepareDerivedState(nullptr)` 语义被保留而不是漏掉。
- 实体级查询与失效入口都带 Scene：`getTerrainMesh(const Scene&, entt::entity)`、
  `findTerrainState(const Scene&, entt::entity)`、`isSkyboxLoading(const Scene&, entt::entity)`、
  `isEnvironmentLightingLoading(const Scene&, entt::entity)`、`markSkyboxDirty(Scene&, entt::entity, ...)`、
  `markEnvironmentLightingDirty(Scene&, entt::entity, ...)`。后两个第一次有了「这个实体属于哪个 Scene」的
  答案：旧签名在 `_pendingStateScene` 不是它时是静默 no-op，现在按传入的 Scene 建 work。
- 其余实体级查询（resolve 状态、preview）落在 `SceneWork` 上：一个 `SceneWork` **就是**一个 Scene 的状态，
  所以 `getSkyboxPreview(entity)` 这种「只有实体」的签名在那里不再有歧义，也不必再问谁是当前 Scene。
- `ViewportDebugCatalogInput::environmentLighting` 从 `EnvironmentLightingProcessor*` 变成
  `const EnvironmentLightingProcessor::SceneWork*`，由 `makeViewportDebugCatalogInput` 用 `inspectScene`
  绑好。这条是**为了不改** `Debug/ViewportDebugCatalogBuilder.cpp`（并发写者的在飞 WIP），同时把「检查器看的
  是哪个 Scene 的 lighting」写进了输入类型。代价：`SceneWork` 成了公开类型，该头因此 include 了
  `EnvironmentLightingProcessor.h`（原来是 forward declaration），字段名 `environmentLighting` 也暂时仍读作
  「processor」——重命名要动那个 WIP 文件，留给它落地后再做。
- 调用点：`RenderFrameExtractor` 传它正在遍历的 Scene（`ctx.scene` 为空时不再去问「当前是哪个」）；
  `AppAutomation` 的两个 loading 判定与 terrain 状态查询传它正在遍历的 `scene`；
  `AppSceneServices::refreshSceneDerivedState` 的失效入口传自己的 `scene`。
- **三个 processor 的归属判断**：它们扫 ECS/Scene、维护 dirty/resolve 状态、按内容键缓存派生 GPU 资源，
  属于「运行时派生资源解析」，不是纯 GPU 管线；但也不能只因为「不是管线」就搬——把它们整体移出 `Render3D`
  （例如落到 `GameRuntime/Render/`）是 AB7 之后的独立判断，本批只做「Scene 显式 + 按 Scene 分状态」。

证据：`Engine/Test/Source/SceneDerivedStateTest.cpp`（落在 `ya-render-3d-test`）三个用例。前两个用两个 Scene
各放一个 skybox 实体——**同一个 entt id**——断言两个 Scene 的 work 与状态互不覆盖、且本 tick 不点名的 Scene
会失去 work；第三个用 terrain 证明 A 的状态对象在下一次 `prepareScenes({A, B})` 后是**同一个对象**（不是重新
seed 出来的），即 B 没有把 A 整片拿走。负向对照（真做）：把 `ensureWork` 临时改回单槽（`_sceneWork.clear()`）
重新构建 → 前两个用例 FAIL，恢复后 3/3 PASS。


- **不在 Framework 里**新增 `RenderCoordinator2` / `RenderContext` / `RenderServiceHub` 之类总入口。
  AB7 的 `RuntimeRenderContext` 不属于这条禁止项：它住在应用侧、装的是「当前应用如何准备与录制这一帧」，
  而这些职责今天已经存在于 `RenderDeviceState` 中——那是一次搬回，不是一次新增。判据是第三节的第 2 问：
  它是否要知道当前应用有哪些 Scene / View / 窗口。
- 不把每个 phase 做成一个 class；主时序必须能在一个入口里读完。
- 不把 Forward / Deferred 合并成一个抽象基类。
- 不把 `App` 拆成十几个 facade。
- 不按行数机械拆 `RenderDeviceState.cpp`。
- 不把 `SceneRenderScheduler` 变成全局 Scene registry。
- 不让 GUI Framework 依赖 Scene / ECS / Render3D。
- 不为了「目录整齐」一次性移动整个 Render3D 树。

## 7. 验收

- `AppRenderState` 里没有本帧排布（tick 的 arrangement 在 tick 里）。
- `GameRuntime/Lifecycle/` 只放生命周期，不放渲染排布。
- renderer 的公开面只回答「录制」与「已发布输出」；编辑器面板要什么由应用侧的名义转发。
- `Engine/Source/Applications/GameEditor` 内没有 `RenderDeviceState`，也没有到 concrete pipeline 的
  `dynamic_cast`。
- acquire / submit / present 的调用者是应用，不是 Framework/Render。
- 阅读入口仍是：`App::run` → `GameRuntimeTickOrchestrator::iterate` →
  `declareViews → extractScenes → prepareViews → buildGameRenderFrame → acquire →
  record → submit`。

## 8. 验证命令

```bash
xmake b ya-game-runtime && xmake b ya-runtime && xmake b ya-game-editor && xmake b ya-testing
xmake r ya-render-3d-test                      # 189/189
./build/macosx/arm64/debug/ya-testing --gtest_filter='RenderRuntime*:HostScene*:ViewFamily*:ForwardFrameGraph*:DeferredRender*:PostProcessing*:Offscreen*:AppKernel*:AppLifecycle*:AppScreenshot*:Widget*:Dock*:Editor*:GameUIHost*:Scene*:UIDocument*:ScriptApi*:RenderGraph*:ViewPersistent*:View*:SurfaceImage*-WidgetTreeTest.SystemLayersCannotBeDetached'
python3 Script/automation/render/run_display_compose_parity.py --skip-build   # PASS, md5 c775245a...
python3 Script/automation/editor/run_widgettree_editor_smoke.py --skip-build  # 六步全过（见下方首帧竞态）
```

**编辑器 smoke 的第 2 步是首帧竞态（2026-09-23 核实，与本批无关）**：脚本 `wait_for_port` 一返回就立刻
查 `get_world_view_state`，而宿主的 host viewport View 在第 0/1 帧还没有发布过输出，于是
`rendered_viewport_extent` 读到 `{0,0}` 并抛 `world view did not render`。证据：
① 同一份二进制（含本批改动）连跑两次，一次 `exit=0` 六步全过、一次在第 2 步失败；
② 用自动化探针在**未含本批改动**的等价树里实测，第 0 帧同样是 `{width:0,height:0}`，第 2 帧起是 `873x470`；
③ 含本批改动的树里同一探针同样从第 2 帧起报 `873x470`。也就是说这条失败与渲染无关，修法属于 smoke 脚本
自己（像第 5 步的 `wait_for_frame_progress` 那样先等一帧真的渲染出来），本批不动别人的 harness。

已知基线失败（与本线无关，不要追）：`EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo`、
`EditorPropertyGraphTest.TextureAssetRowShowsRetainedPreview`、
`WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots`、
`ScriptApiLibraryFixture.GameUIWidgetLifecycleThroughRegistry`、
`GameUIHostTest.BuildSnapshotComposesMountedWidgets`，以及偶发
`RenderGraphCoreTest.ResourceRegistryUsesProvidedImportedImageViewAndRetainsOwner`。
`WidgetTreeTest.SystemLayersCannotBeDetached` 会让测试进程 SIGTRAP，必须从滤镜里排除。
