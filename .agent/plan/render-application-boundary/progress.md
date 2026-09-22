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

## 2026-09-22 AB3-step1 — 编辑器通过应用读渲染器，且不再 downcast 到 concrete pipeline

上一轮的结论指出：真正藏起来的第二条主流程还在 `RenderDeviceState`，而第一步应当先把它的公开面收窄。
本轮执行的就是这一步，但把目标写得更可验收：**GameEditor 不再认识 `RenderDeviceState`**，
并且**不再用 `dynamic_cast` 问渲染器「你是什么管线」**。

### 改动的三段

1. **策略身份、设置、编译后的图成为 typed 契约。**
   - `IRenderPipeline` 增加 `kind()`（新的 `ERenderPipelineKind`）与
     `getLastFrameGraphTopology()`；新增 `IRenderPipelineSettings` facet（`resolveSettings()` /
     `requestSettings()`）。`IRenderPipeline` 现在由 Execution + Settings + RenderTargets + DebugOutputs
     四个 facet 组成，与既有风格一致。
   - `DeferredRenderPipeline::SettingsSnapshot` 上移为 `Render3D/Common/RenderPipelineSettings.h` 的
     `RenderPipelineSettings`，并带上 `kind`。字段名保持原样（`bReverseViewportY` / `ssao*` /
     `bPBR*IBL` / `shadow` / `postProcessing`），所以编辑器侧的改动是类型名与 `kind` 判断，不是字段搬家。
   - Forward 实现同一 facet：读 `shadow` / `postProcessing`，**原样携带**只属于 deferred 的块；
     哪个策略读哪一块由 `kind` 说明，而不是由调用方猜。
   - `PipelineCoordinator::ERenderPipeline` 变成 `ERenderPipelineKind` 的别名（`RenderDeviceState::ERenderPipeline`
     的既有别名因此不变），`toString(kind)` 只有一处（`PipelineCoordinator.cpp` 里那份私有重载删除）。
   - `PipelineCoordinator.cpp` 的私有 `toString` 删除后，该文件不再有需要与公共拼写保持同步的名字。

2. **`AppRenderServices` 成为应用侧唯一缝。** 新增（并分组注释）`hasRenderer()`、
   `getRenderPipelineKind()`、`getPendingRenderPipelineKind()`、`setPendingRenderPipelineKind()`、
   `requestRenderPipelineReload()`、`getRenderPipelineSettings()`、`setRenderPipelineSettings()`、
   `getFrameGraphTopology()`、`getViewExtent()`、`getViewDepthFormat()`、`getViewOutput()`、
   `buildViewportSnapshot()`、`buildRenderTargetCatalog()`、`requestRenderTargetFormat()`、
   `getDebugRenderSystem()`、`getDiagnosticsService()`。落点是 `RenderDeviceState` 的四个新转发：
   `resolveActivePipelineKind/Settings`、`requestActivePipelineSettings`、`getActiveFrameGraphTopology`，
   以及 `getViewDepthFormat`。`DeferredRenderPipeline::resolveSettingsSnapshot` 随之改名为
   `resolveSettings`（facet 名），测试里那 1 处调用同步更新。

3. **删掉没有消费者的公开方法。** `buildPipelineDebugOutputCatalog` / `getDeferredPipelineDebugViews`
   的唯一调用方是 `makeViewportDebugCatalogInput`，改为 private；`AppRenderServices::getRenderPipeline()`
   （返回 `IRenderPipeline*`）零调用方，删除。

### 编辑器侧的迁移

| 文件 | 之前 | 之后 |
| --- | --- | --- |
| `RuntimeRenderSettingsSection.cpp` | 3 处 `dynamic_cast` 到 concrete pipeline + `resolveSettingsSnapshot` | `getRenderPipelineKind()` + `getRenderPipelineSettings()` / `setRenderPipelineSettings()` |
| `RuntimeRenderGraphSection.cpp` | 2 处 `dynamic_cast`，只为拿 topology 与名字 | `getRenderPipelineKind()` + `getFrameGraphTopology()` |
| `RuntimeDiagnosticsSection.cpp` | 5 处 `getDeviceState()->getDiagnosticsService()` | `hasRenderer()` + `getDiagnosticsService()` |
| `RuntimeRenderTargetSection.cpp` | `getDeviceState()->buildRenderTargetCatalog()` | `buildRenderTargetCatalog()` |
| `RuntimeDebugPrimitivesSection.cpp` | 5 处 `getDeviceState()->getDebugRenderSystem()` | `getDebugRenderSystem()`（四个开关共用一个 writer） |
| `EditorModule.cpp` | `getViewExtent` / `getActivePipeline()->getViewDepthFormat` / `buildViewportSnapshot` / `getViewOutput` / assert | 全部走 facade，`RenderDeviceState.h` include 删除 |

