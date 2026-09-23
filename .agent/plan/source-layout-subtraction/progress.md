# Progress

## 2026-09-20 checkpoint：S2c-2 删掉从未接线的 Game UI 层级拖拽

### 唯一目标

`rg 'moveWidgetEntryDocument|canMoveWidgetEntryDocument|EWidgetEntryDropPosition' Engine Example`
归零。

### 根因

`SceneWidgetEntry.h` 的注释承诺“Game UI hierarchy drag-drop 操作”，并写明
“The editor uses it to reject invalid drops visually (red feedback) before delivery”。
实测全仓扫（`Engine` / `Example` / `Script` / `.agent`，排除定义文件与测试）：
`moveWidgetEntryDocument` / `canMoveWidgetEntryDocument` / `EWidgetEntryDropPosition`
**零生产调用方**——唯一调用方是 `SceneWidgetEntryReparentTest.cpp`。编辑器里
`rg 'reparentWidget|WidgetEntry.*[Dd]rop|_widgetEntries' Applications/GameEditor` 也是空。

那两段 + 支撑它们的五个匿名命名空间 helper（`resolveEntryNode` / `documentContains` /
`FDocumentChildEdge` / `takeChildEdge` / `insertChildEdge`）共 **~145 行**，
占 `SceneWidgetEntry.cpp`（437 → 226 行）的近一半。它们是 Scene 模块里最大的 GUI 代码块，
而 Scene 模块本该是引擎能力层。

### 改动

`SceneWidgetEntry.cpp`：从 437 行减到 226 行；`SceneWidgetEntry.h` 删掉
`EWidgetEntryDropPosition` / `moveWidgetEntryDocument` / `canMoveWidgetEntryDocument`
及各自的注释；删除 `SceneWidgetEntryReparentTest.cpp`（它只测这段死代码）。

**保留**：`UIInstanceOverrideSet::applyTo(UIElement&)` —— 生产调用方在
`GameUIHost.cpp:244`（scene 激活时挂载 entry）与 `ScriptApiCore.cpp:508`，不是死的。

### 为什么这不是“砍掉功能”

这条注释描述的是编辑器 UI Designer 的层级拖拽重排。但编辑器里
`rg 'reparentWidget|reparent.*[Ww]idget|WidgetEntry.*[Dd]rop|_widgetEntries' Applications/GameEditor`
是**空**的——这段 API 从实现那天起就没被接上，头注释描述的是一个未兑现的承诺。
与 `OffscreenJobRunner`（2026-09-19）同一形状：找不到生产者一律删除。

### 验证

- `rg` 三条全部归零。
- `ya-scene-core` / `ya-testing` / `ya-game-editor` / `ya-game-runtime` / `ya-engine` /
  `ya-runtime` / `ya-render-3d-test` build ok；`ya-render-3d-test` **175/175**。
- `SceneWidgetEntryTest.*` + `SceneSerializerTest.*` + `UIDocumentTest.*` +
  `EditorUIDesignerSessionTest.*` + `GameUIHostTest.*` + `HostSceneExtractTest.*` =
  **49 passed / 1 failed**，那条（`GameUIHostTest.BuildSnapshotComposesMountedWidgets`）是基线。
- runtime smoke `c775245a`、editor smoke `451fe3cd` 逐字节不变。

### 保留 / 未完成

- **未完成**：`ya-scene-core -> ya-gui-widgets` 这条 target 边仍在。剩下的 226 行里，
  Scene 模块对 GUI 的真实需求是 `UIDocument`（authoring 文档）+ `FCanvasSlotArgs`（槽位意图）。
  要把这条边砍到 0 需要先定 “Game UI 授权数据住在哪”（Scene 按值持有 `std::vector<SceneWidgetEntry>`，
  编辑器授权、GameUIHost 挂载，三者跨 Framework/Application 两层）——这是产品/架构决策，
  已登记在 `plan.md` S2c，不在本刀范围。

## 2026-09-20 checkpoint：S2c-1 渲染器不再了解编辑器

### 唯一目标

`rg 'Editor[A-Z]' Framework/Render/Render3D` 归零。

### 根因

