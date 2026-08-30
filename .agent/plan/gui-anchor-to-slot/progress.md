# GUI layout unified 进度

## 2026-08-30 — CP2 纠偏：authored size 从 child `_size` 迁到 parent-owned slot

- 用户纠偏：所有布局相关属于 slot。child `_size` 不再是 layout 输入；`computeDesiredSize` / `computeIntrinsicSize` 只报告内容（默认 intrinsic `{0,0}`，默认构造 100×50 不是内容尺寸）。
- 收口方式：
  - `resolveCanvasRect` 接收 slot 传入的 `authoredSize` / `autoAxis`；canvas arrange 仍走它，不重复已回滚的绕过；
  - attach 把 `_bAutoSize` 种成 canvas size mode Auto，且 AutoSize 时不把 authored size 种成 fixed/preferred；
  - overlay / single-child 补 `preferredSize`，`setSize` 桥接，measure/arrange 经 `resolveDesiredSize` 覆盖；
  - `attachToLayer` 对 AutoSize child 写 Auto size mode，不再把 `getSize()` 抄成 fixedSize。
- 未改：path-B `computeAnchorRect` 仍读 child 字段；text wrap 仍用 `_size.x` 做 maxWidth fallback；`_size`/`setSize`/`getSize` 字段仍在（CP2 全量删除）。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest`

## 2026-08-30 — CP4/CP5 大步：Popup 与 floating window 不再手写 child rect

- 审计结论：`UIPopupOverlay` 虽已创建 canvas slot，仍自己 `resolveChildRect` + 还有 `_contentPos` fallback；`UIDockFloatingWindow` 是 box host，resize handle 先被 box 打包再被手写覆盖。这两处是剩余的 widget 级 child `layoutAssigned`。
- 收口方式：
  - Popup 安装 `UICanvasLayout`；layoutAssigned 只把 `resolveContentSlotArgs()` 写进 content slot，arrange 交给 layout；删除非 canvas fallback；
  - Floating window 改为 overlay host：chrome box Fill，五条 handle 用 overlay Start/End+Fill；窗口几何仍由 host canvas slot 决定。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetTreeTest.PopupOverlayUsesACanvasSlotForItsContentChild:WidgetTreeTest.PopupOverlayContentExtentLivesOnTheCanvasSlot:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot:ToolControlsTest.MenuSizesPanelFromItemLabels:WidgetLayoutTest.FloatingWindowGeometryLivesOnTheHostCanvasSlot:WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots:WidgetTreeTest.FloatingWindowTabDragBehaviorStartsDockPanelSession`

## 2026-08-30 — CP4/CP5 大步：Dock 收成 typed host，installLayout 真正拥有 slot 工厂

- 审计结论：`installLayout()` 只接管 measure/arrange，`createSlotForChild()` 仍造 base `UISlot`，所以 Panel/TreeRoot 必须各自覆写工厂。`UIDockSpace` 手写把第一个 child 赋成 fill rect；`UIDockFloatingHost` 把整块 host rect 派给所有窗口，窗口再无视 assigned rect 用自己的 `_windowRect`。这是剩余最集中的 child-owned 几何。
- 收口方式：
  - `UIElement::createSlotForChild()` 在已安装 layout 时问它要 typed slot；TreeRoot / Panel 的重复工厂删除；
  - `UIDockSpace` 安装 `UISingleChildLayout`，投影根走 Fill single-child slot；
  - `UIDockFloatingHost` 安装 `UICanvasLayout`；`setWindowRect` 写 `setPosition`/`setSize`，attach 后尺寸在 host-owned canvas slot 上；`layoutAssigned` 消费 assigned rect；
  - Popup 上的 floating host 以 fill canvas edge 挂层，不再默认 100×50。
- 未改：floating window 内部 resize handle 仍在 box arrange 之后手写覆盖（overlay 化留到下一步）。Canvas arrange 仍走 `resolveCanvasRect()`，不重复已回滚的 slot-only 解析。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.DockSpaceArrangesProjectionThroughTheSingleChildSlot:WidgetLayoutTest.FloatingWindowGeometryLivesOnTheHostCanvasSlot:WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview:WidgetTreeTest.DockPanelPayloadCanMergeIntoFloatingWindowThroughBehaviorTarget:WidgetTreeTest.DockSpaceTabDragBehaviorStartsSessionAndTearsOffOnNoTarget:WidgetTreeTest.FloatingWindowTabDragBehaviorStartsDockPanelSession`

## 2026-08-30 — CP4/CP5 大步：attach 把 authored child 几何种到 typed slot

- 审计结论：DSL `.setSize()` / `.setPosition()` 发生在 attach 之前，所以 canvas/box 的 setter 桥接用不上。Editor/Workbench 里大量 `.setSize()` 仍然只写 child `_size`，CP2 删字段后会静默丢尺寸。逐个改调用点不是这一层该做的事；attach 才是唯一建 edge 的地方。
- 收口方式：
  - `setSize` / `setPosition` 打上 `_bAuthoredSize` / `_bAuthoredPosition`；默认构造几何不算 authored；
  - `insertChildEdge` 在 slot init 之前，把 authored size/position 种到 `UIBoxSlot::preferredSize` 或 `UICanvasSlot::fixedSize/offset`；
  - 随后的 layout spec / `FBoxSlotArgs` 覆盖种子。
- 未改：Editor/Workbench 调用点仍可写 `.setSize()`，但 attach 后尺寸已经在 edge 上。layer `attachToLayer()` 仍按显式 args 复制（含未 authored 的当前值），本批不改 layer 契约。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.AttachSeedsAuthoredChildSizeOntoTheBoxSlot:WidgetLayoutTest.AttachDoesNotSeedDefaultChildSizeOntoTheBoxSlot:WidgetLayoutTest.AttachSeedsAuthoredChildGeometryOntoTheCanvasSlot:WidgetLayoutTest.LayoutSpecPreferredSizeWinsOverAuthoredChildSize:WidgetLayoutTest.BoxSlotArgsPreferredSizeLivesOnTheEdge:WidgetLayoutTest.BoxHostSetSizeBridgesToTheBoxSlotPreferredSize:WidgetLayoutTest.CanvasHostSetSizeBridgesToTheCanvasSlotFixedSize:WidgetLayoutTest.BuildWithLayoutAttachmentInitializesTheBoxSlot`
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.SelectableRowArrangesLabelThroughTheSingleChildSlot:WidgetLayoutTest.CheckBoxArrangesLabelThroughTheSingleChildSlot:WidgetLayoutTest.UnifiedLayoutSpecAppliesToSelectableRowSingleChildSlot:DeclarativeContractTest.CompoundWidgetForwardsDesiredSizeAndLayoutToCompositionRoot:ToolControlsTest.SelectableRowWithLabelChildHoverStillHighlightsRow:EditorPropertyGraphTest.AutoPropertySectionMaterializesVec3RowsOnce`

## 2026-08-30 — CP4/CP5 大步：Compound/CheckBox 收成 single-child host，box preferredSize 进 FBoxSlotArgs

- 审计结论：`UICompoundWidget` 仍手写把第一个 child 赋成 fill rect；`UICheckBox` 用手写 contentRect 把 label 推到 box 右侧；inspector 行仍 `setSize()` 写 child。这些都不是 typed slot。
- 收口方式：
  - `UICompoundWidget` 持有 `UISingleChildLayout`，composition root 走 `UISingleChildSlot` Fill；
  - `UICheckBox` 同样收成 single-child host，左 padding = `_boxSize + _labelSpacing`；
  - `FBoxSlotArgs` / `ui::boxSlot()` 补 `preferredSize`，`applyLayoutSpecToSlot(box)` 把 `ui::layout().size()` 写进同一字段；
  - `EditorAutoPropertySection` / inspector 名称栏把行高/列宽写到 box preferredSize，不再 `child->setSize()`。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.CheckBoxArrangesLabelThroughTheSingleChildSlot:WidgetLayoutTest.BoxSlotArgsPreferredSizeLivesOnTheEdge:DeclarativeContractTest.CompoundWidgetForwardsDesiredSizeAndLayoutToCompositionRoot:EditorPropertyGraphTest.AutoPropertySectionMaterializesVec3RowsOnce`

