# Progress

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

## 环境现状（与本线无关的既有失败，动手前先确认是否仍存在）

1. `ya-gui-widgets-test` 编译失败：`Engine/Test/Source/GuiFrameInspectorTest.cpp`
   找不到 `GUI/Compose/GuiFrameInspectorOverlay.h` —— 该 target 的 deps 只有
   `ya-gui-widgets` + `ya-render-resources`，缺 `ya-gui-compose`。属 GUI 线的
   target 配置缺口。
2. `ya-testing` 中 `WidgetLayoutTest.DockTabBarStripDoubleClickFiresHostCallback`
   在 `WidgetTree::beginPointerDispatch` 断言 `EXC_BREAKPOINT`（SIGTRAP）。
   该断言由 `e1c93a0e [gui] crash on leftover pointer capture instead of eating
   the first click`（2026-09-17）引入，且是本次 S1 base 提交的祖先；S1 未触碰
   该测试与指针逻辑，故为既有失败。
