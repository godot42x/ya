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

## 4. Checkpoints

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

AB4-step2（待做）：让**额外的 OS 窗口走同一条路**。现状：额外窗口由 GUI host 的
`presentGuiSnapshot` 自建 acquire/submit/present，并且只呈现 GUI chrome，
整帧录制完全没参与——所以被拖出去的 viewport 面板看不到世界画面。
这一步同时需要额外窗口的 chrome 每帧被 tick（现在 `session->tick` 只对默认窗口调用）。

#### AB4-step2 的执行口径（沿用 `render-view-family` R2 已登记的决定，不再单独拍板）

**一个逻辑帧、一份 `SceneRenderPlan`、N 个 present surface。** R2 的待办
（“让同一逻辑帧的多个 surface/window 共用一个 SceneRenderScheduler/SceneRenderPlan，
避免按窗口重复抽取同一 Scene”）已经把这个口径写死；今天 `RenderFramePlan::present`
只有一个 `PresentFrameInput`，所以这一步的第一件事是把它变成 N 个 display root，
而不是“每窗口各渲一帧”。

当前产品的真实顺序（`EditorModule`，与 step2 要改的正是这两处）：

```
主窗口   tickRender → record（世界 RT → chrome UIImage → compose → present）
额外窗口 onAfterPresent → sweepAndPresentExtraWindows
           → GUIWindowManager::tickTrees + renderAll
           → presentGuiSnapshot（自建 acquire/submit/present，只有 GUI）
```

因此 step2 不只是“多一个 present target”，而是**把额外窗口的 tick / compose / present
从主窗口 present 之后挪进 app 的录制顺序里**——这是唯一的时序改动，也是最容易踩
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

`getPrimarySurfaceContext()` 本身保留（“device 是用哪个窗口创建的”是 bootstrap 事实），但
**每帧代码不该各自去问它**。新增 `AppRenderServices::getHostSurface()` 作为 app 唯一的“我呈现哪个窗口”
出口，三个 app/编辑器调用点改读它（`GameRuntimeTickOrchestrator` 的自动化截图与 `presentFrame`、
`EditorModule` 的 three 处）。AB4-2b 让一帧呈现多个窗口时，改的是这一个出口与它的消费者，
不是散落各处的 `primary…` 调用。

同时把 `IRender.h` / `RenderSurfaceContext.h` 上三处“primary”的头注释改写成“bootstrap 事实、
非等级”：该 surface 在帧循环里没有任何特权（帧簿记是 `beginRecordedFrame`，计时与 flight 槽位是帧号），
`primaryWindow()` 只服务 app/input 的启动期绑定。

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
`App::presentsViewDisplayImage(const IRenderSurfaceContext&)`（`nullptr` 这条退化分支不允许存在：
录制只发生在已 acquire 的 frame 上，surface 必非空）。编辑器答“每个我托管的窗口都由 chrome 填满”
（tear-off 窗口是同一套 chrome 在第二张 surface 上，不是另一种窗口）；`PresentFrameInput::backdrop`
本来就在 present 项上，现在它的来源也是 per surface 的。这就是 UE 里“游戏视口填满窗口”与
“视口是窗口里的一块面板”的区别，与窗口等级无关。

证据：`AppLifecycleTest.TheSurfaceBackdropIsWhatTheLoadedModulesSayItIs` 改为对一个 stand-in
surface 提问（不再能靠 `nullptr` 让断言变空转）。`make test` 2705 passed / 0 failed；parity、
编辑器 smoke 与基线一致。

##### AB4-2a-2 第二步：plan 支持多个 present target（结构批次，产品行为不变）

- `RenderFramePlan::present` 单值 → `std::vector<DisplayRootPlan>`，每项
  `{surface, imageIndex, backdrop, chromeSnapshot(可选), displayViewId}`。
  **无序集合，没有“第一项”特权**：plan 不区分主次，谁是“主窗口”是 app 层的宿主事实
  （编辑器的默认窗口 / 独立 GUI app 的唯一窗口），不是 present 路径的属性。今天读
  `.present.surface` 的单值消费点全部改读“自己那一项”，不留“数组第 0 项就是主窗口”
  这类隐式排序。
- `RuntimeRenderContext::record` 里 present target 获取、UI compose、display compose、
  `sealFrame` 按 display root 循环，**每项走同一条顺序**；空表 = 今天“plan 无 surface”
  的既有语义（不录、不 present）。
