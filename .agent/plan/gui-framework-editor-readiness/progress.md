# Progress

## 当前状态

- 计划建立：2026-09-03
- 实现改动：Phase 4B visible-range contract 已完成闭环
- 提交：本 checkpoint 随代码一并提交
- 当前阶段：Phase 4B（keyed visible window + Content Browser entry virtualization）

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

## Phase 3A 当前 checkpoint

- 新增 ReactiveTransaction，嵌套作用域只在最外层退出时 flush pending refs。
- 同一事务内对同一 Reactive 的多次写入只遍历一次依赖；same-value 写入仍被抑制。
- 通知时先复制 dependent 列表，允许回调期间 bind/unbind/detach，不依赖正在修改的 vector。
- ReactiveBase 销毁会从 pending 队列移除自身，避免事务退出时访问已销毁 signal。
- 新增 transaction coalescing 回归；全量 closure 通过（330/330）。

### Phase 3A 边界

- transaction 是同步 UI-side contract，不提供跨线程投递；UI-thread 约束和跨线程诊断仍待后续 checkpoint 明确。

## Phase 3B 当前 checkpoint

- ReactiveList 增加 revision、keyed replaceKeyed() 与结构 diff 观测面（inserted/removed/moved）。
- keyed replace 会拒绝空 key / 重复 key，避免 list identity 模糊。
- push/removeAt/clear/replace/replaceKeyed 现在都会推进 list revision，并保留最近一次结构 diff。
- UITreeView 开始消费 keyed identity：根数据 revision 变化时会按 live node ids 清理 _expanded 中已移除节点的 transient state，而不是让旧状态泄漏到重新出现的同名节点。
- 新增 keyed ReactiveList diff/revision 回归，以及 TreeView keyed replace 清理 removed expansion-state 的真实 consumer 回归。
- 全量 xmake r ya-gui-closure-test 通过（332/332）。

### Phase 3B 边界

- 当前是 keyed identity/diff baseline，还不是完整 keyed list reconciler：尚未提供细粒度 update API、row instance 复用协议或 UI-thread 诊断。

## Phase 3C 当前 checkpoint

- Computed<T> 不再每次 get() 都重算；现在是 dirty-on-upstream-change、read-time lazy recompute。
- selector 求值期间会收集上游 Reactive / ReactiveList / Computed 依赖，并建立 computed-to-computed / source-to-computed 传播链。
- 上游变化会把下游 computed 标 dirty，并继续通知其下游 widgets / computeds，形成真正的 dependency graph，而不是“每次读取重跑 selector”。
- 新增 recompute/cycle diagnostics，循环 selector 会输出错误并保留最近一次稳定 cache，而不是无限递归。
- 新增 lazy cache、多层 computed 脏传播、cycle 检测回归；全量 xmake r ya-gui-closure-test 通过（335/335）。

### Phase 3C 边界

- 仍未冻结跨线程 mutation 与更严格的 reentrancy contract；ReactiveList 也还没有细粒度 update/move API。

## Phase 3D 当前 checkpoint

- ReactiveBase 析构时会反向通知 computed dependents 执行 unregisterUpstream(this)，不再把已析构 upstream 留在 downstream computed 的 _upstreams 里。
- Computed 保持双向解绑：自身析构会从 upstream 的 computed-dependent 列表中移除，上游析构也会把自己从 downstream 的 upstream 列表移除。
- 新增 upstream 先析构的回归，验证 computed upstream unlink diagnostics 触发且析构顺序安全。
- 全量 xmake r ya-gui-closure-test 通过（336/336）。

### Phase 3D 边界

- 这次解决的是 detach/unbind/lifetime safety，不包含跨线程 mutation 约束；UI-thread/reentrancy contract 仍待单独冻结。

## Phase 3E 当前 checkpoint

- Reactive / ReactiveList 的所有 mutation 入口现在都经过统一的 UI-thread 校验；首个 mutation 线程会被冻结为 mutation owner，其他线程写入会被拒绝并记入 diagnostics。
- notifyDependents() 期间再次触发的 mutation 不再递归立即重入通知；会被加入 pending queue，等最外层 notification/transaction 结束后再 flush。
- 新增 foreign-thread mutation rejected 回归，以及 reentrant mutation deferred until outer notify completes 回归。
- 全量 xmake r ya-gui-closure-test 通过（338/338）。

### Phase 3E 边界

- 这次冻结的是 mutation 线程与重入通知语义；尚未扩展到 keyed list 的细粒度 insert/update/move API，也没有跨线程投递队列。

## Phase 3F 当前 checkpoint

