# Progress

## 当前状态

- S1 已完成并提交（两次提交，保留 blame）。S2–S5 未开始。
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

