# GUI Framework 架构加固 Session Checklist

> 更新时间：2026-09-08
> 作用：每轮推进 `.agent/plan/gui-framework-architecture-hardening` 时的固定开工、实施和收尾步骤。

## 开工前

1. 按顺序读取：
   - 仓库根 `AGENTS.md`；
   - `.agent/skills/gui-framework/SKILL.md`；
   - 涉及 Render2D/RHI 时再读 `.agent/skills/render-arch/SKILL.md`；
   - 本目录 `plan.md`、`todo.md`、`progress.md`、`feature_matrix.json`。
2. 运行 `git status --short` 和目标文件 `git diff`，确认哪些改动已属于其他任务。
3. 确认 `todo.md` 同时最多一个 `[-]`，并同步 `feature_matrix.json.active_task`。
4. 复述本轮单一目标、前置依赖、非目标和可回退边界。
5. 基线任务先写测试/观测；实现任务必须引用已经记录的基线。
6. 按任务选择最小验证入口，不默认跑所有场景。

## 实施中

### 通用

1. 不修改 immutable snapshot -> command recording 的依赖方向。
2. 不让 Widgets include `AssetManager`、Scene/ECS、Editor 或 product host。
3. 不在 paint/layout/command recording 中创建 GPU 资源。
4. 不按材质或 clip 对 draw item 重排；painter order 是半透明 UI 的正确性契约。
5. 不借优化拆 `UIElement`、`WidgetTree`、`GUIAppHost` 或 Editor 主时序。
6. 新增指标必须低成本；release 热路径不引入字符串日志或 per-item heap allocation。

### C1：clip batching

1. 对比 clip 的完整语义：clipped flag、pos、extent；不要用裸内存比较。
2. clip 状态变化前 flush 当前 pending batch；相同状态不得重复 push/pop。
3. 覆盖 clipped/unclipped、A/B、nested flattened clip、空 extent。
4. 不更改 snapshot schema，除非 C1 的简单状态机被证据证明不可行；发生时先停下更新计划。

### C2：layout skip

1. 每个 skip proof 同时检查 assigned rect、layout revision 与 dirty mask。
2. specialized host 的 arrange 前置状态如果改变，必须明确 bump revision。
3. 覆盖 desired size、slot、visibility、structure、scroll/split/popup/dock 状态。
4. 不把 arrange skip 偷换成完整 measure cache。

### C3：texture thread

1. completion 不得从 foreign thread 直接修改 catalog、Reactive dependent 或 item cache。
2. debug 诊断必须发生在数据竞争前。
3. 当前阶段 adapter dispatch，WidgetTree 不承担通用任务队列。
4. 保持 path-keyed invalidation，不退回全树 resource generation。

## 收尾前

1. 运行目标任务定向测试。
2. 至少运行：

```bash
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='<本轮相关 suites>'
```

3. 按影响补跑：
   - `xmake b ya-gui-host`；
   - `xmake b ya-game-runtime`；
   - `xmake b ya-game-editor`；
   - `python3 Script/automation/gui/run_workbench_gpu_parity.py`。
4. 运行 `git diff --check`。
5. 用 JSON parser 验证 `feature_matrix.json`。
6. 对照 `git diff --stat` 和逐文件 diff，排除共享工作区的无关改动。
7. 同步更新：
   - `todo.md`；
   - `progress.md`；
   - `feature_matrix.json`；
   - 仅在设计变化时更新 `plan.md`。
8. checkpoint 未闭环时不得提交；闭环后代码、测试和计划状态同一提交。

## 当前下一刀

`GAH-201`：

1. 让 specialized layout host 复用统一 assignment/skip 入口，不再各自绕过 `tryReuseAssignedLayout()`；
2. clean assigned rect + clean revision 跳过 arrange；局部 dirty 不重排无关 sibling；
3. 不引入 measure cache，不移动控件目录。
