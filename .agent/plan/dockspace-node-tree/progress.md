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
- 计划文件 plan.md 已写好（5 阶段 + 护栏 + 文件清单）。
- 待 ExitPlanMode 审批后进入阶段 1 实施。

### 关键探查结论（证据锚点）

- UISplitPane 两子硬限（UILayout.cpp:552-595）→ 不放开，自建 FDockNode 二叉树。
- layoutAssigned 递归协议（UIElement.cpp:136-140）→ DockSpace 递归分配 rect 给 root 节点。
- addDetachedChild 不标脏（UIElement.cpp:302-310）→ split/merge 后手动 markLayoutDirty。
- detach keepAlive 仅一层（WidgetTree.cpp:428-449）→ 移动子树须额外保活。
- addSprite(nullptr) 画半透明（UIFrameSnapshot.cpp:30）→ hover 预览零新 API。
- findDropTarget 跨层命中（WidgetTree.cpp:1074-1082）→ 浮动面板拖回 dock 可行。
- PopupOverlay 单例+固定定位+无标题栏（PopupOverlay.cpp:39,69-86）→ 不能复用，需新建 UIFloatingPanel。

### 剩余风险

- 嵌套 split 的 ratio/extent 算法需从 UISplitPane 抽或自实现（注意 min extent 约束）。
- 8向分区命中嵌套节点时的「落到哪个子」逻辑需仔细设计（先命中最深 leaf 再据 zone8 决定裂变方向）。
- 浮动面板与 dock 树之间的 widget 所有权转移（tear-off / re-dock）须严格 keepAlive，防 P7 式崩溃。

### 下一步

审批后从阶段 1 开始：DockNode.h + UIDockSpace 递归布局 + buildTree + rebuildLeaf，headless dock.jsonl 验证三栏。
