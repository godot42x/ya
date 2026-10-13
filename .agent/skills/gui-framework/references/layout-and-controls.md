## 布局契约（SizeToContent）

- Slot 按 **parent layout family** 分，不是每个 widget 一种 slot：
  `canvasSlot` → canvas host（`UICanvasPanel`、`UICanvasRoot`）；
  `boxSlot` → `UIContainer` / `UIExpander`；
  `overlaySlot` → 仅 `UIOverlay`（以及 dock leaf / floating window 这类真叠放 host）；
  `contentSlot` → 单 child 内容 host（`UIBorder` / Button / CheckBox / SelectableRow / SizeBox / ScrollViewport / SplitPane pane / CompoundWidget）。
  不要给 Expander / SplitPane 再包一层隐藏 wrapper child；SplitPane 的 pane 复用 `contentSlot`，不另造 `splitSlot`。
- Layout 类型按头文件隔离。控件头只 include 自己的 family（`UILayoutTypes.h` / `UIBoxLayout.h` / `UIContentLayout.h` / `UIOverlayLayout.h` / `UICanvasLayout.h` / `UISplitLayout.h` / `UITableLayout.h` / `UIScrollLayout.h`）。`GUI/Layout/UILayout.h` 是兼容聚合头，给实现 .cpp 用；不要在 Button/Text/WidgetTree 这类宽扇出头里再 include 它，否则改一个 layout 仍会全模块重编。
- Slot args 应用走 `UISlot::applyArgs(args)`：`TArgs::SlotType` 指向接受该 payload 的 slot 类，没有按类型 if/else。新增 slot 类型时加 `FNewSlotArgs::SlotType` + `apply(const FNewSlotArgs&)` + `toArgs()`/`assign(const FNewSlotArgs&)` + `serialize`/`deserialize`/`isAutoSizeActive`，不要改 `applySlotBuilder` / `UIDocument`。`apply` 是 construct-time 语义（零尺寸 = 未设置，运行时 dock / popup 依赖它做部分更新）；`assign(toArgs())` 是精确状态往返，给设计器 slot 检查器用：反射的是 args 副本，写回走 `assign`（setter 与不变量照常生效），不反射 slot 私有字段。新 slot 类型要在 `UILayout.cpp` 反射其 args，并加进 `EditorUISlotEdit::forChild`。layout arrange 读取 typed 字段仍可用 `as<T>()`。
- 运行时类型是 `UICanvasPanel`；DSL 是 `ui::canvasPanel`。没有 `ui::panel` / `ui::canvas()` 别名。type id 仍是 `"engine.panel"`（`kTypeIdCanvasPanel`）。Canvas **不 paint**。`"panel"` / `"panel.canvas"` / `"canvas"` 是 visual theme key，挂在 `UIBorder` 上，不是 layout 类型。`ui::canvasPanel(...).setStyleKey("canvas")` 不再产生 chrome。



## 基础控件权责

| Widget | 负责 | 不负责 |
| --- | --- | --- |
| `UICanvasPanel` | canvas slot：anchor / insets / size mode；多 child 自由放置 | 填色、描边、圆角、纹理、nine-slice |
| `UIContainer` | box slot：align / padding / margin / fill / weight | paint |
| `UIOverlay` | overlay slot：同一 rect 里叠放 + align | paint |
| `UIBorder` | 画实心/主题 fill、outline、圆角；单 content child + padding | 纹理字段、nine-slice 字段、canvas anchors |
| `UIImage` | 内容纹理 / live RT / Stretch·Contain | 当 layout host；nine-slice chrome |
| `FBrush` | Solid / Image / NinePatch / frame-only `Border` 切片 | widget 树 |
| Card | `UIBorder` + `setStyleKey("panel.sidebar.card")` | 单独的类型 |

字段对照（旧 canvas 混在一起的 reflect）：

- `_color` / `_cornerRadius` → `UIBorder`（无 theme 时 `_color` 是 `FPanelStyle.fillColor` fallback，与 `UIText::_color` 同模式）
- `_image` → 不存在。内容图：`ui::overlay().child(ui::border(), overlaySlot().fill()).child(ui::image(), overlaySlot().fill())`
- `_bNineSlice` / `_nineSliceBorder` → `FBrush::ninePatch` / `FBrush::border`，写在 `FPanelStyle.fillColor`（主题 chrome），不是 widget 字段