### 顺带修掉的噪声

`RuntimeDebugPrimitivesSection.{cpp,h}` 与 `RuntimeRenderTargetSection.{cpp,h}` 自 `d9de4739` 起被压成单行
（最大行长 2610 字符）。这两个 `.cpp` 本轮必须改，往单行文件里写新代码没有意义，因此 `git rm --cached`
重建为正常可读形式（无行为变化）。重建时踩到一次 unity 批次重排导致的私有符号重名：我的 `makeCheck`
与 `RuntimeRenderSettingsSection.cpp` 的同名 helper 撞车，改为 `makeDebugSwitch`（记忆文件
`unity_build_duplicate_private_symbol.md` 记录的就是这一类）。

### 验证证据

- build：`ya-render-3d` / `ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-render-3d-test` /
  `ya-testing` 全部 `build ok`。
- `xmake r ya-render-3d-test`：**177/177**。
- `ya-testing` 滤镜：681 passed / 11 skipped / **6 failed**，6 个与已登记基线逐项相同。
- parity：**PASS**，两张截图 md5 仍为 `c775245ae636f15b41da8485319a2267`（逐字节同基线）。
- 编辑器 smoke：**exit=0**，六步全过。
- 结构性证据：`grep -RIn 'RenderDeviceState' Engine/Source/Applications/GameEditor` → 空；
  `grep -RIn 'dynamic_cast<.*RenderPipeline' Engine/Source/Applications/GameEditor` → 空；
  `getDeviceState()` 的剩余调用者只剩 GameRuntime（app 自己）。

### 保留 / 未完成 / 偏离

- 保留：`RenderDeviceState::getDeviceState()` 仍是公开的——它是**应用自己**持有 renderer 的入口，
  app 侧（tick orchestrator、AppAutomation、control service、App）继续用它是对的；本轮的验收对象是编辑器。
- 未完成（AB3-step2）：`DebugRenderSystem&`、`RenderDiagnosticsService&`、`buildRenderTargetCatalog()` /
  `buildViewportSnapshot()` 的返回体仍是 renderer 的类型穿过 facade。它们是 renderer 自己的事实，
  收口方向是 typed command（`setRenderDocCaptureEnabled` 等）与「由数据构造 catalog」的纯函数，
  而不是继续扩大 facade 的引用面。
- 未完成（记录）：`AppAutomation.cpp` 仍在 app 侧直接 include concrete pipeline 头并访问
  `device->_pipelineCoordinator.getSelectedForwardPipeline()` 来施加 automation override。
  这是 app 对 renderer 私有布局的依赖，属于 AB7 拆分时要一起处理的项，本轮未动。
- 偏离：无行为改动；parity 与 smoke 与基线一致。

## 2026-09-22 AB8-step1 — 帧的 View 事实只从计划里读一次

起点是两份结论的同一个判断：`getViewExtent()` 不是「放错层」，而是它的语义缺一个身份；
更根本的是**同一事实有多个来源**。本轮只动两个已被确认重复的事实，命名迁移留给后面。

### 1. pipeline 的 view rect 变成输入

- `RenderDeviceState::_pipelineViewRect` 与公开的 `applyViewResize()` 删除。
  `PipelineCoordinator::applyPendingChanges(Rect2D viewRect)` 现在收本帧的 View rect：
  它来自 `prepareFrameRecord(plan, displayRoot)` 里的 `displayRoot->desc.outputRect`，也就是计划本身。
  coordinator 自己持有 `_appliedViewRect`——**它自己最后套用过的 rect**，不是 View 声明的副本；
  rect 落在已应用值上的变化判定也从 renderer 搬到了这里（它必须留，`requestViewResize` 会标脏并
  在下一次 recordFamily 重建资源）。退化 rect 的语义写清楚了：不是「尺寸 0」，而是「本帧没有 View」，
  已应用的 rect 继续成立——因为 pipeline 会继续按上次的几何渲染，而重建出来的 pipeline 不能回落成
  init 种子。
