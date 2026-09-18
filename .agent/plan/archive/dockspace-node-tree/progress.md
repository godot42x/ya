# Progress — dockspace-node-tree

## 2026-08-19 — 规划阶段

### 已完成的前置工作（在 main 上）

- **P7 原始三栏 DockSpace** 已合入（commit `0b4fc7cc` 可用性修复 + `609a2692` panel 填满修复）：
  - `addPanel(name, widget, zone)` 支持三栏分配（0=left/1=center/2=right）
  - 空 zone 占位（`UITabBar::_emptyPlaceholder` + computeDesiredSize/paintSelf）
  - panel 用 box stretch 填满 zone（setStretchLastChild）
- **引擎级 detach keepAlive 修复**（commit `e8bd74a4`）：WidgetTree::detach 拆边前抓 shared_ptr 保活，修复 use_count==1 析构崩溃——本计划的护栏基础。
- **headless scenario 通路**（commit `e8bd74a4` 同期）：GuiScenarioEventSource 公共化到 App/Kernel，DockSpace 拖拽可在 headless 下端到端验证。

### 当前规划状态

- 阶段 0 已完成：WidgetTree 的通用 drag lifecycle observer 与
  EDragFinishResult（Dropped / NoTarget / Cancelled）已验证；下一切片是纯
  `FDockTreeModel`，仍不改现有三栏 visual projection。

- 用户确认选 B（完整 ImGui 式 DockNode 嵌套树 + 浮动 + 8向预览）。
- 三个 Explore agent 已探查：布局系统 / 拖拽+绘制 / 浮动窗口，证据充分。
- 设计决策已确认：Split 复用 UISplitPane 载体；先单窗口内浮动、预留多 window；payload 保持 string。
- 计划已完成第二轮架构收口，实施顺序扩为 0～6 阶段：generic drag lifecycle → pure
  model transaction → WidgetTree projection → 5 向 → 8 向 → floating → multi-window port。
- 关键修正：`FDockNode` 不再混入 `UITabBar*` / `UIContainer*`；改为 pure model +
  `FDockNodeView` projection + panel registry。此前“手动析构 raw node tree”的方案已废弃。
- 关键 blocker 已明确：现有 `WidgetTree::beginDrag/updateDrag/endDrag` 没有每 move
  observer 和 no-target finish 回调，不能正确做 hover zone 或 tear-off；必须作为阶段 0
  的通用事件系统小改动先完成。
- 8 向 corner 语义已明确为 compound two-split + visible persistent empty leaf；不允许
  用视觉 quarter 预览却在 drop 时退化为 cardinal。
- 当前进入阶段 1；不直接重写 `DockSpace.cpp`，先建立可单测的纯 dock model。

### 关键探查结论（证据锚点）

- UISplitPane 两子硬限（UILayout.cpp:552-595）→ 不放开，自建 FDockNode 二叉树。
- layoutAssigned 递归协议（UIElement.cpp:136-140）→ DockSpace 递归分配 rect 给 root 节点。
- addDetachedChild 不标脏（UIElement.cpp:302-310）→ split/merge 后手动 markLayoutDirty。
- detach keepAlive 仅一层（WidgetTree.cpp:428-449）→ 移动子树须额外保活。
- addSprite(nullptr) 画半透明（UIFrameSnapshot.cpp:30）→ hover 预览零新 API。
- findDropTarget 跨层命中（WidgetTree.cpp:1074-1082）→ 浮动面板拖回 dock 可行。
- PopupOverlay 的 shield/dismiss/focus 语义（PopupOverlay.cpp:34-105）→ 不能复用；
  浮窗应由常驻 `UIDockFloatingHost` + 多实例 `UIFloatingDockWindow` 承载。

### 剩余风险

- Dock model projection 需要确保 UI view rebuild 不使 drag target / focus 缓存变悬挂；
  event callback 只能回查 stable NodeId/PanelId。
- `UISplitPane` 目前只把 divider ratio 写到可选 Reactive；需要一个窄的 controller
  write-back，避免重投影时 divider 跳回旧 model ratio。
