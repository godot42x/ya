# Progress

## 当前状态

- 计划建立：2026-09-03
- 实现改动：Phase 5B style key/type catalog 已完成闭环
- 提交：本 checkpoint 随代码一并提交
- 当前阶段：Phase 5B（theme key + style type catalog/schema 校验）

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

## Phase 5A 当前 checkpoint

- `lookupStyleFieldImpact` / `lookupStylePatchImpact` 按反射字段分类 layout/paint/resource：`FBrush` 为 Paint+Resource；`fontSize` / `padding` / `minSize` 为 Layout；`FScrollBarStyle.width` 保持 Paint-only（overlay 厚度，不是布局输入）。
- `UIStyledWidget::setStyleField` / `setStyle` / `clearStyleField` / `clearAuthoredStyle` 默认走 catalog；`UIText::setFontSize` 仍可用显式 impact 按 AutoSize 覆盖。
- Resource 标志只是后续异步就绪 invalidation 的 metadata，本 checkpoint 不改变 `invalidateProperty` 枚举。
- 新增 catalog、patch union、UIText/UIButton/UIPanel dirty 回归。
- xmake r ya-gui-closure-test 全量通过（351/351）。
- 未完成：resource-ready invalidation、visual state matrix。

### Phase 5A 边界

- 没有引入 per-type 字段表，也没有做 schema key 校验或 brush/font ready 传播。
- `FButtonStyle.padding` 当前不驱动 button content layout（仍是 widget `_contentLayout`），catalog 仍把 `padding` 标成 Layout，避免 Tab/Text badge 和后续消费者漏测。

## Phase 5B 当前 checkpoint

- `YA_GUI_STYLE_CATALOG` / `StyleKey::*` 是 theme key 与 `TStyle` 的单一词汇；`lookupStyleKey` 区分 Known / Empty / UnknownKey / TypeMismatch。
- `UITheme::define`、`UIElement::setStyleKey` 和 document `deserializeFields` 走 `diagnoseStyleKey`：未知 key 与类型不匹配记诊断并 WARN，仍写入（明确诊断，不静默吞掉，也不硬拒绝）。
- `editor.<key>` 复用同一词汇，不是 EditorTheme 第二套机制。`canvas` 登记为 FPanelStyle 角色 key，`ui::canvas()` 使用 `StyleKey::Canvas`。
- `YA_GUI_AUTHORED_STYLE_IO` 现在报告 `getStyleTypeIndex()`，unstyled widget 为 0。
- 新增 catalog 全量 Known、editor prefix、unknown/mismatch 诊断，以及 document deserialize 未知 key 回归。
- xmake r ya-gui-closure-test 全量通过（354/354）。
- 未完成：resource-ready invalidation、visual state matrix。

### Phase 5B 边界

- 没有做编译期 DSL 类型拒绝（builder 仍收 string）；catalog 常量提供可在 C++ 侧引用的词汇。
- 没有要求每个 theme 必须 define 全部 catalog key；缺 key 仍 fallback 到 `TStyle{}`。
- 构造函数直接写入的默认 family key 不走 diagnose（派生 `getStyleTypeIndex` 在基类构造期不可用）；attach 后的 setStyleKey / 文档加载会校验。

## Phase 5C 当前 checkpoint

- `FontManager::resourceRevision()` 在 register/load/unload/clearCache 以及 `flushPendingGlyphs` 真正捕获新字形时递增。`WidgetTree::buildSnapshot` 消费该 revision：首次只记录，之后变化则对整棵已挂载树 `markLayoutDirty(ResourceReady)`，避免嵌套 fill 容器凭 assigned-rect skip 用过期文字度量。
- `UIFrameBuildContext.generation` 只表示 resolver identity（brush/image 纹理就绪）；scale/offset/DPI 仍是 `BuildContextChanged`。Host 继续在 snapshot 之后 `flushPendingGlyphs`（Core Rule 6），不再自己 `invalidateSubtree`。
- 没有把 `EUIPropertyImpact::Resource` 接到 `invalidateProperty`；`bResource` 仍是字段 metadata，就绪事件在树级 revision / generation。
- 新增 FontManager revision、嵌套文字 remasure、image resolver miss→generation bump 命中纹理的回归。
- Theme switch / style key 仍由既有 Phase 5 测试覆盖。
- xmake r ya-gui-closure-test 全量通过（357/357）。
- 未完成：visual state matrix。

