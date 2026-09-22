# Progress

## 2026-09-22 AB1 + AB2 — 应用拥有本帧的 view 排布，且有自己的位置

本轮的起点是用户的判断：**`Framework/Render` 里只应放可复用的管线，实际排布与状态
应该由具体应用（GameRuntime / Editor）来做。**

复核后确认方向成立，但起点与最初设想不同：这一轮没有往 Framework 里搬东西，而是
把**放错位置的「排布」的拥有关系**收回到应用侧——当前代码里可见的问题不是应用缺少
机制，而是应用把自己的排布藏在了长寿命 host state 里，而且和 app 生命周期挤在同一
个目录里。

### AB1：`SceneRenderScheduler` 成为 tick 的局部对象

- 删除 `AppRenderState::sceneRenderScheduler`；`tickRender` 内新建局部
  `SceneRenderScheduler`，`beginTick(App::_hostTick)` 紧跟其后。
- `GameRuntimeTickOrchestrator::declareViews` 增加 `SceneRenderScheduler&` 形参；
  `extractScenes` 本来就已经收引用。
- 删除 `SceneSchedulerGuard`（局部对象析构就是 `clearTick` 的语义，不需要手写守卫）。
- `GameRuntimeTickOrchestrator.cpp` 显式 include `Render3D/Common/SceneRenderScheduler.h`
  （此前靠 `AppRenderState.h` 传递，现在自己是实例化方）。
- `AppRenderState.h` 加了一段说明：它只有 host 的设置与注册，没有 per-tick 排布。
- 语义性质：`beginTick → submit → seal` 描述的就是一个 tick，`seal()` 之后
  `submit()` 失败。局部对象把这个不变量变成结构性的，而不是靠「谁记得清空」。

### AB2：`GameRuntime/Lifecycle/` 拆出 `GameRuntime/Render/`

纯搬迁（`git mv`，无内容改动）：

```
Lifecycle/RenderFrameExtractor.cpp      -> Render/RenderFrameExtractor.cpp
Lifecycle/HostSceneExtract.cpp          -> Render/HostSceneExtract.cpp
Lifecycle/RuntimeGameViewProducer.cpp   -> Render/RuntimeGameViewProducer.cpp
Lifecycle/SceneCameraQuery.cpp          -> Render/SceneCameraQuery.cpp
include/GameRuntime/Lifecycle/<同上>.h  -> include/GameRuntime/Render/<同上>.h
```

include 从 `GameRuntime/Lifecycle/<Name>.h` 改为 `GameRuntime/Render/<Name>.h`，共 10 个
文件：`App.h`、`GameRuntimeTickOrchestrator.cpp`、`HostSceneExtract.cpp`、
`RuntimeGameViewProducer.cpp`、`RenderFrameExtractor.cpp`、`SceneCameraQuery.cpp`、
`AppSceneServices.cpp`，以及 `Engine/Test` 的 `RuntimeGameViewProducerTest.cpp`、
`HostSceneExtractTest.cpp`、`EditorViewProducerTest.cpp`。

`Lifecycle/` 现在只剩生命周期本身：`AppLifecycle`、`AppEventRouter`、`FPSCtrl`、
`AppAutomation`、`HostSdlEventSource`、`GameRuntimeTickOrchestrator`。

### 验证证据

- build：`ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-testing` 全部 `build ok`。
- `xmake r ya-render-3d-test`：**177/177**。
- `ya-testing` 滤镜（`RenderRuntime*:HostScene*:ViewFamily*:ForwardFrameGraph*:DeferredRender*:PostProcessing*:Offscreen*:AppKernel*:AppLifecycle*:AppScreenshot*:Widget*:Dock*:Editor*:GameUIHost*:Scene*:UIDocument*:ScriptApi*:RenderGraph*:ViewPersistent*:View*:SurfaceImage*`，排除会 SIGTRAP 的 `WidgetTreeTest.SystemLayersCannotBeDetached`）：698 ran / **681 passed / 11 skipped / 6 failed**，6 个失败与已登记基线逐项相同。
- `python3 Script/automation/render/run_display_compose_parity.py --skip-build`：**PASS**，
  viewport 与 presentation 截图 md5 均为 `c775245ae636f15b41da8485319a2267`，与改动前基线逐字节相同。
- `python3 Script/automation/editor/run_widgettree_editor_smoke.py --skip-build`：exit=0，六步全部通过
  （含「world view 确实渲染出非退化 extent」那条断言）。

### 保留 / 未完成 / 偏离

- 保留：没有把应用排布搬进 Framework（与 `render-view-family` 4.0.3 的「公开 `Renderer`
  作为整帧入口」方向不同，取舍记在 plan §5）。