## 2026-08-30 — CP4/CP5 大步：SelectableRow 收成 single-child host，box/popup 尺寸落到 edge

- 审计结论：`UISelectableRow` 仍不是 layout host，label 用 `setPosition` 做缩进；box child 的 `setSize()` 只写 `_size`；color palette 仍靠 child `_size` 撑 popup Auto 轴。这些都会在 CP2 删 `_size` 后静默坏掉。
- 收口方式：
  - `UISelectableRow` 按 Button 模型持有 `UISingleChildLayout`，label 走 `UISingleChildSlot`；缩进是 content padding，不是 child position。
  - builder 声明 `kSingleChildHostCaps`，并接受 `child(node, FSingleChildSlotArgs)`；`ui::build(tree, parent, spec >> widget)` 在 attach 时把 spec 写进 parent-owned slot。
  - `UIElement::setSize()` 在已挂 box host 时桥接到 `UIBoxSlot::preferredSize`（canvas 的 fixedSize 桥保持不变）。
  - `UIPopupOverlay` 上提 `_contentExtent`；基类 resolve 把它写成 canvas preferredSize；palette / Dialog / Menu 共用这一字段。
- 调用点：Workbench 行高写 box preferredSize、树缩进写 row padding；Editor 文件行用 `ui::layout().size({0,22}) >> selectableRow` + content padding；color palette 不再 `palette->setSize()`。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.SelectableRowArrangesLabelThroughTheSingleChildSlot:WidgetLayoutTest.BoxHostSetSizeBridgesToTheBoxSlotPreferredSize:WidgetLayoutTest.BuildWithLayoutAttachmentInitializesTheBoxSlot:WidgetLayoutTest.UnifiedLayoutSpecAppliesToSelectableRowSingleChildSlot:ToolControlsTest.SelectableRowWithLabelChildHoverStillHighlightsRow:WidgetTreeTest.PopupOverlayContentExtentLivesOnTheCanvasSlot:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot:ToolControlsTest.MenuSizesPanelFromItemLabels`

## 2026-08-30 — CP5 收口：Menu/MenuBar/Dialog 的行/面板尺寸改写 typed slot

- 审计结论：Menu 行、MenuBar 项、Dialog 面板已经挂在 typed host 下（box / popup canvas），但尺寸仍写 child `setSize()`，靠 `computeDesiredSize()` 回退 `_size`。这和 chrome 那批一样，会在 CP2 删 `_size` 后静默坏掉。
- 收口方式：
  - `UIMenu` 行在 attach 后把 `{rowWidth, itemHeight}` 写到 `UIBoxSlot::preferredSize`；
  - `UIMenuBar::addItem()` 同样把测量宽度写到 box slot；
  - `UIDialog` 像 Menu 一样持有 `_contentExtent`，由 `resolveContentSlotArgs()` 填 `preferredSize`，不再 `panel->setSize()`。
- 验证：
  - `python3 Script/ya.py test --target ya --filter ToolControlsTest.MenuSizesPanelFromItemLabels:ToolControlsTest.MenuBarHoverSwitchesOpenMenu:ToolControlsTest.MenuBarPaintsBottomSeparator:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot`

## 2026-08-30 — CP5 收口：chrome 固定高度与 workbench highlight 改写 canvas slot

- 审计结论：Workbench/Editor chrome 已经用 canvas edge 表达 fill/anchor，但固定高度仍写在 child `setSize()` 上，靠 `resolveCanvasRect` 回退 `_size` 才生效。这会让 CP2 删除 `_size` 后静默变成 0 高。Editor demo 的 ITEMS/PREVIEW 标题用 `setPosition` + 空 canvas slot，slot offset 实际是 0，位置意图并没落到 edge。
- 收口方式：
  - Workbench menubar / status / command strip / rail title / editor-demo toolbar 把固定尺寸写进 `ui::layout().size()`；
  - 删除 fill 路径上的 dead `setSize({0,0})` / 负尺寸残留；
  - ITEMS/PREVIEW 标题与 SelectionHighlight 改为 layout spec 初始化 slot；
  - `syncPresentationState()` 直接写 highlight 的 `UICanvasSlot` offset/fixedSize。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.StretchXFixedHeightChromeLivesOnTheCanvasSlot:WidgetLayoutTest.CanvasSlotOffsetAndFixedSizeCanBeUpdatedAfterAttach:WidgetLayoutTest.CanvasLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry:ToolControlsTest.MenuBarHoverSwitchesOpenMenu`

## 2026-08-30 — CP5 收口：root layer fill 改用 `attachToLayer(..., FCanvasSlotArgs)` 一步完成

- 目标：删掉 “`attachToLayer()` 后再 `attachLayout(fill)`” 的重复流程，让 layer-child 的 fill intent 在 attach 时就落到 layer-owned `UICanvasSlot`。
- 覆盖：
  - `EditorSurface::buildEditorChrome()`：`EditorRoot` 直接以 fill canvas edge 挂到 Content layer；
  - `FWorkbenchSurface::buildUI(tree)`：`WorkbenchRoot` 直接以 fill canvas edge 挂到 Content layer。
- 顺手收尾：补齐两个 `attachToLayer()` 返回值检查，避免 `[[nodiscard]]` 被静默丢弃：
  - `UIDockSpace::syncPreviewOverlay()`：attach preview overlay 时断言 attachment 有效；
  - `UIDesignerPanel::openDocument()`：attach preview root 时断言 attachment 有效。