### Phase 5C 边界

- 没有把 FontManager 接到 GUI Reactive（跨层依赖）。1 帧字形延迟仍在：paint 登记缺失、flush 后下一 snapshot 才消费 revision。
- 没有做 visual state matrix；稀疏/full freeze API freeze 与 naked color 清理不在本 checkpoint。

下一 checkpoint：Phase 5 visual state matrix（normal / hovered / pressed / focused / disabled / selected / error / drop-target）。

## Phase 5D 当前 checkpoint

- `EWidgetVisualFlag` / `composeVisualFlags` / `FVisualChrome` / `resolveVisualFill` 冻结 exclusive fill 优先级：Disabled > DropTarget > Error > Pressed > Selected+Hovered > Selected > Hovered > Focused > Normal。
- `UIButton` 与 `UISelectableRow` 经 `visualChrome(style)` 消费该表；Button 补 selected/error/dropTarget fill；SelectableRow 的 drop 走 `dropTargetFill`（默认等于原 selectedHovered）。
- 没有把 hover/pressed 升到 UIElement 基类，也没有改 CheckBox/TextField/Menu/Tab/TableGrid 的本地 if/else。
- 新增优先级矩阵、disabled 盖住 hover、row drop 盖住 selected 的回归。
- xmake r ya-gui-closure-test 全量通过（360/360）。
- 未完成：其余控件接入同一 matrix；稀疏/full freeze API freeze；naked color 清理；无主题/资源缺失 GPU 回归。

### Phase 5D 边界

- Selected+Hovered 是组合刷，不是第九个独立 exclusive 状态。
- SelectableRow 仍不把 Pressed 送进 matrix（按下外观保持原 hover/select 语义）。
- 新控件 paint 必须走 `resolveVisualFill`，不得再写一套优先级。

下一 checkpoint：把剩余 interactive 控件接到同一 visual fill matrix，或进入 Phase 5 的 sparse/full freeze / naked-color / GPU fallback 回归。

## Phase 5E 当前 checkpoint

- CheckBox / ComboBox / MenuBar / Tab / MenuItem / TableGrid / TreeView 行填充改走 `visualChrome` + `resolveVisualFill`，不再各写一套 hover/selected if/else。
- CheckBox.checked 与 Tab.selected 映射为 Selected；Selected+Hovered 回落到 Selected，保留“选中盖住 hover”。
- Table/Tree 的 idle normal 是透明刷，paint 仍按 alpha 跳过未选中且未 hover 的行。
- 新增 mapping、checked+hover、selected tab 回归。
- xmake r ya-gui-closure-test 全量通过（363/363）。
- 未完成：TextField/SpinBox/Radio/DragFloat 等非这套 chrome 的控件；稀疏/full freeze API freeze；naked color 清理；无主题/资源缺失 GPU 回归。

### Phase 5E 边界

- 没有把 hover/pressed 升到 UIElement 基类。
- MenuItem 的 disabled 仍用 itemNormalFill（原行为），disabled 文案颜色仍是独立字段。
- TreeView 展开箭头 hover 仍用 `arrowHoveredFill`，不是行 chrome。

下一 checkpoint：Phase 5 稀疏/full freeze API freeze，或 naked color 清理，或无主题/资源缺失 GPU 回归。

## Phase 5F 当前 checkpoint

- `UIPanel` 不再在无 theme 时走 `_color` sprite 旁路；fill 只来自 `resolvedStyle()`。无 theme 且未 authored `fillColor` 时 `_color` 作为 fallback（对齐 `UIText`）。
- Image 仍是 content：themed 且无 authored overlay 时 theme chrome 赢；authored `setColor` 给 image tint。
- 新增 unthemed default fill 与 themed 忽略未 sync `_color` 回归。
- xmake r ya-gui-closure-test 全量通过（365/365）。
- 未完成：Workbench/demo `setColor` 字面量改走 theme key；TextField 等几何字段；无主题/资源缺失 GPU 回归。