迁移规则：

- 一个 fill child 的着色壳 → `UIBorder` + `contentSlot().fill()`（单 child 的旧 canvas insets 可以变成 Border padding）
- **多个不同 anchor 的 child 必须留 canvas**，paint 用 sibling fill Border：`canvas.child(border.HitTestInvisible, canvasSlot().fill()).child(real, canvasSlot().anchor/insets)`
- 不要用 Border padding / box align 去顶替多 child 的 canvas gutters

- Slot 未指定时的默认（必须先看得见，再调属性；**不是**一律 fill）：
  `contentSlot` / `overlaySlot` → Fill/Fill（单内容区 / 叠放占满父矩形）；
  `boxSlot` → 主轴 Auto + 交叉轴 Stretch（按内容堆叠；`fill()` 分剩余空间）；
  `canvasSlot` / 裸 `child(node)` → 左上角 Auto/Auto（按 desired 显示）。Canvas 的
  `fill()` 是显式的：默认 fill 会让每个 sibling 叠满父矩形，那是 overlay。
  **跨度 `anchor()` 或非零 `insets()` 会把该轴从 Auto 升成 Fixed（stretch）。**
  只写 `anchor({0,0},{1,1})` 而不 `fill()` 也必须铺满，禁止再量出 0 desired 把整块 UI 变成 0px。
  SizeToContent 落在 stretch 区域里必须显式、且放在 placement 之后：
  `fill().widthSizeMode(Auto)` / `heightSizeMode(Auto)`。
  `ui::canvasSlot()` 必须与 `UICanvasSlot` 同为 Auto；空 `FCanvasSlotArgs{}` 仍是
  Fixed 0x0（历史 `args.fixedSize = {w,h}` 载荷），不要把它当成 DSL 默认。
  Box 的 Fill 子节点在 Auto 父级里 leftover=0，主轴高度为 0（FeatureRail 那种
  `canvas.child(column.child(card, fill))` 没给 column `canvasSlot().fill()`）。
- SizeToContent / Slate DesiredSize 模型完全由 parent-owned slot 表达：canvas 在 attach 时把 Auto 种到 `UICanvasSlot` size mode。每轴解析优先级
  `Auto（preferredSize 非零则用之，否则 `computeDesiredSize`）> stretch（Fixed 轴上的 anchor span / insets）> slot authored size（fixedSize）`。
  DSL 跨度锚点 / insets 会把 Auto 升成 Fixed，所以 stretch 生效；不要依赖“看起来像 fill 的锚点 + Auto 轴”。
  需要在 stretch 区域里按内容测量时，在 placement 之后写回 Auto。`fill()` 是两轴 Fixed + `[0,1]`。
  child geometry 永远不是 layout 输入；`computeDesiredSize` / `computeIntrinsicSize` 只报告内容。不存在仍读取 child authored geometry 的 path-B。
- `UIText`：空字符串的 desired / intrinsic 是 0。非空时宽是最长一行，高是行数 × `lineHeight`（`\n` 硬换行；`_bWrap` 再按 `_maxWrapWidth` 折行，0 则按整段宽度测量，绘制时不超过排布后的矩形）。折行落在空格上，中日韩字符各自可断；一个词比整行还宽时才从中间切开。`_maxWrapWidth` 可序列化。字体经
  FontManager 解析，closure 测试用 `registerFont` 注入合成字体。显式尺寸在 parent-owned slot 上。
- `UIButton` / `UISelectableRow` / `UICheckBox`（Content-Slot）：单 child 容器。标签是内容槽里的 `UIText`
  子节点（DSL：`.child(ui::text(...).setText(...))`）。`UISingleChildLayout` padding + 内容子节点填入
  内缩 rect（`layoutAssigned`，非 child `setPosition`）。CheckBox 的左 padding = `_boxSize + _labelSpacing`。
  desired = 内容子节点 + padding；显式尺寸在 parent slot 上。行缩进用
  `setContentPadding(FMargin{indent, 0, 0, 0})`。
