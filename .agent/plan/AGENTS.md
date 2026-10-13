# Plan Workspace

这里存放阶段性计划、进度、基线和评估工件。默认不要把 `plan/` 当成当前主规范入口。


## 门禁
在完成计划文件编写及执行之前:
除非你有 95% 以上的信心和把握已经理解了目标，就应该向用户询问方案偏差与改动方向，以问题的形式, 不断迭代出最终的计划.

```cpp
float confidence = ratio_that_you_sure_understand_and_could_finish_the_target(this_plan);
while(confidence < 0.95){
    ask_user_some_questions_about_target_and_update(this_plan);
    confidence = ratio_that_you_sure_understand_and_could_finish_the_target(this_plan);
}
completion_and_make_the_final_plan(this_plan);
```

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
- `todo.md`: 给人一眼看清的进度

若某条计划暂时缺这些工件，后续迭代时应优先补齐，而不是继续只在单个 plan 文件上堆文字。

## 当前活跃线

只有下面这些目录代表"现在还有代码要改"的线。不在列表里的都在 `./archive/`，
不需要读，也不要在那里继续追加进度。

| 目录 | 主题 |
| --- | --- |
| `showcase-roadmap/` | 项目级伞形计划（2026-10-07）：渲染半成品与缺口收口、测量与压测（显存与进程内存分列）、2D / 3D 两个可玩 demo、音频、GUI 活跃线排序、架构文档与吸收；只定优先级（P0–P2）与依赖，细节在各子计划 / 已有线。2026-10-11 方向审查后不直接改代码；纯 GUI app 线已迁 `gui-app/` |
| `gui-app/` | 纯 GUI app 线（2026-10-11 自 showcase-roadmap §3 GA 迁出）：gui profile + GUIWorkbench / GUIFrameworkSmoke 独立产品线。GA1 已落地（`2ee635a1`）；GA2 门禁已收口（GA2r 场景按控件定位）；GA3 已收口（与游戏运行时共用 `BenchmarkSession`，profile Dock 数字在 `gui-app/progress.md`）；gui profile 构建只在独立 worktree 做 |
| `perf-baseline/` | 测量、压测与内存统计（`showcase-roadmap` M 组）：基准模式、`bench_summary.json`、pass 级 GPU 计时、GPU 显存与进程内存分列、压测场景、前后对比报告 |
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
| `game-ui-script-framework/` | 界面自带脚本（与宿主无关的 Lua 运行时 + GUI 不透明行为描述）、帧顺序/暂停/结构变更时机成为契约、条目模态与取消路由；验收用例 GreedSnake（S7 已落地，H9 仍 blocked_on_D7） |
| `ui-behavior-capabilities/` | `UIBehavior` 按能力登记、树 tick 注册表、Lua `call()` 热路径（C1–C3 完成；归档等结论进 skill） |
| `reflection-runtime/` | 反射层与脚本层整体规范化（2026-10-08 立项，P1）。稳定规则已进 `skills/reflection`。RR0–RR9 除 RR4 外已落地；RR4 等 GUI 10F-1~5，RR5b / RR8 的 Windows 验证待跑。吸收 `script-native-registry/`（S5.1 已入库，余下并入 RR8，原目录暂停） |
| `shadow-and-light-line/` | 点光源/聚光灯/阴影（R2/R3/R15）：R15a deferred 点光 SSBO 无上限已落地；R15b 聚光灯、R2 阴影 GPU 剔除、R3 CSM 待做；后续演进 UE 逐灯 draw / Forward+ cluster |
| `rdg-cache-dx/` | P0 skinning、P1 registry 诊断、P2 import 按 owner key 匹配保留历史；2026-10-12 P3 feature 切片 / P4 容器拆分转交 `render-pipeline-composition`，不再重复开工；本线剩余 P5 Slang 生成 descriptor layout |
| `render-pipeline-composition/` | RDG 上方组合层收敛（2026-10-12 立项）：一等 feature、typed Forward/Deferred recipe、单源资源要求与显式 prepare→record 数据；接手 rdg-cache-dx P3/P4 及旧 orchestrator/passes/resources 的结构替换。计划已落盘，代码未开始；下一 C1 为真实 SSAO→Lighting 闭环，不是 Bloom-only 或空抽象骨架 |
| `rpg-prototype/` | 2D 世界线（2026-10-02 合并 `scene-2d-world-and-game-ui`）。**2026-10-13 改版为「RPG Tactics」**：`Example/2DRpgPrototype` 主流程从「城镇跑腿」重编为 **2D 战旗 RPG 垂直切片**（格棋盘 + 回合制 + 任务 + 成长养成：等级/装备/技能/队伍），并以此驱动四个新子系统——**后处理运行时控制、音频（SDL3）、粒子、2D 物理/触发器（Box2D）**。验收重点 = 能力覆盖（`feature_matrix.json`）。v1（RPG Maker 式俯视小镇）B1/R0–R4 已落地、工件存 `*-v1-landed.*`，其引擎能力（tilemap/精灵/动画/相机/脚本层/存档/场景转移）被本版复用。新计划工件见目录内 `plan.md` / `todo.md` / `progress.md`；代码未开工，下一步 Wave A F1 |
| `3d-demo/` | 3D demo 线（2026-10-12 自 `showcase-roadmap` §3 G3 立项，G3.0 问答已收口）：第三人称探索解谜（庭院 → 地下室）驱动引擎玩法层——物理组件（Kinematic / 传感器 / glTF 网格与 convex 碰撞 / 接触事件进 ECS）、物理 Lua API、`JPH::CharacterVirtual` 角色控制器、glTF skin / animation / 节点层级导入、动画交叉淡入、第三人称弹簧臂相机与鼠标轴、`world.spawn` 与世界计时器；三区光照依赖 `shadow-and-light-line/`（R15b + 新增聚光灯阴影 + R2 / R3）。工作项、波次、冲突面、素材调研见目录内 `plan.md`；待用户确认：示例目录名、素材选型 |
| `agent-files-maintenance/` | `.agent/` 自身治理（2026-10-13 立项，**当日 A1–A4 全部落地**：`5187d22c` 结构+守卫、`f2684e06` 拆分）：拆 `skills/gui-framework/SKILL.md`（861 → 158 行 + 7 refs；`GameEditor chrome` 166 行属 Application 层、已迁出为 `skills/game-editor/`）、`ya-build` / `render-arch` 同拆、三张路由表合一（`skills/AGENTS.md` 为唯一真源）、索引补全与相对链接层深修复（基线 13 条坏引用 → 0）、新增 `Script/ya_agent_docs_lint.py`（已进 `ci.sh`，当前 0 violation / 0 debt）。边界：只测路径与结构、不重写概念内容；不治理 `plan/archive/`。**无剩余工作** |

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

### 2026-10-02 归档

| 目录 | 理由 |
| --- | --- |
| `resource-handle-events/` | H1–H5 全绿。槽状态、编辑漏斗、三类事件、审计只按间隔且只断言、环境光照选中来源契约已沉淀进 `skills/resource-system`（1） |
| `scene-2d-world-and-game-ui/` | 剩余 P0 契约、混合场景视觉契约、Render2D 上传/提交生命周期、旧管线删除并入 `rpg-prototype` 评审步骤 3 / 5，`P0-contract-matrix.md` 移到该目录；其余条目已 verified（2） |