- `InitDesc.reapplyViewRectSink`（一个捕获 this 的 std::function）删除：重建后要重新套用 rect
  这件事现在由参数表达，`applyPendingRenderPipelineSwitch()` 改为返回「是否重建」。
- init 期的窗口尺寸种子改名为 `_initialViewExtent`，注释说明它只服务第一次 build。
- `declareViews` 里那句 `device->applyViewResize(view.outputRect)` 删除：同一个 rect 过去由
  **两处**（app 每帧 + record 前）推给 renderer，这正是「两个意见」的来源。

### 2. renderer 不再决定哪个 View 是宿主的

- 删除 `_publishedOutputViewId` / `_publishedOutputFlight` / `publishViewOutputIdentity()` /
  `publishedViewOutput()` / `clearPublishedViewOutputs()`。
- 应用侧新增 `HostViewportBinding{viewId, flightIndex}`（`AppRenderState.h`，带说明：这是排布，
  不是 renderer 事实）。唯一写者是 `tickRender`，从 plan 的 display root 写一次，且在 `record()`
  **之前**——因为编辑器的 compose / chrome 阶段是在 record 内部跑的，要读这个 View。
- `record()` 内部改用局部 `displayOutput`。这里有一个必须说清的顺序事实：这个查询只能发生在
  `recordViewFamilies` **之后**（输出是那时发布的），所以变量声明在那一块之后，而不是函数开头。
- 顺带：`insets` 的循环里那句 `getViewOutput(inset.viewId)` 也补上了 flight（inset 自带 viewId，
  过去靠 renderer 记录的「当前 flight」）。

### 3. 查询一律带身份

删除的无身份接口：`getViewExtent()`、`getActiveViewImageShared()`、`getViewDisplayImageShared()`、
`getPostprocessOutputImageShared()`、`getViewDisplayImage()`、`getViewDisplayImageFormat()`。
替代：`getViewOutput(flightIndex, viewId)`、`surfaceImageFor(const RenderViewOutput*)`、
`buildViewportSnapshot(flightIndex, viewId, Scene*)`。`prepareComposePipelines()` 改用
pipeline 自己的 `getPostprocessColorFormat()`。

应用侧 `AppRenderServices` 用 binding 解析出 `getHostViewportOutput()` / `getHostViewportViewId()` /
`getViewOutput(viewId)`，并保留 `buildViewportSnapshot(Scene*)` 这个应用名义的入口。
自动化截图的三张图（postprocess / viewport / presentation）改为在
`GameRuntimeTickOrchestrator::iterate` 里**指名**取值：postprocess 图只在
`display != color` 时存在，否则那张图就是 View 的 color——这个「先给两张，让消费者挑」的规则
过去藏在 device 的两个 getter 里，现在写在调用点。
`get_world_view_state` 的 `rendered_viewport_extent` 改读宿主 View 的 `desc.extent`。
编辑器两处（相机 aspect、canvas 目标尺寸）改读 `getHostViewportOutput()->desc.extent`。

### 4. 顺带删掉的两处已死重复工作

- `prepareFrameRecord` 里那段「有 UI snapshot 就 prepare RuntimeUIComposite」的调用：它读的是
  **上一帧**已发布的 display image 格式（`publishViewOutputIdentity` 在 record 后半段才执行），
  而同一个函数上方三行的 `prepareComposePipelines()` 已经用 pipeline 自己的 postprocess format
  做过同一件事。冗余且是陈旧读。
- `record()` 结尾的三个 `retain(...)`：`retainPublishedViewOutputs()` 已经保活了每个 live view 的
  `displayImage()` / `color` / `depth` / `entityId`，那三个调用没有新增任何保活对象。

### 验证证据

- build：`ya-render-3d` / `ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-render-3d-test` /
  `ya-testing` 全部 `build ok`。