- `UICompoundWidget` 是 single-child host：`construct()` 挂上的第一个 child 经 `UIContentSlot` 填满 compound rect，不再手写 `layoutAssigned`。
- 布局正式分为 `UIElement / UILayout / UISlot`：`UIContainer` 只是第一个 layout host，
  持有 `UIBoxLayout`；它不再持有 `_direction/_spacing/_padding/...` 这类 box 字段。
  `UILayout` 只负责 measure/arrange，`UISlot` 是 parent-owned parent-child 边对象。
  `installLayout()` 的 host 由 `UIElement::createSlotForChild()` 直接问 layout 要 typed slot，不必再覆写工厂（CanvasPanel / TreeRoot / DockSpace / DockFloatingHost）。成员持有 layout 的 host（Border / Button / CheckBox / Compound / SizeBox / Split / Scroll / Overlay / Container）仍自己转发 `createSlot`。
- `UIBoxSlot` 承载每 child 的 `Auto/Fill`、weight、**四边 `FMargin`**、cross alignment、
  min/max/preferred size 与 layout participation；slot setter 会使所属 tree 的 layout 失效。
  Fill 按权重分配剩余主轴空间且遵守 max size；Hidden 默认保留空间，可由 slot 明确关闭。
  Construct：`column.child(node, FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill, .margin = FMargin::all(8), .preferredSize = {120, 22}})`；
  `childFill` 仍是只标 Fill 的简写。`setMargin({x, y})` 走 `glm::vec2` → 左右/上下对称
  （`FMargin` 不是 aggregate，两元素列表不会变成 left/top、right/bottom=0）。
  `FBoxSlotArgs::preferredSize` 非零轴覆盖 child desired；`ui::boxSlot().preferredSize({w,h})` 是 construct-time 写法。
- UIElement 不提供 `setSize/setPosition/getSize/getPosition` 或 authored geometry shadow；运行时与 imperative 构造代码必须先取得当前 `UISlot`，再显式修改 `UICanvasSlot` / `UIBoxSlot` / `UIOverlaySlot` / `UIContentSlot`；detached 构造使用 `addDetachedChild(..., slotInitializer)` 或 builder 的 pending edge intent。最终 rect 通过 `getLayoutRect()` 读取。
- attach 不再从 child geometry 推断 slot；显式 `FCanvasSlotArgs` / typed slot initializer 才是 edge 的唯一 authored placement 来源。默认构造尺寸不是布局输入。
- `UIPopupOverlay::_contentExtent` 是 popup-owned canvas edge 的内容尺寸；基类 Auto + preferredSize，Menu 覆盖为 fixedSize，Dialog 走 preferredSize。不要再通过 child geometry API 写内容尺寸。
- child 用 `getSlot()` 读取当前边，parent 用 `getSlotForChild()` 查询；reparent/detach 时旧 parent 销毁旧 slot，新 parent 创建默认 slot。不要缓存 slot。层挂载默认使用 `attach(*tree.getLayer(layer), widget)`；带几何意图使用 `attach(parent, widget, FCanvasSlotArgs)` 或显式 layer args。
  裸指针跨越 reparent/detach。