`RenderDeviceState::prepareComposePipelines()`（渲染器，Framework 层）准备了
`EditorViewportCompose` 和 `EditorCanvasPreview` 两个 pipeline。全仓扫 `rg 'Editor[A-Z]'
Framework/Render/Render3D`，这两处就是**唯一**的 editor 知识。而这两个 kind 的记录方是编辑器，
它已经在同一帧更早的 `onLogic`（`updateEditorCameraAndPrepareCompose`）里 prepare 过同样的
kind + 同样的格式（depth 也来自同一个 `activePipeline->getViewportDepthFormat()`）。
`preparePassPipeline` 对相同 slot + colorFormat + depthFormat 是缓存早退，所以这是冗余调用。
运行时没有编辑器时，这两个 kind 没人记录，prepare 是纯浪费。

### 改动

删除那两处 prep，只留 `RuntimeUIComposite`（运行时自己的 UI packet，记录方是渲染器的
view compose 阶段，不属于编辑器），并在函数头写明"为什么编辑器的 kind 不在这里"。

**没删的**：`prepareComposePipelines()` 本身与 `RuntimeUIComposite`。查过 `ViewCompose.cpp`：
`recordCameraViewCompose` 的守卫是 `cameraDisplayRT && (uiFrameSnapshot || bHasInsets)`，
即**只画 inset、没有 UI packet** 也会录这个 kind；而 `prepareFrameRecord` 里那处守卫
`if (plan.frame.uiFrameSnapshot)` 只覆盖有 UI snapshot 的情况。删掉这里会让 insets-only
的 compose 拿到未准备好的 pipeline。

### 验证

- `rg 'Editor[A-Z]' Framework/Render/Render3D` 只剩 `RenderFeatures.h` 里 `Gizmo = ... ///< Editor
  companion visuals` 一条注释——那是声明方设置的 feature 位，渲染器不知道"编辑器"。
- `ya-render-3d` / `ya-testing` / `ya-game-editor` / `ya-runtime` / `ya-engine` / `GUIWorkbench`
  build ok；`ya-render-3d-test` **175/175**。
- runtime smoke `c775245a` 逐字节不变。
- editor smoke 是 `451fe3cd`；把本刀改动 `git stash` 后重跑**同样是 `451fe3cd`**——
  是并发作者的 dock 改动 / `editor.dockLayout` 持久化状态在漂移，不是本刀。

### 保留 / 未完成

- **保留**：`ya-render-3d -> ya-gui-compose` 这条 target 边（理由见 `plan.md` S2c 的复查结论）。
- **未完成**：Scene 侧（`ya-scene-core -> ya-gui-widgets`），见下一节。

## 2026-09-19 checkpoint：S2b `ya-gui-framework` 只聚合 GUI 库

### 唯一目标

`ya-gui-framework` 是"纯 GUI 代码的唯一链接目标"，但它公开拉入 `ya-app-kernel` /
`ya-app-control` / `ya-hierarchy` / `ya-gui-tooling`。于是**"链接 GUI framework"等于"你有一个
应用 + 一个 demo 工具"**，这正是"抽不出独立 GUI app"读不出来的根因。

### 改动

`Engine/Source/Framework/GUI/xmake.lua`：public deps 收敛为 GUI 库闭包
（`ya-foundation-core` / `ya-rhi` / `ya-rhi-backend-common` / `ya-rhi-vulkan` /
`ya-render-resources` / `ya-render-2d` / `ya-gui-widgets` / `ya-gui-compose`），
并把每条被删掉的 dep"为什么不该在聚合里"写进 dep 列表上方的注释：

| 删掉的 dep | 依据 |
| --- | --- |
| `ya-app-kernel` / `ya-app-control` | windowless main chain。`ya-gui-host` 已经自己 deps kernel（host 就是 app）；`GUI/Runtime/**` 里**没有任何文件** include `App/Kernel` 或 `App/Control` |
| `ya-hierarchy` | `Framework/GUI/` 下**没有一个文件** include `Hierarchy/` |
| `ya-gui-tooling` | Workbench demo app，不是库。`GUIWorkbench` 已经显式 deps 它 |