- 四角 compound drop 创建 persistent empty leaf 是明确 UX 成本；scenario 和 Workbench
  文案必须令它可理解，后续才能决定是否需要 reset-layout / explicit close-empty-leaf。
- 浮动 panel 与 Popup modal 的 z-order、focus restore 需要按 route trace 真机验证；
  不能复用 `UIPopupOverlay` 的 dismiss/single-content 语义。

### 下一步

建立无 `UIElement*` / `UITabBar*` / `UIContainer*` 的 `FDockTreeModel`、transaction
和 invariant validator，并以 model-only 测试覆盖 merge、cardinal split、source collapse
与 ratio/min-extent 规则。

### 2026-08-19 — 阶段 0 首轮实现

- WidgetTree::beginDrag 接受可选 DragSessionObserver，保持原有三参数调用兼容。
- onMove 对每次 updateDrag 同步通知；onTargetChanged 只在接受目标切换时通知。
- endDrag 区分 Dropped 与 NoTarget，cancelDrag 报告 Cancelled；回调在清理会话状态后
  执行，drop target 通过 shared_ptr 保活到 onDrop 完成。
- 新增 WidgetTreeTest 覆盖每次 move、目标切换、drop/no-target/cancel 以及会话清理。
- 将 widgets 测试中遗留的 `MouseButtonPressedEvent(0)` / release 同类调用改为
  `EMouse::Left`，与当前输入 API 的强类型约束一致。
- `xmake b ya-gui-widgets-test` 通过；聚焦
  `WidgetTreeTest.DragObserver*` 的两个新测试通过。
- `xmake b GUIWorkbench` 通过；headless P7 场景通过，写出
  `dock_initial.json` 与 `dock_dragged.json`，三栏/三个 tab 的结构断言均通过。
- 全量 `ya-gui-widgets-test` 当前为 122/127；以下 5 项是本切片外的既有基线失败：
  `UIFrameSnapshotTest.BuildResolvesItemsToRenderPixelsInPaintOrder`、
  `UIFrameSnapshotTest.ScrollViewportClipsContentToViewportRect`、
  `UIFrameSnapshotTest.SplitPaneClipsChildrenToOwnPaneRect`、
  `UIFrameSnapshotTest.ContainerClipResizeInvalidatesChildSegments`、
  `ToolControlsTest.SplitPaneDividerDragChangesRatioAndEndsSession`。
- `xmake b ya-gui-closure-test` 仍被目录迁移残留阻断：
  `Render2DClipTest.cpp` 引用已不存在的 `GUI/Draw2D/Render2D.h`；这与 observer
  行为无关，留给 GUI Draw2D include-path 基线修复单独处理。

阶段 0 的 generic drag observer/finish result 与 P7 基线 dump 已收口；接下来建立
无 UI 指针的 `FDockTreeModel` 和 model tests。

### 2026-08-19 — 阶段 1 pure Dock model 完成

- 新增无 `UIElement*` / `UITabBar*` / `UIContainer*` 依赖的 `FDockTreeModel` 与公开
  `DockNode` wrapper；模型只持有 binary node tree、panel identity registry、父子关系、
  split ratio/min extent 与 empty-leaf policy。
- 实现 register/add、merge move、四方向 cardinal split、source empty collapse、remove
  以及 invariant validator。注册但尚未放入布局的 panel 被视为合法 detached registry
  entry，split 可将其作为新 panel 放入目标 leaf；重复 identity、未知 id、同 leaf move
  等非法 mutation 在修改前拒绝。
- mutation 采用 precondition → backup → mutate → normalize → validate → rollback 的
  原子语义；同 leaf move 不再先删除后失败。
- 新增 `DockNodeTest` 覆盖 identity、cardinal split、merge/order、empty-source collapse、
  invalid no-mutation、ratio clamp、geometry validation 与 remove registry。
- 验证：`xmake b ya-gui-widgets-test` 通过；`xmake r ya-gui-widgets-test
  '--gtest_filter=DockNodeTest.*'`：7/7 通过。