- ReactiveList 增加 editor 可复用的 insertAt、updateAt、move mutation API，并为 push/removeAt 增加明确的成功/失败返回语义。
- keyed list 下插入/追加会拒绝空 key 与重复 key；updateAt 禁止改变既有 identity key；越界操作不会修改数据。
- replaceKeyed 支持可选 value comparator；未提供 comparator 时对 retained key 保守报告 updated，避免相同 key 的内容变更静默丢通知。
- clear 现在会完整报告 removed indices，Tree/Table consumer 可以据此清理对应行状态。
- mutation 都推进 list revision 并更新最近一次 diff；现有 ReactiveTransaction 仍可对多次 mutation 做批量通知合并。
- 新增 keyed mutation identity、边界拒绝、update/move/clear diff 回归；全量 xmake r ya-gui-closure-test 通过（339/339）。

### Phase 3F 边界

- 当前已具备可复用 mutation/update contract，但还没有把 TableGrid 改造成 keyed row virtualization，也没有通用 row-instance reconciler；这些属于 Phase 4/7 的动态结构与 editor primitive 工作。

Phase 4A 已完成；以下记录本 checkpoint 的闭环与边界。

## Phase 4A 当前 checkpoint

- 新增纯 authoring-time ui::fragment(...) 与可读别名 ui::group(...)；它们只在父 builder 上展开，不创建运行时 widget，不引入 Description/Reconciler。
- 新增构造期条件组合 ui::when(...)、ui::unless(...)、ui::ifElse(...)。条件只决定本次 live construct 是否添加 child；运行时动态结构仍必须由专用控件和 ReactiveList 管理。
- fragment/conditional 通过父 builder 的既有 child(...) 重载展开，因此每个 child edge 继续使用 parent-specific typed SlotArgs；错误的 canvas/box slot 在编译期被拒绝。
- 新增 editor-like 组合回归，验证 fragment flatten、条件分支和匿名节点行为；新增静态 typed-slot acceptance/rejection 断言。
- xmake b ya-gui-closure-test && xmake r ya-gui-closure-test：340/340 通过。

### Phase 4A 边界

- 尚未实现运行时 retained switcher，也没有把 when 误用成每帧重建机制；动态集合继续留在列表控件内部。
- 尚未补通用 withSlot(...) fragment item 语法；当前已有的 .child(node, typedSlot) 保持唯一布局入口，避免 DSL 再造 slot 层。

下一 checkpoint：Phase 4B，基于真实 editor-like 页面审计动态结构边界，先完成 keyed row-instance reconciler / virtualization 的最小闭环，再扩展 switcher 或页面级动态组合。

## Phase 4A follow-up checkpoint

- DSL builder 现在拒绝同一父节点下重复的显式 sibling key；匿名节点仍允许并保持无稳定 identity。
- 拒绝发生在 child edge 加入前，避免重复 key 进入 live WidgetTree；日志提供 parent/key 诊断。
- 新增重复 key 回归测试；closure 全量通过：341/341。

下一 checkpoint仍为 Phase 4B：keyed row-instance reconciler / virtualization 最小闭环。

## Phase 4B 当前 checkpoint

- UITableGrid 的 selection 在 keyed ReactiveList<FTableRow> 上改为 identity-backed：控件记录选中行 key，列表发生 insert/move/remove 后按 key 重映射 index。
- 外部直接写入 Reactive<int> 仍被视为新的 selection intent；只有列表 revision 变化时才执行旧 key 的位置恢复，避免把旧选中对象错误替换成新位置对象。
- 被删除的选中 key 会明确落为 -1，同时清除 hover 行 transient state；非 keyed 数据源保持原有 index selection 语义。
- 新增跨 insert/move/remove 的真实 binding 回归；closure 全量通过：342/342。

### Phase 4B 边界

- 当前完成的是 keyed row selection/lifecycle 基础契约，不是 row widget instance reconciler，也不是 virtualization；TableGrid 仍按数据绘制行文本。
- 下一步仍需先定义可复用 row factory、visible range 与 transient state ownership，再引入真正的 row instance 复用。

## Phase 4B identity migration checkpoint

- UITableGrid 的公开 selection API 已从 Reactive<int> row index 全量迁移为 Reactive<std::string> row id；旧 index 绑定路径已删除，不保留兼容双写。
- keyed list 发生 insert/move/remove 时，selection 直接保持 row id；被删除的 id 归为空字符串。
- GUIWorkbench table demo 与 BindingContractTest 已迁移到 keyed selection；回归覆盖点击、外部 selection 修改及结构变更。
- closure targeted table tests 通过；提交前需完成全量 closure。

### Identity migration 边界

- TableGrid 仍是 flat paint consumer，尚未引入可复用的 row widget instance、visible-range virtualization 或 row factory。
- 下一 checkpoint 需先定义 row instance ownership / factory / slot lifecycle，再实现真正的 keyed row reconciler；不得恢复 index selection 兼容层。

## Phase 4B row-instance checkpoint