`Engine/Test/Test.xmake.lua`：`ya-gui-closure-test` 补 `add_deps("ya-app-kernel")`。该 target
包含 `AppKernelTest.cpp`，它直接驱动 windowless main chain；kernel 不属于 GUI 库，所以由它自己点名
（`ya-app-kernel` 已 public deps `ya-app-control`，control 不需要再写）。

### 验证

1. `xmake l` 读出的 `ya-gui-framework` deps = 上面 8 个，`app-kernel` / `app-control` /
   `hierarchy` / `tooling` 都不在。
2. `xmake show -t GreedySnake` 那类 engine-only 消费方的 include 根不受影响（本刀只动 GUI 行）。
3. `rg -l 'Workbench' Engine/Test/Source/` 命中的两个文件里，`ToolControlsTest.cpp` 只有注释与
   字符串字面量（`"YA Workbench"`），没有真的用 tooling —— 所以断开不损失覆盖。
4. 构建：`ya-gui-framework` / `ya-gui-closure-test` / `ya-gui-host` / `GUIWorkbench` /
   `ya-gui-minimal-host` / `ya-engine` / `ya-runtime` 全部 build ok。
5. 回归：`ya-gui-closure-test` 排除两个既有 SIGTRAP 崩溃用例后 **589 passed / 3 failed**；
   用 `git stash` 去掉本刀改动后**同样是 589 passed / 3 failed**，逐条相同
   （`ToolControlsTest.SplitPaneDividerDragChangesRatioAndEndsSession`、
   `ToolControlsTest.ScrollViewportNestedInsideSplitKeepsCustomLayout`、
   `WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots`）——
   三条都来自并发作者在飞的 dock 改动，本刀零回归。

### 保留 / 未完成 / 偏离

- **保留**：`ya-gui-host` 仍然公开 deps `ya-app-kernel` / `ya-app-control`（host 确实需要它们）。
- **未完成**：(1) S2c（`ya-scene-core -> ya-gui-widgets` 与 `ya-render-3d -> ya-gui-compose`）；
  (2) `skills/gui-framework/SKILL.md` 里描述旧聚合的两处文字待改 —— 该文件当时被另一条线的
  dock 改动占着，不并入本提交以免卷进别人的在飞改动。
- **顺带登记（本轮未修）**：`ya-gui-widgets-test` **编译失败**，
  `GuiFrameInspectorTest.cpp` 找不到 `GUI/Compose/GuiFrameInspectorOverlay.h` —— 该 target 的
  deps 只有 `ya-gui-widgets` + `ya-render-resources`，而 overlay 属于 `ya-gui-compose`。
  这是 `render-view-family` R0 里已登记的既有缺口，早于本刀存在（本刀 stash 后同样失败），
  且属于另一条目标清单，故不在 S2b 里修。

## 2026-09-19 checkpoint：S2a `ya-engine` 不再公开包含 `ya-game-runtime`

### 唯一目标

`ya-engine` 是 Framework 层的聚合门面。`ya-game-runtime` 是 Applications 层的应用形态
（游戏 shell），却挂在它的 public deps 里，于是"链接 engine"隐含"你有一个游戏 app"。
本刀断开这条边，让消费方自己点名。

### 改动

`Engine/YA.xmake.lua`：从 `ya-engine` 的 public deps 删掉 `ya-game-runtime`，并把
"为什么不在里面"写进 dep 列表上方的注释。同时修正文件头对聚合门面的描述
（原文说 "re-exports every module"，现在是 Framework 层）。

四条真实消费方改为显式声明（都是本来就是应用形态的地方）：

| 目标 | 为什么需要 |
| --- | --- |
| `ya-runtime`（`Engine/Programs/YARuntime`） | `Entry.cpp` include `GameRuntime/`，这个 exe 就是游戏 app 本体 |
| `ya-game-editor` | 编辑器是 App 的一个 `IModule`，24 个 TU 用 `GameRuntime/App.h` 等 |
| `HelloMaterial`（Example） | 示例以 `IModule` 挂进 App |
| `ya-testing` | 测试直接驱动 App shell |

