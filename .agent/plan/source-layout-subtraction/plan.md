# Source 布局减法（S1–S5）

> 建立：2026-09-18
> 关联：`./.agent/skills/code-reorganize/SKILL.md`（头文件布局与 include 规则）
> 状态：S1 已落地

## 0. 问题

目录的**形状**（Framework / Applications 两层 + 每模块一个 target）是对的，但读者
依然找不到主流程。根因不是缺抽象，而是三层可删除的噪声加少数职责倒置：

1. `include/` 影子层：每个公开头存在两份（模块根真身 + `include/` 下 2 行转发
   stub），另有多路径别名。每次跳转定义、每次 `rg` 都先落到 stub 上。
2. 少数依赖倒置：`ya-scene-core -> ya-gui-widgets`、
   `ya-render-3d -> ya-gui-compose`、`ya-engine` 公开包含 `ya-game-runtime`、
   `ya-gui-framework` 拉入 AppKernel/AppControl/Hierarchy（"GUI framework" 其实
   是 GUI + app shell + module system）。
3. 体量集中：`EnvironmentLightingProcessor.cpp` 2558 行、`RenderGraph.cpp` 1987、
   `GUIAppHost.cpp` 1910、`WidgetTree.cpp` 1832、`UILayout.cpp` 1774。
4. 角色后缀泛滥：约 150 个 `*Context`/`*Services`/`*Host`/`*Section`/`*Registry`
   命名描述"在层蛋糕哪一层"，不描述"做什么"。
5. `.agent/plan` 自身膨胀到 22 个活跃目录，搜索真相时命中大量过时计划。

## 1. 边界（不做的事）

- 不新增抽象、不再造 bus / facade / 第三套生命周期。
- 不做"框架优化"立项：上面 5 条没有一条能靠新增抽象解决，全部靠删除。
- 不按行数机械拆文件；只在有明确接缝时拆。
- 不改行为、不改接口语义、不改资源时序。

## 2. 阶段

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| S1 | 删掉 `include/` 影子层：公开头唯一物理位置 | 已落地 |
| S2 | 修正聚合边界与依赖倒置 | 待做 |
| S3 | 削掉两个最大的文件（只做有明确接缝的） | 待做 |
| S4 | 命名收敛（在 S1/S2 之后） | 待做 |
| S5 | 计划目录收敛到 2–3 条活跃线，其余归档 | 待做 |

### S1：公开头唯一物理位置

目标形态（已写入 skill，属强制规则）：

- 需要公开的头放在 `<module>/include/<Module>/...`；公开路径 = 相对 include 根的路径。
- 模块根不再保留同名副本；`include/` 下不得出现转发 stub。
- 一个物理头只有一个公开路径（禁止别名）。
- 私有头留在模块根/子目录，紧挨使用它的 `.cpp`。
- 公开头只能 include 公开头；若公开头需要某个私有头，就把该头提升为公开，
  而不是把它的目录也加进 include 根。
- include 字符串一律写公开路径，不写 `../`、不写裸文件名跨目录。

验收：

1. `include/` 下 0 个转发 stub。
2. 0 个物理头有多个公开路径。
3. 全部公开头位于其公开路径。
4. `add_headerfiles` 无失效模式。
5. PCH 指向新物理位置。
6. `xmake b ya-engine` / `ya-testing` / `ya-render-3d-test` / `ya-game-editor` /
   `ya-game-runtime` / `GUIWorkbench` / `HelloMaterial` / `GreedySnake` / `ya-runtime` 通过。
7. 搬迁保留 blame（见 `progress.md` 的两次提交形态）。

## 3. 后续（S2 起，未开始）

S2 的候选清单（每条独立可验收，动手前先确认消费面）：

- `ya-engine` 不再公开包含 `ya-game-runtime`（应用形态不该被"引擎聚合"公开）。
- 拆 `ya-gui-framework`：它现在是 GUI + app shell + module system 的混合体，
  这正是"抽不出独立 GUI app"的根因。`Tooling/Workbench` 是 demo app，不属于 GUI 库。
- 断 `ya-scene-core -> ya-gui-widgets`（SceneWidgetEntry）与
  `ya-render-3d -> ya-gui-compose`（`Render2DComposePass` 出现在
  `RenderDeviceState.cpp` / `RenderFrameCoordinator.cpp` / `ViewCompose.cpp`）。
- 修 `ya-rhi-backend-common` 自依赖自身的笔误。

## 4. 退出条件

- 读一条主链路时不再出现"同一概念四层转译、每层各叫一遍"。
- 目录里不存在同一个东西的两个入口。
- 搜索真相时 `.agent/plan` 不再贡献一半命中。