- 既有故障修复（非本计划引入，但会拖慢 CP2 进入速度）：`UIDocument::instantiate` 在 fields 为 null 时不应让控件 `deserializeFields()` 直接抛 `type_error.307`；已在 `UIElement/UIPanel` 入口统一容错，恢复 `GameUIHostTest` 5 项失败用例。
- 验证：
  - `python3 Script/ya.py test --target ya --filter ToolControlsTest.MenuBarHoverSwitchesOpenMenu:ToolControlsTest.MenuSizesPanelFromItemLabels:WidgetTreeTest.SystemLayersStackAboveProjectContent:WidgetTreeTest.PopupOverlayUsesACanvasSlotForItsContentChild:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot`
  - `python3 Script/ya.py test --target ya --filter UIDesignerPanelTest.ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth:WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview`
  - `python3 Script/ya.py test --target ya --filter GameUIHostTest.*`

## 2026-08-29 — 已提交 8 个 checkpoint

### ✅ CP5 收口：`attachToLayer()` 支持显式 `FCanvasSlotArgs`，减少 layer-child legacy 几何直写（未提交）

- 审计结论：layer 既然已经是 canvas host，继续把 “layer child 的几何意图”先写进 child 再由 `attachToLayer()` 桥接到 slot，会让调用点出现多余的 authored geometry 写入与二次赋值流程；但在 `CP2` 未完成前，又必须维持 detach/reattach 的稳定语义。
- 收口方式：
  - 为 `WidgetTree::attachToLayer()` 新增 `attachToLayer(layer, widget, FCanvasSlotArgs)` 重载：直接用显式 args 初始化 layer-owned canvas slot；
  - 同步写回 child 的 legacy geometry 字段（anchor/position/size），保证 widget 脱离树时仍保留 authored 语义；
  - `updateTooltip()`、drag ghost、以及 `PopupOverlay::open()` 的 layer attach 改为使用新重载，验证“slot 真值 + legacy 同步”闭环。
- 结论：这是 `CP5` 的一条小收口，目的是让 layer-child edge 的意图表达更直接，同时不抢跑 `CP2` 的字段删除。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetTreeTest.AttachToLayerKeepsChildAbsoluteGeometrySemantics:WidgetTreeTest.LayerCanvasSlotTracksPositionUpdatesAfterAttach:WidgetTreeTest.DragGhostLabelUsesCanvasSlotInsteadOfChildZeroSize`
  - `python3 Script/ya.py test --target ya --filter WidgetTreeTest.ModalOverlayUsesModalRoutePolicyAndCanDetachDuringTarget:WidgetTreeTest.ModalOverlayConsumesDismissClickBeforeUnderlyingContent:WidgetTreeTest.PopupOverlayUsesACanvasSlotForItsContentChild:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot`

### ✅ CP3/CP4 收口：Declarative edge attach 改为 inline slot init，`[]` 退回纯 child 语法糖（未提交）

- 这一步先回答了一个架构分歧：是否应该让 widget 直接持有 slot？审计结论是否定的。slot 类型依赖 parent host，若把它挂回 child，会重新把 layout ownership 往 child 身上拉，和整条 `anchor-to-slot` 主线相冲突。
- 真正需要优化的是 declarative attach 的**实现形态**，不是 ownership：此前 `TUILayoutAttachment` 经 `operator[]` 进入 `applyLayout()` 后，会先 `addDetachedChild()`，再回头 `getSlotForChild()` 调 `applyLayoutSpecToSlot()`，导致 `[]` 看起来像“附着 + 二次赋值”的复合动作。
- 收口方式：
  - `UIElement` 新增统一的 child-slot 初始化入口；parent 在 `appendChildEdge/insertChildEdge` 创建 slot 时即可立刻初始化；
  - `BuilderBase` 的 `child(widget)`、`child(slotArgs)`、`operator[]` 全部改为共用这一条 attach helper；
  - `Build.h` 的 `ui::build(..., spec)` 与 `attachLayout(...)` 也改走同一套 slot initializer 入口；
  - `TUILayoutAttachment` 继续只保留为 builder 层的临时 `spec + child` 运输对象，不承载任何 runtime ownership 含义。
- 验证新增：
  - `WidgetLayoutTest.DeclarativeBracketAndAttachLayoutProduceEquivalentCanvasSlots`
  - `WidgetLayoutTest.BuildWithLayoutSpecInitializesTheCanvasSlot`
- 结论：这一步不是新功能，而是一次 **DSL/edge construction 收口**。`[]` 现在更接近“child 语法重载”，而不是一条独立 attach pipeline；runtime 仍保持 parent-owned slot 模型。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.DeclarativeBracketAndAttachLayoutProduceEquivalentCanvasSlots:WidgetLayoutTest.BuildWithLayoutSpecInitializesTheCanvasSlot:WidgetLayoutTest.CanvasLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry:WidgetLayoutTest.CanvasHostSetSizeBridgesToTheCanvasSlotFixedSize:WidgetLayoutTest.SameParentReorderPreservesExistingBoxSlotState`

### ✅ CP4 计划纠偏：grid/table capability 先收窄为 `cell-only`，不抢跑 unified 承诺（未提交）

- 本轮先审计 `Grid/Table` 是否存在“capability 已对外暴露，但 runtime 没兑现”的问题。结论比预想更收敛：`UITableGrid` 目前还没有 declarative builder，因此用户面并不存在可写 `ui::layout().align()/size()` 却被 runtime 静默丢弃的现成入口。
- 真正的偏差在于**声明口径**：`kGridHostCaps` 与对应静态断言此前仍把 `Align/Margin/Size/SizeMode` 算作 grid host 能力，但 `UITableSlot` / `UITableLayout` 运行时事实上只承载 `cell(row,col)`。
- 修正方式：
  - 将 `kGridHostCaps` 收窄为 `EUILayoutCap::Cell`；
  - 同步修正 `WidgetLayoutTest` 中关于 grid host capability 的静态断言，明确 `Align` 当前应被拒绝；
  - 在注释与 plan 中记录：后续若要开放 grid builder，必须先给 `UITableSlot` 建立对应的 runtime 数据与 arrange 语义，再放宽 capability。