两条带 monolith 分支的目标（`ya-game-editor` / `HelloMaterial`）在 monolith 下用
`{ links = false }`，与它们已有的 `ya-engine` 写法一致：monolith 里模块是静态库、由宿主 exe
持有唯一实例，插件通过 `dynamic_lookup` 解析符号。

`GreedySnake` 与 `test/` 下的单文件目标**未改动**——实测它们不使用 `GameRuntime/` 头，
所以是这条边为多余的直接证据。

### 验证

1. `xmake show -t ya-engine` 的 deps 列表里不再有 `ya-game-runtime`（用 xmake 自己的解析结果，
   不是读源码）；只剩 `ya-game-editor`（本来就没在里面）。
2. `rg 'ya-game-runtime' --glob '*.lua'` 只剩 4 处显式声明的消费方 + 它自己的 target 定义 +
   `YA.xmake.lua` 里那条解释性注释。没有任何路径把它重新带回 engine 闭包
   （`ya-gui-framework` / `ya-gui-host` 都不 deps 它）。
3. `xmake show -t GreedySnake`（只 deps `ya-engine`）的 `includedirs` 里**没有**
   `GameRuntime/include`——engine-only 消费方确实拿不到应用形态的 include 根。
4. 构建：`ya-engine` / `ya-game-runtime` / `ya-game-editor` / `ya-runtime` / `ya-testing` /
   `HelloMaterial` / `GreedySnake` 全部 build ok。

### 保留 / 未完成 / 偏离

- **保留**：`ya-engine` 仍然公开聚合整个 Framework 层，这条没动。
- **未完成**：S2b（拆 `ya-gui-framework` 聚合）、S2c（断 `ya-scene-core -> ya-gui-widgets` 与
  `ya-render-3d -> ya-gui-compose`）。
- **偏离**：无。原计划只写了"不再公开包含"，实测还需要给 4 个消费方补显式声明；
  这不是偏离，是原文没写清的实施面。
- 顺带核实：原 S2 候选里的"修 `ya-rhi-backend-common` 自依赖自身的笔误"**已不存在**，
  当前 `Backend/xmake.lua` 里它只 deps `ya-rhi`。属过时假设，已在 `plan.md` 划掉。

## 当前状态

- S1 已完成并提交（两次提交，保留 blame）。S2–S4 未开始。S5 已落地。
- 已知且与本线无关的既有失败：见文末"环境现状"。

## 2026-09-18 checkpoint：S1 公开头唯一物理位置

### 唯一目标

删掉 `include/` 影子层：让每个公开头只有一个物理位置、一个公开路径，
并把这条规则写进 agent 规则。

### 改动规模（实测）

| 项 | 数量 |
| --- | --- |
| include 根 | 33 |
| 删除的转发 stub | 493 |
| 迁移到公开路径的公开头 | 476 |
| 提升为公开的私有头（被公开头 include） | 6 |
| 删除的别名公开路径 | 17（覆盖 16 个物理头） |
| 重写 include 的文件 | 232 |
| 结果：公开头总数 | 550 |
| 结果：留在模块根的私有头 | 18 |

### 关键决策

1. **单一物理位置，永不镜像。** 公开头放 `include/`，私有头留模块根；
   不再有"真身 + 转发 stub"。
2. **一个物理头一个公开路径。** 历史别名（`GUI/Host/GUIApp.h`、
   `Render3D/Component/...`）删除，只保留 canonical。
3. **公开头只能 include 公开头。** 被公开头拉进来的 6 个私有头
   （`PointShadowIndirectResources.h`、`VulkanRenderSurfaceContext.h`、
   `LineRender.h`、`QuadRender.h`、`CompanionManager.inl`、`Entity.inl`）
   按规则提升为公开，而不是给它们的目录新增 include 根。
4. **搬迁必须保留 blame → 拆成两次提交。** git 的 rename 识别只在
   "删除路径 vs 新增路径"之间配对；目标路径若已存在于父提交（stub 就占着），
   一次提交里无论先删哪边都只能得到 `D 旧 + M 目标`，目标路径的 blame 会
   塌到 stub 上。因此：
   - `[source] delete the include/ mirror stubs`（先腾出目录位）
   - `[source] make include/ the single home of public headers`（再搬真身 + 修 include）

   实测：`git show -M --summary` 得到 **482 个 rename**，其中 422 个 0 行改动；
   `git blame` 能看到 2025-06-20 等历史提交；`git log --follow` 可追到历史路径。
   反例（被否决）：单提交 `git mv -f` 覆盖 stub —— 476 个公开头的 blame 全部
   塌成"本提交 + stub 引入提交"。