### Phase 5F 边界

- `_color` 仍保留给 GI-202 `getColor`/`setColor` 与 document 反序列化 fallback，不是第二套 paint 路径。
- 没有改 Workbench demo 的裸 `setColor` 调用；那是 app 层 overlay，不是 framework 旁路。
- 0-extent panel 不再发出 draw item（`addBrush`/`sliceBrush` 跳过空 rect）。相关 snapshot 测试改为显式 `fixedSize`，而不是让空 rect 也能产出 sprite。

下一 checkpoint：Phase 5 GPU/无主题 fallback 回归，或进入 Phase 6 SelectionModel。

## Phase 5G 当前 checkpoint

- 同一棵树覆盖：无 theme 时 panel/image 走 style 默认；theme A/B 切换跟皮肤；resolver miss 画 placeholder；generation bump 后纹理进入 snapshot。GPU compose 只消费这份 snapshot，所以契约在 snapshot/headless 层冻结。
- `GUIHeadlessHost` 无 RHI 路径：第一帧 unthemed fallback，第二帧 theme switch 后颜色翻转。
- xmake r ya-gui-closure-test 全量通过（366/366）；xmake r ya-gui-headless-host-test 全量通过（3/3）。
- 未完成：Workbench/demo `setColor` 字面量；TextField 等几何字段；windowed `--gpu-shot` / `--offscreen-diff` 仍属 Phase 9。

### Phase 5G 边界

- 没有跑 windowed GPU/offscreen 像素 parity；那是 Phase 9 的 release gate，不是本 checkpoint 的 GPU 设备测试。
- UIImage 命中纹理后仍用内容字段 `_tint`，不是 chrome。
- 没有把 TextureLifetime 测试从 engine suite 搬进 closure（closure 已有 fake-texture 延迟就绪用例）。

下一 checkpoint：进入 Phase 6 SelectionModel。

## Phase 6A 当前 checkpoint

- `SelectionModel` 是 identity 选择源：selected 有序集合 + primary（必须在集合内或空）+ 独立的 hover/active/focus。不持有 Entity/Scene 指针。
- TreeView 通过 `primaryRef()` 共享同一模型；两棵树点击同步。EditorSurface 的 Hierarchy / ProjectList 绑定同一模型再写 EditorLayer。
- xmake r ya-gui-closure-test 全量通过（368/368）；xmake b ya-game-editor 通过。
- 未完成：command/action routing；undo/redo；property projection；viewport/inspector 写回同一模型；EditorLayer `_selections` Entity* 尚未替换。

### Phase 6A 边界

- TreeView 仍是单选写入 `primary`；多选 API（add/toggle）存在但控件还没有 modifier 手势。
- hover/active/focus 还没有接到 viewport/hierarchy 指针与键盘；本 checkpoint 只冻结模型。
- 没有把 EditorLayer 的 Entity* 选择向量删掉。

下一 checkpoint：Phase 6 command/action routing，或把 viewport 选择写入 SelectionModel。

## Phase 6B 当前 checkpoint

- `ActionMap` 是 identity 命令表：`define` / `execute(id)` / `dispatchKey`。菜单、快捷键、toolbar 不再各写一份 lambda。
- `FActionChord::primary` 在 macOS 匹配 Cmd，别处匹配 Ctrl；Shift 区分 Save / Save As。文本焦点下不匹配无 modifier 的 chord。
- EditorSurface File/View 菜单走 `UIMenu::FItem::fromAction`；Play/Stop/viewport 按钮走同一 `execute`；未处理的 KeyPressed 才 dispatch shortcut。
- xmake r ya-gui-closure-test 全量通过（370/370）；xmake b ya-game-editor 通过。
- 未完成：command palette / context menu 枚举 ActionMap；undo/redo；property projection；viewport 写入 SelectionModel；ImGui `EditorLayer::menuBar` 仍是平行路径。

### Phase 6B 边界