- 结论：这是一次**方向纠偏**，不是功能扩张。相比“先把 grid capability 看起来补齐”，先让编译期声明重新等于 runtime 事实，能避免重演 scroll/popup 那类假统一。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.TextAutoSizeMeasuresGlyphWidth`

### ✅ CP4 纠偏：同父节点 reorder 改为移动原 slot edge，不再销毁重建（未提交）

- 阶段审计先对 `CP4` 剩余的 reparent/detach 边界做核查，发现一条真实偏差：`WidgetTree::reparentBefore/After()` 即使在 **同一个 parent** 下做 sibling reorder，也会先 `removeChildEdge()` 再 `insertChildEdge()`，导致 slot 被销毁并用默认值重建。
- 这与“slot 属于 parent-owned edge”的原则并不矛盾，反而是误读它的结果：跨父迁移时应重建 edge；但同父重排只是改变 children 顺序，原 edge 及其 `margin / grow / align / anchor ...` 状态都必须保留。
- 修正方式：
  - `reparentBefore/After()` 在 old parent == new parent 时改为直接移动 `_children` 与 `_childSlots` 中的同一条 edge；
  - `reparent(parent, child)` 在 same-parent 场景下也改为“移动到末尾但保留原 slot”，避免同类状态蒸发；
  - 新增 `WidgetLayoutTest.SameParentReorderPreservesExistingBoxSlotState`，直接断言 box slot 指针与 `sizeRule/weight/margin/crossAlignment` 在重排后保持不变。
- 结论：这是一次**CP4 runtime 纠偏**。它把“slot 是 edge-owned”落实为正确的生命周期规则，也为后续 canvas/overlay/table 等 typed slot 的 reorder 行为打下统一约束。
- 验证：
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.SameParentReorderPreservesExistingBoxSlotState:WidgetLayoutTest.BoxSlotsKeepEdgeStateLocalAcrossNestedReparent:WidgetLayoutTest.ReparentingBetweenHostsRebuildsTheSlotForTheNewHost:WidgetLayoutTest.ReparentingAcrossHostsDoesNotLeakTheOldHostIntent:WidgetTreeTest.ReparentAfterMovesSiblingForward:WidgetTreeTest.ReparentBeforeMovesSiblingBackward:WidgetTreeTest.ReparentAfterMovesIntoAnotherParentAtSiblingPosition`

### ✅ CP5 收口 + 计划纠偏：tree/layer 路径删去已失效的 child-owned zero-size 写入（未提交）

- 这一步先做了范围审计，而不是机械搜删 `setPosition/setSize`。审计结论：测试里多数 direct geometry write 仍在定义 absolute/layer-child 语义或夹具初始条件，不应为了“字面上消灭旧 API”去做假重构。
- 真正的误差集中在少数 **runtime 已改由 parent-owned slot 决定几何**、child 侧却还残留 `setSize({0,0})` 的点：
  - `UIMenu` 的 `MenuList` 已由 menu panel 的 canvas slot 决定 content rect；
  - `UIDialog` 的 `DialogStack` 已由 dialog panel 的 canvas slot 决定 fill/content rect；
  - `UIDockSpace` 的 `DockContent` 已由 body panel 的 canvas slot 表达 fill；
  - `WidgetTree` 的 tooltip / drag-ghost label 已由宿主 canvas slot 表达 fill(+inset)。
- 修正方式：
  - 删除上述 5 处 dead `setSize({0,0})`；
  - 新增 `WidgetTreeTest.DragGhostLabelUsesCanvasSlotInsteadOfChildZeroSize`，直接锁定 drag ghost label 的 slot 类型与非零布局结果，避免未来又回退成 child-authored 尺寸兜底。
- 结论：这是一次**收尾纠偏 + 计划口径修正**。`CP5` 不再追求“全仓看不到 direct geometry API”，而是优先清掉那些已经没有语义、只会混淆 layout owner 的残留写入。
- 验证：
  - `python3 Script/ya.py test --target ya --filter ToolControlsTest.MenuSizesPanelFromItemLabels:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot:WidgetTreeTest.DragOperationReachesTypedDropTarget:WidgetTreeTest.DragGhostLabelUsesCanvasSlotInsteadOfChildZeroSize:WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview`

### ✅ CP5 纠偏：`UIDesigner` 连续拖拽改为从 canvas slot 读取起始几何（未提交）

- 阶段审计先确认：测试里大量 `setPosition/setSize` 仍然多数是在定义绝对几何或 layer-child 语义，并非本轮最值得优先清理的 dead write；真正更危险的偏差在 `UIDesigner` 本身。
- 真实误差：此前 `UIDesignerPanel::applyDragDelta()` 在 canvas-host parent 下已经会把 resize 结果写进 `UICanvasSlot`，但下一次 `beginMove()/beginResize()` 仍从 child `getPosition()/getSize()` 取起始状态。连续拖拽时，session 起点可能落回陈旧字段，而不是 slot 真值。
- 修正方式：
  - 新增 `readCanvasIntent()`，在 parent 是 canvas host 时统一从 slot 读取 `offset/fixedSize/anchorMin/anchorMax`，否则才回退到 child 字段；
  - `beginMove()` 与 `beginResize()` 改为共享这一路径，保持 direct-manip session 的读写两端一致；
  - 新增 `UIDesignerPanelTest.ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth`，锁住“第二次 resize 从第一次 slot 结果继续累加”的回归。
- 结论：这是一次**明确纠偏**，不是新增功能。它把 Designer 的 direct manipulation 从“写 slot、读 child”修到同一真值源上，也进一步缩小了 `CP2` 前 legacy geometry fallback 的影响面。
- 验证：
  - `python3 Script/ya.py test --target ya --filter UIDesignerPanelTest.ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth`

### ✅ CP5 收口 + 计划纠偏：`PopupOverlay` 成为独立 full-screen host，并以 canvas slot 表达 content edge（未提交）

- 先做了阶段审计，再决定实现方向。计划原文写的是“PopupOverlay 单独定义 full-screen host 与 content slot，不假设等同于普通 Panel”。审计结论：**需要独立 host 语义，但不必新增一套平行 slot 类型**；更合理的是 popup 自己拥有 shield/full-screen contract，同时复用 `UICanvasSlot` 承载 content edge。
- 审计过程中先暴露出一条更根本的真实误差：此前 popup 作为 layer child 打开时，并没有显式把自己在 Popup layer 上设为 fill，导致 modal shield 的 full-screen contract 其实不稳定。新增测试后这条缺口被直接打红，随后再按 slot 方式收口。
- 现已实现：
  - `UIPopupOverlay` 为其 content child 创建正式 `UICanvasSlot`，不再手写 `child->layoutAssigned({pos, desired})`；
  - base popup 默认用 `_contentPos + Auto size` 解析 content rect；
  - `UIMenu` 覆盖为 `_contentPos + fixedSize(_contentExtent)`；
  - `UIDialog` 覆盖为 `anchor(0.5) + pivot(0.5) + Auto size`，以 slot 表达居中，而不是继续重写 `layoutAssigned()`；
  - `open()` 显式把 popup 自身在 Popup layer 上的 edge 设为 fill，锁住 shield 的 full-screen 语义。
