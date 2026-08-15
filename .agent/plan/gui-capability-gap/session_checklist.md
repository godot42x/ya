# GUI 数据驱动主线 Session Checklist

> 更新时间：2026-08-15
> 作用：`gui-capability-gap` 主线（响应式数据绑定 + 性能管线）每轮的固定开工/收尾步骤。

## 每轮开工前

1. 读 `plan.md` / `audit.md` / `feature_matrix.json`，确认当前里程碑与上一轮停点。
2. `git status --short`，确认工作区干净（不混入无关改动）。
3. 确认当前只推进一个里程碑（M0 → M1 → M2 → M3 顺序）；若上一刀未收口，先收口。
4. 跑最小基线：`xmake b ya-gui-closure-test` + `xmake r ya-gui-closure-test`，确认未坏。

## 每轮进行中

1. 严守「retain 是性能底座」：任何改动不得破坏 UIFrameSnapshot 的 immutable 契约（渲染层只消费 snapshot，不回读 live tree）。
2. 依赖收集必须每次完整重建（Vue 式），不得做增量依赖图——否则条件读取切换 ref 会漏标 dirty。
3. 每改一处数据，都能回答「触发了多少 rebuild、多少 draw item、耗时多少」——性能计数随 M0 起就带着走。
4. 不实现 immediate API（留口子即可）；不引入保留层缓存（那是独立优化）。
5. 若「局部复用」复杂度失控，回退「整帧 rebuild + 只留性能计数」，不强行 diff。

## 每轮收尾前

1. 结构证据：rebuild 计数 / draw item 计数 / 三段耗时 至少留一条。
2. 视觉证据：golden / GPU shot 与改造前零差异（尤其 scroll/split 页）。
3. 更新 `feature_matrix.json`（里程碑状态）+ 本轮进度记录。
4. 工作区保持可接力，不留半套新旧模型并存。

## 当前下一刀

M3 — 性能管线闭环 + 声明式口子内嵌：① **复用 `Core/Profiling` 的 `PerfState`**（方法版 API 始终编译），把 M0 自造的 `std::chrono` 计时替换为 `metrics().beginSample/endSample/setValue`，`GuiPerfStats` 降级为「从 PerfState 读的 per-tree 轻量访问面」，key 用 `gui.tree.*` 命名空间 + domainKey 区分多树；② 性能计数可 scenario 断言；③ 声明式 diff 复用接口内嵌（为 immediate API 留口子）。验收：能用 dump/scenario 回答「改一处数据触发多少 rebuild/draw item/耗时」；`GUIWorkbench` 闭包不被回灌。