5. **清理失效的 `add_headerfiles`。** 头移动后 9 个模块里的旧位置模式
   （`add_headerfiles("*.h")`、`("Node.h")`、`("Core/**.h")`、
   `("Render.h", ...)`、`("Workbench/*.h")`、`("*.h","*.inl")`、
   `("*.h","Controls/**.h","../Binding/*.h","../Layout/*.h")`）变成死配置，
   一并删除；PCH `set_pcheader` 改到 `Core/include/Core/Common/FWD.h`。

### 规则落点

- `.agent/skills/code-reorganize/SKILL.md`：新增"头文件布局与 include 规则"+
  "搬迁必须保留 blame"；把旧的"公共转发头必须由所属模块公开"改成"公开头"；
  验证清单加 3 条（0 stub / 无失效 pattern / 保留 blame）。
- `AGENTS.md`：Core Rules 加第 15 条（单物理位置、单公开路径、无 stub）；
  Repo Facts 从过时的两层前路径（`Engine/Source/Core/`、
  `Runtime/Rendering/` 等）修正为当前 `Framework/` / `Applications/` 两层
  与 include 布局。
- `.agent/skills/render-arch/SKILL.md`：目录锚点同样从旧路径修正。

### 验证

- `xmake b ya-engine`（80.9s）、`ya-testing`、`ya-render-3d-test`、
  `ya-game-editor`、`ya-game-runtime`、`GUIWorkbench`、`HelloMaterial`、
  `GreedySnake`、`ya-runtime`、`ya-gui-framework`、
  `ya-gui-headless-host-test`、`ya-gui-minimal-host`、`ya-gui-closure-test`、
  `ya-render-2d-test`、`ya-gui-tooling`、`ya-resource-runtime-closure-test`、
  `ya-gui-workbench-workspace-test`、`ya-gui-declarative-contract-test`、
  `ya-ecs-core-test` 全部通过。
- `xmake r ya-render-3d-test`：**172/172 通过**。
- 结构自检：0 转发 stub、0 多公开路径、0 未解析 include（第三方除外）、
  0 失效 `add_headerfiles` 模式。

## 2026-09-18 checkpoint：S5 计划目录收敛

### 唯一目标

让 `./.agent/plan` 能一眼区分"现在要改什么"和"当初想过什么"：活跃目录只留
仍有代码要改的线，已收口/被接手/显式延后的进 `./archive/`，并在
`./AGENTS.md` 留一份活跃线入口表 + 归档理由表。

### 归档判据（写进 `./AGENTS.md`）

1. **已完成**：checkpoint 全绿，且结论已沉淀进 skill。
2. **已交由别的线接手**：原方向被另一条活跃线的章节覆盖。
3. **明确延后**：本轮该做的已落地，剩下的是显式推迟的层。

### 归档明细（13 条）

| 目录 | 判据 | 依据 |
| --- | --- | --- |
| `gui-editor-dock-layout` | 1 | 2/2 |
| `gui-editor-tab-lifecycle` | 1 | 5/5，结论已进 `skills/gui-framework` |
| `gui-framework-architecture-hardening` | 1 | 9/9 |
| `gui-gallery-ux-pass` | 1 | 18/18 |
| `gui-perf-observability` | 1 | 5/5 |
| `gui-god-class-peel` | 1 | 3/3 |
| `gui-multi-os-window-editor` | 1 | 50/50，多窗口能力已进代码 |
| `model-instance-authoring` | 1 | 自述"已实现（本轮）" |
| `editor-camera-body` | 1 | 代码已落地，仅剩人工目视确认 |
| `gui-animation` | 1、3 | 框架层已落地并进 skill；Game UI 轨道层延后，记录在 `gui-invalidation-architecture/animation-integration.md` |
| `gui-capability-gap` | 2 | 被 `gui-framework-editor-readiness`（Phase 8 移除 ImGui 双栈）接手 |
| `render-pipeline-dedup-runtime-split` | 2 | 被 `render-view-family` §4.0.2 接手 |
| `dockspace-node-tree` | 2 | 已由 `gui-editor-dock-layout` / `gui-editor-tab-lifecycle` 落地并收口 |

