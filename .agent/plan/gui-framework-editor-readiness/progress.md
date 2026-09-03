# Progress

## 当前状态

- 计划建立：2026-09-03
- 实现改动：Phase 2D 已完成一轮闭环
- 提交：待本轮 checkpoint 创建
- 当前阶段：Phase 2D（layout proof generation）

## 已确认

- Slot-first layout 已完成主迁移。
- WidgetTree retained 基础、snapshot、focus、popup、drag/drop 已可运行。
- GUIWorkbench 和 EditorSurface 已提供真实 consumer。
- 完整 GameEditor 仍未完成，主要缺口是 cache correctness、增量布局、Reactive 数据契约、editor data/undo/selection/commands、retained editor primitives 和 ImGui 清理。

## 本轮已做

- 为 UIElement 增加 subtree layout dirty mask，区分 measure / arrange。
- child measure dirty 沿祖先传播；slot arrange dirty 绑定到 owning host。
- 新增 child measure 传播与 slot arrange 归属测试。
- 修正 detach/reattach 测试的 rebuild 统计口径。
- xmake r ya-gui-closure-test 全量通过（328/328）。
- 未修改既有 `.gitignore`。

## Phase 0 已完成

- 已建立本计划的目标、硬边界、阶段路线、验收标准和能力矩阵。
- 已将 GUI skill 中过时的 path-B、child authored geometry 和 GUIAppHost compatibility alias 语义改为当前最终契约。
- 未引入任何 legacy 兼容内容。

## Phase 1 当前 checkpoint

- 已为每个 `UIElement` 增加单调递增 runtime id，paint cache 不再以裸地址作为身份。
- `WidgetTree::detach` 会清理被移除 subtree 在双缓冲 cache 中的绘制段。
- `UIFrameDrawItem` 增加完整字段比较，G2 校验覆盖 texture、UV、font、corner radius、text scale 等字段。
- 新增 detach/reattach paint-cache 回归测试。
- 新增 cross-tree reparent cache 回归测试，确认旧 tree 不保留 widget、目标 tree 首次构建重新绘制。
- 新增 destroy/reallocate runtime identity 测试，以及 drag ghost cancel 后 snapshot 清理测试。
- 明确 snapshot build mutation policy：业务 paint/layout 回调禁止结构变更，框架 tooltip/drag 维护仅在显式边界执行。
- `xmake r ya-gui-closure-test` 全量通过（325/325）。

## Phase 1 已完成

- paint cache identity、detach 清理、cross-tree 隔离、对象重分配和 drag ghost teardown 已形成完整回归。
- snapshot/full repaint 校验已覆盖当前 `UIFrameDrawItem` 的全部渲染字段。
- 当前未发现需要额外引入新的 cache manager、compat 层或 legacy 双写。

## Phase 2A 当前 checkpoint

- `WidgetTree::invalidateLayout` 现在接受 `Arrange / Measure / Structure` scope。
- Slot 的 arrange/measure setter 已分别标记对应 scope，不再全部降级为同一种 layout dirty。
- Tree 内部使用累计计数，`GuiPerfStats` 在每次 snapshot 中提供累计 scope 统计。
- `WidgetTree` 文档明确 snapshot build 期业务结构变更策略。
- 新增 `LayoutInvalidationScopesAccumulateAcrossSnapshots` 回归测试。
- 完整 GUI closure 通过：326/326。

## Phase 2A 已完成

- 完成 dirty taxonomy 的第一条可运行契约。
- 尚未实现真正的 subtree layout skip、measure cache、arrange cache；这些保留给后续 checkpoint。

## Phase 2B 当前 checkpoint

- child measure dirty 会沿布局祖先传播，确保父布局能够感知子内容尺寸变化。
- arrange dirty 绑定到 slot owning host，避免把几何变更误判为更大范围结构脏。
- 统计口径仍是 tree-level；本阶段只验证传播与分类，没有宣称局部跳过已经完成。

## Phase 2C 当前 checkpoint

- UIElement::layoutAssigned() 增加 assigned-rect proof：节点 rect 未变、节点及其祖先无 layout dirty 时，跳过该节点的 layout/children walk。
- tree-level layout invalidation 会确保 root 重新进入一次布局，clean sibling subtree 仍可局部跳过。
- attach/reparent 会清除旧 assigned-layout proof，并标记新 parent 的结构布局 dirty，避免跨 host 的 slot intent 或旧布局证明泄漏。
- UILayout 的 measure/arrange 参数变化现在标记 owning host 的节点 dirty，而不是只标记 tree bool。
- GuiPerfStats::layoutSkippedWidgets 提供累计局部跳过计数。
- 新增 Canvas sibling skip 与跨 host reparent 回归；修正 direct construct container 布局回归。
- targeted 与全量 xmake r ya-gui-closure-test 均通过（329/329）。

### Phase 2C 保留项

- 当前只实现 assigned-rect subtree skip，尚未引入独立 measure cache / arrange cache；后续必须以 generation/依赖契约补齐，不能把 bool proof 扩展成隐式全局缓存。

## Phase 2D 当前 checkpoint

- 将 assigned-layout proof 从 bool 升级为单调 layoutRevision / assignedLayoutRevision，任何 layout/arrange invalidation 都会使旧 proof 失效。
- attach/reparent 会推进 revision 并清空 assigned revision；同一 rect 也必须重新消费新的 parent-owned slot/layout contract。
- layout host 的 measure/arrange 参数变化改为标记 owning host 的节点 dirty，revision 与 dirty mask 一起形成可解释的失效边界。
- 针对跨 host reparent、同 rect 重新布局、Canvas sibling skip 和 direct-construct box layout 的 targeted + 全量 closure 回归均通过（329/329）。

### Phase 2D 未完成

- 尚未实现独立 measure cache / arrange cache，也未宣称当前 skip 已覆盖所有 specialized layout；下一阶段先补可观测计数与 cache dependency contract。

## 下一 checkpoint

下一 checkpoint：Phase 2E，建立可观测的 measure/arrange 计数与 cache dependency contract，再扩展到 Box/Overlay/Split/Scroll；不改变本轮已验证的 parent-owned slot 语义。
