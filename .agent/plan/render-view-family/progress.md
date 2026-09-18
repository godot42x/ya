# Progress

## 2026-09-19 checkpoint：tickRender 的每一步自己持有自己的存储

接同一天上一刀（删 overlay 通道 + per-tick 后端查询）。这一刀是 `todo.md`「逻辑→渲染链减法」批次的
第 1 项：让 `tickRender` 的三处"调用方持有的生命周期"不再靠注释说明。三处是同一个根因——
**一个步骤把自己的临时存储交给调用方**，于是"它活到什么时候"只能写在注释里。

1. `declareViews(..., SceneViewCollector& collector)` → `declareViews(app, dt, device)`，collector 变成
   这一步的局部变量。原来的签名与注释声称"调用方要持有 collector，因为拿回去的声明后面还会被引用"，
   查证后**两半都不成立**：`SceneRenderScheduler::submit(SceneViewDesc desc)` 是按值接收（拷贝进本帧
   调度状态），而返回的 `const SceneViewDesc*` 在 `tickRender` 里**根本没有被使用**——V1 之后 host
   camera 走 `ExtractedSceneRender::hostFrameData()`，不再需要那个指针。于是返回值删除（只有声明没有
   消费者，出现即删），collector 收回本步。`SceneViewDesc` 内只有值字段与 `Scene*`（Scene 活得比本帧
   长），没有指回 collector 的引用，所以这个收回是安全的。
2. `buildGameRenderFrame(app, dt, flightIndex, UIFrameSnapshot& outUiSnapshot)` → 返回 `TickFrame`。
   out-param 存在的唯一理由是"返回的 `FramePacket` 指向它"，于是调用方必须先声明 `uiFrameSnapshot`、
   再在调用之后继续保持它活着——一条只写在头文件注释里的契约。现在 `TickFrame{ uiSnapshot; frame; }`
   把两者放在一起，`boundFrame()` 在读取时把 `frame.uiFrameSnapshot` 绑到自己的 snapshot 上。
   **绑在读取时而不是存储时**，是因为这个值是按值返回的：存下来的指针会留在被移动走的那个对象上。
3. `hostViewDesc`（指进 collector vector 的局部指针）随第 1 条一起消失。

顺带的头文件清理：`TickFrame` 按值持有 `UIFrameSnapshot` 与 `FramePacket`，所以 orchestrator 头
显式 include `GUI/Widgets/UIFrameSnapshot.h` 与 `Render3D/Common/RenderFrameInputs.h`（后者原本靠
`IRenderPipeline.h` 传递才完整，属于"碰巧能编"）；上一刀之后已经没人用的 `RenderOverlay.h` include
与 `UIFrameSnapshot` / `SceneViewCollector` / `FramePacket` 三个已由 include 满足的前向声明删除。

### 验证

| 检查 | 结果 |
| --- | --- |
| `xmake b` | `ya-game-runtime` / `ya-game-editor` / `ya-runtime` / `ya-engine` 全部 ok |
| `ya-testing` 渲染/编辑器滤镜 | 282 passed / 3 failed，3 个失败与基线逐项相同 |
| HelloMaterial viewport 截图 | exit=0，`md5=1c6668976be1cdd5d755d1f1365700f7` **逐字节相同** |
| run-editor viewport 截图 | exit=0，`md5=5b8f5dd8acdbc90bd5c383feb3f7bcec` **逐字节相同** |

这一刀不只是"移动代码"：runtime 截图走的正是 UI snapshot 被 `boundFrame()` 绑定后送进 display compose
的那条路，所以逐字节相同证明绑定点真的生效（如果绑定漏了，snapshot 指针为空、Game UI 不会上屏）。

三个失败逐个核对：`EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo` 与
`EditorPropertyGraphTest.TextureAssetRowShowsRetainedPreview` 是资产路径/预览，`GameUIHostTest.BuildSnapshotComposesMountedWidgets`
失败在 `GameUIHost::buildSnapshot()` **内部**（`snapshot.items.size()` 为 0，测试环境问题），三者都不经过
本次改动的代码路径。

### 本刀边界（未做）

- `HostViewState` 单一写者（今天 4 个写者 / 3 个文件）。
- 公开 `Renderer` 合并（4.0.3 checkpoint 2/3）：`friend struct RenderFrameCoordinator` 与 5 处
  `_device->_` 私有写入仍在。

## 2026-09-19 checkpoint：删掉主机 screen-overlay 通道与 per-tick 后端查询

本轮范围仍是用户本轮的目标：**只做 app 主流程 + 渲染主流程上的减法**。判据只有两条——
"没有生产者的东西"删掉；"每个 tick 重复做的展示工作"从 tick 里移出。四条删除：
不可达 demo、per-tick 后端查询、死步骤 `syncViewportState`、`resolveViewportExtent` +
`SceneViewCollectContext::viewportExtent`；以及第 5 条——**主机 screen-overlay 通道，含一整条
每帧都在跑的 graph pass**。第 5 条是本刀最大的净删除，也是"同一原则在更大颗粒度上的实例"。

### 1. 不可达的 demo 路径

`AppMode` / `App::_appMode` / `App::clicked`、`dispatchInputFallbackEvent` 里的
`MouseButtonReleased && _appMode == Drawing` 分支、`buildScreenOverlaySprites`（26 行）全删。
`_appMode` 只有初值 `Control` 且全仓无写入点，所以那个分支恒假、`buildScreenOverlaySprites`
首行即返回空 vector：这是当年留下的 demo，不是产品路径。保留 `_lastMousePos`
（`gameUIHost->dispatchEvent` 在用）。

这条 demo 正是下面第 5 条 overlay 通道的**唯一生产者**——它一删，通道就没有生产者了。

### 2. 窗口标题移出 per-tick

tickLogic 末尾每帧做两件不该在 tick 里的事：`render->as<VulkanRender>()` 取
`_selectedDeviceInfo.deviceName`，再 `nativeWindow->setTitle(...)`。`as<>` 是无运行时检查的
后端降型（非 Vulkan 构建下就是错值/UB），setTitle 是每帧重复的展示工作。

- 新增 `IRender::getDeviceName()`（默认返回 `{}`，`VulkanRender` override 返回
  `_selectedDeviceInfo.deviceName`），替代降型。
- 标题改在 `RenderDeviceState::initRenderBackend` 里 `_render->init(renderCI)` 之后设一次：
  它是**设备初始化**的事实，不是每帧事实。

**偏离说明（如实记录）**：一开始把标题整段删掉了，随后恢复成一次性 init。删掉等于无解释地砍掉
一个特性；改成 init 才是"净效果相同 + 归属诚实"。

### 3. 死步骤 `syncViewportState`

唯一实现是 `(void)app;`，还带一个 `Logic/ViewportSync` profile scope（profile 里会出现一个
恒定零耗时的行）。删。

### 4. `resolveViewportExtent` 与 `SceneViewCollectContext::viewportExtent`

（用户提出的问题：viewport 概念是否也需要移除。）查证结果：

- `SceneViewCollectContext::viewportExtent` **零读者**；
- `context.viewportRect` 只有一个消费者 `RuntimeGameViewProducer`；
- 三段兜底里 `device->getViewportExtent()` 在 init 之后恒非零，所以 `viewportRect` 段与
  `_windowSize` 段都是死代码；
- 于是它实际返回的永远是"**上一 tick 发布的 device extent**"——正是这条链在删的"全局 viewport"。

现在 host camera 的 aspect 直接读 `app._renderState->hostView.viewportRect.extent`
（`syncRuntimeCameraAspect` 自己忽略退化 extent）。这是 logic 链上对"上一 tick 发布结果"的
最后一次读取，去掉后 logic 段只依赖本 tick 的 host 事实。

**语义澄清（"viewport 概念"的处置）**：没有消失，而是它就是已存在的两件事——
`SceneViewDesc::extent`（声明方说"这个 view 要多大"）与 `RenderDeviceState::getViewportExtent()`
（V3 起只有 published / `{}` 两个来源，读的是**已发布输出**的尺寸）。真正被删掉的是两者之间的
**投影函数**：一个把"上一帧的输出尺寸"当成"这一帧的输入尺寸"的转译层。producer 需要 extent 就
从 `viewportRect` 自己派生（`SceneViewProducer.h` 的字段注释已如此写）。

### 5. 主机 screen-overlay 通道（整条 pass 删除）

顺链路 trace 时才看出来：`FramePacket::OverlayInput`（screenSprites / worldSprites /
screenTexts / worldLines 四路可选 vector）的唯一生产者是第 1 条删掉的 demo；
`buildViewportOverlaySnapshot` 在四路全空时返回 `nullptr`；而 Forward/Deferred 的 overlay
pass 在 `nullptr` 下**仍然 append 并每帧录制一次**。即 `kTopologyPassOverlay` 在每个被测
路径上都以空 snapshot 运行，且没有任何测试能发现——因为**空输入是合法输入**。

删除清单：

- 输入/上下文：`FramePacket::OverlayInput` 与 `FramePacket::overlay`、
  `ViewFamilyRecordContext::overlaySnapshot`、`RenderPipelineFrameContext::viewportOverlaySnapshot`、
  `ForwardFrameGraphPasses::BuildInputs::viewportOverlaySnapshot`、
  `DeferredFrameGraphPassContext::viewportOverlaySnapshot`。
- pass：`DeferredOverlayPassParams`、`deferred_frame_graph_passes::appendOverlay`、
  `kTopologyPassOverlay`（Forward/Deferred 各一）、`appendOverlayPass`、`OverlayPassParams`、
  `ForwardViewportPassParams::overlay`、两处 `.viewportOverlaySnapshot =` 赋值。
- 类型与实现：`RenderViewportOverlaySnapshot`、`recordRenderViewportOverlayPass`、
  `prepareRenderViewportOverlayPipeline`；`RenderOverlay.cpp` 整文件 `git rm`。
- 观测：`perf::sample::renderViewportOverlay()` 与 `GameRuntime/Profiling.cpp` 里对应的两行。
- 签名：`RenderFrameCoordinator::recordViewFamilies(plan, cmdBuf, overlaySnapshot)` 去掉第三参；
  `buildViewportOverlaySnapshot` helper 删除。

`RenderOverlay.h` 保留 `RenderOverlayText2D` / `RenderOverlayLine3D`（编辑器 HUD 与选中相机
视锥仍用），但头注释写明它们是**值**、由"谁录 overlay 谁直接读"，不是让 renderer 携带的 snapshot：
编辑器在自己的 viewport compose 里画 overlay，所以"编辑器的 overlay"是编辑器自己的事实。

**为什么能活到现在**：这条通道的输入是"四路可选 vector"，空是合法值，于是"没有生产者"与"这一帧
没有 overlay"在代码里长得完全一样。修法是删除，不是补一个消费者。已同步进
`.agent/skills/render-arch/SKILL.md` 第 15 条（把"只有声明没有消费者的接口方法"扩到"接口方法 /
字段 / 整条 pass"），并写下 `.agent/memories/dead_snapshot_channel_survives_empty_input.md`。

### 验证

| 检查 | 结果 |
| --- | --- |
| `xmake b` | `ya-render-3d` / `ya-game-runtime` / `ya-game-editor` / `ya-testing` / `ya-render-3d-test` / `ya-engine` / `GUIWorkbench` / `GreedySnake` / `ya-gui-closure-test` / `ya-gui-headless-host-test` / `ya-render-2d-test` / `ya-gui-widgets` 全部 ok |
| `xmake r ya-render-3d-test` | **175/175**（176 → 175；唯一减少的是钉住已删状态的 `OverlaySnapshotEmptyIncludesWorldLines`） |
| `ya-testing` 渲染/编辑器滤镜 | 271 passed / 3 failed，3 个失败与 V1–V8 基线逐项相同（`EditorPropertyGraphTest` ×2 + `GameUIHostTest.BuildSnapshotComposesMountedWidgets`） |
| HelloMaterial viewport 截图 | exit=0，0 error，`md5=1c6668976be1cdd5d755d1f1365700f7` **逐字节相同** |
| run-editor viewport 截图 | exit=0，0 error，`md5=5b8f5dd8acdbc90bd5c383feb3f7bcec` **逐字节相同** |

删除的测试：`RenderRuntimeSnapshotTest.OverlaySnapshotEmptyIncludesWorldLines`、
`DeferredRenderPipelineTest` 里的 `DeferredOverlayPassParams` 断言块、
`ForwardFrameGraphOrchestratorTest` 的 `EXPECT_FALSE(inputs.viewportOverlaySnapshot)`，
以及两个 producer 测试 context 里的 `.viewportExtent`。两张截图逐字节相同说明：这条通道在
**实际产品路径上本来就是空跑**，删掉它不改变任何像素。

### 本刀边界（未做项，已记入 todo.md）

- `tickRender` 里三处"调用方持有的生命周期"（`SceneViewCollector` / `UIFrameSnapshot` /
  指向 collector 的 `hostViewDesc` 指针）仍靠注释解释谁活到什么时候。
- `HostViewState` 仍是 4 个写者 / 3 个文件（`AppLifecycle.cpp`、`AppRenderServices::setViewportRect`、
  `prepareHostViewState` 的 identity reset + `declareViews` 覆盖）。
- 公开 `Renderer` 合并（4.0.3 checkpoint 2/3）：`friend struct RenderFrameCoordinator` 与 5 处
  `_device->_` 私有写入仍在，`record()` 的 7 步录制前准备与 10 步录制还没拆成两个函数。

## 2026-09-19 checkpoint：V7 离屏 pump 具名 + V8 viewport debug catalog 移出 device

两条独立、互不相关，同一个 commit：都属「把隐式/错位的东西显式化」，且都不动 GPU 时序。

### V7 离屏 pump 从 tickRender 的一行变成具名步骤

`tickRender` 里原先只有一行 `device->getOffscreenTaskService().tick(app.getTaskManager());`，
读代码看不出它在等什么、为什么必须在 View 准备之前。现在提成
`GameRuntimeTickOrchestrator::pumpOffscreenTasks(app, device)`，带 profile scope，并在
`tickRender` 与头文件两处说明它是**录制前的前置阶段**：上一次 tick 提交的离屏任务在这里
被 fence 等待并 finalize，本 tick 排队的任务在这里录制提交（下一个 pump 才可读），所以它
必须在任何 View 准备读派生资源之前。

顺带删掉 `RenderDeviceState::isOffscreenPending()`：全仓零消费者，正是上一轮写进 skill 第 15
条的「只有声明没有消费者的接口方法，出现即删」。V7 的验收原文是「isOffscreenPending() 的
消费者说明它在等什么」——它没有消费者，所以处置是删除，不是补一个消费者。

### V8 viewport debug catalog 移出 device

`RenderDeviceState.ViewportDebug.cpp`（652 行）整体移到
`Render3D/Debug/ViewportDebugCatalogBuilder.cpp`，公开头
`Render3D/Debug/ViewportDebugCatalogBuilder.h`。搬法不是「换个地方继续读 device」，而是把它
变成**已解析句柄的纯函数**：

- 新增 `ViewportDebugCatalogInput`：`bForwardPipeline` / `bDeferredPipeline` /
  `RenderPipelineDebugOutputCatalog` / `DeferredPipelineDebugViews` / brdfLut /
  `environmentLighting` / `inspectScene` / **铺平的 36 个 point-shadow face 句柄**。
- 三个自由函数取代三个 device 方法：`buildViewportDebugCatalog(input)`、
  `appendViewportDebugImages(images, catalog, input)`、`viewportDebugCatalogSignature(input)`。
- 原先 builder 内部把点光面查询包成 lambda 的写法（`runtime.getShadowPointFaceDepthResource`）
  改成直接读 `input.pointShadowFaces[]`——**输入是数据，没有回调**，与 V6 的原则一致。
- `mutable` 缓存移出 device，变成 `ViewportDebugCatalogCache`（持 digest + catalog），device 只留
  `mutable ViewportDebugCatalogCache _viewportDebugCache{}`。
- device 保留唯一的解析点 `makeViewportDebugCatalogInput(Scene*)`：它决定「这个 renderer 愿意给
  检查器看哪些资源」，是这条路里唯一有资格读 device 内部的地方。
- `RenderPipelineDebugOutputCatalog` 从 `RenderDeviceState.h` 移到 builder 头。

**验收偏离（如实记录）**：plan 附.2 的 V8 验收写「rg ViewportDebug 在 RenderDeviceState* 为 0」。
实际不成立，也不该成立：device 仍有两处提到这个类型——`makeViewportDebugCatalogInput`（解析句柄）
与 `buildViewportSnapshot`（取 catalog 与 images）。真正达标的是**实质**：`RenderDeviceState` 的
公开面不再有 `buildViewportDebugCatalog` / `appendViewportDebugImages` /
`buildViewportDebugCatalogSignature` / `ensureViewportDebugCatalog`，650 行实现也不在 device 的文件里。
把「device 完全不许提这个类型」当目标是错的——它必须提供句柄来源。

