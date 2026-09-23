# Session Checklist

## 开工前

- [ ] 阅读根 `AGENTS.md`、`.agent/plan/AGENTS.md`。
- [ ] 读 `ya-build`、`render-arch`；改到 GUI compose 时再读 `gui-framework`。
- [ ] `git status`，确认没有别的写者正在改同一批文件（本仓同时有并发写者）。
- [ ] 复述本轮唯一目标、边界、保留项、非目标。
- [ ] 用 `grep`/`rg` 核对真实符号与调用方，不用旧计划里的路径代替源码事实。
- [ ] 确认本轮不会把「排布」搬进 Framework/Render（见 plan §1）。

## 实施中

- [ ] 只改当前 checkpoint 需要的抽象、调用点与测试。
- [ ] 目录搬迁单独成刀：先 `git mv` 不改内容，再改 include；目标路径此前不存在，
      git 会按相似度识别为 rename，blame 保留。
- [ ] 不在 command recording 中途重建 GPU 资源；检查 deferred deletion / keepalive。
- [ ] 新增 include 显式写公开路径，不依赖别人传递进来的头。
- [ ] 不把 Forward / Deferred 抽成万能基类；不新增总入口式 `Renderer` 编排。

## 收尾前

- [ ] 受影响目标 build：`ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-testing`。
- [ ] `xmake r ya-render-3d-test`（期望 185/185；batch 2 删掉 2 个只测已删接口的 case，第三批与本批
      各加了 case）。
- [ ] `ya-testing` 滤镜跑一遍，与已登记基线比对（排除 `WidgetTreeTest.SystemLayersCannotBeDetached`）。
- [ ] `run_display_compose_parity.py --skip-build` 期望 PASS、md5 `c775245a…`。
- [ ] `run_widgettree_editor_smoke.py --skip-build` 期望 exit=0。
- [ ] `git diff --check`；确认没有动生成文件、`.vscode/settings.json`、根 `xmake.lua`、`Engine/Plugins/log.cc`。
- [ ] 更新 `progress.md` / `feature_matrix.json`。
- [ ] 明确记录保留项、未完成项、偏离项。
- [ ] 代码、测试、plan 文件同一 commit 提交，格式 `[render/app] …`。

## 最近一次 checkpoint

- 2026-09-23 第三批收尾（不再被声明的 View 必须被淘汰）：`c0e2275a` 只做了「View 资源按身份分键」，
  没有回答「本 tick 不再声明的 View 怎么办」——选中相机→声明 preview、取消选中→不再声明，那条 entry
  与它唯一的 `shared_ptr<RenderTexture>` 附件永久留下，查询返回上一帧的图（`render-arch` 契约禁止的
  兜底）。判据用**本 tick 的 `SceneRenderPlan::viewTasks`**（新助手 `planDeclaresView`，无定时器/纪元
  计数）：`ViewResourceTable::retainIf` + 新虚方法 `IRenderPipeline::reconcilePublishedViews(plan)`，
  由 `RenderDeviceState::prepareFrameRecord` 在 command buffer 打开前**整 tick 调用一次**（`recordFamily`
  一个 tick 会进多次，而 `recordViewFamilies` 在 plan 无 View 时根本不进——按 family 淘汰既会误杀也会
  漏掉「视口标签页关掉」这条真实路径）。释放安全性已核实：表里的 shared_ptr 不是唯一保活，
  `retainPublishedViewOutputs` 已把同一批 owner 放进 `RenderSubmission::_keepalives` 并对 command buffer
  `retireResource`，flight 的 keepalive 只在其换 token 复用（fence 已过）时清空，**keepalive 设计无需改动**。
  顺带：`RenderTargetCatalog::Entry` 增加 `SceneViewId viewId`（0 = 不属于任何 View），
  `RuntimeRenderTargetSection` 显示它，同尺寸不同身份的 View 行从此可区分。验证（**APFS clonefile 副本**：
  主仓库被并发写者对 `ForwardViewResources` / `DeferredViewResources` 的在飞 WIP 卡住，副本里把这两个头
  checkout 回 HEAD，主仓库未动）：五个目标 build ok；`ya-render-3d-test` 185/185（181 + 4 个新 case，
  其中两个用 `weak_ptr` 证明附件真的被释放）；§8 滤镜 703 ran / 686 passed / 11 skipped / 6 failed（与
  基线同 6 个）；parity PASS（两张图 md5 仍为 `c775245a…`）；编辑器 smoke exit=0 六步全过。
