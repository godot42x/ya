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
- 当前仍是规划阶段；下一次开工从阶段 0 开始，不直接重写 `DockSpace.cpp`。

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

从阶段 0 开始：补 `WidgetTree` generic drag observer/finish result、dock model dump
schema、P7 基线 scenario；通过后才建无 UI 指针的 `FDockTreeModel` 和 model tests。