下一步进入阶段 2：以 `FDockTreeModel` 为唯一事实源建立 `UIDockSpace` 的 WidgetTree
projection，先复现当前三栏默认布局，再逐步替换 `_groups[3]`，不在 model 中引入视觉
widget 指针。

### 2026-08-19 — 阶段 2 projection bridge 首轮

- `FDockTreeModel` 增加 `splitEmptyLeaf` 与稳定 leaf preorder 查询；默认三栏布局由模型
  创建两个嵌套 split 和三个持久 leaf，不再需要虚假 panel 占位。
- `UIDockSpace` 开始持有 model、zone leaf ids 和 stable panel ids。兼容的 `addPanel` 会先
  注册/挂载 model panel，再建立现有 tab/content visual projection；跨 zone move 先提交
  model transaction，成功后才更新视觉 groups。
- 这是一轮过渡 bridge：当前 `_groups[3]` 视觉结构仍保留，尚未完成递归 `FDockNodeView`
  projection；因此 n2 继续保持 in_progress，下一切片要把 split/leaf view 从 model 递归
  materialize。
- 验证：`xmake b GUIWorkbench` 通过；Dock headless scenario 通过，初始与拖拽 dump 均生成。

### 2026-08-19 — 阶段 2 递归 projection 切片

- `UIDockSpace::layoutAssigned()` 不再手写固定 `outerSplit/innerSplit` 视觉树，改为从
  `FDockTreeModel::root()` 递归 materialize：split 节点映射为 `UISplitPane`，leaf 节点
  映射为 tab bar + content host。
- 默认模型 ratio 明确写入 `0.22` 与 `0.78`，保持 P7 三栏的既有几何契约；模型仍是布局
  事实源，视觉树只在首次 attach 时生成。
- 当前仍保留 `_groups[3]` 作为过渡 panel/widget registry；它不再构造 split 布局，但
  还承担 leaf view 的 tab/content 绑定。按 stable leaf id 回查 zone，未知 leaf 只生成空容器。
- 验证：`xmake b GUIWorkbench` 通过；
  `xmake run GUIWorkbench --start-page Dock --scenario Example/GUIWorkbench/Scenarios/dock.jsonl
  --headless --scenario-dump-dir build/dock_dump_phase2_recursive2` 通过。
  `DockZoneLeft` 宽度断言 `278.600006`、center/right、三个 tab、拖拽 checkpoint 均通过。
- 本切片不宣称 projection 阶段完成：divider ratio 尚未回写 model，`_groups[3]` 尚未替换
  为按 `DockPanelId` / `DockNodeId` 管理的独立 `FDockNodeView` registry。

### 2026-08-19 — 阶段 2 divider model write-back

- `UISplitPane` 增加窄的 ratio-change callback；divider 拖动仍只修改布局状态，但在同一
  次交互中将最终 ratio 通知宿主。
- `FDockTreeModel::setSplitRatio()` 作为唯一模型 mutation 入口，负责有限值检查、clamp
  与 invariant 保证；`UIDockSpace` materialize split 时按 stable `DockNodeId` 绑定回写。
- 这样 resize / 重新 materialize 时不会因为视觉 split 的瞬时 ratio 丢失而跳回旧值；仍未做
  model mutation 后的局部 subtree rebuild 和独立 view registry。
- 验证：`xmake b GUIWorkbench` 通过；Dock headless initial/dragged scenario 通过。

### 2026-08-19 — 阶段 2 selection ownership

- 增加 `FDockTreeModel::selectPanel()`，tab selection callback 先更新 model，再做 content
  widget detach/attach；selected panel 不再只存在于 `UITabBar` 的视觉状态。
- 增加 model-only selection ownership 测试；DockNode focused suite 8/8 通过。
- 当前阶段仍不重建整个 subtree；下一步是建立按 `DockNodeId` / `DockPanelId` 索引的
  projection registry，把 view 生命周期从三组 zone 临时数组中分离出来。

### 2026-08-19 — 阶段 2 leaf view registry