- 2026-09-23 AB9（review 第二批：收回本 tick 的 View packet owner）：`AppRenderState::viewFrameDataPerFlight`
  删除（它把「本 tick 的 View 准备数据」和「跨帧保活/免分配」两件事混在 App 长期状态里，还逼出
  `AppLifecycle` 的 quit / `handleSceneDestroy` 两处手工清空）。`ExtractedSceneRender` 现在自己持有
  `_frameData`，`pairViewFrames()` 不再收外部容器；因为 `tickRender` 会把 `sceneRender` 移进
  `RenderFramePlan`，move constructor/assignment 必须显式重绑 `SceneViewRecording` 的 task/frameData
  借用指针（不能依赖 vector 存储地址不变），新增的测试断言就是钉这条。顺带删除零生产消费者的
  `ExtractedSceneRender::hostFrameData()`——display-root 身份的唯一来源仍是
  `SceneRenderPlan::displayRootTask()`。保活审计：`RenderFrameData::sceneResources` 只含录制期读取的
  descriptor handle 与 processor 指针，跨 submit 的 GPU 保活由 `RenderSubmission` 与
  `retainPublishedViewOutputs()` 负责，没有「packet 必须留在 App 才能保活」的约束。
  验证（**主仓库直接跑，不需要 batch 1 的 APFS 副本**——并发写者已把他们的三个文件改到自洽，只保持未
  stage）：`ya-render-3d-test` build ok、175/175（原 177 减 2 个死接口 case）；`ya-game-runtime` /
  `ya-runtime` / `ya-game-editor` / `ya-testing` build ok；§8 滤镜 696 ran / 679 passed / 11 skipped /
  6 failed（与基线同 6 个，比 batch 1 少的 2 个就是被删的 case）；parity PASS（两张图 md5 均为
  `c775245a…`）；编辑器 smoke exit=0 六步全过。详见 progress.md 对应小节。
- 2026-09-22 AB9（review 第一批：计划对齐 + 删噪声）：修掉 `GameRuntimeTickOrchestrator::pumpOffscreenTasks`
  ——它只有自我递归、没有任何调用者，而 `tickRender` 直接匿名调 `getOffscreenTaskService().tick(...)`；
  修法是把 663e0f82 想命名的那一步接回去（`tickRender` 调它，函数体做实际工作），不是删名字。删除零调用方的
  `IRenderPipeline::getViewExtent()` / Forward / Deferred 三个实现，以及 `declareViews` 已不再使用的
  `device` 形参。`render-view-family/plan.md` 加状态校正（Coordinator 已删、`Renderer` 改名方向已作废、
  下一刀在 `render-application-boundary`），§2 顶部加「先读这条」校正块并保留原文标注为修复前状态。
  新增 plan §4b：把已核对未完成的六项所有权偏差逐条记清（整帧录制编排、`viewFrameDataPerFlight` 归属、
  pipeline 单 View 资源、`setActiveSceneProvider` 隐式当前 Scene、`recordExtensions` 携带行为、renderer 的
  编辑器查询面与 `Render3D -> GUI/Compose` include）。验证：全目标 build ok；`ya-render-3d-test` 177/177；
  滤镜 681 passed / 6 failed（与基线同 6 个）；parity PASS（md5 `c775245a…`）；编辑器 smoke exit=0。
  **注意**：证据是在 APFS 副本里取得的——主仓库当时被另一位并发写者的半截 WIP（删 `ViewResources` 的
  `color`/`depth` 别名而 `ViewportDebugCatalogBuilder.cpp` 仍在读）编译不过；副本里把他们的两个头 checkout 回
  HEAD 后得到等价于本 checkpoint 的树。这在 progress.md 里有完整说明，不要误记成主仓库当时是全绿的。
- 2026-09-22 AB8-step2：`HostViewState` 拆成设置与排布。`HostViewState.h` →
  `HostRenderSettings.h`（只剩 `clock` / `renderResolution` / `renderScale`）；新增
  `GameRuntime/HostViewportView.h` 的 `HostViewportView{viewId, flightIndex, view, projection,
  cameraPos}`，由 `tickRender` 从 plan 的 display root **一次写入**，`declareViews` 不再写 host state。
  编辑器（`EditorViewportCompositor` / `makeEditorSurfaceContext` / `EditorLayer::pickEntity`）与
  automation 改读该排布值。自我修正：上一版计划说「需先定 PIE 相机归属」是错的——保持今天行为
  即逐字节等价，那个问题现在是独立的产品选择，不再阻塞。验证：4 个目标 build ok；
  `ya-render-3d-test` 177/177；滤镜 681 passed / 11 skipped / 6 failed（与基线同 6 个）；parity PASS
  （md5 `c775245a…`）；编辑器 smoke exit=0。下一刀：AB7（把整帧录制编排搬到应用侧
  `RuntimeRenderContext`），开刀前先给 `AppAutomation` 一个 typed 入口替掉它对
  `_pipelineCoordinator` 私有布局的访问。