**没做的部分**：plan 原文说「顺带断 `ya-render-3d -> ya-gui-compose` 中属于 debug/compose 的那部分」。
查证后这两者无关：debug catalog 的旧文件并没有 include GUI/Compose；`Render2DComposePass.h` 出现在
`RenderDeviceState.cpp` 与 `RenderFrameCoordinator.cpp`，用途是 `prepareRender2DComposePassPipeline`
（runtime UI compose 的 pipeline 准备），属 compose 而非 debug。把它移出 renderer 需要先把 compose
prep 交给 host 调用（编辑器已经自己调了，runtime 路径没有），是一个独立的依赖边界改动，记在
`source-layout-subtraction` S2（`ya-render-3d -> ya-gui-compose`）而不是塞进本刀。

### 验证

| 检查 | 结果 |
| --- | --- |
| `xmake b` | `ya-render-3d` / `ya-game-runtime` / `ya-game-editor` / `ya-testing` / `ya-render-3d-test` / `ya-engine` / `GUIWorkbench` / `GreedySnake` / `ya-gui-closure-test` / `ya-gui-headless-host-test` / `ya-render-2d-test` 全部 ok |
| `xmake r ya-render-3d-test` | **176/176** |
| `ya-testing` 渲染/编辑器滤镜 | 265 passed / 3 failed（与 V1–V6 基线逐项相同） |
| `HelloMaterial` viewport 截图 | exit=0，0 error，`md5=1c6668976be1cdd5d755d1f1365700f7` **逐字节相同** |
| `run-editor` viewport 截图 | exit=0，0 error，`md5=5b8f5dd8acdbc90bd5c383feb3f7bcec` **逐字节相同** |

debug catalog 只在编辑器 Debug Images 面板消费，smoke 不打开该面板，因此两张截图相同主要证明
「这条路的搬移没有波及渲染主链」。catalog 内容等价性由代码保证：搬移是把 `runtime.X()` 换成
`input.X`，唯一语义变化是 point-shadow face 查询改成读预解析数组，而那个数组由同一个
`getShadowPointFaceDepthResource` 在同一帧填出。

### 遗留（V 系列至此收口）

V1–V8 全部落地。剩余的是计划里本就排在别处的项：`RenderViewRecordingContext` 与
`RenderPipelineFrameContext` 并层、`RenderFrameData` → `PreparedViewRenderData` 改名（P3 命名批次）、
owner-scoped `SceneViewKey`（4.0.3 checkpoint 5）、公开 `Renderer` 合并（4.0.3 checkpoint 2/3 与
P2 flight 改名）。

+## 2026-09-19 checkpoint：V6 RenderFramePlan 去掉回调

### 唯一目标

让"一帧的录制顺序"在一个文件里连续可读。原先 `RenderFramePlan` 携带四个
`std::function`（`viewCompose.recordCompose`、`displayCompose.extensions.recordBeforeExtensions`、
`recordExtensions`、`appendCapture`）：host 在 GameRuntime 里组装闭包，Render3D 决定
它们在哪一行被调用，**两边都读不出完整时序**。

### 做法

新增 `Render3D/Common/FrameRecordExtensions.h` 定义 `IFrameRecordExtensions`，
把阶段命名成渲染侧的词汇：

| 接口方法 | 原回调 | 阶段 |
| --- | --- | --- |
| `recordViewCompose` | `viewCompose.recordCompose` | world/UI/view compose 之后，display compose 之前 |
| `recordBeforeDisplayExtensions` | `extensions.recordBeforeExtensions` | display compose 内，presentation graph build 之前 |
| `recordDisplayExtensions` | `extensions.recordExtensions` | display compose 内，host 自己的 graph 内容 |
| `appendDisplayCapture` | `extensions.appendCapture` | display compose 内，追加 capture pass（automation） |

每一个都是带默认空实现的虚函数——"这个 host 在这一步什么都不录"是合法状态
（headless、UI-only 帧正是如此）。

- `RenderFramePlan` 现在只有一个 `IFrameRecordExtensions* recordExtensions`，
  **`std::function` 数量 0**（验收要求 ≤1）。
- `DisplayComposeInput` 整个删除（它本来只装 extensions）。
  `ViewComposeInput` 只剩 `insets` 数据——行为不再挂在数据上。
- `PresentationGraphService::Extensions` 整个删除（它是一份平行的阶段描述），
  `recordDisplayCompose(float, IFrameRecordExtensions*, ICommandBuffer*)` 直接吃接口。
- `App` 实现 `IFrameRecordExtensions`；三个 `recordModule*` 转发方法改名为接口方法
  并直接写循环，`appendDisplayCapture` 吸收原先在 GameRuntime 里那两个 automation
  调用。host 侧因此不再有"装配回调"的代码。
- `recordCameraViewCompose` 丢掉 `viewCompose` 形参（它只用来调 `recordCompose`）；
  该阶段调用点移到 coordinator —— 位置等价（都在 Render2D compose pass 之后、
  display compose 之前），但顺序现在写在 `record()` 里。
- `record()` 头部补一段顺序注释，把
  `graphics → UI → view compose → display compose → capture` 一次说清。

### 诚实边界：这一刀修的是什么，不是什么

- **修了**：顺序只在一处（`record()`）；plan 不再携带行为；阶段成为 Render3D 的
  词汇，不再由 host 侧定义一份平行描述。
- **没修**：host 忘记实现某一步仍然是静默空转（默认虚函数是 no-op）。这一点与
  回调版本一致，且**不能**用断言消除——headless / UI-only 帧本来就不该录任何东西。
  计划附.2 原写的"缺 step 时 assert"因此被否决，记在这里。
- 也没有引入 plan 原文提的"贡献者列表（span）"：当前只有一个 host（`App`），
  span 只是给将来留位，现在做会多一层生命周期负担而收益为零。真需要多贡献者时
  再把指针换成 span，改动是局部的。

### 验证

| 检查 | 结果 |
| --- | --- |
| `rg 'std::function' RenderFrameInputs.h FrameRecordExtensions.h` | 0（`PresentationGraphService.h` 只剩 init 期一次性的 `viewportDisplayImageProvider`，不是 per-frame 行为） |
| `rg 'DisplayComposeInput\|recordModuleViewportCompose\|recordModulePresentation\|recordBeforeExtensions\|recordExtensions\(\|appendCapture\)'`（Engine + Example） | 仅剩新名字，旧名字 0 |
| `xmake b` | `ya-render-3d` / `ya-game-runtime` / `ya-game-editor` / `ya-testing` / `ya-render-3d-test` / `ya-engine` / `GUIWorkbench` / `GreedySnake` / `ya-gui-closure-test` / `ya-gui-headless-host-test` / `ya-render-2d-test` 全部 ok |
| `xmake r ya-render-3d-test` | **176/176** |
| `ya-testing` 渲染/编辑器滤镜 | 265 passed / 3 failed（与 V1–V5 基线逐项相同） |
| `HelloMaterial --screenshot-target=viewport` | exit=0，0 error，`md5=1c6668976be1cdd5d755d1f1365700f7` **逐字节相同** |
| `run-editor --screenshot-target=viewport` | exit=0，0 error，`md5=5b8f5dd8acdbc90bd5c383feb3f7bcec` **逐字节相同** |

两张 smoke 都带 `--screenshot`，因此 `appendDisplayCapture` 这条被移动过的阶段确实
被执行过；编辑器的 `onPresentation`（编辑器 chrome）与 `onBeforePresentation` 也被
执行过，画面逐字节相同说明阶段搬位是等价的。

### 无单测的部分

`RenderFrameCoordinator::record` 需要活 device + swapchain，因此"阶段顺序"没有单测，
旧的回调版本同样没有。新增的静态断言只钉住"plan 里装的是接口指针"这一形状。
顺序的可验证性来自代码本身（一个函数、五步），而不是测试——这一点记录在此，
避免下次误以为它被覆盖了。


+## 2026-09-19 checkpoint：V4 + V5 IRenderRuntimeServices 删除与 FramePacket/PreparedView

### 唯一目标

删掉让 pass 在录制中途向"当前状态"提问的两个出口（V4），并让每个 View 的相机数据
只存在于该 View 自己身上（V5）。两条同批的理由见 plan 附.3：`IRenderRuntimeServices`
的 env lighting 出口正是 PreparedView 要接的东西，分开做会留下"一半 pass 从全局拿、
一半从 view 拿"的半迁移状态。

### V4a 时间：从接口查询变成 View 自己的数据

`RenderFrameData` 已经有 `frameIndex`；新增 `timeSeconds`（`ViewPrepareInput.
elapsedTimeSeconds`，host 从 `HostViewState.clock.elapsedTimeMS` 填）。
Forward 的 lit/unlit pass 从 `fd.frameIndex` / `fd.timeSeconds` 取帧常量，
不再 `_runtimeServices->getHostTick()` / `getElapsedTimeSeconds()`。

### V4b 场景资源：录制前解析进 View

新增 `Render3D/Common/RenderViewSceneResources.h`：一个 View 录制期要绑的
Scene-keyed GPU 绑定（skybox DS / IBL DS / 派生资源 / env lighting processor）。
`RenderDeviceState::resolveViewSceneResources(scene, out)` 在 `record()` 里、
`beginFrameCommandBuffer` **之前**按 View 逐个解析（与 `prepareDerivedState` /
`applyPendingMutations` / `applyViewportResize` 同一个 pre-record 接缝）。
Forward 的 `buildPassContext` / `buildSkyboxInput` 与 Deferred 的
`buildOverlayFrameInputs` 改读 `frame.view.frameData->sceneResources`。

顺带把 `EnvironmentLightingSceneResources` 从 `EnvironmentLightingProcessor.h`
拆到 `Render3D/Common/EnvironmentLightingSceneResources.h`：它是**结果类型**不是
processor 状态，单独存在后 `EnvironmentLightingResultProvider.h` 不必再拉进整个
processor 头（ECS 组件 + cubemap 管线）。

### V4c DebugRenderSystem 改为构造注入

`DeferredRenderInitDesc` / `PipelineCoordinator::InitDesc` 的 `runtimeServices`
换成 `debugRenderSystem`，由 `RenderDeviceState.Resources.cpp` 在建 pipeline 时
传入（值仍是 `DebugRenderSystem::get()`，但出口从接口方法变成构造参数）。

### V4d 删除

- `IRenderRuntimeServices.h` 整个删除；`RenderDeviceState` 不再继承它，其具体方法
  保留（`getHostTick` / `getEnvironmentLightingProcessor` / `getSceneSkyboxDescriptorSet`
  / `getDebugRenderSystem` 等仍有内部与编辑器消费者），去掉 `override`。
- `getGameplayResourceBinding()` 删除：接口上的零调用方，`App::getGameplayResourceBinding()`
  也跟着删（它只转发给前者，全仓无调用方）。`_gameplayResourceBinding` 本身保留
  （每 tick 在 `prepareDerivedState` 里驱动）。
- 五个 pass 类（Lit/Unlit/Aux/ForwardViewportStage/Deferred）的 `_runtimeServices`
  成员、InitDesc 字段与 `runtimeServices` 转发链全部删除。AuxPasses 的
  `_runtimeServices` 本来就只赋不用，属死字段。

### V5 `CameraFrameInput` → `FramePacket`，删掉整条 per-view patching

原 struct 同时是"这一帧"和"主 view"，`cameraForViewRecording(host, recording)` 靠
"拷贝 host 再按 recording 覆盖"把两组字段粘在一起。现在：

- `FramePacket` 只留帧级事实：`flightIndex` / `frameIndex` / `deltaTime` /
  `viewportFrameBufferScale`（host render scale，不是 View 属性）/ `shadowSettings` /
  `overlay` / `uiFrameSnapshot`。
- `cameraForViewRecording` 删除。view 级字段本来就在 View 上：矩阵与 extent 来自
  `SceneViewDesc` / `SceneViewportTask`，特征位与相机位置来自 `RenderFrameData`
  （V4 之后还带 scene resources）。
- `RenderPipelineFrameContext.camera` → `const FramePacket* frame`；
  Forward/Deferred 的 `shouldSkipView` / `beginViewRecording` / `syncFrameSettings` /
  `captureShadowSettings` / `postContext` / `collectViewOutput` 全部改读
  `frame.view.*`。`collectViewOutput` 的 `camera` 形参删除（它只用 View 的 extent）。
- `renderViewFamilies` 里 `view.frameData ? view.frameData : camera.frameData` 这类
  兜底删除：`pairViewFrames` 保证每个存活 View 都有自己的 frameData，兜底永远不可达。
- `recordCameraViewCompose` 不再吃 camera 包，改为 `(uiFrameSnapshot,
  logicalViewportExtent)`；coordinator 用 display root 的 `output.extent` 作为
  逻辑视口（= 原 `plan.camera.viewportRect.extent`，同一个值）。
- `RenderFramePlan.camera` → `.frame`；`record()` 里 `applyViewportResize` 与
  `publishViewOutputIdentity` 都改用 `displayRoot`，不再经由 host camera 副本。

保留未动：`derivedScene`（它本来就是"从该 View 的 task 读 Scene"，不是 patch），
`RenderViewRecordingContext`（它本身已是 View 级），`SceneViewRecording`（task +
frameData 的配对）。plan 附.2 原列的"删掉四层转译"因此收敛为"删掉 patching 与相机包
这一层"；剩下两层的合并（`RenderViewRecordingContext` 并入 `RenderPipelineFrameContext`）
留到 P3 收尾，因为它们的字段此刻都在被真实消费者读写。

### 验证

| 检查 | 结果 |
| --- | --- |
| `xmake b` | `ya-render-3d` / `ya-game-runtime` / `ya-game-editor` / `ya-testing` / `ya-engine` / `GUIWorkbench` / `GreedySnake` / `ya-render-2d-test` / `ya-gui-closure-test` / `ya-gui-headless-host-test` 全部 ok |
| `rg IRenderRuntimeServices\|CameraFrameInput\|runtimeServices\|getGameplayResourceBinding`（Engine + Example） | **0 命中** |
| `xmake r ya-render-3d-test` | **176/176** |
| `ya-testing` 渲染/编辑器滤镜 | 265 passed / 3 failed（与 V1–V3 基线逐项相同） |
| `HelloMaterial --exit-after-frame=90 --screenshot-target=viewport` | exit=0，0 error，`md5=1c6668976be1cdd5d755d1f1365700f7` **与基线逐字节相同** |
| `run-editor` 同参数 | exit=0，0 error，`md5=5b8f5dd8acdbc90bd5c383feb3f7bcec` **与基线逐字节相同** |

两张截图都与 V1–V3 之前（乃至 4d-3 收口时记录）的基线相同，说明这一刀是纯结构迁移，
没有改变渲染结果。V4 与 V5 各自落地后各跑过一次同样的双 smoke，两次都逐字节相同，
所以"V4 没动画面"与"V5 没动画面"是分别成立的。

### 遗留

- `RenderFramePlan` 上仍有 4 个 `std::function`（I1 / V6），录制顺序仍跨两个文件。
- 离屏任务仍由 `tickRender` 顺带 pump（I6 / V7）。
- viewport debug catalog 仍挂在 device 上（I7 / V8），`buildViewportSnapshot` 的
  else 分支也仍读 pipeline 的 depth/entityId 句柄。
- `RenderViewRecordingContext` 与 `RenderPipelineFrameContext` 仍是两层（P3 收尾）。


+## 2026-09-19 checkpoint：V1–V3 主 view 身份 + 输出发布 + 兜底链

### 唯一目标

让"宿主视口是哪个 View"只有一个答案，并删掉围绕它的兜底与循环赋值。
三条（V1/V2/V3）同批，理由见 plan 附.3：它们都是删隐式身份、不动 GPU 时序，
且必须排在 V5 之前，否则迁移中途会同时存在旧 `front()` 约定与新 `PreparedView`。

### 权威定义

`SceneViewDesc::ownsHostViewport()`（`composeOntoViewId == 0`，结构属性）是唯一
判据。原先 `declareViews` 按 `viewId == kPrimarySceneViewId`（id 约定）找宿主
view，而 `displayRootTask()` 按结构找——两个谓词回答了同一个问题，所以一个
声明了 `viewId=7, composeOntoViewId=0` 的 View 可以在 plan 里成为 display root，
却不是宿主相机的来源。现在两边都读结构，`kPrimarySceneViewId` 退回它的本义：
**预览视图 compose 的目标槽位**。

### V1 删掉的四处"主 view"副本

实现时又找到两处 plan 附.1 没登记的同类写法（一并删除）：

| 位置 | 原写法 | 现写法 |
| --- | --- | --- |
| `declareViews` | `view.viewId == kPrimarySceneViewId` | `view.ownsHostViewport()` |
| `buildGameRenderFrame` | `&viewFrames.front()` | `sceneRender.hostFrameData()` |
| `pairViewFrames` | 只写 `views[]`，索引含义留在注释里 | 顺带记录 `_hostFrameData` |
| `SceneRenderPlan::displayRootTask` | 没人 owns 就回退 `viewportTasks.front()` | 返回 `nullptr` |
| Forward/Deferred `recordFamily` | `recordings.empty()` 时合成一个 `task=nullptr` 的 View；输出里 `bDisplayRoot \|\| result.views.empty()` | 空 family 直接返回；只认 `bDisplayRoot` |
| `sceneViewOwnsHostViewport(nullptr)` | 视为 owns | 视为不 owns |