- 结论：这一步既是 **CP5 的 runtime 收口**，也是一次**计划口径修正**。popup 不再被暗中当作 panel/path-B 特例，但也没有为了“看起来专用”而平行造一套新 slot。
- 验证：
  - `python3 Script/ya.py test --target ya --filter ToolControlsTest.MenuSizesPanelFromItemLabels:WidgetTreeTest.ModalOverlayUsesModalRoutePolicyAndCanDetachDuringTarget:WidgetTreeTest.ModalOverlayConsumesDismissClickBeforeUnderlyingContent:WidgetTreeTest.PopupOverlayUsesACanvasSlotForItsContentChild:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot`
  - `python3 Script/ya.py test --target ya --filter ToolControlsTest.MenuSizesPanelFromItemLabels:ToolControlsTest.MenuBarHoverSwitchesOpenMenu:ToolControlsTest.MenuBarPaintsBottomSeparator:ToolControlsTest.MenuSupportsIconsAndHoverOpenedSubmenus:ToolControlsTest.MenuSeparatorUsesDedicatedApiAndPaintsRule:ToolControlsTest.MenuKeyboardNavigatesSubmenuAndSkipsSeparators:ToolControlsTest.MenuDisabledItemsDoNotActivateAndAreSkippedByKeyboard:ToolControlsTest.MenuReservesColumnsForCheckmarkIconAndShortcut:WidgetTreeTest.ModalOverlayUsesModalRoutePolicyAndCanDetachDuringTarget:WidgetTreeTest.ModalOverlayConsumesDismissClickBeforeUnderlyingContent:WidgetTreeTest.PopupOverlayUsesACanvasSlotForItsContentChild:WidgetTreeTest.DialogCentresContentThroughThePopupCanvasSlot`

### ✅ CP5 纠偏：`attachToLayer()` 补齐 canvas edge 的 fixed-size 桥接（未提交）

- 阶段前误差检查先发现一条真实偏差：此前 `attachToLayer()` 在 layer 已升为 canvas host 后，只把 child 的 `anchor/position` 迁进 `UICanvasSlot`，却没有同步既有 `size`。这与“attach 时桥接 child geometry -> slot”的 checkpoint 目标不一致，也会让 tooltip / popup / drag ghost 这类“先设尺寸再挂 layer”的路径继续依赖 child-owned fallback。
- 纠偏方式：
  - `UICanvasSlot` / `FCanvasSlotArgs` 新增 `fixedSize`，`UICanvasLayout` 在非 stretch、非 Auto 轴上优先消费 slot 的 fixed size；
  - `UIElement::setSize()` 在 child 已挂 canvas host 下时改为桥接到 `UICanvasSlot::setFixedSize()`，不再只停留在 child 自身 geometry；
  - `WidgetTree::attachToLayer()` 现在会把 child 当前 `getSize()` 一并迁进 layer-owned canvas slot；
  - `UIDesignerPanel::applyDragDelta()` 在 parent 为 canvas host 时同步写入 slot fixed size，避免拖拽/缩放结果只落在 child fallback state。