- 没有做 palette UI 或 hierarchy context menu。
- 没有把 viewport 2/3 键迁入 ActionMap（仍是 EditorLayer 在 viewport focus 时处理）。
- ImGui 菜单栏未删除。

下一 checkpoint：Phase 6 undo/redo transaction，或 viewport 选择写入 SelectionModel。

## Phase 6C 当前 checkpoint

- `UndoStack` 是 identity 撤销历史：`push` 记录已应用的 undo/redo 闭包（push 不调用 redo）。`beginMerge`/`endMerge` 只合并同一 merge session 且 `mergeKey` 相同的连续 push，用来收口拖动。`UndoTransaction` 把嵌套 push 收成一步。栈不持有 Entity/Scene 指针。
- `edit.undo` / `edit.redo` 走 ActionMap；EditorSurface 增加 Edit 菜单。macOS Redo 是 Cmd+Shift+Z，别处 Ctrl+Y。
- Inspector 变换拖动经 `UIDragFloat::_onDragBegan/Ended` 开闭 merge；键盘/commit 各成一步。`setValue(..., false)` 给 sync，避免 gizmo 刷新写入 undo。重命名按 UUID 查找 Node，不把 Node* 存进栈。
- xmake r ya-gui-closure-test 全量通过（374/374）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 通过。
- 未完成：property projection；multi-object / mixed value；viewport 写入 SelectionModel；gizmo 编辑未接入 UndoStack；闭包仍可能捕获 PropertyHandle 内的 void*。

### Phase 6C 边界

- 没有做 command palette。
- 没有把 ImGuizmo / viewport 变换写入 UndoStack。
- 没有把 EditorLayer Entity* 选择向量删掉。

下一 checkpoint：Phase 6 property projection，或 viewport 选择写入 SelectionModel。

## Phase 6D 当前 checkpoint

- `PropertyGraph::project` 是反射 → editor field model 的唯一入口：`build` 之后应用 `PropertyProjectionRegistry`。
- Transform projection 同时写入显示名和 `setPosition` / `setRotation` / `setScale`；未投影的 `build` 仍是直接字段写，不会标 dirty。
- Inspector 不再手写 Transform section：按选中实体上的 ECS component 指纹重建，凡 `hasRetainedEditors()`（vec3/float/bool/string）的类型物化 `EditorAutoPropertySection`。
- 删除 `EditorTransformSection`；控件仍不持有 Entity*，binding 由 projection 提供。
- xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 6/6 通过。
- 未完成：multi-object mixed UI；validation / missing resource；viewport 写入 SelectionModel；enum/color/asset 编辑器（Phase 7）。

### Phase 6D 边界

- 没有做 mixed-value 显示或批量提交 UI。
- 没有把 ImGui DetailsView / TypeRenderer 删掉。
- 没有把 viewport 选择写入 SelectionModel。

下一 checkpoint：Phase 6 multi-selection / mixed value，或 viewport 选择写入 SelectionModel。

## Phase 6E 当前 checkpoint

- Inspector 对 `EditorLayer::getSelections()` 的 **component 交集** 调用 `PropertyGraph::project`，一份 graph 持有全部 instance。
- `UIDragFloat` mixed 画 "—"；编辑把该轴写成相同值并清 mixed。`PropertyHandle::copy/restore*` 让 undo 恢复每个 instance 的原值，而不是用第一个覆盖全部。
- xmake r ya-gui-closure-test 全量通过（374/374）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 7/7 通过。

### Phase 6E 边界

- 没有做资源缺失/校验错误 UI。
- 没有把 Hierarchy modifier 多选接到 SelectionModel。
- ImGui DetailsView 仍是平行路径。

下一 checkpoint：Phase 6 viewport 选择写入 SelectionModel，或 validation/error state。

## Phase 6F 当前 checkpoint