最后两条是同一个隐式规则的第四、五个写法，plan 附.1 只登记了前三条，已回填。
`recordings.empty()` 那条路径在代码里没有任何生产调用方或测试，属于 legacy 合成
入口，按"不留 legacy"直接删除。

`ExtractedSceneRender::primaryTask()` 删除，统一叫 `displayRootTask()`——
同一个对象上一个方法的两个名字，也是同一类重复。

### V2 输出发布显式化

- 新增 `RenderDeviceState::publishViewOutputIdentity(flightIndex, displayViewId)`：
  `_publishedOutputViewId` 的唯一写入点，`0` 表示显式清空。
- `publishFamilyResult` 只发布 outputs，不再在 `SceneViewFamilyPlan` 循环里
  逐 view 写身份（"最后写入者赢"）。
- 删 `beginFrameCommandBuffer` 里"published 的 view 不在本 flight 就清空"的补偿：
  它只是把陈旧身份归零，而 `publishedViewOutput()` 本来就会 `find` 失败返回
  nullptr；V2 之后每帧录制都显式重设身份，这个窗口不再存在。
- `record()` 里两分支 set/clear 收成一句：`displayRoot ? viewId : 0`。
  位置在 `recordViewFamilies` 之后、compose insets 之前——那一句之后的所有读取
  （inset 的 `getViewOutput`、`recordCameraViewCompose` 的 display 目标）都落到本
  帧的同一个身份上。
- 顺带修掉一个 latent bug：原先 plan 非空但 `displayRoot == nullptr`、或
  `displayRoot->desc.viewId == 0` 时，**不会**清空身份，上一帧的 View 会继续被
  当作本帧输出。现在两种情况都显式清空。

### V3 删兜底链

| 访问器 | 原来源链 | 现来源 |
| --- | --- | --- |
| `getActiveViewportImageShared` | published → `pipelineViewportColorImage()` | published |
| `getViewportDisplayImageShared` | published | published（未变） |
| `getViewportExtent` | published → `_pipelineViewportRect` → active pipeline → `{}` | published → `{}` |
| `getViewOutput(viewId)` | published flight → 扫 0..`MAX_FLIGHTS_IN_FLIGHT` | published flight |
| `pipelineViewportDisplayImage` | `getPostprocessOutputImageShared()` → `pipelineViewportColorImage()` | 删除（**零调用方**，死代码） |
| `pipelineViewportColorImage` | — | 删除（删掉上面两处兜底后零调用方） |