- `xmake r ya-render-3d-test`：**177/177**（`RenderRuntimeSnapshotTest.EmptyDevicePublishesEmptyViewportResources`
  改为按 (flight, viewId) 查询，正是本轮要证明的语义）。
- `ya-testing` 滤镜：681 passed / 11 skipped / 6 failed，6 个与已登记基线逐项相同。
- parity：**PASS**，两张截图 md5 均为 `c775245ae636f15b41da8485319a2267`（逐字节同基线）。
- 编辑器 smoke：**exit=0**，六步全过（其中第 2 步断言 `rendered_viewport_extent` 非退化，
  现在这条断言走的就是「宿主 View 的 output extent」）。

### 保留 / 未完成 / 偏离

- 保留：`HostViewState.{view,projection,cameraPos}` 仍在（= AB8-step2）。编辑器视口的相机、
  overlay 与 picking 今天读的仍是它，所以 declareViews 里的三个赋值这轮没有动。
- 保留并记录一处差异（不录制的那一帧）：binding 在 `record()` 之前写，所以当 `acquirePresentFrame`
  失败、整帧不录制时（surface 被最小化等），binding 已经指向「本帧本来打算用的 flight」；旧代码那时
  保留上一帧的 identity。在 `flightFrameSize = 1` 的实际配置下 flight 恒为 0，两者都读到上一次录制的
  结果，行为一致；多 flight 时这是一个需要重新审视的点，记在这里而不是假装不存在。
- 未完成：AB8-step2（HostViewState 拆分，需要先定 PIE 相机归属）、AB7（record 编排搬回应用侧）。
- 偏离：无。parity 与 smoke 与基线逐字节一致。


## 2026-09-22 AB8-step2 — HostViewState 拆成设置与排布

step 1 留下的那份重复：`HostViewState` 同时装着 `clock/renderResolution/renderScale`（设置）
与 `view/projection/cameraPos`（某个 `SceneViewDesc` 的副本），于是「这个 struct 是设置还是
某一帧的 View」在读到它的时候无从判断。

### 改动

- **文件与 struct 改名**：`HostViewState.h` → `HostRenderSettings.h`，
  `HostViewState` → `HostRenderSettings`，只剩 `clock` / `renderResolution` / `renderScale`。
  头注释重写：说明它**只是设置**，以及为什么这么拆（原来一个 struct 里既有设置又有 View 声明的副本）。
- **新增 `GameRuntime/HostViewportView.h`**：`HostViewportView{viewId, flightIndex, view,
  projection, cameraPos}`，把上一轮的 `HostViewportBinding{viewId, flightIndex}` 与它需要的相机
  合成一个值，并加 `isBound()` / `viewProjection()`。
- **一个写者**：`tickRender` 从 plan 的 display root 一次性写整个值（viewId / flightIndex /
  view / projection / cameraPos）；`declareViews` 因此**不再写任何 host state**——它现在只做
  「声明 + submit」，之前那段 `bHostViewportDeclared` 循环与 identity 回退分支整体删除。
  相机仍来自 plan 的 display root，所以 PIE 下依旧是游戏相机：行为不变。
- **读者改名**：`EditorViewportCompositor::compose` / `composeWorldFallback` /
  `composeWorldFromScene`、`makeEditorSurfaceContext`、`EditorLayer::pickEntity` 的参数从
  `const HostViewState&` 变成 `const HostViewportView&`（`worldComposeDesc` 里的
  `projection * view` 也改为值上的 `viewProjection()`）；`get_world_view_state` 的 `camera_pos`
  读排布值；`AppRenderServices::getHostViewState()` → `getHostRenderSettings()` 加
  `getHostViewportView()`。

### 一处自我修正

上一轮我在计划里写「step 2 需要先决定 PIE 下 overlay/picking 用哪个相机，所以不机械搬迁」。
**这个判断是错的**：保持今天是宿主 display root 的相机（PIE 下即游戏相机）就是逐字节等价，
本轮的目标是消除「设置里抄一份 View 声明」，不是换相机。剩下的是一个独立的产品问题——
**PIE 下编辑器视口的 overlay/picking 该不该跟着游戏相机**——它现在有了明确的落点
（`HostViewportView` 的一个字段），可以在不牵动其它结构的情况下单独讨论。计划文件已改正。

### 验证证据

