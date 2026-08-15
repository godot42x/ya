# GUI 失效与增量更新进度

> 建立日期：2026-08-15  
> 作用：记录已发生事实、验证结果和下一轮接力点；未实施的设计不在这里冒充完成。

## 2026-08-15 — 建立审计与主计划

### 本轮完成

- 基于当前 Reactive、per-widget draw-item cache、Workbench presenter 和特殊 paint override 完成架构审计；
- 明确当前不存在 ancestor dirty propagation，也不存在可跳过 subtree traversal 的 boundary；
- 将计划调整为 correctness-first：Phase 0/1A/1B/2 必做，batching/subtree cache/reconciliation 条件启动；
- 识别两个额外正确性缺口：
  - dirty level 当前错误地挂在 reactive value 上，而非 dependency/property edge；
  - draw-item cache 未覆盖 build context 与 inherited paint context validity。

### 当前结论

- 第一刀不是 boundary 或 Virtual DOM，而是 invalidation reason、dependency edge 和 cache validity；
- `UISplitPane::bindSplitRatio()` 的 persistent binding 生命周期必须在统一 paint pipeline 前修正；
- public field + setter 只能作为迁移态。

### 验证

- 文档级 review；
- 本轮未修改 `Engine/Source`，未运行构建。

## 2026-08-15 — 补齐长期计划执行工件

### 本轮完成

- 新增 `framework-lessons.md`，把 WPF、Flutter、Vue、Qt、Slate、React、Godot、Dear ImGui 的可吸收长处映射到 YA 阶段；
- 新增 `todo.md`，将 Phase 0~5 展开为 `GI-*` 任务、依赖、验收和提交边界；
- 新增 `process.md`，定义 correctness-first 实施顺序、验证层级和 Phase 决策门；
- 新增 `feature_matrix.json` 与 `session_checklist.md`；
- 更新 `audit.md`：补 WPF property metadata，并链接跨框架吸收表；
- 更新 `plan.md`：登记配套工件与跨框架能力吸收边界。

### 当前结论

- 当前可直接领取的第一项为 `GI-001 invalidation metrics + reason trace`；
- 在 GI-001~GI-004 基线完成前，不开始 batching、boundary 或 reconciliation。

### 下一轮直接接力点

1. 将 `GI-001` 标记为进行中；
2. 定位现有 WidgetTree dump/perf metrics 输出点；
3. 先补 reason/transition 测试，再接 profiling metrics。

### 验证

- 文档与 JSON 静态一致性检查；
- 本轮仍未修改行为代码。

## 2026-08-15 — GI-001 完成：invalidation reason + transition 计数

### 本轮完成

- 定义 `EUIInvalidationReason`（None / PaintProperty / LayoutProperty / ReactivePaint /
  ReactiveLayout / ChildStructure / GeometryChanged / BuildContextChanged /
  InheritedPaintContext / Volatile），enum 无字符串分配；
- `UIElement::markPaintDirty/markLayoutDirty` 增加 reason 参数，仅在 0->1 dirty
  transition 记录 `_lastInvalidationReason` 并累计 tree 级 transition 计数；
- `GuiPerfStats` 扩展 `paintDirtyTransitions`/`layoutDirtyTransitions`/
  `cacheInvalidations`；`WidgetTree` 新增累计成员与 `getLastInvalidationReason()`；
- `ReactiveBase::notifyDependents` 传入 ReactivePaint/ReactiveLayout reason，并
  累计 `ReactiveDiagnostics{notifyCalls, dependentVisits}`（process-wide，经
  `getReactiveDiagnostics()` 读取）；
- 打点：VisualFlag->PaintProperty、setLayoutRect->GeometryChanged、
  notifyDependents->ReactivePaint/ReactiveLayout；Phase 1A/2 的
  PaintProperty/LayoutProperty/BuildContextChanged/InheritedPaintContext 枚举保留待打点。

### 代码/行为结论

- `UISplitPane::paint()` 覆盖完整 paint 流程、不清除 `_bPaintDirty`（已确认，是
  plan.md 1.2 记录的已知问题）。测试 2 改用 `ReactiveListProbeWidget` +
  `setDirtyLevel(Layout)` 验证 Layout reason；split 的 paint 统一属于 Phase 2
  (GI-302)，不在本诊断基线范围内。

### 验证

- `xmake b ya-gui-closure-test` 通过；
- `xmake r ya-gui-closure-test` 131 tests PASSED（新增 3 个：
  ReactivePaintMutationRecordsReasonAndTransition、
  ReactiveLayoutMutationRecordsReasonAndTransition、
  SameValueReactiveSetSkipsInvalidation）。

### 下一接力点

- `GI-002`：build-context/resource-generation cache validity 测试 seam + 保守 cache reset。

## 2026-08-15 — GI-002 完成：build-context cache validity baseline

### 本轮完成

- `UIFrameBuildContext` 新增 `uint64_t generation`（host 提供单调 token，用于 uiScale/offset
  之外无法自行比较的坐标映射/resource resolver 变化，如 viewport resize / DPI / asset reload）；
- `WidgetTree` 新增 `_bHasBuildContext`/`_lastGeneration`/`_lastUiScale`/`_lastOffset`，
  在 `buildSnapshot` 开头比较上下文；变化时清空两份 `_itemCache`、
  `++_cacheInvalidations`、并记录 `BuildContextChanged` reason（保守、correctness-first）；
- 首次 build 无前序上下文，不触发失效。

### 代码/行为结论

- 按 plan.md 3.5 决策落地：uiScale/offset 由 WidgetTree 直接比较（数值），
  resolver 不做 `std::function` 比较，由 host 经 `generation` 显式声明失效；
- 这是 Phase 2「unified paint template」之前就需要的正确性基线：否则 clean tree
  在窗口 resize / DPI 切换 / 纹理重载后会复用不兼容的 target-pixel 段。

### 验证

- `xmake b ya-gui-closure-test` 通过（13.6s）；
- `xmake r ya-gui-closure-test` 134 tests PASSED（新增 3 个：
  CleanTreeOffsetChangeRebuildsResolvedItems、
  CleanTreeUiScaleChangeRebuildsResolvedItems、
  CleanTreeGenerationChangeDropsCache）。

### 下一接力点

- `GI-003`：paint-collected / persistent reactive edge 生命周期回归测试。