调用方的显式回落已经有：编辑器 2D 画布用面板尺寸（`EditorModule` 原注释就写着
"2D 模式 pipeline 不发布 viewport resources、`getViewportExtent()` 恒为 0x0，所以
按面板尺寸`"），host 用 `resolveViewportExtent` 的 `viewportRect` → 窗口尺寸。
截图源为空时 `AppScreenshotCapture::request` 已有分支返回 "failed to enqueue"。

### 尚未完成（本刀明确不做，避免半迁移）

- `buildViewportSnapshot` 的 else 分支仍读 `pipeline->getViewportDepthImageShared()`
  / `getEntityIdImageShared()`。它是"给面板看的目录"，归 V8（debug/presentation
  移出 device），现在删会让 2D 编辑器的深度/拾取句柄来源无处安放。
- `IRenderRuntimeServices`（I5）与 `CameraFrameInput` 拆分（V5）未动，按附.3 同批做。
- `RenderFramePlan` 的 `std::function`（I1/V6）、离屏 pump（I6/V7）未动。

### 验证

| 检查 | 结果 |
| --- | --- |
| `xmake b` | `ya-render-3d` / `ya-game-runtime` / `ya-game-editor` / `ya-testing` / `ya-engine` / `GUIWorkbench` / `GreedySnake` 全部 ok |
| `xmake r ya-render-3d-test` | **176/176**（174 + 本轮 2 个新用例） |
| `ya-testing` 渲染/编辑器滤镜 | 265 passed / 3 failed |
| 同滤镜 stash 基线（只回退本刀 9 个文件） | 263 passed / 3 failed |
| 回归对比 | **+2 通过，0 新增失败**，失败集合相同 |
| `HelloMaterial --exit-after-frame=90 --screenshot-target=viewport` | exit=0，日志 0 error，`md5=1c6668976be1cdd5d755d1f1365700f7` **与基线逐字节相同**（1395200B） |
| `run-editor` 同参数 | exit=0，`md5=5b8f5dd8acdbc90bd5c383feb3f7bcec`（570289B）；同会话 stash 前后两次运行同 md5，编辑器侧"同一会话内前后对比"成立 |

编辑器截图的历史基线 `1bfb16e7ca543abb7b517325df508b90`（679231B）来自面板更大的
持久化 dock 布局，本轮不与其比较：改用同会话 stash 探针（本刀代码回退后重编再跑），
前后 md5 一致，说明本刀没有改变渲染结果。

本轮**修好**的既有失败：无。3 个失败（`EditorPropertyGraphTest` ×2、
`GameUIHostTest.BuildSnapshotComposesMountedWidgets`）在基线上同样失败，与本刀无关；
`WidgetTreeTest.SystemLayersCannotBeDetached` 的 SIGTRAP 仍在（全量跑会中断，
故滤镜显式排除它）。

### 新增用例钉住的契约

- `RenderRuntimeSnapshotTest.EmptyDevicePublishesEmptyViewportResources`（扩充）：
  未发布时四个访问器全部返回 `nullptr` / `0x0`。
- `RenderRuntimeSnapshotTest.HostFrameDataFollowsTheDisplayRootNotThePairingSlot`：
  overlay View 先声明（落在 slot 0）时，`hostFrameData()` 仍指向 display root 的
  slot，钉住"配对顺序不等于主 view"。
- `RenderRuntimeSnapshotTest.TickThatDeclaresNoDisplayRootHasNoHostFrameData`：
  只声明 overlay 的 tick 没有 display root，也没有宿主 frame data。


## 2026-09-18 隐式驱动审计（V1–V8）

问题：R2/R3/R4 已经写清"往哪走"，但渲染路径上仍有一批**没人明说、代码照样跑**
的规则。本轮先登记再动手，结论进 `plan.md` 附.1–附.4、checkpoint 进 `todo.md`。

查证到的七条（每条都能指到代码）：

1. `RenderFramePlan` 带 `std::function`（`recordCompose` / `recordBeforeExtensions` /
   `recordExtensions` / `appendCapture`），真实录制顺序一半在 GameRuntime、一半在
   Render3D；漏挂 step 静默跳过。
2. "主 view"四个答案：`declareViews()` 的 `primaryView`、`viewFrames.front()`、
   `displayRootTask()`、`publishFamilyResult` 循环里最后写入者赢。
3. 兜底链：`getActiveViewportImageShared` / `getViewOutput` / `getViewportExtent` /
   `buildPipelineDebugOutputCatalog` 都是"找不到就换地方找"。
4. `record()` 内部做 `prepareDerivedState` / `applyPendingMutations` /
   `applyViewportResize` / `prepareComposePipelines`，安全点是"`begin()` 之前"的惯例。
5. `IRenderRuntimeServices` 让 pass 在录制中途取全局状态。
6. 离屏任务被 `tickRender` 顺带 pump。
7. `RenderDeviceState.ViewportDebug.cpp` 652 行呈现目录挂在 device 上；`Render3D`
   还 include `GUI/Compose/Render2DComposePass.h`。

排序结论：V1–V3 是纯删除、不动 GPU 时序，先做（做完"谁是宿主 view"才有唯一答案，
是 V5 的前置）；V4+V5 必须同批，否则会出现"一半 pass 从全局拿、一半从 view 拿"的
半迁移状态；V6/V7/V8 独立。owner-scoped `SceneViewKey`（4.0.3 checkpoint 5）排在
V2 之后，不提前做。

本轮只登记，未动代码；工作区仍只有用户自己改的三个文件。

## 当前状态

- 阶段：R0/R1 契约已落地。R2 功能向 ViewFamily 靠近，架构未收口。真实 loop 是 `AppKernel` → `GameRuntimeTickOrchestrator::tickRender` → `RenderFrameCoordinator::record` → `recordFamily` → host present。`RenderRuntime` 类已删除。
- 已确认：RenderFrameInputs.h 已有四组输入；RenderFrameData 的 Scene snapshot owner 已与 View-owned draw buckets 分离。
- 已确认前置：多 OS window 的 surface/present 改造属于 gui-multi-os-window-editor，不在本计划重复实现；本计划也不引入 WorldInstance/WorldRegistry。
- 当前 checkpoint：Surface 持有 presentation blit 的 tone-map descriptor set。Checkpoint C 把 View tone-map DS 从 processor 挪走后，display compose 仍调用 `BasicPostprocessing::render` 且不传 set，MoltenVK 在 `vkQueueSubmit` encode 时对 `VkDescriptorSet 0x0` 空解引用。下一刀仍是 4.0.3 公开 `Renderer` owner。不要发明 PIE authoring PiP，也不要把产品双 viewport / 双 Surface 当成下一刀。
- 架构审计结论（2026-09-17）：DeviceState+Coordinator 是不完整拆分（friend 越界）；DeviceState 是 persistent renderer 不是 RHI device；`RenderSubmission::finish` 不 submit；输入/context 层过多；`recordFamily` 仍是 per-view 状态机外包装；Stage 仍有 `_frameInputs` / `_preparedViewSlot`；Scene recording 仍与 present Surface 耦合；`tickRender` 淹没 orchestration。Checkpoint C/D/E 均为部分完成。A/B 入口仍有效。host live Scene 列表已落地，默认产品帧仍提交当前 viewport Scene。
- 本轮完成 RenderFrameData ownership 收口：RenderFrameData 不再继承 SceneFrameSnapshot，而是持有 shared snapshot 并独立保存 View-owned draw buckets；Forward/Deferred/Shadow/Debug/EntityId 消费者通过显式路径读取 View buckets、shared skinning palettes 和 light presence。
- R2 第一切片：RenderRuntime::FrameInput 已显式携带 SceneRenderPlanInput；GameRuntime 将 sealed plan 与 parallel view recordings 传入，Runtime 在 command recording 前校验每个 task 的 snapshot 归属。
- GPU lifetime guard：FrameUploadArena 现在按 `flightIndex + frameToken` 识别一次 submission；同一 token 的第二次 begin 已改为幂等 no-op。Forward / Deferred / Shadow 的 frame descriptor 已改为 View-owned；skinning 已按 Scene family 持有，同 Scene 多 View 共享一份 SSBO，不同 Scene 不再以 flightIndex 为共享 key。

## 2026-09-17 checkpoint：View 声明 / 收集边界 review（无代码改动）

## 2026-09-18 checkpoint：tickRender 读起来就是它的步骤（4.0.3 第 7 条）

- 唯一目标：把 `tickRender` 那 240 行内联实现收成有名字的步骤，让「一个 tick 的编排」不用逐行读也能看清。只做命名抽取，**不改行为、不改顺序**。
- 拆出的步骤（全部是 `GameRuntimeTickOrchestrator` 的私有静态函数，不新增 coordinator/packet 层次）：`declareViews`（跑 producer、提交声明、把主 view 的相机与 rect 采纳进 hostView）、`extractScenes`（seal + 抽取 + `renderedScenesLastTick`）、`prepareViews`（配对 View 记录并逐个 `prepareView`）、`buildGameRenderFrame`（相机包 + UI snapshot）、`recordFrame`（一次 `coordinator.record()`）、`submitRecordedFrame`（提交 + present + trace）。
- 所有权安排：`SceneViewCollector` 与 `UIFrameSnapshot` 由 `tickRender` 持有并传入，因为返回的声明指针与相机包要指向它们——builder 不返回「指向自己局部量的指针」这种结构。`declareViews` 返回的 `const SceneViewDesc*` 因此活到 tick 结束。
- 顺序发现（保留现状并登记）：计划原文把 `prepareModules` 排在 `prepareViews` 之后，而实际代码一直在最前面（早于 `prepareHostViewState`）。本刀不动顺序；「模块准备 vs view 声明」的先后是行为变更，要单独验，已写进 plan §4.0.3 第 7 条与 todo。
- 验证：`xmake b ya-render-3d ya-game-editor ya-testing`；`ya-render-3d-test` 174/174；`ya-testing` 相关滤镜 110/110；游戏→编辑器两轮（4 次运行）全部 exit=0、日志 0 error，四张截图与基线逐字节相同（`1c6668976be1cdd5d755d1f1365700f7` / `1bfb16e7ca543abb7b517325df508b90`），`Submit tick` trace 90 条/次——纯重排，像素与流程都不变。
- 头文件代价：步骤签名需要 `CameraFrameInput` / `ExtractedSceneRender` / `SceneViewCollector` 等类型，用前置声明（`class SceneRenderScheduler;` 等）而不是把它们全 include 进来；MSVC 兼容面因此踩到一次 `-Wmismatched-tags`（`SceneRenderScheduler` 是 class，不能前置声明成 struct），按类实际关键字声明即可。

## 2026-09-18 checkpoint：录制结果成为一个值（4.0.3 checkpoint 2/3 入口）

- 唯一目标：把「这一次录制产出了什么、host 该提交什么」从裸 `ICommandBuffer*` 变成一个显式值，并把 host 那条 record→submit 边界变得可观测。这是 checkpoint 2（公开 `Renderer` owner，`recordFrame(plan, surfaceTarget) -> RecordedFrame`）的入口产物，不提前做类合并。
- `RecordedFrame`（`Render3D/Common/RecordedFrame.h`）：`commandBuffer` + `flightIndex` + `frameToken` + `valid()`。`RenderFrameCoordinator::record()` 返回它；orchestrator 只提交它（`valid() ? {handle} : {}`），不再用「指针是否为空」表达成功/失败，也不再需要另一处推导才能提交。
- 合同收紧（刻意）：seal 失败（`RenderSubmission::finish()` 拒绝）现在返回**无效** `RecordedFrame`，host 走空提交——acquired image 仍被 present 合法化（`FPresentFrame` 的既有语义：「空 command list 仍然合法化已 acquire 的 image」）。旧代码在同样失败下只打一条 error 然后**照旧提交**那个 command buffer，属于「补丁之上再打补丁」；改成值之后失败是可表达的。诚实说明：今天 `finish()` 只在 submission 非 recording 时失败，而 `beginFrameCommandBuffer` 已经保证了 recording，所以这条路径当前不可达——它定义的是 P2 拆出 `FrameRecording`/`FrameFlightResources` 之后 seal 会真正失败时的契约（arena/descriptor 耗尽等）。
- 可观测性：`tickRender` 的 record→submit 边界新增一条 trace（`Submit tick N: flight=F token=T recorded=B`）。这不只是装饰——它当场暴露了下面那条 flight 发现。
- 验证：`xmake b ya-render-3d ya-game-editor`；`ya-render-3d-test` 174/174；`ya-testing` 相关滤镜 108/108；runtime 与 editor 的 viewport 截图与上一刀逐字节相同（`1c6668976be1cdd5d755d1f1365700f7` / `1bfb16e7ca543abb7b517325df508b90`）；游戏→编辑器序列不再崩溃。无新增单测：`RecordedFrame` 是纯值类型，给它写 `valid()` 断言是同义反复，而 seal 失败路径当前不可达，所以这一刀的验收是「构建 + 两条 smoke + 截图不变 + trace 可读」，不假装有单测覆盖。
- **顺带发现（已登记，未改）**：trace 显示 90 tick 全部 `flight=0`。原因不在本刀改动，而在 `VulkanRenderSurfaceContext::flightFrameSize = 1`（`advanceFrame()` 取模后 `currentFrameIdx` 恒为 0），于是 `resolveFlightIndex` 恒返回 0，渲染侧双槽 flight 表（`RenderSubmissionPool` / `RenderViewOutputTable` / `viewFrameDataPerFlight` / `FrameUploadArena`）在生产里只走槽位 0；它之所以仍安全，是 `begin()` 在 acquire 前调 `waitAllGraphicsFences()`（每帧等齐 GPU）——与 `flightFrameSize = 1` 自洽的 wait-idle 策略。结论与动作项见 temporal_semantics.md「M4 现状发现」与 todo.md：先判定 flight 深度是 1（余量）还是 2（需要改三处并两后端验证），再决定 M4 的记账范围。
- 保留未完成：checkpoint 2 的类合并（`RenderDeviceState` + `RenderFrameCoordinator` → 公开 `Renderer`）与 friend 收口；checkpoint 3 的 `RenderSubmission` 拆分与 `flightIndex` → `flightSlot` 改名（P2）。

## 2026-09-18 checkpoint：一条 View 声明必须描述整像素（4d-3 收口）

- 触发：4d-3b 落地后，`run`（游戏）紧接 `run-editor` 会让编辑器以 exit 255 退出（连中 3 次），单独跑编辑器则正常。崩溃报告 `EXC_BREAKPOINT`，栈是 `RenderGraph::createTexture`（`RenderGraph.cpp:1027` 的 extent 非零断言）← `SSAOStage::appendGraphPass` ← `DeferredFrameGraphOrchestrator::build` ← `tickRender`。
- 根因：`Rect2D` 没有默认成员初始化器、glm 默认构造平凡，于是 `EditorLayer` 的 `viewportRect` / `_viewportMouseRect` / `_viewportBounds[2]` 是未初始化内存；未初始化浮点常是**非规格化小数**（观测到位模式 `0x00000096` / `0x00000001`，另一次是 `(0, 1.17e27)`），`extent > 0.0f` 判真，于是「还没布局的作者视口」被当成有尺寸；`seal()` 的 `Extent2D::fromVec2` 截断成 0×0，进到 SSAO 的 persistent texture 时命中断言。4d-2 及之前同一根因经 pending-resize 传播（只在尺寸变化且鼠标未捕获时），4d-3b 把声明提到每帧，垃圾值就每帧进图。
- 处置三处（缺一不可）：① `Rect2D` 成员默认初始化——类型本身不该能是垃圾，一处覆盖仓库里 13 个同类声明；② `EditorLayer::describesPixels()`——rect 必须有限且至少一整个像素，`notifyViewportWidgetRect` 与 `getViewportRect` 都用它，未布局/折叠面板回落到编辑器默认尺寸；③ `SceneRenderScheduler::submit()` 拒绝非有限或截断后为 0×0 的声明——View 的贴图尺寸来自这个 rect，"描述不了一个像素的声明不是 View"，失败留在声明边界。
- 测试数据随之修正：`RenderRuntimeSnapshotTest` / `ViewFamilyRendererTest` 原来有 11 处不带 rect 的 `SceneViewDesc{.scene=..., .viewId=...}`，在新契约下不可受理，改为共用的 `makeView(scene, viewId)`（声明一个 1280×720 的合法 View）。新增 `HostSceneExtractTest.DeclaringWithoutAWholePixelIsRejected`（亚像素与非规格化小数被拒、1 像素通过）与 `EditorViewProducerTest.PanelGeometryTooSmallForAPixelDoesNotBecomeTheViewRect`。
- 验收：修复前「游戏→编辑器」3/3 崩溃；修复后同一序列 8/8 通过（5 轮 + 3 轮），editor 与 runtime 的 viewport 截图仍与 4d-3b 逐字节相同（`1bfb16e7ca543abb7b517325df508b90` / `1c6668976be1cdd5d755d1f1365700f7`）；`ya-render-3d-test` 174/174；`ya-testing` 相关滤镜 103/103。
- 排查方法记入 `.agent/memories/uninitialized_rect_and_view_rect_contract.md`：调试构建的断言文本会随异步日志丢失，用 `atos -o <debug dylib> -l <image base> <faulting addr>` 从 .ips 直接反查源码行；lldb 会改变时序与堆布局从而掩盖该 bug；只在出错条件命中时写 stderr 诊断，从声明侧与消费侧同时取值。
- 对本计划的影响（口径修正）：4d-3b 的验收当时只跑了「单独 editor」的 smoke，把一次「游戏→编辑器」的 255 当成偶发；这一刀把该序列纳入常规验证。声明语义也补上一条此前没写下的不变量：**View 的 rect 是整数像素语义**（`> 0` 不等于「有一像素」）。

## 2026-09-18 checkpoint：shadow 的准备结果显式回传（4.0.2 C 收口 6b）

- 唯一目标：删掉计划点名的另一半隐式 current View——`BasicShadowMapTechnique` 记住「上一次 prepare 的是哪个 view slot、其中有多少盏点光」并在 append 时复用。与 6a 的差别是这一半不是死状态（它真的被读），所以要把 prepare 的结果显式传回去。
- token：新增 `ShadowPreparedView{viewSlot, pointLightCount, valid()}`（`Render3D/Shadow/IShadowTechnique.h`）。`IShadowTechnique::prepare()` 改为返回它；`pointLightCount` 随 token 走，是因为 append 会用自己的 `frameData` 重建 payload，而那份 `frameData` 未必是 prepare 看过的那个包（Forward 的 `stageCtx.frameData` 是相机包，prepare 用的是 `recording.frameData`），显式带回才能保证 pass 用的就是「为它准备过的」那份计数。
- 传递链：`ShadowStage::prepareView` 返回 token → Forward 的 `ForwardFamilyViewBranch.shadowPrepared` / Deferred 的 `DeferredFamilyViewBranch.shadowPrepared` → `appendViewportPassGraph(...)` / `appendDeferredViewToGraph(...)` → 两个 orchestrator 的 `BuildInputs.shadowPrepared` → `ShadowStage::appendGraphPasses(graph, ctx, prepared, dependency)` → technique。Stage 与 technique 上不再有 `_preparedViewSlot` / `_lastPreparedPointLightCount`。
- 顺手去掉两处：append 里对 `pointLightCount` 的二次 `std::min(..., MAX_POINT_LIGHTS)`（prepare 时已经 clamp 过）与无人使用的 `getLastPreparedPointLightCount()`。
- 行为修正（刻意）：prepare 被拒（无 `frameData`、或 submission 未在录制）的 View 现在以无效 token append，什么都不加。旧代码在这种情况下会复用上一个 View 的 slot 与计数，把别人的 shadow pass 加进这一帧的图里——这正是「Stage 记住当前 view」会造成的错误。
- 新测试：`Engine/Test/Source/ShadowPreparedViewTest.cpp`——token 默认无效；在 shadow 打开的前提下，未 prepare 的 View append 出来的是空输出且图里不多出 pass。`ForwardFrameGraphOrchestratorTest.BuildInputsDefaultsStayEmpty` 补一条 `shadowPrepared` 默认无效。
- 验收：`xmake b ya-render-3d ya-render-3d-test ya-game-editor ya-testing`；`ya-render-3d-test` 174/174（原 172 + 2）；runtime 与 editor 的 viewport 截图与 4d-3b 逐字节相同。
- 验证口径补强（这一刀顺手做了）：先确认 shadow 确实影响这两张图，再谈截图不变。用 `--automation-config` 把 `shadow.quality` 设为 0（注意该键在 automation 文档根下，与 `smoke.postprocess.*` 的前缀不同）：runtime 视口整幅变化（bbox 全图、单通道最大差 162），editor 视口同样整幅变化（最大差 96）。所以「开关 shadow 会改变像素、而本刀前后截图逐字节相同」才构成 shadow 路径未被改坏的证据。
- 保留未完成：无（Stage current-view 至此清空）。

## 2026-09-18 checkpoint：Stage 不再记住当前 view（4.0.2 C 收口 6a）

- 唯一目标：删掉 Stage 上「上一次准备的是哪个 view」这类隐式槽位里的死的那一半。计划的 4.0.2 C 收口点了两个名字，这一刀先处理 `LightStage`（另一半 `BasicShadowMapTechnique::_preparedViewSlot` 需要把 prepare 的结果显式回传，拆成 6b）。
- 死状态确认：`LightStage::_frameInputs` 的唯一写入方是 `setFrameInputs()`，而它在全仓库没有调用方（`rg -n 'setFrameInputs' Engine` 只有定义与声明）——真正在录 light pass 的是 `DeferredFrameGraphPasses`，它把 View-owned descriptor set 显式传给 `LightStage::execute(ctx, frameAndLight, environmentLighting, gBufferTextures, shadows)`。因此 `IRenderStage::execute(ctx)` 这个入口过去只会用空 handle 转调，必然早退。
- 落地：删 `LightStage::FrameInputs`、`_frameInputs`、`setFrameInputs()`，destroy 里的 `_frameInputs = {}` 一并去掉；`execute(const RenderStageContext&)` 保留为有注释的空实现（`IRenderStage` 要求它存在），注释写明「一个存下来的当前 View 输入对同一 graph 里的第二个 View 必然是过期的」。
- 验收：`xmake b ya-render-3d ya-render-3d-test ya-game-editor`；`ya-render-3d-test` 172/172；runtime 与 editor 的 viewport 截图与 4d-3b 逐字节相同（`1c6668976be1cdd5d755d1f1365700f7` / `1bfb16e7ca543abb7b517325df508b90`）——删掉的是从未被写入的状态，渲染结果不变。
- 保留未完成：6b（`BasicShadowMapTechnique::_preparedViewSlot` / `_lastPreparedPointLightCount` → prepare 返回显式 token，穿过 `ShadowStage` 与两个 orchestrator 的 BuildInputs 由 pipeline 在 append 时传回；顺带删掉 append 里对缓存计数的二次 clamp，`getEffectivePointLightCount()` 已经 clamp 到 `MAX_POINT_LIGHTS`）。

## 2026-09-18 checkpoint：作者视口 rect 由声明方给出（4.0.3 4d-3b）

- 唯一目标：把作者视口的 rect 从「编辑器经 `AppRenderServices::setViewportRect` 推给宿主、宿主再放进 collect context 让 producer 读回」这条往返，改成编辑器在声明里直接给出；顺带把编辑器里那套 pending-resize 同步机制（最后一个手写 sync）删掉。
- 声明权归编辑器：`EditorLayer::getViewportRect()` 给出面板几何（尚未布局时用编辑器自己的默认尺寸兜底，View 不会以空 rect 被声明）；`EditorViewProducer` 的作者视口与预览 inset 的 composeRect 都用它，不再读 `context.viewportRect`。
- 手写 sync 删除：`EditorLayer` 的 `_pendingViewportRect` / `_bViewportResizePending` / `_resizeTimerHandle` / `getPendingViewportResize()` / `queueViewportResize()` 与无人订阅的 `onViewportResized` 全部删除；`EditorModule::applyPendingViewportResize()` 及其在 onLogic 的调用删除（`setViewportRect` 与 `device->applyViewportResize` 都不再由编辑器调用）。
- device extent 跟随声明：`tickRender` 找到主 view 后，除了原有的相机回填，还把 `primaryView->viewportRect` 回填进 `hostView.viewportRect` 并调用 `device->applyViewportResize(...)`——设备「expect 哪个 viewport extent」现在是声明的派生结果，而不是别处推入的状态（该调用本身在未变化时 early-out）。
- automation：`smoke.viewportResize` 只写宿主 view rect（那个 rect 由游戏视口声明），不再直接 `device->applyViewportResize`。
- 刻意接受的行为差异：automation 的 viewportResize 改的是宿主视图几何（独立游戏视口 / 游戏态），编辑器作者视口的尺寸由它自己的面板决定，所以该键在编辑器里不再改 RT 尺寸。「让编辑器视口变成某个尺寸」的诚实做法是改窗口/布局，而不是从外部改一个 docked 面板的渲染尺寸。另一处：`_viewportSize` 现在始终跟随面板 rect，删掉了「鼠标捕获时不更新」的分支——那条分支的唯一用途是延迟 RT resize。
- 验收：`xmake b ya-game-editor`、`xmake b ya-testing`；`ya-testing` 相关滤镜 92/92（`EditorViewProducerTest` 扩到 3 例，新增「声明出来的 rect 就是面板 rect」与「未布局时用默认尺寸兜底」）；`rg -n 'getPendingViewportResize|queueViewportResize|_bViewportResizePending|_pendingViewportRect' Engine/Source` 为空，`EditorLayer::onViewportResized` 已删（`IRenderPipeline::onViewportResized` 是 device 级 hook，仍在，由 `RenderDeviceState::applyViewportResize` 调用）；runtime viewport 截图与 4d-3a 逐字节相同（MD5 `1c6668976be1cdd5d755d1f1365700f7`，1395200 字节）；编辑器 viewport 截图与 4d-3a 逐字节相同（MD5 `1bfb16e7ca543abb7b517325df508b90`，235x188、13649 色、98.7% 非黑，说明世界视图确实在画）。
- 基线口径修正（本刀发现）：编辑器 viewport 截图不再有跨会话稳定的字节基线——本环境里编辑器面板尺寸受持久化 dock 布局/窗口状态影响，本会话是 235x188，而 4b–4d-2 记录过的 679231 字节来自当时更大的面板。因此编辑器侧改看「同一会话内的前后对比」（本次两刀之间逐字节相同），runtime 侧（1024x768 / 1395200）仍可跨会话比对。
- 保留未完成：checkpoint 5（view 身份改 owner-scoped `SceneViewKey`，并按此建 `ViewHistoryStore` 稳定键）。

## 2026-09-18 checkpoint：gizmo 开关归编辑器（4.0.3 4d-3a）

- 唯一目标：把「编辑器视口要不要画编辑器家具」从 `AppRenderState::bShowEditorGizmos` 这个跨层格子，交回编辑器自己的 view option，并让 automation 的开关经编辑器而不是经 App。4d-3 原含两件事（feature 归位 / 作者视口 rect 归位），拆成两刀，这一刀只做 feature。
- 开关归编辑器：`EditorLayer` 增加 `isEditorGizmoShown()` / `setEditorGizmoShown(bool)`（view option，与 `_bShowViewportCameraOverlay` 同类，默认 false）。`EditorViewProducer` 的作者视口仍是「authoring 时一律画、之后按开关」，预览 inset 读同一个开关；`EditorSurface` 的 View 菜单直接读写 layer。
- 格子删除：`AppRenderState::bShowEditorGizmos` 与 `App::isEditorGizmoShown` / `App::setEditorGizmoShown` 删除（`AppLifecycle.cpp` 里那两个函数体是它的全部实现）。编辑器不再经 App 的渲染状态表达自己的视图诉求。
- 游戏视口不再受编辑器开关影响：`RuntimeGameViewProducer` 只声明 `features = Game`。刻意接受的行为差异——「Show Editor Gizmos」是编辑器的 view option，一个不由编辑器声明的视口不该被它改变；独立游戏本就没有这个开关可点。
- automation 改走编辑器：`IEditorAutomationControl` 增加 `setEditorGizmosVisible(bool)`，`EditorModule` 转给 `_layer`；`handleSetEditorGizmosVisible` 不再写 App，改为「有编辑器才成功，没有则报 `set_editor_gizmos_visible requires a loaded editor`」。
- 新测试：`Engine/Test/Source/EditorViewProducerTest.cpp`（2 例：authoring 视口恒画编辑器家具；预览 inset 的 gizmo feature 由编辑器的 view option 决定）与 `RuntimeGameViewProducerTest.cpp`（2 例：Runtime 态只声明 `Game`；authoring 态什么都不声明）。前者顺带补上了 4d-2 登记的空缺——预览 inset 第一次被「真实选中一台相机实体」驱动（`App app;` + `EditorLayer layer(&app)` 夹具，不需要 device）。
- 验收：`xmake b ya-game-editor`、`xmake b ya-testing`；`ya-testing` 相关滤镜 114/114（含新增 4 例）；`rg -n 'bShowEditorGizmos' Engine Example` 只剩 `EditorLayer` 自己的 `_bShowEditorGizmos`，`rg -n 'featuresForView|App::isEditorGizmoShown' Engine Example` 为空；经 control 入口起 editor 实例（pid/端口由 `control start` 给出），`set_editor_gizmos_visible {"visible": true}` 返回 `{"visible": true}`（新路由生效），同一调用打到 game 实例返回 `requires a loaded editor`（无编辑器时明确失败而不是静默成功）；game 实例 viewport 截图 1395200 字节，与 4b/4c/4d-1/4d-2 基线一致，说明 runtime 视口输出没变。
- 保留未完成：4d-3b（作者视口 rect 由声明方给出，`setViewportRect` 不再由编辑器写，automation 的 resize 改走声明）、checkpoint 5（owner-scoped `SceneViewKey`）。
- 记录的既有失败（非本刀）：`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots` 与 `InputRoutesByWindowId` 失败，原因是这两个源码守卫用例硬编码的源文件路径被 include/ 归并（`4c1c6e3c`）挪了位置；同批还有并发工作线的 `GameUIHostTest.BuildSnapshotComposesMountedWidgets`。

## 2026-09-18 checkpoint：抽取移出 seal（4.0.3 4a）

- 唯一目标：让 `SceneRenderScheduler::seal()` 只做分组，把 ECS 抽取变成调用方的显式一步。改前 `seal()` 在给请求分组的同时调用 `request.buildSnapshot()`，于是「抽 Scene」不是一步而是分组步骤的副作用，且请求队列里躺着捕获 `Scene*` / `TerrainProcessor*` 的闭包。
- 落地：`SceneRenderRequest` 删掉 `buildSnapshot`，成为纯声明（SceneId/revision、viewId、familyId、相机、rect、compose）；`seal()` 只建表与分组（快照表按 (sceneId, revision) 去重建好、内容为空），family 分组抽成文件内 `buildViewFamilies()` 供两步复用；新增显式第二步 `buildSceneSnapshots(SceneRenderPlan&, const SceneSnapshotResolver&)`，按每个表项向宿主要不可变快照。
- 行为保持：无法解析内容的 Scene 由第二步剔除其 view 并重新分组（回到「该 Scene 这一 tick 不产生 view、其余 Scene 照常录制」），旧的 `if (!snapshot) continue` 语义没有丢；`submit()` 的校验从「有 builder」改成「有 sceneId 与 viewId」。
- 宿主侧：`submitHostSceneViews(scheduler, views)` 不再收 `TerrainProcessor*`、不再建 builder 表；新增 `extractHostSceneSnapshots(plan, views, terrainProcessor)` 承担抽取（按 (sceneId, revision) 在提交列表里找 `Scene*`）。`tickRender` 现在是三步：declare → seal → extract。
- 测试：`HostSceneRenderSubmitTest` 明确断言「seal 之后快照表存在但内容为空」，再断言抽取成功；`RenderRuntimeSnapshotTest` 去掉所有 builder，改由 `sealWithEmptySnapshots` 或显式 resolver 驱动，并新增 `SceneSchedulerDropsViewsOfUnresolvedScene`（坏 Scene 被剔除、好 Scene 不受影响）；`SceneSchedulerDeduplicatesSnapshotPerScene` 与 `SceneSchedulerRebuildsSnapshotWhenSceneRevisionChanges` 现在同时断言「分组不抽取」（buildCalls==0）与「每唯一 Scene 抽一次」。
- 验证：`xmake b ya-render-3d-test ya-game-editor ya-testing`；`ya-render-3d-test` 174/174；`ya-testing` HostSceneRenderSubmit/RenderRuntimeSnapshot/ViewFamilyRenderer/AppKernel/AppAutomationConfig/Editor* 91/91；`HelloMaterial --exit-after-frame=90 --screenshot-target=viewport` 与 `run-editor` 同参数均 exit=0、日志 0 error，viewport 截图 1024x768 有 10625 种颜色 / 98.5% 非黑、编辑器截图 7733 种，说明世界抽取与录制确实在跑而不是退化成 UI-only 空帧。
- 保留未完成：4b（plan 携带 tick-local `Scene*`、删三个反查校验）、4c（合并 `SceneViewDesc`）、4d（`ISceneViewProducer` 与删全局格子）。`SceneSnapshotResolver` 是 4b 要消掉的东西：计划拿到 Scene 句柄后这一步不再需要 resolver。

## 2026-09-18 checkpoint：一份声明结构（4.0.3 4c）

- 唯一目标：让「一个 View 的声明」在全链路上只有一份。改前声明被写了两遍——宿主 `HostSceneViewSubmit`（10 字段）逐字段搬进 `SceneRenderRequest`（11 字段），`seal()` 再逐字段搬进 `SceneViewportTask`（14 字段）；字段没增加表达能力，只增加「该在哪一层读」的记忆负担。
- 一份 `SceneViewDesc`（新文件 `Render3D/Common/SceneViewDesc.h`）：Scene 句柄、viewId、sceneRevision、policyId、view/projection/cameraPos、viewportRect、composeOntoViewId/composeRect，加两个派生成员 `ownsHostViewport()` / `viewProjection()`（`makeCameraViewProjection` 也搬到这里，全仓库唯一一份实现）。`HostSceneViewSubmit` 与 `SceneRenderRequest` 两个类型删除，`SceneViewId` / `kPrimarySceneViewId` 随声明类型一起落到这个头文件。
- 计划条目内嵌声明：`SceneViewportTask` 变成 `{ SceneViewDesc desc; RenderViewOutputDesc output; snapshotIndex; familyIndex; }`，`seal()` 只写 `desc = desc` 加自己派生的 output 与索引，不再逐字段抄；读侧统一 `task.desc.*`（Forward/Deferred/coordinator/cameraForViewRecording/viewDisplayInsetsFromPlan 共约 20 处）。
- 键改用句柄：`SceneViewFamilyKey`、`SceneSnapshotEntry`、seal 内的 `SnapshotKey` 以及 `snapshotFor` 的校验都从派生整数 `sceneId` 换成 `Scene*`（`SceneId` 别名与 `scene->getInstanceId()` 调用从调度器里消失）。族键本来就是 tick-local（`RenderSubmission::_families` 在每次 submission 重用时清空），所以句柄身份足够，且比整数少一层派生。
- 顺手删掉没有写方的 `renderFlags`：它在声明、计划条目、族键三处都存在，全仓库无一处写非 0；一份「唯一声明」不该带一个没人写的字段，族键也同步去掉这个恒 0 分量。
- 宿主侧那层转发删除：`submitHostSceneViews`（只做 skip 空 Scene + `makeCameraViewProjection`）与 `HostSceneRenderSubmit.*` 一起消失；orchestrator 直接把 `SceneViewDesc` 交给 `scheduler.submit()`，文件只剩抽取这一步，改名 `HostSceneExtract.{h,cpp}`（`extractHostSceneSnapshots` 名字不变）。`pendingRequestCount()` 随「request」词汇一起改成 `declaredViewCount()`。
- 测试：`HostSceneRenderSubmitTest.cpp` → `HostSceneExtractTest.cpp`（3 个用例：双 Scene 隔离、同 Scene 两 View 共享 snapshot 且相机不同、tick 外声明被拒；原来那个只测转发层的 `ClosedSchedulerRejectsSubmit` 与 `SceneSchedulerRejectsRequestsOutsideFrame` 重复，删掉）。`SceneFamilyResourcesTest` 的族键构造改用真实 Scene；`RenderRuntimeSnapshotTest` / `ViewFamilyRendererTest` 改用 `SceneViewDesc`，新增 `static_assert` 钉住「计划条目内嵌 desc」与「快照表项带句柄」。
- 验证：`xmake b ya-render-3d ya-game-runtime ya-game-editor ya-testing`；`ya-render-3d-test` 172/172；`ya-testing` HostSceneExtract/RenderRuntimeSnapshot/ViewFamilyRenderer/SceneFamilyResources/AppKernel/AppAutomationConfig/Editor* 93/93；`HelloMaterial` 与 `run-editor` 各跑 `--exit-after-frame=90 --screenshot-target=viewport` 均 exit=0、日志 0 error，截图字节数与 4b 完全一致（1395200 / 679231），说明这一刀没有改变渲染结果。
- 保留未完成：4d（`ISceneViewProducer`、五个全局格子、declare 路径清零 ECS 查询）、checkpoint 5（`PreparedView` 收口 `CameraFrameInput` patch 与 owner-scoped `SceneViewKey`）。

## 2026-09-18 checkpoint：相机预览与它的选择归编辑器（4.0.3 4d-2）

- 唯一目标：把「预览显示哪台相机」从宿主铸造的 view + 两个全局格子，交回持有选择的编辑器；顺带把声明里两件只有声明方知道的事（画什么 feature、从哪个实体渲染）从 orchestrator 的拼装挪进声明。
- 预览归位：`EditorViewProducer`（原 `EditorAuthoringViewProducer` 改名——它现在声明编辑器的两个 view）在声明作者视口之外，按 `EditorLayer::getCameraPreviewEntity()`（用户在 Hierarchy 选中的相机实体）声明预览 inset：`composeOntoViewId = kPrimarySceneViewId`、`composeRect = makeViewDisplayInsetRect(...)`、`viewOwner = 该相机实体`。view id `kEditorPreviewViewId = 2` 是编辑器自持常量，宿主不再铸造 `kHostOverlayPreviewViewId`；`cameraProjectionForOutput`（按输出 rect 的 aspect 出投影）随之落到编辑器。
- 两个格子删除：`AppRenderState::bCameraPreviewHostOwned` 与 `cameraPreviewEntityUUID`（含 `AppRenderServices` 的四个访问器）。`EditorLayer::onUpdate` 因此只剩 `_lastDeltaTime`——它的全部职责就是往这两个格子里写状态。`resolvePreviewCamera` 同时删除。
- FOV 线框换家：选中相机的线框原本经 `cameraFrame.overlay.worldLines`（宿主相机包络）画进世界图，选择信息来自 `cameraPreviewEntityUUID`。现在它由编辑器自己的 world overlay pass 画（`recordSelectedCameraFrustum`，与网格/gizmo/HUD 同批），用的是同一个 `appendCameraFrustumOverlayLines`，所以「选择」这条信息不再需要穿越宿主。
- 声明补两件事：`SceneViewDesc` 增加 `features`（`FRenderFeatureMask`）与 `viewOwner`（`entt::entity`）。前者让 orchestrator 的 `featuresForView` 与其「按 viewId 猜这是谁的 view」的逻辑消失；后者让 `prepareView` 的 viewOwner 直接来自声明，而不是「是不是预览 → 用预览相机，否则用 runtime 相机」的三分支猜法。RuntimeGameViewProducer 因此显式声明 `features = Game | (gizmo 开关 ? Gizmo : 0)`（保留 PIE 下的调试覆盖）与 `viewOwner = 游戏相机实体`（那台相机自己的机身体不该出现在它自己的视野里）。
- 验收：`ya-render-3d-test` 172/172；`ya-testing` 相关滤镜 101/101（`GameUIHostTest.BuildSnapshotComposesMountedWidgets` 是并发工作线那侧的既有失败，与本刀无关，已在 4d-1 记录过一次）；`HelloMaterial` 与 `run-editor` 各跑 `--exit-after-frame=90 --screenshot-target=viewport` exit=0、日志 0 error、截图字节数仍与 4b/4c/4d-1 完全一致（1395200 / 679231）。
- 刻意接受的行为差异（两条，都需要知会）：① 独立游戏（无编辑器）在 Runtime 态若有第二台相机，过去宿主会自动在角落声明一个预览 inset，现在没有——按计划「预览相机的选择策略回到持有选择的编辑器」，游戏二进制不该自己挑一台相机做画中画；若将来要，`RuntimeGameViewProducer` 显式声明即可（接缝已在）。② 选中相机的 FOV 线框从「世界图 overlay（可能被几何遮挡关系按原 pass 处理）」改为「编辑器 world overlay pass 的非深度测试段（与网格同批）」，即线框现在与网格同样始终可见。
- 未覆盖：预览 inset 与 FOV 线框需要「用户选中一台相机」才会出现，而 smoke 场景只有一台相机、也没有 UI 选择动作，所以两条路径只经过代码审查与构建验证，没有被自动化覆盖；补法要么给 `EditorViewProducer` 一个能构造 `EditorLayer` 的夹具（当前 `ya-testing` 没有构造 `EditorLayer` 的先例，需要带 device 的 App 夹具），要么经 automation `invoke` 走一次选择动作。已登记为待补。
- 保留未完成：4d-3（`bShowEditorGizmos` 格子归 `EditorLayer`；作者视口 rect 由声明方给出）、checkpoint 5（owner-scoped `SceneViewKey`）。

## 2026-09-18 checkpoint：世界视口由持有方声明（4.0.3 4d-1）

- 唯一目标：让「这个 tick 渲染哪个视口」由**持有该视口的 owner** 声明，而不是编辑器写全局格子、runtime 再去读。4d 按计划拆三刀，这一刀做接缝 + 世界视口。
- 接缝：`Render3D/Common/SceneViewProducer.h` 新增三件套——`SceneViewCollectContext`（activeScene / viewportRect / viewportExtent / hostTick / deltaTime）、`SceneViewCollector`（纯 sink，无策略：不想要就不要 declare）、`ISceneViewProducer::collectSceneViews(context, collector)`。比计划原文多一个显式上下文参数：帧事实应当被读，塞进 collector 会把 sink 和输入混在一起。宿主侧 `AppRenderState::viewProducers` + `App::add/removeSceneViewProducer`。
- 两个声明方：`RuntimeGameViewProducer`（GameRuntime，Runtime 态用游戏相机；无相机实体时仍声明、相机为单位矩阵）与 `EditorAuthoringViewProducer`（GameEditor，非 Runtime 态且非 2D canvas 时用编辑器相机；2D canvas 与 PIE 都不声明）。`EditorModule` 在 onAttach/onDetach 注册与注销。
- 删掉的两个格子：`AppRenderState::extensionHostView`（含 `set/clearExtensionHostViewState`）与 `AppRenderState::bWorldSceneRenderEnabled`（含 `set/isWorldSceneRenderEnabled`）。`prepareHostViewState` 相应收成「宿主几何 + 时钟」（viewportRect / framebuffer scale / clock），主 view 的相机由声明回填进 `hostView`——相机包络与 offscreen resize 仍读 `hostView`，渲染结果不变。
- 动画策略的诚实输入：`SkeletonAnimationSystem::setTickPolicy` 从「某个 viewport 的世界开关」改为「上一 tick 是否为该 Scene 产出了内容」——`AppRenderState::renderedScenesLastTick`，由 `renderedScenes(plan)` 在抽取后写入（该 helper 从 coordinator 文件内提到 `SceneRenderScheduler.h` 与宿主共用）。一 tick 滞后是结构性的（system 跑在 view 声明之前），正是 UE `bRecentlyRendered` 的形态。
- 场景查询归位：orchestrator 静态函数 `getPrimaryCamera` 移入 `Utility/SceneCameraQuery`（`findPrimaryCamera` / `findSecondaryCamera`），`AppSceneServices::getPrimaryCamera` 与 `syncRuntimeCameraAspect` 改用它；orchestrator 内的 `findNonPrimarySceneCamera` 删除（`resolvePreviewCamera` 复用 `findSecondaryCamera`）。预览相机选择与 frustum 线仍留在此文件，属 4d-2。
- 验收：`ya-render-3d-test` 172/172；`ya-testing` 相关滤镜 101/101（新增 `AppLifecycleTest.*`）；`rg -n 'extensionHostView|setWorldSceneRenderEnabled|isWorldSceneRenderEnabled|bWorldSceneRenderEnabled|findNonPrimarySceneCamera' Engine Example` 为空；`HelloMaterial` 与 `run-editor` 各跑 `--exit-after-frame=90 --screenshot-target=viewport` exit=0、日志 0 error、截图字节数与 4b/4c 完全一致（1395200 / 679231），说明 runtime 与 editor 两条线的主 view 都由 producer 正确声明。
- 过程中的一次误判记录：`AppLifecycleTest.SaveScenePersistsAndReloadsRoundTrip` 曾失败一次，用 `git stash` 隔离并强制重建后复跑 5 次全绿，确认与本刀无关；当时是可执行文件与 dylib 处于两次部分重建之间的不一致状态。
- 保留未完成：4d-2（相机预览生产者 + 删 `bCameraPreviewHostOwned` / `cameraPreviewEntityUUID` / 宿主铸造的 `kHostOverlayPreviewViewId`）、4d-3（`SceneViewDesc.features` + 删 `bShowEditorGizmos` 与 `featuresForView`）、checkpoint 5（owner-scoped `SceneViewKey`）。

## 2026-09-18 checkpoint：计划保留 Scene 句柄（4.0.3 4b）

- 唯一目标：把「这一 tick 每个 view 渲染哪个 Scene」从反查变成声明本身的事实，从而删掉 `derivedSceneForHostView`、`SceneRenderPlanInput::complete()`、`derivedScenesAgreeWithPlan()` 三个运行时校验和 4a 的过渡物 `SceneSnapshotResolver`。
- 声明携带句柄：`SceneRenderRequest.sceneId` → `SceneRenderRequest.scene`（宿主本就持有 live Scene，句柄整 tick 有效）；`SceneViewportTask` 与 `SceneSnapshotEntry` 各带 `Scene*`，`sceneId` 由 `seal()` 从 `scene->getInstanceId()` 派生，同一事实不再存两份。
- 抽取不再反查：`SceneSnapshotResolver(SceneId, revision)` → `SceneSnapshotExtractor(Scene&)`，宿主侧 `extractHostSceneSnapshots(SceneRenderPlan, TerrainProcessor*)` 收 sealed plan（by value），不再需要 `span<HostSceneViewSubmit>` 去按 id 找 Scene，也去掉了「每条表项扫一遍提交列表」的二次查找。
- 构造期不变量取代运行时校验：新增 `ExtractedSceneRender`（私有 `plan` + `views`，只有 `buildSceneSnapshots()` 是 friend）。只有它进入 `RenderFramePlan::sceneRender`，于是「忘了抽取」从运行时日志变成编译错误；view 与 task 只能经 `pairViewFrames()` 配对，`views.size() == viewportTasks.size()`、`views[i].task == &viewportTasks[i]`、`frameData != nullptr` 由构造保证，`complete()` 与其错误分支消失。`pairViewFrames()` 同时接管宿主原本分散在 `tickRender` 里的 frame-data 策略（每存活 view 一槽、UI-only tick 只留相机那一槽并丢弃陈旧 snapshot）。
- 每 view 的 Scene 只来自自己的 task：`SceneViewRecording::derivedScene`、`ViewFamilyRecordContext::derivedScene`、`derivedSceneForFamily` 删除，Forward/Deferred 改为 `.derivedScene = recording.task ? recording.task->scene : nullptr`；`uniqueDerivedScenes` 变成 `RenderFrameCoordinator.cpp` 的文件内 `renderedScenes(plan)`，直接读已解析的快照表（表项按 (sceneId, revision) 去重，故 derived state 也按 `Scene*` 去重）。`derivedScenesAgreeWithPlan` 校验的两种错误（同 sceneId 两份 Scene、不同 sceneId 共用一份）在只剩一份句柄后不可能构造出来。
- 附带：`ya-render-3d-test` 增加 `ya-scene-core` 依赖，plan 测试改用真实 `Scene`（不再是 `reinterpret_cast<Scene*>(0xA000)` 这种无法解引用的假句柄）；删除 `MixedDerivedSceneInOneFamilyIsRejected`、`SharedDerivedSceneAcrossSceneIdsIsRejected`、`SceneRenderPlanInputRequiresPlanAndMatchingViewRecordings` 三个只测已删机制的用例，新增 `EveryTaskCarriesTheSceneItsDeclarationNamed`、`SameSceneViewsShareOneSnapshotAndScene`、`UiOnlyTickKeepsOneFrameSlotAndPairsNoView`、`ExtractedSceneRenderPairsEveryTaskWithItsOwnFrameData`。
- 验证：`xmake b ya-render-3d ya-engine ya-testing`；`ya-render-3d-test` 172/172（净减 2：删 3 加 1 个新用例）；`ya-testing` HostSceneRenderSubmit/RenderRuntimeSnapshot/ViewFamilyRenderer/AppKernel/AppAutomationConfig/Editor* 89/89；`HelloMaterial --exit-after-frame=90 --screenshot-target=viewport` 与 `run-editor` 同参数均 exit=0、日志 0 error，截图 1024x768 非黑（运行时 1395200B / 编辑器 679231B），世界抽取与录制仍在跑。
- 保留未完成：4c（合并 `HostSceneViewSubmit` 与 `SceneRenderRequest` 为 `SceneViewDesc`，消掉 12/14 字段两次机械搬运；也是 4d 的前置）、4d（`ISceneViewProducer`、删五个全局格子、declare 路径清零 ECS 查询）、checkpoint 5（owner-scoped `SceneViewKey`）。
- 已登记不做的偏离：`ForwardRenderPipeline::appendViewportPassGraph` 与 `DeferredRenderPipeline` 仍在录制期遍历 live ECS（direction gizmo / billboard / skybox），属于 §3.10.4 里随 P3 `PreparedView` 一起删的同一类问题，本刀不顺手改。


- 触发：核查 `isWorldSceneRenderEnabled()` 该属于 Scene 还是 viewport client。结论：它既不属于 Scene 也不属于 Renderer，而属于**声明方是否声明**；该格子的存在是 view 收集链缺少 producer 接口的症状。
- 盘点的现状（登记进 plan §3.10 / temporal_semantics M9）：真正的 view 声明只有 GameRuntime 一处；GameEditor 经四个全局格子（`bWorldSceneRenderEnabled`、`extensionHostView`、`bCameraPreviewHostOwned`+`cameraPreviewEntityUUID`、`viewportRect`）影响它，且全靠 hook 顺序成立（写在 `onLogic`、读在 `tickRender`）。一个 view 被声明六次（`HostSceneViewSubmit` → `SceneRenderRequest` → `SceneViewportTask` → `SceneViewRecording` → `CameraFrameInput` → `RenderViewOutputDesc`），字段只被机械搬运。
- 三处空转：① `renderFlags` 无写方、`sceneRevision` 恒 0、`familyId` 恒 1，于是 `SceneViewFamilyKey` 退化为 `snapshotIndex` 单键；② `seal()` 内直接调 `buildSnapshot()`，ECS 抽取是分组步骤的副作用；③ plan 丢弃 Scene 指针，导致宿主必须用 `derivedSceneForHostView` / `complete()` / `derivedScenesAgreeWithPlan()` 三个函数反查校验。
- 保留的判断：调度器的「帧内聚合 + 按 (SceneId, sceneRevision) 去重 snapshot + 按 ViewFamily 分组」对应 UE family/renderer 分层，是对的，不删；不引入 WorldRegistry；GUI Framework 仍不认识 Scene。
- 落地：写入 plan §3.10（现状、六层重复、三处空转、目标形态、明确不做）、4.0.3 执行顺序第 4 刀（4a 抽取移出 seal / 4b plan 保留 Scene 句柄 / 4c 合并 `SceneViewDesc` / 4d `ISceneViewProducer`）、R1/R3 交叉引用、temporal_semantics M9 与批次 P1e、todo 六个工作项、feature_matrix `view_declaration_contract`。
- 本 checkpoint 无代码改动、无测试执行；上面登记的是下一步待实现项，不当作已完成。

## 2026-09-17 checkpoint：Surface 持有 presentation blit descriptor set

- 唯一目标：display compose 不再绑定空的 `BasicPostprocessing` input set。View tone-map DS 仍在 `ViewResources::post.toneMap`；swapchain blit 是 Surface 轴，由 `PresentationGraphService` 在 `init` 分配并传入 `render()`。
- 落地：`PresentationGraphService` 持有 `_presentationInputPool` / `_presentationToneMap`；`recordDisplayCompose` 传入 `.toneMap`；`BasicPostprocessing::render` 在 set 为空时拒绝 bind/draw。
- 验证：`xmake b ya-game-runtime`、`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='PostProcessingStageTest.*:ViewPassResourcesTest.*:DeferredPassParamsTest.*:DeferredRenderPipelineTest.*'` 10/10；`python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=3 --log-level=warn --log-detail-level=error` 退出码 0，无 `VkDescriptorSet 0x0` / MoltenVK `bindDescriptorSets` 崩溃；`git diff --check` 通过。
- 保留未完成：4.0.3 Renderer 合并；FrameRecording/FlightResources 拆名；PreparedView；Stage current-view；压缩 tickRender；产品双 Scene 显示；双 Surface GPU。C 仍未清除 Stage current-view。

## 2026-09-17 checkpoint：automation / TaskManager 的 per-tick 命名（M1 P1d）

- 来源：P1b-2a 扫 perf 命名面时发现同一根轴上还有一批 host tick 语义的符号仍叫 frame。它们不在原 M1 清单里，属于「同轴遗留」，单独补一批，避免 M1 收尾后 automation 内部同时存在 tick 与 frame 两套叫法。
- 落地：`AppAutomation::isFrameAutomationEnabled` / `hasFrameAutomationConfig` → `isTickAutomationEnabled` / `hasTickAutomationConfig`；`shouldRequestQuitAfterFrame` → `shouldRequestQuitAfterTick`；`EAppAutomationExitReason::ExitAfterFrame` → `ExitAfterTick`；`isAutomationStableFrameReady` / `bStableFrameReady` → `isAutomationStableTickReady` / `bStableTickReady`（它们比较的本来就是 `stableTicks` / `settleTicks`）；`TaskManager::registerFrameTask` / `hasFrameTasks` → `registerTickTask` / `hasTickTasks`（`taskManager.update()` 在 `tickLogic` 每 tick 调一次）；`AppAutomationTickContext` 的 `frameContext` 参数 → `tickContext`。调用方同步：`GameRuntimeTickOrchestrator`、`AppAutomationControlService`、`AppLifecycle`、`EditorLayer`、`EditorActionCatalog`、`RuntimeRenderSettingsSection`、`AppKernelTest`。
- 保留的边界：日志文案 `warmup frames` / `stable frames`、`getAutomationExitReasonName` 返回的 `exit-after-frame`、以及 `--exit-after-frame` / `frame_index` 等 CLI、config、协议键都不改——它们是外部可见面。
- 列对齐：`onTickCompleted` 里的 `bStableTickReady` 赋值列已补回，diff 只剩标识符。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing`；`xmake r ya-testing --gtest_filter='AppKernelTest.*:AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*'` 66/66（含改名后的 `AppKernelTest.HeadlessLoopHonorsExitAfterTick`）；`git diff --check` 通过。