- 未完成：AB3（renderer 不再承载编辑器查询面）、AB4（presentation 拆纯 pass + 应用侧 present）、
  AB5（命名）、AB6（`Common`/`Services` 归类）、AB7（`RenderDeviceState` → `Renderer`）。
- 偏离：无。AB2 是纯搬迁，未混入任何行为改动。

## 2026-09-22 AB4-step1 — present target 从「主窗口」变成 per-surface

复核到的结构性阻塞：`RenderDeviceState::initPresentationResources` 只为主窗口做一次
`PresentationGraphService` + `SurfaceWritePass`，且 `SurfaceWritePass` 的 pipeline 是从**主
swapchain 的 format** 建的。这使得「第二个 OS 窗口」在结构上要求复制整个 renderer——这正是
多窗口（`gui-multi-os-window-editor`）的硬阻塞。

### 改动

- 新增 `Render3D/Services/SurfacePresentation.{h,cpp}`：一个 OS 窗口的 present 目标，拥有
  `PresentationGraphService`（该 surface 的导入图 + 每图 executor）与它**自己**的
  `SurfaceWritePass`（按该 surface 的 swapchain format 构建），并转发
  `recordDisplayCompose` / `currentImageShared`。
- `RenderDeviceState`：删 `PresentationGraphService _presentationGraphService` 与
  `stdptr<SurfaceWritePass> _surfaceWritePass`，改为
  `std::vector<std::unique_ptr<SurfacePresentation>> _surfacePresentations`，配
  `findSurfacePresentation`（非创建）与 `acquireSurfacePresentation`（find-or-build）。
- `initPresentationResources` → `initSurfacePresentations`：init 期不再建 GPU 资源，也不再
  有主 surface 特权；这里只 push teardown，保证每个 surface 的导入图与 write pass 都在
  render backend 之前销毁（与其它 device 资源同一条有序栈）。
- `record()`：在 `prepareFrameRecord` 之前按 `plan.present.surface` 解析/构建 present target。
  选择点写在注释里——这是录制前的 safe point，`prepareComposePipelines` 已经在同一段建管线，
  而命令录制开始后不再有任何构建。
- `getPresentationImageShared()` 改为收 `IRenderSurfaceContext&`，并明确是**非创建**查询：
  没呈现过的 surface 返回空，不为回答查询而建图。`GameRuntimeTickOrchestrator::iterate` 里两个
  automation 消费点改为显式读主 surface 的 presentation（不再依赖 renderer 上的匿名“当前窗口”）。
- `buildRenderTargetCatalog()` 里那处 `_presentationGraphService` 用法改为显式取主 surface 的
  presentation，并在注释里点明：它是编辑器面向的查询，属于计划 AB3 要搬出 renderer 的部分。

### 验证证据

- build：`ya-render-3d` / `ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-render-3d-test` /
  `ya-testing` 全部 `build ok`。
- `xmake r ya-render-3d-test`：**177/177**。
- `ya-testing` 滤镜：681 passed / 11 skipped / 6 failed，6 个与基线逐项相同。
- parity：**PASS**，两张截图 md5 均为 `c775245ae636f15b41da8485319a2267`（逐字节同基线）。
- 编辑器 smoke：exit=0，六步全过。

### 保留 / 未完成 / 偏离

- 保留：`PresentationGraphService` 本身没改（它已经是「吃一个 surface」的形状），改名到
  `DisplayComposer` 属 AB5。
- 未完成：AB4-step2（额外 OS 窗口走同一条 present 路）与 step3。
- **未验证的部分要说清楚**：产品路径上还没有任何「非主 surface 的 presentation」，
  所以这次改动的好处尚未端到端验证；已验证的是主 surface 路径逐字节不变、
  且表结构允许第二个 surface 自带自己的 format 与 write pass。额外窗口今天仍走 GUI host 的
  `presentGuiSnapshot`（自建 acquire/submit/present，只呈现 GUI chrome，`record` 不参与），
  所以拖出去的 viewport 面板看不到世界画面——这是 step2 要解决的既有缺口。
- 偏离：无。

### 下一刀建议

AB4-step2 优先：让额外窗口的 chrome 每帧被 tick，并让它通过 `record` + 自己的
`SurfacePresentation` 呈现（世界 View 的 display image 与 GUI chrome 一起）。这一步会同时
回答「拖出去的 viewport 为什么不显示世界」这个既有缺口。AB3（renderer 公开面收窄）紧随其后，
因为 `buildRenderTargetCatalog` 这类查询现在就带着一处「主 surface」假设。