- 结论：这一步属于**纠正已落地偏差**，不是新增能力。它把 layer/panel canvas edge 上的 fixed-size intent 收回到 parent-owned slot，减少了 `CP2` 前过渡桥对 child `_size` 的依赖面。
- 验证：
  - `python3 Script/ya.py cfg`
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest.CanvasLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry:WidgetLayoutTest.CanvasHostSetSizeBridgesToTheCanvasSlotFixedSize:WidgetTreeTest.AttachToLayerKeepsChildAbsoluteGeometrySemantics:WidgetTreeTest.LayerCanvasSlotTracksPositionUpdatesAfterAttach`

### ✅ CP4 纠偏：ScrollViewport 补齐 typed slot 闭环，移除一处“假统一”误差（未提交）

- 审计发现一条明确偏差：`UIScrollViewport` 已声明 single-child host 能力，builder 也接受 `FSingleChildSlotArgs` / `ui::layout().align(...) >> child`，但 `UIScrollLayout` 实际未创建 `UISingleChildSlot`，导致接口看似统一、runtime 却仍落回 base `UISlot`。
- 现已为 `UIScrollLayout` 补上 `createSlot()`，scroll content edge 与 Button / SizeBox / Split 一样正式落到 `UISingleChildSlot`。
- 新增验证 `WidgetLayoutTest.UnifiedLayoutSpecAppliesToScrollViewportSingleChildSlot`，同时断言：
  - scroll child 实际拿到 `UISingleChildSlot`；
  - unified `align()` intent 真正写入 slot；
  - scroll 只在 cross axis 应用对齐，不会错误影响 main axis 的滚动内容 extent。
- 结论：此前 `ScrollViewport` 的 unified child-layout 入口属于“假统一”；本轮已纠正为真正可兑现的 typed host。

### ✅ CP3 纠偏：删除过渡 public API（未提交）

- 删除 `ui::panelSlot()` / `FCanvasPanelSlotBuilder`。public DSL 只保留 `parent[ui::layout() >> widget]`，不再并存旧 panel-bound slot factory。
- 删除 `FCanvasPanelSlotArgs` / `UICanvasPanelSlot` 过渡别名；`Build.h`、`LayoutBuilders.h`、`UILayoutIntent.h`、`UIElement.h` 注释统一到 `FCanvasSlotArgs` / `UICanvasSlot`。
- 这是一次**纯纠偏**：不改变 Canvas 运行时语义，只消除与当前计划冲突的旧 public 入口和旧命名。
- 验证边界：
  - `rg` 全仓确认代码层不再残留 `panelSlot` / `FCanvasPanelSlot*` / `UICanvasPanelSlot` / `canvas-panel` 命名。
  - `python3 Script/ya.py test --target ya --filter WidgetLayoutTest` 证明 `ya-testing` 目标可成功构建并启动，但由于 filter 未命中，**本轮不宣称相关测试用例已执行**。

### ✅ CP4/CP5 纠偏：Box `layout().size()` 改为 slot-only（未提交）

- `applyLayoutSpecToSlot(box)` 不再通过 `child.setSize(spec.size)` 回写 child geometry。
- `FUILayoutSpec::size()` 现在映射到 `UIBoxSlot::preferredSize`；`minSize/maxSize` 也直接落到 `UIBoxSlot`，因此 Box host 的尺寸意图真正停留在 parent-owned slot 上。
- 这一步修正的是先前审计中明确记录的架构偏差：Box layout intent 之前仍借用 legacy child state 兑现。
- 新增 `WidgetLayoutTest.BoxLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry`，断言：
  - slot 记录 `preferredSize({0,120})`；
  - child 自身 `getSize()` 不被 layout spec 改写；
  - 最终布局高度仍按 slot 生效。
- 回归验证：`WidgetLayoutTest.PathAFillIsExpressedOnTheSlotNotTheChild` 仍通过。

### ✅ CP4 纠偏：unified `ui::layout()` 已打通 SingleChild / Overlay slot 消费（未提交）

- 审计发现一条真实偏差：capability 编译期约束已经覆盖 `single-child / split / overlay` 宿主，但 `applyLayoutSpecToSlot()` 此前只兑现了 `canvas / box / table`，因此 unified `ui::layout() >> widget` 虽能编译，部分 intent 仍可能在运行时静默丢失。
- 现已补齐 `UISingleChildSlot` 与 `UIOverlaySlot` 分支，并通过测试确认 `align()` intent 落到 typed slot。
- 新增验证：`WidgetLayoutTest.UnifiedLayoutSpecAppliesToSingleChildSlot`、`WidgetLayoutTest.UnifiedLayoutSpecAppliesToOverlaySlot`。
- 结论：当前 unified DSL 已经不只是“类型上统一”，而是确实能把 intent 落到 box / single-child / overlay 这些已声明支持的 slot 上。

### ✅ CP5 纠偏：TreeRoot 改为正式 canvas host，system layer fill 不再依赖 root 自身 `_anchor*`（未提交）

- 本轮先收口最核心、最集中的 legacy fill 主干：`WidgetTree` 内部 `TreeRoot` 不再靠 `_anchorMin/_anchorMax` 自己做全屏拉伸，而是安装 `UICanvasLayout`，并以 `UICanvasSlot` 为四个 system layer（Content/Popup/Tooltip/DragIme）表达 fill intent。
- `WidgetTree::layout()` 现在直接对 root 调用 `layoutAssigned({0,0, logicalExtent})`，不再让 root 先跑一遍 legacy `computeAnchorRect()`。
- 关键边界：这次只把 root -> layer 的 edge slot 化，layer 本身仍是纯传播节点。这样不会误伤 `attachToLayer()` 现有语义；layer 下的业务子节点仍暂时沿用 legacy self-positioned 路径，留待后续 CP5 全仓迁移。
- 审计中先发现一次真实代码误差：仅安装 layout 不够，root 若不重写 `createSlotForChild()` 仍会生成 base `UISlot`，导致 layer fill intent 无法落地。已修正为真正的 canvas-host root，并补测试锁住。
- 新增验证：
  - `WidgetTreeTest.TreeRootUsesCanvasSlotsToStretchSystemLayers`
  - `WidgetTreeTest.AttachToLayerKeepsChildAbsoluteGeometrySemantics`
  - `WidgetTreeTest.SystemLayersStackAboveProjectContent`

### ✅ 方向审计：暂不为 `attachToLayer()` 暴露统一 layout-spec 入口（未提交）

- 本轮尝试评估“先给 system layer attach 增加 spec 入口”是否能作为下一 checkpoint 的前置。审计结论是否定的：当前 layer 仍是 base propagation node，不是 typed layout host。
- 因此若现在先加 `attachToLayer(layer, widget, spec)`，只会制造新的假统一：API 看起来接受 `ui::layout()`，但 layer->child edge 仍无法稳定兑现 canvas/box intent。
- 该尝试已在代码层及时回撤，没有留下新的过渡 API。后续必须先决定 layer 本身的 typed host 形态，或定义一套不会伪装成 unified slot 的 system-layer attach 契约。

### ✅ CP5 纠偏：system layer 升为 canvas host，并在 attach 时桥接 child geometry -> slot（未提交）

- 在上一轮否决“先暴露 `attachToLayer(spec)`”后，本轮验证了另一条可行路径：让 layer 本身成为 canvas host，但**不改变对外 attach API**，而是在 `attachToLayer()` 内部把 child 当前 `_anchorMin/_anchorMax/_position` 迁入 layer-owned `UICanvasSlot`。
- 同时补上运行时桥接：当 child 已挂在 canvas host 下时，`UIElement::setPosition()` 优先写 slot offset，而不是继续只停留在 child 自身 geometry。这样 drag ghost / tooltip 这类 attach 后持续移动的位置语义不会断。
- 这不是最终形态；它仍是 CP2 前的过渡桥。但它把 layer path 的真实布局 owner 从 child 挪到了 slot，并且保持现有 `attachToLayer()` 调用面不炸。
- 新增验证：
  - `WidgetTreeTest.AttachToLayerKeepsChildAbsoluteGeometrySemantics`（扩充为同时断言 layer slot）
  - `WidgetTreeTest.LayerCanvasSlotTracksPositionUpdatesAfterAttach`
  - `WidgetTreeTest.SystemLayersStackAboveProjectContent`
  - `WidgetTreeTest.SystemLayersOwnHoverBeforeLowerLayers`

### ✅ CP5 纠偏：Workbench shell 删除无效 child-authored geometry，改回 parent-owned edge intent（未提交）

- 审计确认 Workbench shell 中有一批 geometry 写入是**确定无效或重复**的：
  - `WorkbenchRoot` 挂在 Content layer 下时，继续写 `_anchorMin/_anchorMax` 只是重复系统层 attach 的 slot 语义；
  - `FeatureRail` / `DemoContentFrame` 挂在 `UISplitPane` 下时，split 负责 pane rect，child 的 anchors / position / `{0,0}` size 并不决定布局；
  - `RowList` 挂在 `UIScrollViewport` 下时，scroll 的 first-child arrange 也不会消费 child authored anchors。
- 这批写入现已删除，并给 `WorkbenchRoot` 的 parent edge 显式补上 `layout().fill()`，使 shell root 的 fill 语义回到 parent-owned slot，而不是继续依赖 child-owned anchors。
- 本轮**没有**删除仍承担 intrinsic/preferred-size 语义的 `setSize()`，例如 `FeatureRailTitle`、`DemoHost`、row label 等；这些仍属于下一阶段的 size-intent 收口，不在本次“无效位置/锚点写入”清理范围。
- 验证：
  - `ToolControlsTest.StackLaysOutChildrenWithGapAndPadding`
  - `ToolControlsTest.SpecializedLayoutsAppearInTreeDump`
  - `WidgetLayoutTest.CanvasHostIsNotBoundToThePanelVisuals`

### ✅ CP5 纠偏：Dock preview overlay 删除 dead child-authored layout（未提交）

- `UIDockSpace::_previewOverlay` 挂在 `DragIme` system layer 下后，先前继续写 `_anchorMin/_anchorMax`、`setPosition({0,0})`、`setSize({0,0})` 只是重复 layer attach 的 slot 语义；preview 的真实几何来自 owner 在 paint 阶段使用全局 rect 作图，而不是 overlay 自己解析 child-authored geometry。
- 本轮删除这 4 处 dead write，不改变 drop preview 的绘制/交互逻辑。
- 测试从错误假设中纠偏：这里要锁的是“overlay 已是 layer-owned canvas slot child”，而不是强行要求它必须是 fill/stretch slot。
- 验证：
  - `WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview`
  - `WidgetTreeTest.SystemLayersStackAboveProjectContent`

### ✅ CP1 冻结新布局协议 — `[gui/layout] add UIConstraints / EWidgetSizeMode / UILayoutIntent protocol`

- 新增 `Runtime/Layout/UILayoutIntent.h`：`EWidgetSizeMode`（Fixed/Auto）、`UIConstraints`（measure 可用空间 min/max）、`UILayoutIntent`（合并 slot+path 的父→子 intent）。
- 仅类型定义，未接调用点。

### ✅ CP3 运行时：Canvas 成为一等 layout host — `[gui/layout] remove child-authored stretch anchors; migrate to canvas slot` + `[gui/layout] make Canvas a first-class layout host`

- 删除 `fillParent/setAnchors/fillWidth/fillHeight`（8 个重载），DSL 层面子节点不再能自写拉伸锚点。
- `UICanvasSlot` + `FCanvasSlotArgs`（原 Panel 绑定命名已改），`UISlot::as<T>()`。
- 新增 `UICanvasLayout`（createSlot / measure / arrange）。
- `UIPanel` 成为 Canvas layout host（layout / layoutAssigned / computeDesiredSize / createSlotForChild），镜像 UIContainer 模式。
- `UIElement::resolveCanvasRect()` 成为单一锚点+尺寸解析契约，`computeAnchorRect` 委托它，两条路径不会漂移。
- 基础 `UILayout::createSlot` 还原为 base slot：canvas 边由安装该 layout 的 host 提供。
- **有意偏离**：canvas slot 默认 `{0,0}-{0,0}`（历史绝对行为），`.fill()` 才拉伸——否则既往未授权锚点的 panel 子节点会被静默改成 fill。

### ✅ CP3 public DSL：`ui::layout()` + `parent[spec >> widget]` — `[gui/declarative] add unified ui::layout() spec and parent[spec >> widget] DSL` + `[gui/declarative] migrate all call sites to parent[ui::layout().fill() >> widget]`

- 新增 `LayoutSpec.h`：`FUILayoutSpec`（fill/grow/anchor/cell/align/margin/offsets/size/sizeMode capability + 能力掩码）与共享宿主解析 `applyLayoutSpecToSlot()`（canvas / box / table）。
- `ui::layout()` 构造器、`operator>>` 绑定（支持 builder 与 shared_ptr 两种 child 形态）、parent `operator[]` 收集。
- `ui::build/buildAs` 增加 `FUILayoutSpec` 重载；新增 `ui::attachLayout()`。
- 调用点全量迁移：EditorSurface / EditorInspectorTab / WorkbenchSurface / WorkbenchDemoPages / Menu.cpp。
- 测试：两个 `fillWidth()` 陷阱测试改写为边意图语义；契约测试改为断言 slot；late-child 放置改到边上表达。

### 验证（本轮）

- 构建：`ya-gui-framework`、`ya-game-editor`、`GUIWorkbench` 全通过。
- 测试：`ya-gui-closure` 260、`ya-gui-widgets` 223、`ya-gui-headless-host` 2、`ya-gui-workbench-workspace` 8 —— 全通过。

### ✅ CP5 部分：`UIElement` layout-host 钩子 + panel 子节点 / designer 回归修复

- `UIElement::installLayout()/getLayout()`：任何元素都可安装 layout；`layout()`/`layoutAssigned()`/`computeDesiredSize()` 有 layout 时走它，否则保持 legacy 自定位。**注意：这只是把一部分宿主迁到统一 hook，尚未消除 legacy self-positioned fallback；不能视为运行期二分已消失。**
- `UIPanel` 改为通过共享钩子安装 `UICanvasLayout`，不再私有持有成员 + 重写三个布局入口。
- **发现并修复回归**：`UIPanel` 成为 canvas host 后，子节点自写锚点被静默忽略。已迁移全部此类内部点：
  - `WorkbenchSurface`：menuBar / workspaceSplit / pageRailTitle / pageRailCard / demoHost / statusText / commandResultText（父为 root / featureRail / contentFrame 等 panel）
  - `DockSpace`：dock leaf content 填充 body panel
  - `Dialog`：dialog stack 填充 dialog panel
  - `WidgetTree`：tooltip label（fill+inset）与 drag ghost label（fill）
  - `UIDesignerPanel`：拖拽读/写锚点改走 canvas slot（否则设计器对 panel 子节点的拖拽结果全丢失）
- 父节点不是 layout host 的位置（tree root / layers / split pane）保持 legacy 自定位路径不变。

### ✅ CP3 完成：Canvas slot 能力补全 — `[gui/layout] complete Canvas slot: four-edge offsets, size mode and alignment`

- `UICanvasSlot` / `FCanvasSlotArgs`：新增四边 `FMargin` offsets（单边设置即令该轴拉伸，"四边"= fill 减 insets）、alignment H/V、per-axis size mode（Fixed/Auto），各自触发 invalidateMeasure 或 invalidateArrange。
- `UICanvasLayout::resolveChildRect()`：尺寸按轴解析（Auto→测量、拉伸→anchor span 减 insets、否则保留作者化尺寸），再在可用区域内对齐。**非拉伸轴保留父区域**，否则固定尺寸子节点没有可对齐的空间。
- `EWidgetSizeMode` 下沉到 `UILayout.h`（`UILayoutIntent.h` 依赖 `UILayout.h`，方向必须如此），slot 复用同一枚举。
- `ui::layout()` 暴露 `offsets(l,t,r,b)` / `widthSizeMode()` / `heightSizeMode()`。
- 新增 3 个测试（四边 insets → fill 减 insets；Auto 宽度 + 拉伸高度；对齐放置固定尺寸子节点）。

### ✅ CP3/§3.4 完成：capability 编译期隔离 — `[gui/declarative] reject unsupported layout intent at compile time`

**这是本计划的核心保证**：布局意图现在携带在**类型**里，宿主无法兑现的意图在编译期被拒绝，而不是静默丢弃。

- `LayoutSpec.h`：每类宿主的能力集合（box / canvas / grid / single-child / split / overlay）、`LayoutCapsCompatible` concept、`allowedLayoutCaps<T>()`（未迁移的 builder 默认宽松）。
- `SlotBuilders.h`：`FUILayoutSpecBuilder<Caps>` —— 每个 modifier 为 `&&` 限定并返回"能力加宽"后的 builder 类型，因此类型携带全部已应用能力的并集。
- `BuilderBase.h`：`TUILayoutAttachment<Caps, TChild>` + 宿主约束的 `operator[]`。
- `LayoutBuilders.h`：各宿主声明 `kAllowedLayoutCaps`。
- box 宿主现在真正应用 `size()`（此前是静默 no-op），保证没有能力被空转。
- **抓出并修复一个潜在误用**：`WorkbenchDemoPages` 的拖拽区挂在 column（box 宿主）上却指定 `anchor(...)` —— box 宿主从不支持 anchor，该意图此前就是死代码（静默失效）。已改为 `size()`（box 宿主确实实现）。这正是新检查要拦截的情形。
- 测试：覆盖每对 accept/reject、builder 类型的能力并集、以及两个真实宿主类型的 `static_assert`。

### ✅ CP3 收尾：pivot / preferred size / `ui::canvas()` 宿主 — `[gui/layout] finish Canvas: pivot, preferred size and a ui::canvas() host`

- `UICanvasSlot` / `FCanvasSlotArgs`：新增 `pivot`（子节点的哪个点落在解析出的位置上，归一化子空间）与 `preferredSize`（Auto 轴请求的尺寸；零分量回退到测量值）。
- `resolveChildRect()`：Auto 轴优先用 preferredSize；最终位置再减去 `pivot * size`，因此**无需事先知道子节点尺寸**即可让它居中/右/下对齐。
- `Pivot` 成为新的 capability，仅 canvas 宿主接受。
- `ui::layout()` 新增 `pivot()` / `preferredSize()`。
- **`ui::canvas()`**：不绑定 Panel 视觉的 canvas 宿主（无背景/圆角，`styleKey="canvas"`）。**Canvas 现在是一个独立的 layout 类型**，不再只是 Panel 的行为。
- 顺带修正仍引用已删除 `ui::panelSlot()` 的注释。
- 新增 3 个测试（pivot 居中、preferredSize 驱动 Auto 轴、canvas 宿主几何）。

**纠偏说明（已完成）：CP3 的 public 过渡 API（`ui::panelSlot()` / `FCanvasPanelSlotBuilder` / 旧 canvas-panel 命名）已删除；剩余偏差不在 DSL public API，而在 CP2/CP4 的 runtime geometry fallback。**

### ✅ CP6 部分：序列化侧旧锚点清理 — `[gui/serialize] drop the last authored-anchor reader from serialization`

- 核查范围：schema / 文档 / dump / 脚本绑定。结论：**`serializeFields()` 早已不再输出锚点**（反射中已移除），唯一残留的 JSON 锚点输出是新 slot 的 `appendRuntimeDiagnostics`（属于新 schema，保留）。
- `SceneWidgetEntry`：拖放时"双方均为 point-anchored"才做父相对位置校正的判定 —— 锚点不可作者化后该条件**恒真**（`instantiate()` 后锚点恒为运行时默认 `{0,0}`）。移除死条件，行为不变。
- 删除过期保存文档 `Engine/Saved/GUIWorkbench/smoke.yaui`（含 2 处旧 `_anchorMin`，无任何引用；plan 明确旧文档不做兼容读取，下次运行会重新生成）。
- `_bAutoSize` 仍广泛使用（SizeToContent 机制，仍是合法运行时状态），**保留**。

### ⚠️ 发现既有故障（非本次改动引入，需用户处理）

`ya-testing` 中 5 个 `GameUIHostTest` 失败，全部同一根因：

```
C++ exception: [json.exception.type_error.307] cannot use erase() with null
  - ActivateMountsAutoMountEntriesByZOrder
  - BuildSnapshotComposesMountedWidgets
  - ControllerReplacementPerformsHandover
  - PieRestartDoesNotAccumulateWidgets
  - SceneSwitchUnmountsPreviousAndMountsNext