- build：`ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-testing` 全部 `build ok`。
- `xmake r ya-render-3d-test`：**177/177**。
- `ya-testing` 滤镜：681 passed / 11 skipped / 6 failed，6 个与已登记基线逐项相同。
- parity：**PASS**，两张截图 md5 均为 `c775245ae636f15b41da8485319a2267`（逐字节同基线）。
- 编辑器 smoke：**exit=0**，六步全过。
- 结构性证据：`grep HostViewState Engine/Source` 只剩 `prepareHostViewState` 这个函数名（它写的是
  clock，函数名可以留）；`hostView.` 字段访问全仓为 0。

### 保留 / 未完成 / 偏离

- 保留：`prepareHostViewState` 这个函数名还用「HostViewState」的说法——它现在写 `HostRenderSettings`
  的 clock，改名（例如 `updateHostClock`）是纯改名，留到命名批次一起做，不在本轮混入。
- 未完成：AB3-step2（debug/diagnostics/catalog 的引用面收窄）、AB4-step2/3、AB7。
- 偏离：无。parity 与 smoke 与基线逐字节一致。


## 2026-09-22 AB9 — 计划对齐与死代码清理（review 第一批）

这一轮不改大类，只做「先把计划和实际状态对齐 + 删掉噪声」。起点是一份 review，它的判据我认同：
偏差集中在**所有权已经移动了一部分，但语义和状态仍留在旧 owner 里**。

### 1. 修掉一处真死代码（不是文档问题，是地雷）

`GameRuntimeTickOrchestrator::pumpOffscreenTasks` **只有自我递归调用，没有任何调用者**：

```cpp
void GameRuntimeTickOrchestrator::pumpOffscreenTasks(App& app, RenderDeviceState* device)
{
    YA_PROFILE_SCOPE("Render/PumpOffscreenTasks");
    pumpOffscreenTasks(app, device);   // <- 自己
}
```

而 `tickRender` 直接调 `device->getOffscreenTaskService().tick(app.getTaskManager())`。663e0f82
（上一轮会话）的意图是把这一步命名出来，但命名只写进了头文件和一个自我调用的函数体，真实调用留在
tickRender 里匿名。修法是**把步骤接回去**：`tickRender` 调 `pumpOffscreenTasks`，函数体做实际工作，
注释说明为什么它必须是录制前的步骤。命名本身是正确意图，删名字只会丢掉它。

### 2. 删除已死的 pipeline 尺寸接口

`IRenderPipeline::getViewExtent()` / `ForwardRenderPipeline::getViewExtent()` /
`DeferredRenderPipeline::getViewExtent()` 删除（零调用方）。它暗示「一个 pipeline 有一个 View 尺寸」，
而多 View（材质预览 / 编辑器作者视口 / 游戏视口尺寸不同）下这个语义不够——这正是 review 指出的缺口。
**只删接口不代表缺口已补**：pipeline 内仍保存单套 View 资源，那一项记在 plan §4b。

### 3. 删除不再使用的形参

`declareViews(App&, float, RenderDeviceState* device, SceneRenderScheduler&)` 的 `device` 自
`applyViewResize` 删除后再无使用（AB8-step1 之后）。删掉。

### 4. 计划与实际状态对齐（防误导）

`render-view-family/plan.md` 的开头状态、§1 主线、§2 主 loop 事实、§3.7 命名映射表、§4 checkpoint 2
都仍写着「`RenderDeviceState` + `RenderFrameCoordinator` 是一次不完整拆分，下一刀是合并为公开
`Renderer`」。Coordinator 早已删除，而「改成公开 `Renderer`」这个方向也已在 AB7 被推翻。

- 文件头加了状态校正：Coordinator 已删除；**下一刀在 `render-application-boundary`**；本文件保留历史
  叙述，方向性决策以后者为准。
- §2 顶部加了一段「先读这条」的校正块，列出真实主 loop、`RenderDeviceState` 今天的完整职责清单、
  `PreparedView` 尚未落地、多 View extent 尚未完成。**§2 以下的原文保持不动**并明确标注「描述的是修复前
  的状态」——历史叙述的价值在于记录当时为什么那样判断，重写它等于销毁证据。
  （一处例外说明：那行原文里的 `→` 字符无法被 patch 精确匹配，我改用「在章节顶部加校正块」，
  效果相同且不动原文。）
