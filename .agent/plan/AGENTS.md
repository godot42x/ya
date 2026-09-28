# Plan Workspace

这里存放阶段性计划、进度、基线和评估工件。默认不要把 `plan/` 当成当前主规范入口。

## 边界

- `plan/` 记录的是某条重构线或阶段任务在当时的目标、决策、验证命令和进度。
- 它可以包含历史命令、历史结构名、兼容路径或当时有效但现在已过时的执行口径。
- 因此：`plan/` 不保证始终代表当前默认工作流。

## 何时读取

- 需要理解某条长线重构的上下文、阶段目标或已完成事项时
- 需要查某个专项任务当时的 baseline、对照命令或风险评估时
- 需要决定某条阶段性结论是否已经应该沉淀进 skill / memory 时

## 何时不要读取

- 只是要找当前构建、运行、测试或 package 命令时
- 只是要找当前默认架构语义或主入口规则时
- 只是因为“里面可能有用”就预读整个计划目录时

## 使用顺序

1. 先看仓库根 `AGENTS.md` 和相关 skill，确认当前默认路径。
2. 只有当任务明确关联某条重构线时，再进入对应 `plan/<topic>/`. 
3. 若 plan 中内容已经变成稳定规则，优先把它迁到 skill；若只是历史坑，优先迁到 memory。

## 长线计划最低工件要求

参考 @"./Effective harnesses for long-running agents.md"
以后任何正式的长期重构/架构计划，默认都不能只有一个 `plan.md`。至少需要：

- `plan.md`：目标、边界、phase、验收；
- `progress.md`：每轮完成内容、验证结果、剩余问题；
- `feature_matrix.json`：场景/能力/状态；
- `session_checklist.md`：每轮固定开工/收尾步骤。

若某条计划暂时缺这些工件，后续迭代时应优先补齐，而不是继续只在单个 plan 文件上堆文字。

## 当前活跃线

只有下面这些目录代表"现在还有代码要改"的线。不在列表里的都在 `./archive/`，
不需要读，也不要在那里继续追加进度。

| 目录 | 主题 |
| --- | --- |
| `render-view-family/` | view/frame 语义拆分、`PreparedView`、RenderRuntime 状态与逻辑分离（开放项最多） |
| `render-application-boundary/` | Framework/Render 只放可复用管线，本帧的排布与状态归应用（AB1/AB2 已落地） |
| `gui-framework-editor-readiness/` | retained GUI 承载完整编辑器的能力补齐，含 Phase 8 移除 ImGui 双栈 |
| `gui-framework-editor-runtime-refactor/` | 编辑器向 GUI framework 收敛的运行期契约 |
| `gui-kernel-ux-parity/` | 独立 GUI app 与引擎内 GUI 的 UX 对齐 |
| `gui-invalidation-architecture/` | invalidate / dirty / 增量绘制与动画集成 |
| `gui-style-system-convergence/` | style / theme 收敛 |
| `gui-anchor-to-slot/` | slot-first layout 迁移尾巴 |
| `gui-editor-structure/` | GameEditor 结构收敛 |
| `font-framework-convergence/` | 字体栈收敛 |
| `editor-undo-redo/` | 编辑器 undo/redo |
| `source-layout-subtraction/` | 目录/头文件布局减法（S1 已落地，S2–S5 进行中） |
| `editor-ui-grouping/` | GameEditor/UI 关切分组（G1 已落地） |
| `display-compose-encoding/` | display compose 只搬运不改色，且 surface 层不再持管线（F1–F3 已落地；仅剩 gamma 开关语义待定） |
| `game-ui-script-framework/` | 界面自带脚本（与宿主无关的 Lua 运行时 + GUI 不透明行为描述）、帧顺序/暂停/结构变更时机成为契约、条目模态与取消路由；验收用例 GreedSnake（F0 帧顺序契约已落地；下一步 S1） |
| `scene-2d-world-and-game-ui/` | 现有 Scene/Transform/Camera 上补 authored sprite，并与 UI Compose 分层形成 2D 游戏闭环（Render2DList 值化、screen/world draw contract、Render3D→GUI 解耦已落地；D1–D3 按坐标系拆 draw list / 删全局 Render2D / View overlay 归编辑器待做；P0 混合语义和 Scene graph integration 未完成） |

## 归档判据

一条线进 `./archive/` 的条件（满足任一即可，理由记在下表；不要在每个被归档的
目录里再写一份）：

1. **已完成**：checkpoint 全绿，且结论已沉淀进 skill（没有 skill 吸收的要先吸收再归档）。
2. **已交由别的线接手**：原计划的方向被另一条活跃线的章节覆盖，原目录只留历史。
3. **明确延后**：本轮该做的已落地，剩下的是被显式推迟的层，且不打算近期开工。

归档 ≠ 删除。历史命令、当时的 baseline、被推翻的方案都保留，方便查"当初为什么
不那样做"。但归档目录里的路径可能指向已经不存在的位置。

### 2026-09-18 归档

| 目录 | 理由 |
| --- | --- |
| `dockspace-node-tree/` | 已交由 `gui-editor-dock-layout` / `gui-editor-tab-lifecycle` 落地，两者均已收口（3） |
| `gui-editor-dock-layout/` | 2/2 完成（1） |
| `gui-editor-tab-lifecycle/` | 5/5 完成，结论已进 `skills/gui-framework`（1） |
| `gui-framework-architecture-hardening/` | 9/9 完成（1） |
| `gui-gallery-ux-pass/` | 18/18 完成（1） |
| `gui-perf-observability/` | 5/5 完成（1） |
| `gui-god-class-peel/` | 3/3 完成（1） |
| `gui-multi-os-window-editor/` | 50/50 完成，多窗口能力已进代码（1） |
| `model-instance-authoring/` | 自述"已实现（本轮）"（1） |
| `editor-camera-body/` | 代码已落地；仅剩人工目视确认，不构成待规划项（1） |
| `gui-animation/` | 框架层已落地并写进 skill；Game UI 轨道层显式延后，留在 `gui-invalidation-architecture/animation-integration.md`（1、3） |
| `gui-capability-gap/` | 方向被 `gui-framework-editor-readiness`（Phase 8 移除 ImGui 双栈）接手（2） |
| `render-pipeline-dedup-runtime-split/` | 被 `render-view-family` §4.0.2 明确接手（2） |

### 2026-09-23 归档

| 目录 | 理由 |
| --- | --- |
| `render-view-resource-ownership/` | 5/5 checkpoint 全绿，结论已沉淀进 `skills/render-arch` 第 18 条「View target 资源只有一套所有权」（1） |