```

根因：`UIDocument::instantiate()` 无条件调用 `root->deserializeFields(fields)`，而 `UIDocument::fields` 默认构造为 **null**；`UIPanel::deserializeFields` 里的 `rest.erase("_bExplicitFill")` 对 null json 会抛 `type_error.307`。

**证据表明这是既有问题，非本次改动引入**：
1. `UIPanel::deserializeFields` 的未加保护 `erase` 与我动手前 `fd631393^` **逐字节相同**（`git show` 验证）。
2. `UIDocument::instantiate` 同样未变。
3. `GameUIHost*`、`GameUIHostTest.cpp` 均不在我改动的 31 个文件内。
4. 抛出点代码我从未触碰。

最小修复是给 `UIPanel::deserializeFields` 加 null 保护（`if (!rest.is_object()) rest = nlohmann::json::object();`），或在 `UIDocument::instantiate` 侧跳过空 fields。**因属计划外，未擅自修改**，等你决定。

## 未完成（相对当前 plan 的差距）

- **CP2 未做**：`_anchorMin/_anchorMax/_position/_size/_minSize/_maxSize/_bAutoSize`、`setPosition/setSize/getPosition/getSize`、`computeAnchorRect`、`reportStretchAnchorsIgnored` 仍在 UIElement 上。
  - 现状：anchor 已从反射移除并降为 runtime-only；几何字段仍是 legacy fallback 的运行时 I/O，大量非 DSL 代码（DockSpace / Dialog / WidgetTree / 测试）仍直写它们。
- **CP3 已完成**：`UICanvasSlot` 已具备 anchor / offsets / alignment / size mode / pivot / preferred size / min-max；`ui::canvas()` 宿主 DSL 已建；旧 panel-bound public slot API 与旧 canvas-panel 命名已清理。
- **§3.4 已做**：capability 编译期隔离。plan 要求的 `column[anchor(...) >> w]` 现在确实编译失败。
- **CP4 未闭环**：Box / Overlay / SingleChild 的 unified-spec 消费现已验证；但 Split / Scroll / Grid 的 spec 覆盖面与所有 host 的“纯 slot arrange”收口仍未整体关账，reparent/detach 的 slot 重建也还缺更系统的验收。
- 已修正：同父节点 `reparentBefore/After/reparent(parent, child)` 不再销毁并重建 slot；same-parent reorder 现会保留原 edge state。
- 已修正：`applyLayoutSpecToSlot(box)` 不再通过 `child.setSize(spec.size)` 回写 child geometry；Box 的 `size/min/max` 已改走 slot。
- **CP5 未完**：虽然 root/layer/popup/designer 以及一批 dead write 已完成收口，但 `attachToLayer()` 下的业务子节点仍保留 legacy geometry bridge，`UIElement` 上的 authored geometry 仍是运行期过渡输入。
  - 审计纠偏：大量测试 `setPosition/setSize` 目前仍是合法夹具写法，不应再把“测试直写清零”作为独立 checkpoint 目标；后续应只在这些调用变成语义噪声时再顺手移除。
- **CP6 未做**：旧 JSON 字段 / schema / UIDocument / Designer inspector / 快照 dump 未清理。
- **CP7 未完**：编译期断言、几何测试（四边 offsets / alignment / min-max / reparent）、snapshot parity 未补。

## 仍存在的过渡物（plan 明确要求删除，尚未删）

- `child(node)` 默认重载（plan：只能是新布局系统的 default slot，不是 legacy 兼容）。
