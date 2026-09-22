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

### 下一刀建议

AB4 优先于 AB3：presentation 与 surface 的耦合是多窗口（`gui-multi-os-window-editor`）的
硬阻塞，而 AB3 只是公开面收窄。AB4 的第一步是把 primary-surface 耦合解开——
`RenderDeviceState::initPresentationResources` 目前从主 swapchain 的 format 建
`SurfaceWritePass`，这让第二个 OS 窗口必须复制整个 renderer。