- 新增 `FLeafView` 与 `_leafViews: unordered_map<DockNodeId, FLeafView>`；leaf 的
  `UITabBar/UIContainer` 句柄按稳定 leaf id 管理，不再存放在 `FTabGroup[3]`。
- `FTabGroup` 现在只保留 zone 过渡数据与 panel/widget registry；materialize、rebuild、tab
  selection 都通过 leaf id -> view registry 查找视觉对象。
- 首次 projection 时清理 view registry，避免重复 materialize 后残留旧 leaf view。
- 验证：`xmake b GUIWorkbench` 通过；Dock headless 初始/拖拽 scenario 全部通过。
- 仍待下一切片：view registry 的非 zone leaf 支持、局部 subtree rebuild、panel registry 从
  `FTabGroup[3]` 解耦，以及 model mutation 后的 view teardown/focus 保护。
- 增加 `SplitRatioMutationIsClampedAndAtomic` model test；DockNode focused suite 10/10 通过。

### 2026-08-20 — 阶段 3 cardinal preview/drop 首轮

- `UIDockSpace` 现在通过 drag observer 的 `onMove` 维护 immutable preview；`paintChildren`
  后绘制半透明 preview，避免被 leaf content 盖掉。preview 区域按 target leaf 的 center /+  cardinal 结果计算，不再只是整块 outline。
- payload 改为稳定 `DockPanelId` 编码（`dock-panel:<id>`）；drop 时先解析 preview，再
  按 preview 执行 `FDockTreeModel::movePanel / splitLeaf`。same-leaf cardinal split 已
  回归为真正的 model mutation，而不是 zone swap。
- 为了保住三栏锚点，`movePanel(..., collapseSource=false)` 让 merge 路径先不塌缩源 leaf；
  默认 P7 三栏命名仍可稳定通过旧 smoke。`DockTabBar0/1/2` 与 `DockZoneLeft/Center/Right`
  基线保留。
- 新增正式 smoke 场景 `Example/GUIWorkbench/Scenarios/dock_cardinal_split.jsonl`；headless
  验证通过，拖到 leaf 边缘会生成 `DockSplit`，而不是只换 tab。
- 下一步仍缺：corner compound preview、非 zone leaf 真正投影、以及 panel registry 从
  过渡 zone 数据结构里完全剥离。

### 2026-08-20 — 阶段 4 corner compound docking 首轮

- 新增 `EDockCorner` 与 `FDockTreeModel::splitLeafCorner()`：四角拖放现在会真的生成
  “外层 vertical split + 内层 horizontal split + persistent empty leaf” 的 compound tree，
  不再静默退化成 cardinal split。
- `UIDockSpace::resolveDropPreview()` 识别四角区域并给出角落预览；corner drop 直接走
  `splitLeafCorner()`，headless smoke 已能在 dump 里观察到 `DockLeaf` 数量上升，说明
  compound tree 已进入可见投影。
- 新增 model focused test `CornerSplitCreatesCompoundTreeWithPersistentEmptyLeaf`，以及正式
  smoke 场景 `Example/GUIWorkbench/Scenarios/dock_corner_split.jsonl`。
- `UIDockSpace::resolveDropPreview()` 现在补上 cardinal / corner 的最小尺寸 gating：
  小于 240px 的单轴 split 会直接进入 disabled preview，不高亮且不接受 drop；corner
  要求宽高都至少 240px，避免在过小 viewport 下生成会立刻失真的 compound layout。
- DockSpace dump 现在会带出 preview.active / disabled / kind / disabledReason，可直接被
  headless scenario 与后续自动化断言消费，而不必靠人工猜测为什么某个角落不能停靠。

- 拖到 tab bar 上的 drop 现在优先按 merge 处理，不再被当成四角/四边 split 入口——这修复了
  “拖到左上角 tab 附近 tab 越来越多”的扩散路径（每次 corner/cardinal split 都新开 leaf/tab）。