- 2026-09-22 AB8-step1：帧的 View 事实只从计划里读一次。`RenderDeviceState::_pipelineViewRect`
  与 `applyViewResize()` 删除，`PipelineCoordinator::applyPendingChanges(Rect2D viewRect)` 收本帧
  View rect（来自 plan 的 display root），自己只持有 `_appliedViewRect`；`reapplyViewRectSink` 删除；
  `declareViews` 不再把 rect 推给 device。renderer 不再记录「哪个 View 是宿主的」：
  `_publishedOutputViewId` / `publishViewOutputIdentity()` / `publishedViewOutput()` 删除，应用侧新增
  `HostViewportBinding{viewId, flightIndex}`（唯一写者 `tickRender`，从 plan 的 display root）。
  无身份查询（`getViewExtent` / `getActiveViewImageShared` / `getViewDisplayImageShared` /
  `getPostprocessOutputImageShared` / `getViewDisplayImage` / `getViewDisplayImageFormat`）全部删除，
  改为 `getViewOutput(flightIndex, viewId)` / `surfaceImageFor(...)` /
  `buildViewportSnapshot(flightIndex, viewId, Scene*)`。顺带删掉 `prepareFrameRecord` 里读上一帧
  display image 格式的冗余 prepare，与 `record()` 结尾三个重复的 `retain`。验证：6 个目标 build ok；
  `ya-render-3d-test` 177/177；滤镜 681 passed / 11 skipped / 6 failed（与基线同 6 个）；parity PASS
  （md5 `c775245a…`）；编辑器 smoke exit=0。下一刀：AB8-step2（`HostViewState` 拆分），需先定 PIE
  下编辑器视口 overlay/picking 用哪个相机。
- 2026-09-22 AB3-step1：编辑器通过应用读渲染器。`IRenderPipeline` 增加 `kind()` /
  `getLastFrameGraphTopology()` / `IRenderPipelineSettings` facet；`DeferredRenderPipeline::SettingsSnapshot`
  上移为 `Render3D/Common/RenderPipelineSettings.h` 的 `RenderPipelineSettings`（带 `kind`），Forward 实现同一 facet；
  `PipelineCoordinator::ERenderPipeline` 变成 `ERenderPipelineKind` 别名。`AppRenderServices` 成为应用侧唯一缝
  （新增 `hasRenderer` 与 pipeline/设置/topology/view extent/output/catalog/debug/diagnostics 转发）。
  GameEditor 的 `RenderDeviceState` 引用与 concrete-pipeline `dynamic_cast` 都归零；
  `buildPipelineDebugOutputCatalog` / `getDeferredPipelineDebugViews` 改 private，`AppRenderServices::getRenderPipeline()`
  删除。同时展开两个自 `d9de4739` 起被压成单行的 Section 文件。验证：6 个目标 build ok；`ya-render-3d-test` 177/177；
  滤镜 681 passed / 11 skipped / 6 failed（与基线同 6 个）；parity PASS（md5 `c775245a…`）；编辑器 smoke exit=0。
  未完成：AB3-step2（`DebugRenderSystem&` / `RenderDiagnosticsService&` / catalog 返回体仍是 renderer 类型穿过 facade）
  与 `AppAutomation.cpp` 对 `_pipelineCoordinator` 私有布局的依赖。下一刀：AB7（把整帧录制编排搬到应用侧 `RuntimeRenderContext`）。
- 2026-09-22 AB4-step1：present target 变成 per-surface。新增 `SurfacePresentation`
  （一个 OS 窗口的导入图 + 每图 executor + 按该 surface format 建的 `SurfaceWritePass`）；
  `RenderDeviceState` 由单个 `PresentationGraphService` + `SurfaceWritePass` 改为
  `_surfacePresentations` 表，`record()` 在 `prepareFrameRecord` 前按 `plan.present.surface`
  解析/构建；`initPresentationResources` → `initSurfacePresentations`（init 期零 GPU 工作、
  无主 surface 特权，只登记 teardown）；`getPresentationImageShared(IRenderSurfaceContext&)`
  为非创建查询。验证：六个目标 build ok；`ya-render-3d-test` 177/177；滤镜 681 passed /
  11 skipped / 6 failed（与基线同 6 个）；parity PASS（md5 `c775245a…`）；编辑器 smoke exit=0。
  **注意**：产品路径尚无「非主 surface 的 presentation」，好处未端到端验证；额外窗口仍走 GUI
  host 的 `presentGuiSnapshot`（只呈现 chrome），拖出去的 viewport 看不到世界画面属既有缺口。
  下一刀：AB4-step2（额外窗口每帧 tick 自己的 chrome 并经 `record` 呈现）。
- 2026-09-22 AB1+AB2：`SceneRenderScheduler` 从 `AppRenderState` 的字段变成 `tickRender`
  的局部对象（`declareViews` 收 `SceneRenderScheduler&`，删 `SceneSchedulerGuard`）；
  `GameRuntime/Lifecycle/` 的四个渲染排布文件搬到 `GameRuntime/Render/`（纯 `git mv` +
  include 改写）。验证：四个目标 build ok；`ya-render-3d-test` 177/177；滤镜 681 passed /
  11 skipped / 6 failed（与基线同 6 个）；parity PASS（md5 `c775245a…`）；编辑器 smoke exit=0。
  下一刀：AB4（presentation 拆纯 pass + 应用侧 present，先解 primary-surface 耦合）。