## 2026-09-17 checkpoint：Scene snapshot 命名（M2 P1c）

- 唯一目标：把 `SceneFrameSnapshot` 改成 `SceneSnapshot`，不改任何行为。它是「某个 Scene 在某个内容版本上的不可变内容」，Scene 不变时被同一帧的多个 View 共用；名字里的 Frame 会让它看起来像按渲染帧产出的数据，而它恰恰是跨 View 共享、与 host tick 无关的那部分。
- 落地：定义（`RenderFrameData.h`）与全部前置声明；`SceneRenderScheduler` 的 `SceneSnapshotEntry` / `SceneRenderPlan::snapshotFor` / `SceneRenderRequest::buildSnapshot`；`SceneFamilyResources`（成员、ctor、`snapshot()`、`bindSnapshot()`）；`RenderSubmission::allocateSceneFamily`；`RenderFrameExtractor::extractSceneSnapshot` / `prepareView` / `extractSceneLights`；`HostSceneRenderSubmit` 的 builder 类型；`RenderRuntimeSnapshotTest` / `SceneFamilyResourcesTest` / `ViewFamilyRendererTest`。
- 列对齐：`RenderFrameExtractor`（成员块与续行参数）、`RenderSubmission`、`SceneFamilyResources`、`RenderFrameData` 的声明列按原列补回；测试里两处 `static_assert` 的续行缩进随锚点一起左移，diff 只剩标识符。
- 计划同步：`plan.md` 里描述现行类型的 `SceneFrameSnapshot` 一并改成 `SceneSnapshot`（旧名只保留在 M1/M2 的 old→new 映射记录里）。
- 验证：`xmake b ya-render-3d-test`、`xmake b ya-testing`、`xmake b ya-game-editor`；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`git diff --check` 通过。
- 保留未完成：测试文件名 `RenderRuntimeSnapshotTest.cpp` 仍是旧命名（它的内容也已不是「runtime snapshot」，与 `RenderFrameExtractor` 拆分一起处理，属 P3）。