- `SurfacePresentation` 已经是 per-surface（step1 落地，按该窗口自己的 swapchain format
  建 write pass），这一批不改它。
- 顺带把“这个 surface 的内容是不是 View 的图”改成 per-surface 政策：`App::presentsViewDisplayImage()`
  今天用“有没有模块 `fillsPrimarySurface()`”回答，且只有一个 surface 可答 —— UE 里对应的
  区别也不是“主窗口”，而是“这个窗口的内容是不是直接就是 viewport 的图”（游戏视口填满窗口、
  编辑器视口是面板内的一块）。所以 `IRuntimeModule::fillsPrimarySurface()` → `fillsSurface(surface)`，
  `ESurfaceBackdrop` 落到每个 display root 各自一个值。
- 验收：`ya-render-3d-test` 全绿；`run_display_compose_parity.py --skip-build` 两张图
  md5 逐字节不变（`c775245ae…`）；`make test` 全绿。

##### AB4-2b：额外窗口的 chrome 进同一帧

- `sweepAndPresentExtraWindows` 的 `tickTrees + renderAll` 改为：额外窗口的 tree tick
  与 snapshot 构建挪到 **record 之前**（app 帧循环内），snapshot 交给 `RuntimeRenderContext`
  作为第二项 display root，compose 落到该窗口自己的 swapchain；
  `GUIWindowManager::renderAll` 从编辑器产品路径退役（GUIApp / headless 仍可用它）。
- `EditorWindowSession` 的回收语义不变：close-requested 的 extra 仍由 app 侧收口，
  不允许在录制中途销毁 session（`app_teardown_order_and_instance_lock.md` 的边界）。
- 验收：`GUIAppCrossWindowDragTest.*` / `GUIWindowManagerTest.*` / `EditorNativeTearOffTest.*`
  全绿；新增一条断言：额外 surface 的 present 真的进了 app 的 record
  （`getPresentationImageShared(extraSurface)` 非空），而不是 `presentGuiSnapshot` 的自循环。

##### AB4-2c：某个窗口里的 viewport 面板渲世界

- 该窗口内的 viewport tab 由 `EditorViewProducer` 声明到**那个 surface**（owner-scoped
  `SceneViewKey` 已就位；同 Scene 双 View 的 snapshot 复用表在 R2 已有验证，这里直接接上）。
  chrome 的 `UIImage` 采样该 View 的 display RT——与任何窗口同一机制，不是把别的窗口的图复制过来。
- 验收：一条自动化：把 viewport 面板 tear-off 成独立 OS 窗口 → 断言第二张 swapchain
  的图非空、extent 非退化，且其内容来自**它自己**那个 View（不同相机位姿下该图必然
  不同于其它窗口的 presentation 图，防“把别处一张现成图拷给了第二张 surface”）；
  世界内容正确性留给目视 + 现有 smoke。

##### 非目标（本条不做）

- 不改 `PresentFrameInput::backdrop` 的语义（那是 display-compose 的待定项）。
- 不在本条里决定 flight 深度（R2/temporal_semantics M4，独立决策）。
- 不新增“第二个 renderer”：AB4-step1 已经把第二窗口变成 `_surfacePresentations` 表的第二项。

AB4-step3（待做）：`PresentationGraphService` 只保留「把 ready image 写进 surface」+ 宿主
display stage 的顺序；acquire / submit / present 的编排留在应用。

### AB8 — 同一事实只有一个来源：帧的 View 事实（step 1 + step 2 已落地）

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
| `RenderFrameData` | `PreparedView` | 它是 View 级 camera/light/draw packet，不是 frame |
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
  `record(const RenderFramePlan&) -> RecordedFrame`；**函数体就是那条顺序**：present target 获取 →
  prepare → begin → graphics → inset 合并 / inset 图构建 → UI compose → `recordExtensions->recordViewCompose`
  → display compose（capture 仍在它内部）→ retain → end → seal。display root 的解析、inset 的合并与
  backdrop 的选择都在这里，Framework 看不到。
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
Framework 侧的公开入口限定在 `init/shutdown/record(plan, surface)/publishedViewOutput(viewId)`，
看不见 `_pipelineCoordinator` / `_submissions` / `_viewOutputs` / `_surfacePresentations`。
取名用 `RuntimeRenderContext`（当前编辑器仍跑在 `GameRuntime::App` 上，叫 `GameEditorRenderer` 不准确）。

## 5. 与 `render-view-family` 4.0.3 的取舍

## 4b. 已确认但未完成的所有权偏差（2026-09-22 review）

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