- `UIBoxLayout` 主轴按 desired/slot 排列，cross 轴默认 stretch；`computeDesiredSize` 聚合
  child + margin + spacing + padding。scroll/split 仍读取内容 desired，specialized layout
  已收口为 `UIScrollLayout` / `UISplitLayout` / `UIOverlayLayout`；`UIButton`、`UISelectableRow`、`UICheckBox`、`UICompoundWidget` 与 `UISizeBox`
  使用 `UISingleChildLayout`。  `UIDockSpace` 也是 single-child host：投影根填满 dock。  `FDockTreeModel::exportLayoutJson` /
  `importLayoutJson` 按 panel `stableKey` 持久化 split/leaf 树（不持久化 NodeId），leaf 可带
  `hideTabBar`（默认展示 tab strip；Tools/Generic 仅在至多一个 tab 时可通过右键
  「Hide Tab Bar」Collapsed 掉 title bar，面板内容继续填满 leaf。title bar 隐藏后
  在内容左上角画 UE 式直角三角，hover 高亮，点击恢复 strip。
  split ratio / 选 tab / hide-tab-bar 只改 live chrome 并
  `notifyDockLayoutListeners()`；结构变化才 `syncProjection(Structure)`，且会先 unlink 再
  reparent/attach 已挂载的 panel widget，禁止对仍有 parent 的 panel `addDetachedChild`。
  floating 停靠 cardinal split 时，drag keepAlive 仍握着刚 detach 的 floating window，
  panel 的 `_tree` 已空但 `_parent` 还在 chrome 上：`releaseMountedPanels` 必须把
  **dock tree 里每一块 panel**（不只是旧 `_leafViews`）从任何 parent 上摘下来，
  `unlinkWidgetFromVisualParent` 在无 tree 时也要 `removeChildEdge`。否则 north-of-leaf
  新叶是空白，直到再切一次 tab。
  叶内 tab 可关（`UITabButton` close hit-zone → `FDockContext::closePanel`）。
  Page-role leaf 的 inner well 与 hide-affordance 永远 Collapsed（title chrome 才是
  page tabs）；reveal 三角不得把 Level/UI 再画进 dock。
  TabWell 内左右拖只 `movePanel` 重排，不开始 dock session、不画 ghost。指针离开 well
  才 `_onTabDragBegin`。同 stack 的内容区显示 chooser；hover 到 cardinal 块则
  `splitStack`（含把当前 leaf 的一个 tab 拆出去）。未落在 chooser 块或 well 上的
  释放是 `NoTarget` → floating/tear-off。`canAccept` 只在 `commitsDrop()` 时为 true。
  Locked 页签（Level）`_bDraggable=false` 且 `_canBeginTabDrag` 拒绝 tear-off：
  press 仍 capture（避免 Hybrid Drag 整窗移动），`onDragDetected` 返回空，
  不出现 ghost、不开始 dock session。
  `FDockContext::exportLayoutJson` / `importLayoutJson` 在同一 JSON 上拆开
  overlay `floating[]` 与 native `windows[]`（panel keys + pos/size + selected +
  `hideTabBar` + `projection` / `geometrySpace` / `sourceScope` / `targetWindowId` /
  opaque `ownerEditorId` / `documentKey`）。overlay 坐标永远是 tree-local（Popup），
  缺 `projection` 的旧 `floating` 视为 `inProcessOverlay`，**不得**把 Popup 坐标升格成屏幕坐标。
  legacy `floating` 里的 `nativeWindow` 缺 `geometrySpace` 时仍是 TreeLocal，下次 export 迁到 `windows[]`。
  Editor 经 `ConfigManager` `editor.dockLayout` 恢复（v5 信封 `windows[]` 记录
  OS 窗 topology，MW-801）。产品启动 restore extra OS
  window 并在 primary submit 之后 `renderAll`（C9-P）。`FDockContext::appendOnDockUpdated`
  与 `appendOnFloatingUpdated` 写回。Editor chrome 打开 `bAllowFloating` /
  `bAllowTearOff`，`UIDockFloatingHost` 挂在 Popup 层，只画 overlay placement。
  `UIDockFloatingHost` 是 canvas host；floating window 的位置/尺寸写在 host-owned `UICanvasSlot`，
  `setWindowRect` 直接更新 host-owned `UICanvasSlot`。窗口本身是 overlay host：chrome
  box Fill，resize handle 走 overlay Start/End+Fill，不再在 box arrange 之后手写 handle rect。
  `UIPopupOverlay` 安装 `UICanvasLayout`；每帧把 `resolveContentSlotArgs()` 写进 content slot，再交给 canvas arrange。
  specialized widget 只保留 paint/input transient state，不能再把 ratio/offset/padding 等几何状态塞回 widget 字段。
- `UIOverlay` 是叠放 host（不是 `UIPopupOverlay`）：每个 child 经 `UIOverlaySlot` 在同一父
  rect 内独立 Fill/Start/Center/End + 四边 padding + **preferredSize**。child 的 canvas anchor 被忽略。
- `UISizeBox` 是单 child 约束盒：padding + 可选宽/高 override + min/max。
- `UISplitLayout` 管 orientation/ratio/min extent/divider/padding + first-two-child arrange；
  `UIScrollLayout` 管 axis/offset/step/max offset + first-child arrange；scroll 到边界必须
  返回未处理，以便 route bubble 到外层。tree dump 的 `layout.type` 统一输出
  `box/singleChild/split/scroll/overlay/sizeBox/canvas`。
- 布局 rect 尺寸永远 clamp ≥0（负尺寸会传染进 clip/scissor）。