- 新增 UIKeyedChildReconciler，以 parent-owned child edge 为生命周期边界：按非空唯一 key 创建、复用、移除和重排 live UIElement。
- 同 key 的 widget 实例在 reconcile 后保持原指针与 transient state；消失 key 通过 WidgetTree::detach 清理 tree membership、focus/capture/paint cache；顺序调整使用同 parent reparent，保留原 Slot。
- 新增 factory failure、duplicate/empty key rejection 的统一诊断入口，并要求 reconciler 运行在已挂载 parent 上，避免 detached subtree 的隐式生命周期。
- 新增真实 closure 回归，验证三轮 reconcile 的创建次数、实例复用、移除和顺序；targeted 与全量 closure 均通过：342/342。

### Row-instance checkpoint 边界

- 这是通用 keyed child lifecycle 基础设施，还没有把 TreeView/TableGrid 的 flat paint 改造成 row widget factory，也没有 visible-range virtualization。
- 下一步将把该 reconciler 接入一个真正的 editor-like list consumer；在接入前先定义 row slot initializer、可见范围和滚动 offset 的契约。

## Phase 4B Content Browser consumer checkpoint

- EditorSurface 的 Content Browser mount list 与 entry list 已改用 UIKeyedChildReconciler；目录刷新不再先 detach 全部 rows 再重建。
- 同名 mount/entry 在刷新时复用原 UISelectableRow 及其 label child，只更新 item id、文案、选中态和 activate callback；消失条目通过 reconciler detach，顺序变化保留原 BoxSlot。
- UIKeyedChildReconciler 增加 optional updater：create/reuse 之后对当前 key 顺序调用，供 consumer 刷新文案和回调而不重建实例。
- Content Browser 的两类列表仍由 FileExplorer fingerprint 驱动，未引入每帧重建；Scene Save dialog 暂不迁移，避免把两个 consumer 混入一个 checkpoint。
- 修正 EditorInspectorTab 中 canvas parent 错用 overlaySlot 的真实 typed DSL 错误。
- 新增 updater 复用回归；xmake r ya-gui-closure-test 全量通过（344/344）；ya-game-editor 构建通过。

### Consumer checkpoint 边界

- 该 consumer 已具备 keyed child instance 复用，但滚动容器仍由 UIScrollViewport 管理，尚未实现 visible-range virtualization；当前所有目录 rows 仍保留在 live tree。
- 下一步迁移 Scene Save rows，并随后设计真正的 row factory + visible range/scroll offset contract。

## Phase 4B Scene Save consumer checkpoint

- EditorSurface 的 Scene Save mount list 与 entry list 已改用同一套 UIKeyedChildReconciler + updateContentRow 协议；目录刷新和选中态变化不再先 detach 全部 rows 再重建。
- 同 key 的 Scene Save row 在选中目录变化时复用原 UISelectableRow，只更新 selected / callback；关闭对话框会 reset reconciler，避免持有已销毁 parent。
- Content Browser 与 Scene Save 共用 row factory 与 preferred-height slot 写入，不引入第二套 list lifecycle。
- 新增 selectable-state updater 回归，锁定“选中对象变化不重建 row instance”。
- xmake r ya-gui-closure-test 全量通过（345/345）；ya-game-editor 构建通过。

### Scene Save checkpoint 边界

- 两个 editor list consumer 都已接入 keyed instance reuse，但仍没有 visible-range virtualization；大目录仍会把全部 rows 留在 live tree。
- 下一 checkpoint 设计 row factory + visible range / scroll offset contract，再进入真正的虚拟化；不得恢复 detach-all rebuild。

## Phase 4B visible-range checkpoint

- 新增 FKeyedVisibleWindow / computeKeyedVisibleWindow / sliceKeyedVisibleWindow：由 itemCount、row extent、spacing、viewport、scroll offset、overscan 计算 first/count 与 leading/trailing spacer。
- 未布局的 viewport（extent<=0）会物化全表，避免第一帧没有内容高度；过扫描在列表两端被 clamp。
- UIKeyedChildReconciler 增加 SlotBinder，create/reuse 后按当前 key 顺序写 parent-owned slot（Content Browser / Scene Save 的 row height 走这条路径，不再在 updater 里改 slot）。
- Content Browser entry list 改为 leading SizeBox + keyed visible rows + trailing SizeBox；滚动 offset / viewport 变化会刷新窗口，目录切换重置 scroll。Mount list 与 Scene Save 仍全量物化。
- 新增 window/overscan 与 spacer 保高回归；xmake r ya-gui-closure-test 全量通过（347/347）；ya-game-editor 构建通过。

### Visible-range checkpoint 边界

- 这是均匀行高的窗口化，不是 widget pooling：滚出窗口的 key 会 detach，滚回来走 factory 重建。
- TableGrid / TreeView 仍是 flat paint consumer；Scene Save 与 mount list 未窗口化。
- 下一阶段可进入 Phase 5 style/resource invalidation，或把同一窗口契约接到 TableGrid/Hierarchy。