- `SelectionModel::replace` 批量设置 selected 有序集合与 primary（去重、primary 前置、无变化不 bump）。
- `EditorLayer::selectionGeneration` 在 `setSelections` / `setSelectedWidgetEntryId` 递增；`EditorSurface::syncSelectionFromLayer` 在 `syncPresentation` 中把 viewport/widget 选择映射为 `e:{uuid}` / `ui:{entryId}` 并 `replace` 到共享 `_selection`。
- Hierarchy `setOnSelectionChanged` 仍写 layer（单选）；viewport 多选经 layer → generation → `replace` 同步回 TreeView primary。
- xmake r ya-gui-closure-test 全量通过（376/376）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter='BindingContractTest.SelectionModel*'` 通过。

### Phase 6F 边界

- 没有把 Hierarchy modifier 多选直接接到 SelectionModel（仍经 ImGui SceneHierarchyPanel → layer）。
- ImGui DetailsView 仍是平行路径。
- validation / missing resource error state 未做。

下一 checkpoint：Phase 6 validation/error state，或 Phase 7 retained inspector primitives（enum/color/asset）。

## Phase 6G 当前 checkpoint

- `PropertyHandle::validationError` 读取反射 `manipulator_spec` 的 min/max；`EditorAutoPropertySection` 在 `sync` 时对 `UIDragFloat`/`UITextField` 调 `setError`，construct 时把 manipulate spec 写入 drag 的 `_min/_max/_speed`。
- `FDragFloatStyle`/`FTextFieldStyle`/`FImageStyle` 增加 `errorFill`；`UIImage` 对非空 `_assetPath` 解析失败或 `setResourceMissing(true)` 画 error fill（与中性 placeholder 区分）。
- `EditorSurface::syncViewportTexture` 在 scene 已加载但 viewport RT 不可用时对 viewport image 标 `setResourceMissing`。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 9/9 通过。

### Phase 6G 边界

- 没有把 Hierarchy modifier 多选直接接到 SelectionModel。
- ImGui DetailsView 仍是平行路径。
- enum/color/asset retained 编辑器属 Phase 7。

下一 checkpoint：Phase 7 retained inspector primitives（enum/color/asset），或 Content Browser retained controls。

## Phase 7A 当前 checkpoint

- `PropertyHandle` 增加 enum 读写：`isEnum`、`enumLabels`、`tryGetEnumIndex`、`setEnumByIndex`、`copyEnum`/`restoreEnum`；`isMixed` 覆盖 enum。
- `UIComboBox` 增加 `setSelectedIndex(..., bNotify)` 与 `setMixed`（显示 "—"）。
- `EditorAutoPropertySection` 对 enum 字段物化 `UIComboBox`，支持多选 mixed 与 undo。
- `PropertyGraph::hasRetainedEditors` 识别 enum 类型。
- xmake r ya-gui-closure-test 全量通过；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 11/11 通过。

### Phase 7A 边界

- color（`glm::vec4`/`UIColorEdit`）与 asset reference 编辑器未做。
- Content Browser / Hierarchy virtualization 未做。

下一 checkpoint：Phase 7B retained color editor，或 asset reference picker。

## Phase 7B 当前 checkpoint

- `PropertyNode::bColor` 从反射 `Meta::Color` 元数据写入；`PropertyHandle::isColor` + `tryGetColor`/`setColor`/`copyColor`/`restoreColor` 支持 `glm::vec3`/`glm::vec4`。
- `UIColorEdit` 增加 `setColor(..., bNotify)` 与 `setMixed`（swatch 显示 "—"）。
- `EditorAutoPropertySection` 对 `.color()` 字段物化 `UIColorEdit`（非 color 的 vec3 仍走 DragFloat）。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 13/13 通过。

### Phase 7B 边界

- asset reference picker 未做。
- color 拖动连续改色未做 undo merge。

下一 checkpoint：Phase 7C asset reference picker，或 Content Browser retained controls。

## Phase 7C 当前 checkpoint

- `PropertyHandle::isAssetRef` + `assetRefKind` + `tryGetAssetPath`/`setAssetPath`/`copyAssetPath`/`restoreAssetPath`；`hasAssetResolveError` 读 `TextureRef`/`ModelRef`/`MeshRef` resolve state；multi-select mixed 比较 path。
- `EditorAssetPickerCallback`（`GameEditor/UI/EditorAssetPicker.h`）把 Browse 接到现有 `FilePicker`；`EditorInspectorTab` 注入，`PropertyHandle` 不依赖 editor UI。
- `EditorAutoPropertySection` 对 asset ref 物化 `UITextField`（path commit + undo）+ `UIButton`（Browse）；resolve failed 时 `setError`。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 16/16 通过。

### Phase 7C 边界

- ImGui `TypeRenderer` 的 `pathWrapper` 仍是平行路径（Phase 8 删除）。
- MeshRef Browse 复用 model picker（无独立 mesh picker）。
- Content Browser / Hierarchy virtualization 未做。

下一 checkpoint：Content Browser retained controls，或 Hierarchy virtualization。

## Phase 7D 当前 checkpoint

- 修复 Content Browser fingerprint：纳入 `getSearchText()` 与 `getSelectedPath()`，搜索过滤即时刷新 entry 列表。
- `FileExplorer::getSearchText()` 公开搜索状态；搜索框失焦时 sync 回写。
- Content Browser entry 行支持单选高亮（`selectContentItem` → `setSelectedPath`）；纹理文件选中时调用 `inspectAsset`。
- 抽取 `EditorListRows.h`（`contentRow` / `updateContentRow` / `makeContentRowFactory`）供 Content Browser 与 Scene Save 共用。
- Scene Save dialog fingerprint 纳入 search text。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`FileExplorerNavigationTest` 4/4；`EditorListRowsTest` 1/1。

### Phase 7D 边界

- ImGui `ContentBrowserPanel` / `FileExplorer::render` 仍是平行路径（Phase 8 删除）。
- Icon view / 缩略图 / view-mode toggle 未在 retained 路径实现。
- Hierarchy virtualization 未做。

下一 checkpoint：Hierarchy virtualization，或 retained Content Browser icon view。

## Phase 7E 当前 checkpoint

- `UITreeView` 在 `UIScrollViewport` 子节点下用 `computeKeyedVisibleWindow` 只绘制可见行窗口（overscan=2）；`getPaintedRowCount()` / diagnostics `paintedRows` 暴露绘制行数。
- `EditorSurface` Hierarchy 面板用 `HierarchyScroll` 包裹 `HierarchyTree`，大树可滚动且 paint 不随节点数线性增长。
- `ToolControlsTest.TreeViewVirtualizesPaintInsideScrollViewport`：50 行树在 100px viewport 下 paintedRows < visibleRows。
- xmake r ya-gui-closure-test 全量通过（378/378）；xmake b ya-game-editor 通过。

### Phase 7E 边界

- flatten/hit-test 仍遍历完整可见行集；仅 paint 窗口化，不是 row widget pooling。
- ImGui `SceneHierarchyPanel` modifier 多选 / UI entry drag-drop 未做。
- TableGrid flat paint 仍未虚拟化。

下一 checkpoint：viewport/gizmo overlay，或 Hierarchy filter + drag-drop。

## Phase 7F 当前 checkpoint

- `EditorHierarchyOps`：`editorHierarchyEntityIdKey` / `parseEditorHierarchyEntityIdKey` / `moveEditorHierarchyEntity`（TreeView drop mode 0/1/2 → before/into/after；拒绝 `ui:` 行）。
- `EditorSurface` Hierarchy：`HierarchyFilter` `UITextField` + `bindFilter`；`setReorderable(true)` + `setOnReorderHandler` 经 `Scene::moveNode` 重排实体并保持选中。
- `EditorHierarchyOpsTest`：`MoveEntityBeforeSiblingReordersChildren`、`RejectsUiEntryIds`。
- xmake r ya-gui-closure-test 全量通过（378/378）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorHierarchyOpsTest.*` 2/2 通过。

### Phase 7F 边界

- 仅 scene 实体（`e:{uuid}`）可拖放重排；Game UI `ui:` 条目与 ImGui `SceneHierarchyPanel` UI entry drag-drop 仍平行。
- Hierarchy modifier 多选、context menu、viewport/gizmo overlay 未做。
- TableGrid flat paint 仍未虚拟化。

下一 checkpoint：viewport/gizmo overlay contract，或 drag-drop asset workflow。