## 2026-09-17 checkpoint：按 tick 排期的字段（M1 P1b-2b）

- 唯一目标：把「按 tick 排期」的字段名从 frame 改成 tick，不改任何排期行为。这些值都是 host tick 序号（`App::_hostTick` / `EnvironmentLightingResultProvider` 的 tick provider），不是 Scene 版本、View 采样或 flight 槽。
- 落地：`TerrainProcessor::_nextResolveAuditFrame` 与 `EnvironmentLightingProcessor::_nextResolveAuditFrame`、`GameplayResourceBinding::_nextMaterialAuditFrame` → `_nextResolveAuditTick` / `_nextMaterialAuditTick`；`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`MATERIAL_AUDIT_INTERVAL_FRAMES` → `*_TICKS`（含 `AppAutomationConfigTest.ResourceResolveDerivedResourceGcDelayConstantIsStable` 的常量断言）；`TerrainDerivedResource::lastUsedFrame` → `lastUsedTick`；`TerrainComponent` 的 `getRebuildNotBeforeFrame` / `setRebuildNotBeforeFrame` / `_rebuildNotBeforeFrame` / `invalidate(rebuildNotBeforeFrame)` 与 `TerrainProcessor::markTerrainDirty(..., rebuildNotBeforeFrame)` → `*Tick`。
- 列对齐：改名缩短了成员名，`TerrainComponent`、`TerrainDerivedResource`、`TerrainProcessor`、`GameplayResourceBinding` 的声明列与 `TerrainProcessor.cpp` 里两处赋值块已按原列补回，diff 只剩标识符本身。
- 验证：`xmake b ya-render-3d-test`、`xmake b ya-testing`、`xmake b ya-game-editor`；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`git diff --check` 通过。
- 保留未完成：`DebugPrimitives::updateFrameUBO` / `_frameData`（按 `flightIndex` 索引，属 P2 flight 轴，随 `FrameFlightResources` 一起改）；P1c `SceneFrameSnapshot` → `SceneSnapshot`。

## 2026-09-17 checkpoint：perf 命名面收口（M1 P1b-2a）

- 唯一目标：把 perf 采样轴上残留的 `Frame` 改成 `Tick`，不改任何采样行为。host tick 的相位（logic / render / event pump / …）现在与代码其余部分说同一种话；trace 里再出现 `Frame/Logic` 就一定是别的语义。
- 落地：`perf::sample::renderFrame()` → `hostTick()`，key `Render/Frame` → `Tick/Total`（它就是整个 host tick 的 CPU/GPU 总量，也是 unaccounted 的基准）；`frameLogic` / `frameEventPump` / `frameFpsControl` / `frameRender` / `frameMainThreadCallbacks` / `frameAutomation` / `frameUnaccounted` / `frameRenderCallbacks` → `tick*`，key `Frame/*` → `Tick/*`；`YA_PERF_FRAME_SCOPE` → `YA_PERF_TICK_SCOPE`、`PerfFrameScopeTimerConditional` → `PerfTickScopeTimerConditional`（含 `frameSampleKey`、`ya_perf_frame_timer_`、局部 `frameValue`）。
- 名字选择：总量没按组名改成 `Render/Tick`，因为 tick 内还有一个 `Tick/Render`；`renderTick` 与 `tickRender` 只差词序，正是本轮要消除的阅读负担。总量用 `Tick/Total`，与 `Tick/*` 相位同组。
- profile 产物同步：`frameCycle` / `frameCpuMs` / `frameGpuMs` → `tickCycle` / `tickCpuMs` / `tickGpuMs`；`buildFrameCycleJson` → `buildTickCycleJson`。产物 Schema 在仓库内无消费方（`Script`、`Engine/Config`、`Example`、`.agent` 均无引用），因此随命名一起改，不保留别名。
- 明确的边界（不改）：用户可见文案 `Frame CPU:` / `Frame GPU:`（profiling 面板）与 `Frame {}`（stats 面板）保留，与 `--exit-after-frame` / `frame_index` 等外部键同类。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing`、`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*:RenderViewBindingTableTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*'` 41/41；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppKernelTest.*'` 66/66；`git diff --check` 通过。
- 保留未完成：tick 排期字段（`_nextResolveAuditFrame`、`MATERIAL_AUDIT_INTERVAL_FRAMES`、`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`getRebuildNotBeforeFrame()`）；`DebugPrimitives::updateFrameUBO`（随 P2 flight 轴）；P1c `SceneFrameSnapshot` → `SceneSnapshot`。
- 本轮扫到的同轴遗留（未做，登记备查）：`AppAutomation::isFrameAutomationEnabled` / `hasFrameAutomationConfig`、`shouldRequestQuitAfterFrame`、`AppAutomationTickContext` 参数名 `frameContext`、`AppTaskManager::registerFrameTask`/`hasFrameTasks` 仍是 host tick 语义。

## 2026-09-17 checkpoint：host view state 命名（M1 P1b-1）

- 唯一目标：把 `AppRenderFrameState` 改成 `HostViewState`，不改任何行为。它保存的是 host 自己那个 view 的 clock、viewport rect 与相机矩阵，既不是 per-View packet，也不是 present/swapchain 状态；`frameState` 这个名字会让读者以为它就是「这一帧的渲染状态」。
- 落地：`Applications/GameRuntime/AppRenderFrameState.h` → `HostViewState.h`（含 `include/GameRuntime/` 转发头）；`AppRenderState::frameState` → `hostView`、`extensionFrameState` → `extensionHostView`；`AppRenderServices::getRenderFrameState` / `setExtensionRenderFrameState` / `clearExtensionRenderFrameState` → `getHostViewState` / `setExtensionHostViewState` / `clearExtensionHostViewState`；`EditorViewportCompositor` 与 `EditorSurfaceContext` 的声明、定义与参数名同步（`makeEditorSurfaceContext` 的 `frame` → `hostView`）。消费方同步：`GameRuntimeTickOrchestrator`、`AppLifecycle`、`AppAutomationControlService`、`EditorModule`、`EditorLayer.Interaction`。
- 前置：阻塞这次改名的 GameEditor 在途改动已先单独提交（`[editor] own the authoring viewport compose in a compositor`、`[gui/layout] drop the applyArgs nodiscard callers intentionally ignore`），本轮才有干净、可编译、可独立提交的边界。
- 列对齐：类型名变短会打断声明列，`AppRenderState` / `AppRenderServices` / `EditorViewportCompositor` / `EditorSurfaceContext` 的成员与参数已按仓库风格重新对齐，没有留下错位。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing` 通过；`xmake r ya-testing --gtest_filter='EditorWindowSessionTest.*:EditorRootSessionTest.*:EditorDockWorkspaceTest.*:HostSceneRenderSubmitTest.*:AppAutomationConfigTest.*:AppKernelTest.*'` 66/66；`git diff --check` 通过。
- 保留未完成：perf key / profile scope `Frame/*`（`PerfKeys.h` + 约 30 处调用）；tick 排期字段（`_nextResolveAuditFrame`、`MATERIAL_AUDIT_INTERVAL_FRAMES`、`DERIVED_RESOURCE_GC_DELAY_FRAMES`、`getRebuildNotBeforeFrame()`）；`DebugPrimitives::updateFrameUBO`（按 `flightIndex` 索引，随 P2 flight 轴）；P1c `SceneFrameSnapshot` → `SceneSnapshot`。

## 2026-09-17 checkpoint：host tick 命名（M1 P1a）

- 唯一目标：把 host 调度批次的命名从 frame 改成 tick，不改任何行为。理由与逐符号清单见 `temporal_semantics.md`：`frame` 现在同时表示 host tick、Scene 内容版本、View 渲染采样、录制作用域、GPU submission、flight 槽和 Surface present 次数，单窗口单 View 时这些值恰好同步递增，多 View 后不再成立。
- 落地：`GameRuntimeFrameOrchestrator` → `GameRuntimeTickOrchestrator`（类 + `.h`/`.cpp` + `include/` 转发头；`prepareRenderFrameState` → `prepareHostViewState`）；`RenderRuntimeClockState` → `HostClockState` 且字段 `frameIndex` → `hostTick`；`App::_frameIndex`/`getFrameIndex()`/`currentFrameIndex()` → `_hostTick`/`getHostTick()`/`currentHostTick()`；`IRenderRuntimeServices` 与 `RenderDeviceState` 的 `getFrameIndex()` → `getHostTick()`；`setFrameIndexProvider`/`_getFrameIndex` → `setHostTickProvider`/`_getHostTick`；`lastUsedFrame` → `lastUsedTick`、`TerrainProcessor::currentFrame()` → `currentHostTick()`；`SceneRenderScheduler` 的 `beginFrame`/`clearFrame`/`isFrameOpen`/`frameId()` 与 `SceneRenderPlan::frameId` → tick 命名；automation 的 `markFrameCompleted`/`completedFrameCount`/`shouldAutomationExitAfterFrame`/`exitAfterFrame`/`AppAutomationFrameContext`/`onFrameCompleted`/`recordedFrameIndex`/`earliestFrameIndex`/`screenshotFrameIndex`/`screenshotWarmupFrames`/`screenshotSettleFrames` → tick 命名。
- 明确的边界（不改名）：CLI / config 键 `--exit-after-frame`、`exitAfterFrame`、`screenshot.frame`、`smoke.*.frame`；automation 协议键 `frame_index`、`warmup_frames`；Lua 脚本 API `time.getFrameIndex`；UI 文案 `Frame {}`；`DeferredDeletionQueue::currentFrame()`、`VulkanRender::_frameIndex`、`Instrumentor::_frameIndex`（fence 轴 / profile 事件索引，与本语义无关）。
- 刻意延后：`AppRenderFrameState` → `HostViewState`（当时消费方 `EditorModule.cpp` 与 `EditorViewportCompositor.{h,cpp}` 属于另一条在途改动；该改名已由后面的 M1 P1b-1 checkpoint 落地）；`DebugPrimitives::updateFrameUBO`（按 flightIndex 索引，属 P2 flight 轴）；perf key / profile scope `Frame/*`（`PerfKeys.h` + 约 30 处调用）；`RenderRuntimeSnapshotTest` → `RenderFramePlanningTest`；`_nextResolveAuditFrame` 等 tick 排期字段。
- 验证：`xmake b ya-render-3d-test`、`ya-game-runtime`、`ya-game-editor`、`ya-testing`、`ya-gui-closure-test`、`ya-gui-headless-host-test`、`ya-gui-minimal-host`、`GUIWorkbench` 全部通过；`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:ViewFamilyRendererTest.*'` 25/25；`xmake r ya-gui-closure-test --gtest_filter='AppKernelTest.*'` 3/3；`xmake r ya-testing --gtest_filter='AppAutomationConfigTest.*:EditorWindowSessionTest.*:HostSceneRenderSubmitTest.*'` 20/20；`git diff --check` 通过。
- 与本轮无关的既有失败（已确认不在改动面内）：`ya-gui-widgets-test` 因 `Engine/Test/Source/GuiFrameInspectorTest.cpp` 找不到 `GUI/Compose/GuiFrameInspectorOverlay.h`（该 target 的 deps 不含 compose 模块）而编译失败；`ya-gui-headless-host-test` 有 3 个 GUI snapshot 断言失败（`lastItemCount` 期望 > 0，实际 0，日志显示 `draw=0 painted=6`）。两者都落在另一条进行中的 GUI 改动上，本轮未触碰任何 GUI widget / layout / snapshot 代码。

本轮同时写入计划结论：`IRenderRuntimeServices` 作为抽象没必要（唯一实现者、无 mock、死方法 `getGameplayResourceBinding()`、与 `EnvironmentLightingResultProvider` 重复、并被 pass 用来在录制期查 live ECS），处置见 `plan.md` 3.9 与 `temporal_semantics.md` M8，随 P3 `PreparedView` 收口一起删除。

## 2026-09-17 checkpoint：修正 C/D/E 计划状态

- 唯一目标：把计划改成与源码一致。C/D/E 从“完成”改为“部分完成”；删除把 DeviceState+Coordinator 当成闭环、把 RenderRuntime 当成现行 orchestrator 的叙述；下一刀改为公开 `Renderer` owner。
- 本刀不改引擎代码、不跑测试。
- 保留未完成：4.0.3 Renderer 合并；FrameRecording/FlightResources 拆名；PreparedView；Stage current-view；压缩 tickRender；Surface 与 View 正交；产品双 Scene 显示；双 Surface GPU。

## 2026-09-17 checkpoint：host 提交 live Scene 列表

- 唯一目标：host 不再把 `getActiveScene()` 当成唯一可提交的 Scene。`HostSceneViewSubmit` 列出本帧 live Scene viewport；同 Scene* 共享 extract；不同 Scene 隔离 snapshot/family；recording `derivedScene` 从列表查找。
- GameRuntime 默认仍把当前 viewport Scene（及同 Scene camera overlay）放进列表。未接 PIE authoring 第二 viewport，未做双 Surface。
- 验证：`xmake b ya-game-runtime`、`xmake b ya-testing`、`xmake r ya-testing --gtest_filter='HostSceneRenderSubmitTest.*'`（3/3）、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（76/76）、`git diff --check`。
- 保留未完成：产品帧同时显示两个 Scene viewport；display-root inspector getter fallback；双 Surface GPU 验收；ViewHistoryStore 待 TAA；sceneRevision 仍为 0。

## 2026-09-17 checkpoint：family-scoped derived Scene

- 唯一目标：coordinator 不再用一个 frame-wide Scene* 服务所有 family。每个 recording 携带产出该 snapshot 的 host Scene。
- 删除 `RenderFramePlan::derivedScene`。录制前对每个 unique derived Scene 调用 `prepareDerivedState`；family record context 从该 family 的 recording 取值。同 SceneId 必须共享指针，不同 SceneId 不得共享。Scheduler 仍不持有 Scene。Host 当前仍只提交一个 live Scene。
- 验证：`xmake b ya-render-3d`、`xmake b ya-game-runtime`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（76/76）、`git diff --check`。
- 保留未完成：host 提交两个 live Scene 的产品录制（随后由 live Scene 列表切片落地）；display-root inspector getter fallback；双 Surface GPU 验收；ViewHistoryStore 待 TAA。

## 2026-09-17 checkpoint：拆除 RenderRuntime facade

- 唯一目标：host frame 编排、device 持久状态、Scene view rendering、compose/present 不再由一个类同时拥有。
- `RenderDeviceState` 拥有 backend、persistent services 与 safe-point mutation（`applyViewportResize` / `applyPendingMutations` / `prepareDerivedState`）。`RenderFrameCoordinator::record(RenderFramePlan)` 创建 submission、按 family 调用 `recordFamily`、compose/present。`RenderFramePlan::derivedScene` 是本帧 Scene，不是 InitDesc locator。Host `AppRenderState` 拥有 `bWorldSceneRenderEnabled` 与 viewport rect；空 `sceneRender` 是 UI-only。删除 `RenderRuntime`、`ViewportStateService`、`getActiveScene`。未引入空的 `ViewHistoryStore`（TAA 无消费者）。
- 验证：`xmake b ya-render-3d`、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（72/72）、`git diff --check`。
- 保留未完成：display-root inspector getter fallback；双 Scene 产品录制；双 Surface GPU 验收；ViewHistoryStore 待 TAA。

## 2026-09-17 checkpoint：ViewFamily renderer

- 唯一目标：pipeline 不再以 `tick()/beginTick()/getCurrent*()` 表示一次 View；一个 family graph 可产生多个 typed `RenderViewOutput`。
- `IRenderPipeline::recordFamily` 取代 `tick`；coordinator 别名 `ISceneViewFamilyRenderer`。Deferred/Forward 对同一 family 建一个 graph：skinning prepare 一次，per-view 追加 shadow/GBuffer/forward/post；`familyPredecessor` 把门后续 View。export/pass 名走 `makeViewGraphName`。删除 `_currentGBufferResources` / `_publishedGraphOutputs` / `_currentPostprocessOutput` / `_currentOverlayFrameInputs` / `_currentEnvironmentLighting*` 作为 publish source。
- RenderRuntime 按 `plan.viewFamilies` 调用 `recordFamily`，`RenderViewOutputTable` 只 ingest `result.views`；debug catalog 优先 typed output。`_debugViews` / Forward `_viewportResources.publish` 只保留 display-root inspector fallback。未改文件名为 `*ViewFamilyRenderer` / `*GpuResourceLibrary`。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewFamilyRendererTest.*:ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（71/71）、`git diff --check`。
- 保留未完成：Checkpoint E；display-root inspector getter fallback；双 Scene 产品录制；双 Surface GPU 验收。

## 2026-09-17 checkpoint：typed View/Pass resources

- 唯一目标：SSAO / Light / EntityId / Overlay / Forward-debug / postprocess 的 graph execute 不再更新 persistent Stage/Processor；每个 View 的 DS/UBO 由 typed `ViewResources` 子结构持有。
- `DeferredFrameResourceSet::ViewResources` / `ForwardFrameResourceSet::ViewResources` 组合 frame Binding 与 typed pass bindings（`SSAOPassBindings`、`DeferredLightingPassBindings`、`EntityIdPassBindings`、`OverlayPassBindings`、`ForwardDebugPassBindings`、`PostprocessPassBindings`）。Bloom/BasicPost 的 viewId map 删除。PointShadow instance/cull packet 进入 Shadow View Binding。
- graph-resolved CIS 仍可在 execute 写入 captured View DS；Stage 只保留 layout/PSO。未重命名 FrameResourceSet / Stage，也未改 `recordFamily`。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPassResourcesTest.*:SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*:DeferredPassParamsTest.*:PostProcessingStageTest.*'`（66/66）、`git diff --check`。
- 保留未完成：Checkpoint E；display-root inspector getter fallback；双 Scene 产品录制；双 Surface GPU 验收。

## 2026-09-17 checkpoint：SceneFamily owner

- 唯一目标：同一 submission 里 Scene A/B 的 skinning GPU packet 互不覆盖；同 Scene 的多个 View 引用同一个 `SceneFamilyResources`。
- `SceneRenderScheduler::seal()` 物化 `SceneViewFamilyPlan[]`（按 sceneId/revision/snapshot/renderFlags/policyId 分组）。`RenderSubmission::allocateSceneFamily()` 按 key 返回稳定 owner。Forward/Deferred/Shadow `prepareSkinning` 写入 family SSBO，不再按 `flightIndex` 共用 `PerFlightFrameResourceSetBase` 槽位。
- 未改 Stage CIS、processor viewId map、PointShadow indirect、也未把 pipeline 改成 `recordFamily`。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='SceneFamilyResourcesTest.*:RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*'`（57/57）、`git diff --check`。
- 保留未完成：Checkpoint D–E；pipeline last-view 图袋；双 Scene 产品录制；双 Surface GPU 验收。

## 2026-09-17 checkpoint：RenderSubmission owner

- 唯一目标：一次 command submission 的 command buffer、frame token、upload arena、transient descriptor、keepalive 与 finish 由 `RenderSubmission` / `RenderSubmissionPool` 维护；pipeline/resource set 不再各自 `beginSubmission()`。
- 删除 `RenderSubmissionContext` / `RenderSubmissionTable`。Forward/Deferred/Shadow `beginView(RenderSubmission&)` 从 submission 分配 upload slice 与 descriptor set；`writeViewPayloads(arena)` 只留给 identity 单测。RHI cmd begin/end 仍在 RenderRuntime coordinator。skinning 仍在 `PerFlightFrameResourceSetBase`。
- 验收：同 token 连续两 View slice 不覆写；finish 后 keepalive 存活到该 flight 新 token；二次 finish、finish 后 allocate/retain、同 token 再 acquire 均被拒绝。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderSubmissionTest.*:RenderViewBindingTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewOutputTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*:ViewPersistentResourceKeyTest.*:CameraFrustumOverlayTest.*:DirectionalShadowMathTest.*:ForwardGraphInputsTest.*'`（53/53）、`git diff --check`。`xmake b ya-game-runtime` 因既有 `ModelComponent._childNodes` 编译失败，与本切片无关。
- 保留未完成：Checkpoint B–E；skinning 仍按 flight 共享；Stage CIS / last-view 图袋 / processor viewId map；双 Scene 录制；双 Surface GPU 验收。

## 2026-09-16 plan：Renderer 生命周期与 ViewFamily graph（4.0.2 重审）

- 唯一目标：review 当前计划和代码，并把可执行的大尺度重构写入 plan；本轮不改引擎代码。
- 修正遗漏：原三层 Device/Submission/View 漏了 SceneFamily；同 submission 双 Scene 下，per-flight skinning/scene GPU packet 仍会串。
- 修正图粒度：不再固定一 View 一 graph；`SceneViewFamilyPlan` 是 graph 编译单位，同 Scene/策略的 family-shared work 与 per-view branch 同图，不相关 family 分图但可共 submission。
- 状态/逻辑原则：allocator/submission/history 等维护不变量的 owner 保持状态+行为；snapshot/plan/prepared view/result 是值；pass recipe 只持 device-lifetime 配方。
- `begin/end` 结论：保留 RHI command protocol；删除 persistent pipeline 上表达隐式 current state 的 tick/beginTick/beginView/getCurrent。
- 执行顺序改为 A Submission owner → B SceneFamily owner → C typed pass resources/recipes → D family renderer → E 拆 RenderRuntime facade。
- 允许类名/文件名重构并删除 legacy API；每个 checkpoint 仍需单一验收目标。

## 2026-09-16 plan：配方与 View 数据分离（已被上述 4.0.2 重审收编）

- 唯一目标：把结论写进计划，不写代码。Stage/Pipeline 混有 last-view 数据，禁止再叠 viewId map。
- 目标三层：Device 配方（pipeline/layout/池）、Submission（arena/skinning）、View（Binding + RDG persistent）。录制 lambda 不改 Stage 成员。RDG 不接管 DS/UBO。不抽 BaseRenderPipeline。
- 原“全部 CIS 塞进 Binding”只保留为问题清单，不再作为目标形态；typed View/Pass resources 取代 mega Binding。
- 双 Scene / 双 Surface 排在 4.0.2 之后。Bloom/BasicPost 的 viewId map 视为迁移期 hack。
- 未改引擎代码；plan.md 3.3 / 4.0.2、todo、feature_matrix、session_checklist 同步。

## 2026-09-16 checkpoint：View-own postprocess / bloom display 资源

- 唯一目标：同一 submission 里两个 View 不再共用 postprocess/bloom 的 GPU 图和 descriptor set，避免两路画面相同并闪烁。
- 根因：tone mapping 默认开启。`BasicPostprocessing` / Bloom extract-composite 只有一套 CombinedImageSampler set，View B 在同一 cmdbuf 里原地 update 后，View A 已录制的 bind 在 submit 时也采样 View B。Bloom/Postprocess 输出曾是 transient，同 extent 时会被 registry 回收给下一个 graph。
- `ViewDescriptorSetAllocator` 支持 CombinedImageSampler。`BasicPostprocessing` 与 Bloom extract/blur/composite 按 viewId（blur 再加 passIndex）分配独立 set。`Postprocessing.Output` 与 `Bloom.CompositeOutput/Extract/BlurPing/BlurPong` 走 `createViewPersistentTexture`。Forward/Deferred 把 task viewId 传入 bloom/finalize。
- 未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴；SSAO/EntityId 仍有 maxSets=1 的辅助路径，不在本切片。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ViewKeyedPostprocessTexturesStayIndependentAcrossSequentialGraphs:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:PostProcessingStageTest.*:CameraFrustumOverlayTest.*:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（56/56）、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`git diff --check`。
- 保留未完成：双 Scene 录制；双 Surface；PointShadow indirect 仍是 flight 轴；viewport click picking 无 camera mesh。

## 2026-09-16 checkpoint：overlay View 与 host viewport identity 分离

- 唯一目标：修正 camera preview 的硬编码、过大 frustum、以及点击 camera 后主 viewport 缩小到角落并闪烁。
- `SceneRenderRequest` / `SceneViewportTask` 增加 `composeOntoViewId` + `composeRect`。`viewportRect` 只描述该 View 自己的离屏 RT；compose dest 不再进入 `cameraForViewRecording` 的 host rect。Runtime 从 plan 收集 insets，不再依赖 `kCameraPreviewViewId`。
- Forward/Deferred 仅在 `ownsHostViewport()` 时 `requestViewportResize`。Overlay graph 使用 View-local RT spec extent（`frame.view.viewportExtent`），host `_viewportRTSpec` 保持 WorldView[0] 尺寸。
- Camera frustum 改为 compact gizmo：沿 FOV ray 画 `visualDepth`，不 unproject clip far。`makeViewDisplayInsetRect` 是 host layout helper，不属于 frustum overlay。
- 未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴；material preview 仍未作为独立 request。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='CameraFrustumOverlayTest.*:ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`git diff --check`。
- 保留未完成：双 Scene 录制；双 Surface；PointShadow indirect 仍是 flight 轴；viewport click picking 无 camera mesh。

## 2026-09-16 checkpoint：Editor world Camera preview PiP

- 唯一目标：产品路径为选中的 world Camera 再 submit 一个同 Scene 的 SceneRenderRequest；录制后把该 View 的 output blit 到主 viewport 右下角；world Camera 在主 View 上画 3D frustum 线。
- GameRuntime 在 Editor 选中 `CameraComponent` 时 submit `kCameraPreviewViewId`；standalone 自动预览第一台非 primary camera。Preview 使用独立 output extent，与 primary 共享 Scene snapshot。
- `ViewComposeInput.insets` 描述要合成到 primary display RT 的 View；Runtime 从 `getViewOutput` 取样，不写 swapchain。这不是双 Surface。
- World Camera 可视化用 viewport overlay 的 `RenderOverlayLine3D` / `makeWorldLine`，不是 screen-space Line2D，也不是新 mesh。Hierarchy 点击即可选中；viewport 点击仍需要 mesh/billboard 写 entityId。
- `getViewportExtent` / `buildViewportSnapshot` 优先读 published primary output，避免 preview 的较小 RT 污染 editor picking 和 camera aspect。
- 未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴；material preview 仍未作为独立 request。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='CameraFrustumOverlayTest.*:ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（53/53）、`xmake b ya-game-runtime`、`xmake b ya-game-editor`、`git diff --check`。
- 保留未完成：双 Scene 录制；双 Surface；PointShadow indirect 仍是 flight 轴；viewport click picking 无 camera mesh。

## 2026-09-16 checkpoint：循环录制 SceneViewportTask

- 唯一目标：RenderRuntime 录制 sealed plan 里的每一个 SceneViewportTask；同一 Scene 的两个 View 共享一份 SceneFrameSnapshot，并各自发布 output。
- `SceneRenderPlanInput` 用与 `plan.viewportTasks` 平行的 `SceneViewRecording` 替换单一 task 指针；`complete()` 要求数量、指针和 snapshot 都对齐。`cameraForViewRecording` 把 task 的矩阵/extent/`frameData` 盖到 host camera 上。
- Runtime 对每个 recording tick pipeline、立即 publish；compose/present 仍用 primary（第一个 task）。GameRuntime 对每个 task `prepareView`，`viewFrameDataPerFlight` 按 flight 持有 View-owned `RenderFrameData`。
- 未让 GameRuntime submit 第二个 camera；未做双 Surface GPU 验收；PointShadow indirect 仍是 flight 轴。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（48/48）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：GameRuntime 仍单 camera submit；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：View-keyed persistent keys / RTs

- 唯一目标：Forward/Deferred viewport（含 GBuffer/SSAO）persistent key 按 View 分开，两个 View 不能共用一张 GBuffer/color。
- 新增 `makeViewPersistentTextureKey` / `createViewPersistentTexture`：key 为 `{base}.view{id}`；viewId 0 仍是 `.view0`，避免缺失 task 时回到未分名的全局 key。
- Forward `BuildInputs.viewId` 与 Deferred `BuildInputs`/`PassContext`/`SSAO` params 从 `frame.view.task->viewId` 传入；graph export 名保持单 graph 内唯一，不在本切片改成 mega-graph。
- 未循环 SceneViewportTask，因此 Runtime 仍只录制一个 View；PointShadow indirect 仍是 flight 轴。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='ViewPersistentResourceKeyTest.*:ForwardGraphInputsTest.*:RenderGraphCoreTest.ViewKeyedPersistentTexturesStayIndependent:RenderGraphCoreTest.ResourceRegistryReusesStableResourcesAcrossSyncs:RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DeferredPassParamsTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（47/47）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：仍只录制单 View；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：View-keyed output handles

- 唯一目标：每个 View 拥有独立的 output/extent/format 句柄；发布 View B 不得改写 View A；ViewportStateService 不再表示 View 输出身份。
- 新增 `RenderViewOutput` / `RenderViewOutputTable`：按 flight 持有堆上独立 record，追加 View 不会因 vector 扩容让已发布句柄失效。
- `SceneViewportTask` 在 seal 时写入 `output.viewId` 与 `output.extent`；同一 Scene 的两个 task 可有不同 extent，仍共享 snapshot。
- Runtime 在 compose 后把当前 pipeline 的 color/display/depth/entityId 发布到该 task 的 ViewId；`getViewOutput(viewId)` / display getter 优先读表。
- 未循环 SceneViewportTask，未把 Forward/Deferred persistent key（`ForwardViewport.Color`）改成 View-keyed，因此真实 GPU 仍是一份 viewport RT。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewOutputTableTest.*:RenderRuntimeSnapshotTest.*:RenderSubmissionTableTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（38/38）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：仍只录制单 View；pipeline 单一 persistent RT；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：RenderRuntime submission 保活到 fence-safe reuse

- 唯一目标：RenderRuntime 持有一次 submission 直到该 flight 的 GPU fence 安全复用；overlay 与 graph-exported image 不得只活到 `renderFrame()` 返回。
- 新增 `RenderSubmissionTable`：按 `MAX_FLIGHTS_IN_FLIGHT` 持有 `RenderSubmissionRecord`（context + keepalives）。同 token 幂等保留 keepalives；新 token 才释放上一轮 owner。`markRecordingComplete` 不 drop keepalives。
- `beginFrameCommandBuffer` 在 cmdBuf begin 后写入 live submission；tick 使用表内 context，不再构造临时 `RenderSubmissionContext`。overlay 在 tick 前 retain 到 table 与 cmdBuf。
- 录制结束后把 viewport display / viewport / postprocess 导出图 retain 到同一 flight；`renderFrame()` 返回后 `getLiveSubmission(flight)` 仍 occupied。
- 未把 FrameUploadArena / descriptor pool 搬进 Runtime，也未循环 SceneViewportTask，也未建立独立 View output。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderSubmissionTableTest.*:RenderRuntimeSnapshotTest.*:RenderViewBindingTableTest.*:DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*'`（32/32）、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：仍只录制单 View；无独立 View output；无双 Surface；PointShadow indirect 仍是 flight 轴；upload arena 仍由 pipeline resource set 持有。

## 2026-09-16 checkpoint：Shadow beginSubmission / beginView

- 唯一目标：Shadow resource set 为同一 submission 的每个 View 提供独立 Binding（cascade/face descriptor set + upload slice），View B 的准备不得改写 View A。
- `ShadowFrameResources` 接入 `PerFlightFrameResourceSetBase`（skinning）与 `ViewDescriptorSetAllocator` / `beginFrameResourceSubmission` / `writeUploadSlice`；删除 `prepare` / `getBinding(flightIndex)`。
- `IShadowTechnique::prepare` 改为接收 `RenderSubmissionContext` / `RenderViewRecordingContext`；Forward/Deferred pipeline 通过 `ShadowStage::prepareView` 传入与 viewport 相同的 submission/view。
- Directional/Point graph pass 从 `getViewBinding(flight, viewSlot)` 读取 Binding。PointShadow indirect instance/cull 缓冲仍按 flight 覆写，不在本切片展开。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewBindingTableTest.*'`（8/8，含 ShadowViewSlicesStayIndependent）、snapshot/deferred/arena 回归 27/27、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：RenderRuntime 不持有 submission 到 fence；仍只录制单 View；无独立 View output；无双 Surface；PointShadow indirect 仍是 flight 轴。

## 2026-09-16 checkpoint：Deferred beginSubmission / beginView

- 唯一目标：Deferred resource set 为同一 submission 的每个 View 提供独立 Binding（descriptor set + upload slice），View B 的准备不得改写 View A；抽取 Forward 已稳定的 pool 增长与 submission 开头，供 Forward/Deferred 共用。
- 新增 `ViewDescriptorSetAllocator`（chunked UniformBuffer pool，allocate 前增长）和 `beginFrameResourceSubmission` / `writeUploadSlice`；Forward 四条 pool 链与 Deferred 三条都改用该 allocator。
- `DeferredFrameResourceSet` 拆出 submission-scoped `SkinningBinding` 与 `RenderViewBindingTable<Binding>`；删除 `prepare` / `prepareSSAO` / `prepareSkybox` / `getBinding(flightIndex)`。`beginView` 在同一 View slot 写入 frame/light 以及可选 SSAO/skybox。
- `DeferredRenderPipeline::executeDeferredMainGraph` 按 Forward 同序 `beginSubmission` → `prepareSkinning` → `beginView`，graph 与 Light/SSAO/overlay 消费 View Binding，不再按 flight 覆写。
- 未统一 Forward/Deferred payload struct，也未抽万能 pipeline 基类。Shadow 仍按 flight 覆写 Binding。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewBindingTableTest.*'`（7/7，含 DeferredViewSlicesStayIndependent）、`DeferredFrameResourceSetTest.*:DeferredRenderPipelineTest.*:DeferredFrameGraphResourcesTest.*:RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*` 回归通过、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：Shadow View binding；RenderRuntime 不持有 submission 到 fence；仍只录制单 View；无独立 View output；无双 Surface。

## 2026-09-16 checkpoint：Forward beginSubmission / beginView

- 唯一目标：Forward resource set 为同一 submission 的每个 View 提供独立 Binding（descriptor set + upload slice），View B 的准备不得改写 View A。
- 新增 `RenderSubmissionContext` / `RenderViewRecordingContext`，`RenderPipelineFrameContext` 携带它们；RenderRuntime 在 tick 时填入 flight/token/cmdBuf/task。
- `RenderViewBindingTable` 按 flight 持有 View slot：同 token 追加，新 token rewind live count 并复用已有 slot。
- `ForwardFrameResourceSet::beginSubmission` 打开 arena flight；`beginView` 分配/复用该 View 的 frame descriptor sets，写入独立 slices，再更新那一组 set。`getBinding(flightIndex)` 已删除。
- Skinning 仍走 per-flight CRTP buffer，beginView 把 flight 的 skinning handle 抄进 View Binding；同 Scene 多 View 共享 palette，不同 Scene 的 skinning 隔离留到多 Scene 录制。
- 验证：`xmake b ya-render-3d`、`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderViewBindingTableTest.*'`（6/6）、`RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*:RenderGraphCoreTest.FrameUploadArena*` 回归通过、`xmake b ya-game-runtime`、`git diff --check`。
- 保留未完成：Deferred/Shadow 仍按 flight 覆写 Binding；RenderRuntime 不持有 submission 到 fence；仍只录制单 View；无独立 View output；无双 Surface。

## 2026-09-16 checkpoint：pipeline View-local graph context

- Forward/Deferred pipeline 删除 `_lastTickCtx` 与 `_lastFrameInput` 两个跨调用保存槽位。
- Forward 的 postprocess/overlay graph build 改为接收 `executeViewportPass()` 栈内构造的 `FrameContext`，当前 `frame.viewportOverlaySnapshot` 也直接从本次 View 输入传入。
- Deferred 的 graph build 改为使用 `executeDeferredMainGraph()` 栈内构造的 `FrameContext`，不再把当前 frame input 存入 pipeline 成员。
- 该切片只消除 pipeline 层的跨 View 临时状态，不代表 upload arena、descriptor binding 或 output 资源已经完成 View 隔离。
- 验证：`xmake b ya-render-3d-test`、`xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*'`（14/14）、`xmake b ya-game-runtime`。

## 2026-09-16 checkpoint：submission upload cursor

- `FrameUploadArena::beginFlight(flightIndex, frameToken)` 对同一 token 改为幂等调用，不再因为第二个 View 进入而 rewind cursor。
- 同一 submission 内的后续 View 可以继续 `allocate()` aligned slice；测试确认首个 slice offset 为 `0`，追加 slice offset 为 `16`，cursor 从 `4` 增长到 `20`，且 backing buffer identity 保持不变。
- 新 frame token 的 begin 仍保留原有 fence-safe flight 重置语义；本切片没有改变 descriptor pool/set、pipeline binding 或 output image 的所有权。
- 验证：`xmake b ya-render-3d-test`；`xmake r ya-render-3d-test --gtest_filter='RenderGraphCoreTest.FrameUploadArena*'`；`git diff --check`。
- 保留未完成：`RenderSubmissionContext` / `RenderViewRecordingContext`、per-view descriptor/slice table、双 View GPU 录制和双 Surface 验收。允许同 token 追加 allocation 不等于 descriptor binding 已安全隔离。

## 2026-09-16 checkpoint：View order ranges

- RenderFrameData 的 View-owned draw bucket 已从 vector<RenderDrawItem> 副本迁移为 source + order：source 借用不可变 SceneFrameSnapshot 候选 vector，order 由每个 View 独立拥有。
- DrawCandidateView 支持 contiguous/indexed 两种只读 range、operator[]、range-for 和 subview()；indexed view 不物化候选，也不暴露连续 data()。
- RenderFrameExtractor::prepareView() 只初始化和排序 View-owned order，比较器从 shared candidates 读取 camera distance，不再修改共享 RenderDrawItem::sortKey。
- Forward、Deferred、Shadow、EntityId、Debug 和 auxiliary pass 消费者已迁移到 View bucket / DrawCandidateView；Scene-owned RenderShadingDrawBuckets 仅保留候选数据定义和 extractor source 输入。
- 验证：xmake b ya-render-3d-test、xmake r ya-render-3d-test --gtest_filter='RenderRuntimeSnapshotTest.*:DrawCandidateViewTest.*'（14/14），xmake b ya-game-runtime。
- 本 checkpoint 未处理：RenderRuntime 多 View command recording、submission/View descriptor 与 upload lifetime 拆分、双 Surface 验收；下一 checkpoint 仍是 RenderSubmissionContext / RenderViewRecordingContext 生命周期隔离。

## 同 Scene View 复用审计

SceneRenderPlan 已按 SceneId + sceneRevision 去重并持有 shared_ptr<const SceneFrameSnapshot>。本轮已将 RenderFrameData 改为共享 snapshot owner，并把 View-owned draw bucket 进一步迁移为 source pointer + order indices；RenderFrameExtractor::prepareView() 不再按值复制 snapshot 或 RenderDrawItem，也不修改共享候选的 sortKey。

已确认的复用边界：transforms、mesh/material/entity 引用、原始灯光、skinning palette、候选 vector 和资源句柄可跨同 Scene 多 View 共享；camera 矩阵、viewport、visibility、LOD、sort order、shadow/cascade、View targets 和 GPU bindings 必须按 View 独立。shared snapshot、共享候选数据与 View-owned order 已作为一个原子迁移完成。另一个边界是 scheduler scope：同一逻辑帧的多个 surface/window 必须共用一个 SceneRenderScheduler/SceneRenderPlan，否则会重复抽取同一 Scene。GameRuntime 当前 sceneRevision = 0 仍为迁移期占位，不能作为长期缓存 key。

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
| R1 World/View 分离 | 已完成（单 View 契约） | RenderFrameData 组合 Scene snapshot；现有单 View pipeline topology | GameEditor/preview 多 request |
| R2 ViewFamily | 进行中（A/B 入口落地；C/D/E 部分完成；host live Scene 列表落地） | Forward/Deferred topology；display-root inspector getter fallback；DeviceState+Coordinator 不完整拆分 | 4.0.3 Renderer owner，随后才是产品双 viewport / 双 Surface |
| R3 GUI2D/GameUI | 未开始 | WidgetTree live source、UIFrameSnapshot 输入 | UI-only 与 GameUI[ViewId] |
| R4 性能收口 | 未开始 | 优化由 profile 触发 | cache、submit、第三 pipeline 决策 |

## 下一轮接力点

R0/R1 已完成。A/B 入口有效。C：typed pass 入口在，Stage current-view 未清。D：`recordFamily` 入口在，内部仍是 per-view 状态机。E：RenderRuntime 类已删，DeviceState+Coordinator 未闭环。下一刀是公开 `Renderer` owner，不是产品双 viewport。

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

当前边界：`RenderFrameData` 已组合不可变 Scene snapshot，并在 `prepareView()` 中复制 snapshot 后写入 per-view shadow/cascade 和 sortKey；共享 snapshot 不被 View 原地修改。

R1 第一小步已完成：SceneFrameSnapshot 显式承载当前可识别的 Scene lights、draw buckets 和 skinning palettes；RenderFrameData 组合它并继续保留 camera、viewport、frame metadata。随后所有现有 Forward/Deferred/Shadow/Debug/EntityId 消费者已改为显式读取 `sceneSnapshot`。

关键新增约束：UI GPU compose 前必须存在一个明确的 SceneRenderScheduler 边界。它收集 SceneRenderRequest，输出 immutable SceneRenderPlan；UI compose 只能消费 plan 产生的 viewport outputs，不应在 UI 过程中临时触发 Scene/ECS extraction。UI widget tick/buildSnapshot 的先后由 host/product 依据输入依赖决定，不被 Scheduler 强制锁死。

## 设计评估结论

- 设计方向合理：Scene owner 提交 offscreen request，调度器在一帧内按 Scene 去重并按 viewport 展开任务，RenderRuntime 只录制 immutable plan。
- 与 UE 对齐点：Scene/FScene 与 ViewFamily/FSceneRenderer 分离；同一 Scene 的多个 View 共享一次 scene extraction。
- 与 Unity 对齐点：Camera/request 进入 pipeline，ScriptableRenderContext 类似的 plan/record 边界隔离 Scene 领域对象。
- 与 Godot 对齐点：Viewport 是离屏输出和显示绑定点，SubViewport/preview 可对应多个 Scene request。
- 与 ImGui 对齐点：后端只消费 immutable UIFrameSnapshot/SceneRenderPlan，不读取 live WidgetTree/Scene。
- 必须坚持的修正：Scene 不直接依赖 RHI/RenderRuntime；SceneRenderScheduler 不属于 GUI Framework；UI 之前指 GPU compose 之前，不是强制 UI logic/snapshot 晚于 request collection。
- SceneFrameSnapshot 是当前唯一场景快照语义，不得重新引入 WorldFrameSnapshot、RenderFrameExtractor::extract() 或全局 world 抽象。