- §3.7 命名映射表的 `Renderer` 行标记为**已作废**并写明理由。
  §4 checkpoint 2 改写为「前半已完成、后半作废」，指向 AB7。

### 5. 把核对过的偏差一次记清（plan §4b）

新增一节表格，逐条列出**已确认但未完成**的所有权偏差，每条带当前证据与目标归属：整帧录制编排；
`AppRenderState::viewFrameDataPerFlight`（并注明动手前必须先证明保活关系，`hostFrameData()` 目前只有
测试在读）；pipeline 单 View 资源；`setActiveSceneProvider` 的隐式当前 Scene；`recordExtensions` 仍
携带行为；renderer 的编辑器查询面与 `Render3D -> GUI/Compose` 的 include。

### 验证证据

> **证据的取得方式需要说明**：主仓库在当前时刻**编译不过**，原因不是本轮改动——另有并发写者正在把
> `ForwardViewResources` / `DeferredViewResources` 的裸指针别名（`color` / `depth` / `entityId`）删掉，
> 而 `ViewportDebugCatalogBuilder.cpp` 仍有 4 处读 `.color` / `.depth`（未提交的 WIP，含一行注释掉的旧
> 表达式）。那是他们的文件，我不改。为了拿到真实的端到端证据，我把仓库用 APFS clonefile 复制到临时目录，
> 在**副本里**把这两个头文件 checkout 回 HEAD（即去掉他们的 WIP，得到等价于本 checkpoint 的树），
> 在那里构建并运行全部验证。这样既不触碰他们的文件，也不是「只跑单测」。

- build：`ya-render-3d` / `ya-runtime` / `ya-game-editor` / `ya-render-3d-test` / `ya-testing` 全部
  `build ok`（`ya-game-runtime` 在主仓库当时的干净时刻也已 `build ok`）。
- `xmake r ya-render-3d-test`：**177/177**。
- `ya-testing` 滤镜：**681 passed / 11 skipped / 6 failed**，6 个与已登记基线逐项相同。
- parity：**PASS**，viewport 与 presentation 截图 md5 均为 `c775245ae636f15b41da8485319a2267`
  ——与基线逐字节相同。这条是本轮最有价值的证据：它证明「把 `pumpOffscreenTasks` 接回去」与原来的匿名
  调用**行为完全一致**（唯一差异是那个原本就在死函数里的 profile scope 现在真的生效了）。
- 编辑器 smoke：**exit=0**，六步全过。

### 保留 / 未完成 / 偏离

- 保留：`AppRenderState::viewFrameDataPerFlight` 与 `AppLifecycle` 的两处清空循环**没动**。它的迁移
  需要先证明 tick 之外没有读者与保活依赖，属于 review 第二批，单独一刀。
- 未完成：review 第二至五批（viewFrameDataPerFlight 归属、pipeline 单 View 状态、移除当前 Scene
  provider、拆 `RenderDeviceState`）。方向与证据已写入 plan §4b，不再需要重新调研。
- 偏离：无。

### 下一刀建议

AB7 现在是下一刀：帧的 View 事实已经有唯一来源（AB8），renderer 不再持有任何「当前 View」的
记忆，「这一帧如何组装、如何 present」是最后一块还留在 Framework 的应用职责。开刀前要先处理一项
已知挡路依赖：`AppAutomation.cpp`（以及控制面里施加 automation override 的地方）直接访问
`device->_pipelineCoordinator.getSelectedForwardPipeline()`——这是 app 对 renderer 私有布局的依赖，
拆分时会挡住，先给它一个 typed 入口。

AB4-step2（额外窗口）是 AB7 的下游：额外窗口之所以只能自己 `presentGuiSnapshot`，正是因为
「这一帧如何组装、如何 present」被写死在 renderer 里。

另有一个独立的产品问题挂着（不阻塞以上任何一步）：**PIE 下编辑器视口的 overlay / picking
该不该跟着游戏相机**。今天跟（因为读的是宿主 display root 的相机），另一种答案是跟编辑器相机。
AB8-step2 之后这个选择只剩 `HostViewportView` 的一个字段，可以单独讨论。