- corner preview 现在会把 compound split 会留下的 persistent empty leaf 也用低强调 outline
  画出来（ghost），让用户提前看到这次操作会保留第二个可投放区域。
- dock 视觉收口：DockSpace 加 canvas 底色，tab 选中态改为“内容同底 + 顶部亮条 accent”、
  hover/normal 拉出层次，split divider 提亮以建立分区感；已用真实窗口 GPU frame 像素采样确认。
- 下一次仍缺：非 zone leaf 真正投影、panel registry 从过渡 zone 数据结构完全剥离、以及 corner
  preview 的视觉实机验收。

### 2026-08-20 — 移除三栏锚点，收敛为真正嵌套 dock

- 删除 P7 三栏语义：_groups[3] / kZoneLeft/Center/Right / _zoneLeafIds[3] /
  ensureModelLayout / refreshZoneLeafIds 与 addPanel(zone) 全部移除。
- 初始不再固定 left/center/right 三栏：FDockTreeModel 默认就是一个 root leaf，所有注册
  panel 先塞进 root leaf；拖到边缘/角落才真正裂变。
- addPanel(name, widget) 不再带 zone；布局/命名统一为动态 DockLeaf<N> / DockTabBar<N>。
- merge 路径 movePanel(collapseSource) 改回 true：source 被拖空后按真实 dock 语义向上
  collapse，不再为“保三栏锚点”而硬关塌缩 —— 这正是之前“为什么有空 dock leaf”的根因之一。
- smoke dock.jsonl / dock_cardinal_split.jsonl / dock_corner_split.jsonl 全部改为单 root
  leaf 语义与动态节点名，三个场景 + DockNodeTest 12/12 全绿。
- 后续仍未做：floating panel、跨 window、持久化；非 zone 的更深 subtree 投影仍需扩展验证。

### 2026-08-20 — 守住“split 不制造空白 leaf”的模型不变量

- 复现：把一个 panel 拖到自己所在 leaf 的边缘/角落（source == target），当该 leaf 只有一个
  panel 时，splitLeaf / splitLeafCorner 会把它抽成新 leaf，旧一侧变成空的非 persistent
  leaf —— 反复操作就堆积大量空 leaf。
- 修复：same-source 分支在抽空 oldPanels 时直接拒绝该事务（rollback，返回 false）。模型层
  因此保证“任何 split 都不会凭空制造非 persistent 空 leaf”，不依赖 UI 层兜底。
- 新增 SinglePanelSameLeafSplitDoesNotCreateEmptyLeaf model test；DockNodeTest 13/13 通过。

### 2026-08-20 — 拖自己 title 悬停自己 leaf 不再显示 dock 提示

- resolveDropPreview 现在在 source == target leaf 时直接返回 disabled preview（不高亮、
  canAcceptDrop 为 false），不再把“把自己 dock 到自己”当作可停靠区域；诊断 reason 为
  “cannot dock a panel onto its own leaf”。

### 2026-08-20 — phase 5 切片 A：集中 UIDockWorkspace 层

- 新增 UIDockWorkspace（DockWorkspace.h/.cpp）：集中持有 FDockTreeModel、panel registry、
  panel id 分配，以及 bAllowDocking / bAllowFloating / bAllowTearOff 三个策略开关；是将来
  IDockWorkspaceCoordinator 的地基（一个 workspace 可被多窗口共享）。
- UIDockSpace 改为引用 workspace（setWorkspace + ws->dockModel()/findPanel），不再私有持有
  model / panels / id；addPanel 委托 workspace。demo 创建 UIDockWorkspace 并绑定。
- 同 leaf 语义细化：拖自己 title 悬停自己 —— tab bar / 中心 merge 禁用（自己 merge 自己
  无意义），但边缘/角落 split 保留（单 root leaf 时这是长出多 leaf 的路径），仅当该 leaf
  只有 1 个 panel 时才禁用（避免留空 half）。
- 验证：GUIWorkbench 构建、三个 dock smoke、DockNodeTest 13/13 全绿。

### 2026-08-20 — 移除 8 向 corner，收敛为 5 向停靠（消除空 DockLeaf）

