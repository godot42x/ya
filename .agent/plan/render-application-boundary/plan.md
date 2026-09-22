# Framework 只放可复用管线，应用拥有排布与状态

> 建立日期：2026-09-22
> 状态：AB1 / AB2 已落地；AB3–AB7 待做。

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
`RenderDeviceState::record` 完全没参与——所以被拖出去的 viewport 面板看不到世界画面。
这一步同时需要额外窗口的 chrome 每帧被 tick（现在 `session->tick` 只对默认窗口调用）。

AB4-step3（待做）：`PresentationGraphService` 只保留「把 ready image 写进 surface」+ 宿主
display stage 的顺序；acquire / submit / present 的编排留在应用。

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

### AB7 — `RenderDeviceState` 拆成应用级 RenderContext 与 Framework 管线对象（待做）

前置：AB3（查询面收窄）与 AB4（presentation 解开）。

**这一条已经从「改名成 `Renderer`」改成结构性拆分。** 修订理由：名字不是问题，职责才是。
`RenderDeviceState` 同时是 RHI/device 生命周期、持久 GPU 资源、以及**产品级的整帧录制执行器**
（prepare → begin → recordFamily → publish → view compose → UI compose → display compose → capture）。
只把它改名成 `Renderer` 会让「Framework 拥有整帧排布」这件事换一个更好看的名字继续存在。

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
xmake r ya-render-3d-test                      # 177/177
./build/macosx/arm64/debug/ya-testing --gtest_filter='RenderRuntime*:HostScene*:ViewFamily*:ForwardFrameGraph*:DeferredRender*:PostProcessing*:Offscreen*:AppKernel*:AppLifecycle*:AppScreenshot*:Widget*:Dock*:Editor*:GameUIHost*:Scene*:UIDocument*:ScriptApi*:RenderGraph*:ViewPersistent*:View*:SurfaceImage*-WidgetTreeTest.SystemLayersCannotBeDetached'
python3 Script/automation/render/run_display_compose_parity.py --skip-build   # PASS, md5 c775245a...
python3 Script/automation/editor/run_widgettree_editor_smoke.py --skip-build  # 六步全过
```

已知基线失败（与本线无关，不要追）：`EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo`、
`EditorPropertyGraphTest.TextureAssetRowShowsRetainedPreview`、
`WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots`、
`ScriptApiLibraryFixture.GameUIWidgetLifecycleThroughRegistry`、
`GameUIHostTest.BuildSnapshotComposesMountedWidgets`，以及偶发
`RenderGraphCoreTest.ResourceRegistryUsesProvidedImportedImageViewAndRetainsOwner`。
`WidgetTreeTest.SystemLayersCannotBeDetached` 会让测试进程 SIGTRAP，必须从滤镜里排除。
