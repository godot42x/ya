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

**P0 收口 + 布局坏掉修复**：响应式绑定 + Styles/StyleSet + TreeView 三块砖已全部落地（closure 126/126）。下一刀是修 GUIWorkBench 布局坏掉的回归（根因见 memory `project_gui_layout_paint_dirty.md`）：`WidgetTree::layout()` 全量重排更新 `_layoutRect` 但不标 paint dirty，增量复用复用了旧像素坐标的 draw items。修复方向：`UIElement::layout()/layoutAssigned()` 对比新旧 rect，变了就 `markPaintDirty()`（注意 `UISplitPane::layoutAssigned` 重写了基类需同样处理）。

注意（样式依赖收集的坑，已解决）：paint 属性的绑定必须走「paint 时 get() 收集」，不能走 bind 时显式注册——后者会被基类 paint 重跑时的 `clearDependencies()` 清掉。layout 属性（如 SplitPane ratio，layout 阶段读、无 paint 上下文）才用显式注册。