- 反复出现的空 DockLeaf 根因之一是 8 向 corner 的 compound two-split：每次停靠都会额外
  制造一个可见 persistentEmptyLeaf 占位，self-drop 反复操作会堆积空 leaf。
- 拍板移除 corner：EDockCorner、splitLeafCorner()、corner preview/ghost、corner gating
  与 dock_corner_split.jsonl 全部删除；corner 命中统一归入对应 cardinal split。
- DockSpace.h/.cpp：FDropPreview 去掉 corner / emptyLeafRect / bCorner 字段与
  isDropPreviewCorner；rejectForExtent 去掉 bCorner 分支；resolveDropPreview 精简为
  center(merge) + 4 cardinal(split)；onDrop 去掉 splitLeafCorner 分支。
- DockNode.h/.cpp：删除 EDockCorner 与 splitLeafCorner()；保留任何 split 不制造非
  persistent 空 leaf 的同 leaf 抽空守卫。
- DockNodeTest：删除 CornerSplitCreatesCompoundTreeWithPersistentEmptyLeaf，更新
  SinglePanelSameLeafSplitDoesNotCreateEmptyLeaf 不再调用 splitLeafCorner。
- 现在停靠只产生 center(merge) 或 4 cardinal(split) 两种结果，不再出现因 corner 而生的
  空 leaf；模型不变量继续成立（source 被拖空才 collapse）。

### 2026-08-21 — phase 5 slice B：floating host + tear-off + re-dock

- FDockTreeModel 新增 detachFromTree(panelId)：把 panel 移出 dock tree（collapse 空 source
  leaf）但保留 registry，供 floating 使用；新增 DetachFromTreeKeepsRegistryForLaterRedock
  单测（DockNodeTest 13/13）。
- UIDockWorkspace 拥有 floating records（FFloatingWindow）与 tearOffPanel / dockPanelHome /
  endFloatingForPanel / isPanelFloating，以及 onDockUpdated / onFloatingUpdated 两个回调，
  让 DockSpace 与 floating host 通过 workspace 解耦协同。
- 新增 UIDockFloatingWindow：标题 tab（复用 UITabBar 拖拽，携带 dock-panel:<id> payload）+
  content；NoTarget 释放时把浮窗重定位到释放点；close 按钮 dock 回 root leaf。
- 新增 UIDockFloatingHost（挂在 Popup 层，non-modal，HitTestInvisible 空区放行到下层）：
  从 workspace.floatingWindows() 同步浮窗、bringToFront（拖拽激活时 reparent 到顶部）。
- DockSpace：tab drag NoTarget 且允许 tear-off/floating 时 tearOffPanel（浮窗默认位
  180,140，避开 chrome 页签）；resolveDropPreview/onDrop 支持 floating（sourceLeaf 为空）
  的 panel，re-dock 走 addPanel/splitLeaf 后 endFloating 并重建投影。
- 关键修坑：
  - 浮窗宿主最初用 SelfHitTestInvisible，会连子树一起不可命中，导致浮窗收不到输入；改
    HitTestInvisible（自身不可命中、子窗口仍可命中）。
  - 一开始用 delta-move 让浮窗跟随指针，导致 re-dock 释放点落在浮窗自身而非 DockSpace；
    改为 NoTarget 释放才重定位，re-dock 更干净。
  - DockSpace 填满整个 viewport，tear-off 释放点落在顶部 chrome（页签条）上会触发页签
    切换；浮窗固定默认位避开之。
- 验证：GUIWorkbench 构建通过；dock.jsonl / dock_cardinal_split.jsonl / dock_floating.jsonl
  三个 headless smoke 全绿（dock_floating 覆盖 tear-off→FloatingWindow→re-dock→回到 dock
  tree，最终 root=DockLeaf1 含 Scene/Inspector/Console，浮窗消失）；DockNodeTest 13/13。
- slice B 后剩余：modal 打开时禁止激活浮窗、focus restore、单浮窗多 tab、窗口边界 clamp。