### 保留 12 条活跃线

`render-view-family`、`gui-framework-editor-readiness`、
`gui-framework-editor-runtime-refactor`、`gui-kernel-ux-parity`、
`gui-invalidation-architecture`、`gui-style-system-convergence`、
`gui-anchor-to-slot`、`gui-editor-structure`、`font-framework-convergence`、
`editor-undo-redo`、`source-layout-subtraction`、`editor-ui-grouping`。
其中 9 条仍有开放 checkpoint，压到 2–3 条不现实；原计划里的"2–3 条"是理想值，
不是本轮验收项。

### 顺带修复的引用

搬目录会打断指向它们的链接。同步修改 5 处：

- `skills/gui-framework/SKILL.md` 4 处（`gui-editor-tab-lifecycle` ×2、
  `gui-animation/plan.md` ×2）
- `skills/render-arch/SKILL.md` 1 处（`gui-multi-os-window-editor/c2_view_model.md`）
- `Engine/Test/Source/GuiAnimationTest.cpp` 注释 1 处

理由：这些引用是"设计记录在这里"的指路牌。不跟着改，归档就等于把结论弄丢。

## 环境现状（2026-09-23 复核；此前两条已修，登记当前失败清单）

1. ~~`ya-gui-widgets-test` 编译失败（GuiFrameInspectorOverlay 在 Compose）~~
   **已修（b562347a）**：`GuiFrameInspectorTest.cpp` 在 `ya-gui-closure-test` 里已
   有一份注册，`ya-gui-widgets-test` 里的重复条目删除，闭合门禁语义恢复原样。
2. ~~`WidgetTreeTest` 系统层 detach 断言 trap 截断全量套件~~ **已被 GUI 线解决
  （760fd1f1）**：`WidgetTree::detach` 对系统层从 `YA_CORE_ASSERT(false)` 改为
   日志拒绝，测试语义（detach 被拒、层仍在）成立。`DockTabBarStripDoubleClick…`
   的 trap 亦不再复现。
3. `ToolControlsTest` 两个 split 用例：`UISplitLayout` 默认值在 c0ae42e4 拆头文件时
   改变（`dividerThickness` 6→4、`minFirst/SecondExtent` 40→0），测试断言仍按旧默认。
   **已修**：fixture 显式 pin `setDividerThickness(6.0f)` / `setMinFirst/SecondExtent(40.0f)`
   （测试的意图是 drag/clamp 行为，几何自持；`ya-testing` 与 `ya-gui-closure-test` 双绿）。
   已在 2aef14d4 基线 worktree 验证这三个失败全部预存，不是近期渲染/GUI 改动引入。
4. ~~当前 `ya-testing` 全量 8 个失败~~ **全部收口（2026-09-23 测试门禁批次）**。
   归因与处置见 `../memories/stale_test_assertions_after_contract_change.md`：5 条是断言过期
   （`engine.panel` 拆出绘制、剪贴板是进程级、按下标找控件、tab 条未隐藏就断言手柄、
   `visitAllProperties` 默认参数翻转），2 条是用例本身无意义（`EXPECT_EXIT` 里再起线程、
   依赖被注释掉的代码生成器），1 条是本机 SDL 的 minimized 标志残留（surface 判据无误）。
   该文件同时记下了当时的基线对照方法与逐项归属，本段保留为历史。
5. 现在的门禁入口是 `make test`（= `xmake test` → `xmake b|r -g test`），所有测试 target
   都带 `set_group("test")`：13 个 target 报 PASSED，**2703 passed / 0 failed / 1 skipped**
   （那条 skip 是本机 SDL 最小化前提不成立的 RHI 用例）。它取代了「逐个 target 手动跑」
   和此前无入口的 `python3 Script/ya.py test --target ya`。
