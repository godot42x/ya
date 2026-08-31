# GUI layout unified 进度

## 2026-08-30 — CP3/CP5 纠偏 checkpoint：Canvas Auto 优先级、Popup content slot 与 detached pending bridge

- 误差审计确认：Canvas Auto 轴若回读 `fixedSize`，会让 Auto 意图被旧 authored 尺寸遮蔽；Popup 测试若把内容尺寸写到 child slot，也会越过 popup-owned content contract。
- 收口方式：Auto 轴只解析 slot `preferredSize` 或 child desired/intrinsic；PopupOverlay 统一由 `_contentExtent` 写入 popup-owned `UICanvasSlot`；detached `setPosition/setSize` 只排队 parent-edge initializer，attach 时消费 typed slot，不新增 child geometry 写入。
- 首版边界：本 checkpoint 不引入任何 legacy 兼容语义；旧字段仍只是待删除 shadow，不能作为新的布局输入。
- 验证：`xmake b GUIWorkbench`；`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`（315/315 通过）。
- 未完成：no-arg `attachToLayer`、UIElement geometry 字段/API 与其他 pending bridge 仍需在后续 CP 中删除；本 checkpoint 不宣称 CP2/最终兼容清零完成。

## 2026-08-30 — CP5 生产清理：Workbench demo 删除 child `_bAutoSize` 写入

- 误差审计：Workbench demo 仍直接设置 `UIText/UIButton::_bAutoSize`，与“SizeToContent 由 parent-owned slot size mode 决定”的契约冲突。
- 收口方式：删除 4 处 widget-owned AutoSize 写入；文本由 intrinsic/desired measurement 提供尺寸，按钮由其 box/single-child slot 的默认 Auto/Fill 规则决定尺寸。
- 验证：`xmake b GUIWorkbench`；`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`（315/315 通过）。
- 未完成：WidgetTree no-arg layer attach 与 UIElement geometry API 仍是待删除过渡物。

## 2026-08-30 — CP5 纠偏 checkpoint：no-arg layer attach 停止推断 child geometry

- 误差审计：`attachToLayer(layer, widget)` 仍从 `_anchorMin/_anchorMax/_position/_size/_bAutoSize` 反推 canvas slot，形成运行时 legacy self-positioning 入口。
- 收口方式：no-arg attach 现在只创建 layer 的 default canvas slot，不读取 child geometry；需要 placement 的调用点改为显式 `FCanvasSlotArgs` 或已排队的 typed-slot initializer。显式 args 仍在 attach 时写入 parent-owned slot。
- 测试夹具同步：Auto/Fill、固定尺寸、Dock full-screen、snapshot 与 document reload 用例改为显式 slot intent；不再以 child `_bAutoSize` 或直接 anchor 字段驱动 attach。
- 验证：目标组合 73/73 通过（含全部 `WidgetLayoutTest`、Dock drag、UIDocument style reload、snapshot theme/repaint）。
- 未完成：no-arg overload 本身、UIElement geometry 字段/API、剩余 detached bridge 仍需最终删除；本 checkpoint 不宣称 legacy 清零。

## 2026-08-30 — CP4/CP5 基础设施 checkpoint：UIElement O(1) slot 回指与 kind-tag cast

- 误差审计：child `getSlot()` 每次都通过 parent 扫描 `_childSlots`；布局/DSL 代码也重复使用裸 `dynamic_cast<UISlot*>`。
- 收口方式：UIElement 新增 non-owning `_slot` 回指，插入后绑定、remove/detach/tree teardown 前清空、跨父 reparent 后重绑；same-parent reorder 只移动 slot ownership，不重建 edge。
- 类型识别：UISlot 子类增加稳定 `ESlotKind`，`UISlot::as<T>()` 改为 checked kind + static cast；布局/声明式/控件 slot 热路径改用该接口，widget RTTI 暂不混入。
- 同步纠偏：no-arg layer attach 只创建 default canvas edge，不再读取 child geometry；相关测试夹具改为显式 slot intent。
- 验证：`xmake b ya-gui-closure-test`；`xmake r ya-gui-closure-test`（315/315 通过）。
- 未完成：仍有少量 layout 类型/Widget 类型 RTTI，以及 no-arg API、UIElement geometry 字段/API 和 pending bridge 待后续完整 checkpoint 删除。

## 2026-08-30 — 方向纠偏：移除 `ESlotKind` 与 `UISingleChildSlot`

- 用户审计指出：engine-owned `ESlotKind` 把新增 slot 类型绑定到核心枚举，用户扩展必须修改底层；`UISingleChildSlot` 与 `UIOverlaySlot` 的 edge 数据完全重叠，没有独立语义。
- 收口方式：删除 `ESlotKind` 及所有 `kSlotKind/getKind`；`UISlot::as<T>()` 恢复开放的 RTTI checked cast。删除 `UISingleChildSlot` / `FSingleChildSlotArgs`，single-child layout 统一创建并消费 `UIOverlaySlot`，声明式 builder 改用 `overlaySlot()` / `FOverlaySlotArgs`。
- 序列化与测试同步：single-child edge 统一按 overlay slot 写入/恢复；保留 `UISingleChildLayout` 作为父级 measure/arrange 策略，而不是 slot 类型。
- 验证：`xmake b ya-gui-closure-test`；`xmake r ya-gui-closure-test`（315/315 通过）。
- 未完成：UIElement authored geometry 字段/API、no-arg attach、pending bridge 仍待后续最终删除；本轮不引入任何 legacy 兼容。

## 2026-08-30 — CP2/CP5 API 收口：移除 UIElement 几何 setter 转发层

- 误差审计确认：`UIElement::setPosition/setSize` 虽已把值转发到 slot，但仍让调用方误以为 child 拥有布局；这与“调用方显式取得 slot、直接修改 edge”不一致。
- 收口方式：删除 UIElement 的 `setPosition/setSize`；GUI 测试夹具改用显式 edge initializer，Workbench/GUIFrameworkSmoke 使用 pending slot initializer，HelloMaterial 通过 `GameUIHost::addToWorld(..., FCanvasSlotArgs)` 直接表达 root edge。
- Game UI API：增加带 `FCanvasSlotArgs` 的 `addToWorld` 重载，默认 controller 直接挂载到 content layer 的 canvas edge；旧无参入口仍只代表 default fill attach，不再承载 child geometry 推断。
- 验证：`xmake b ya-gui-closure-test`、`xmake r ya-gui-closure-test`（315/315）；`xmake b GUIWorkbench`；`xmake b ya-gui-minimal-host` 均通过。
- 未完成：`UIElement::getPosition/getSize` 与 authored geometry 字段仍待下一完整 checkpoint 删除；本轮不引入 geometry setter 兼容入口。

## 2026-08-30 — CP2 geometry shadow 收口：删除 UIElement size/position getter 与字段

- 误差审计确认：`_position/_size/_bAuthoredPosition/_bAuthoredSize` 已不再参与布局，只是旧 authored shadow；继续保留会形成第二套几何真值。
- 收口方式：删除上述字段及 `getPosition/getSize/hasAuthoredPosition/hasAuthoredSize`；布局输出使用 `_layoutRect`，跨模块读取通过 `getLayoutRect()`；UILayout measure 不再依赖 child geometry。
- 测试/生产迁移：Smoke 使用显式 root `FCanvasSlotArgs`，EditorSurface root fill 不再读取 widget geometry；旧 child geometry 断言改为 slot/layout rect 断言，SceneWidgetEntry 不再接受 `_position` override。
- 验证：`xmake b/r ya-gui-closure-test`（315/315）、`xmake b GUIWorkbench`、`xmake b ya-gui-minimal-host`、`xmake b ya-runtime` 均通过。
- 未完成：`_anchorMin/_anchorMax/_bAutoSize`、no-arg attach、pending bridge 与旧 schema 负向审计仍待后续 checkpoint。

## 2026-08-30 — CP2 SizeToContent shadow 收口：删除 UIElement `_bAutoSize`

- 误差审计确认：`isAutoSizeActive()` 已完全从 parent-owned slot 推导，`_bAutoSize` 只剩测试/注释中的旧 shadow，不应继续存在。
- 收口方式：删除 UIElement `_bAutoSize`，更新 Text/CheckBox 的契约注释与测试；Auto/Fill/desired 行为全部由 Canvas/Box/Overlay slot size mode/alignment 表达。
- 验证：`xmake b/r ya-gui-closure-test`（315/315）、`xmake b GUIWorkbench`、`xmake b ya-gui-minimal-host`、`xmake b ya-runtime` 均通过。
- 未完成：`_anchorMin/_anchorMax`、no-arg attach、pending bridge 与旧 schema 负向审计仍待后续 checkpoint。

## 2026-08-30 — CP2 anchor shadow 收口：删除 UIElement `_anchorMin/_anchorMax`

- 误差审计确认：生产布局已经只读取 `UICanvasSlot` anchor；剩余 child anchor 写入全部是测试夹具，属于旧 shadow。
- 收口方式：新增显式 `authorSlotAnchors` 测试 helper，迁移 dock/toolbar/bar 测试到 pending canvas-slot initializer，删除 UIElement anchor 字段及相关旧注释。
- 验证：`xmake b/r ya-gui-closure-test`（315/315）、`xmake b GUIWorkbench`、`xmake b ya-gui-minimal-host`、`xmake b ya-runtime` 均通过。
- 未完成：`computeAnchorRect`/self-positioned fallback、no-arg attach、pending bridge 与旧 schema 负向审计仍待后续 checkpoint。

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
## 2026-08-31 — CP2/CP5 root attach 收口：删除 no-arg `attachToLayer`

- 误差审计确认：无参 `attachToLayer(layer, widget)` 已不再承担 geometry 推断，只是 `attach(*getLayer(layer), widget)` 的隐式糖衣；继续保留会违反“root/layer attach 必须显式选择 parent edge”的首版契约。
- 收口方式：所有测试和默认 controller 调用改为显式 `attach(*tree.getLayer(layer), widget)`；新增通用 `attach(parent, widget, FCanvasSlotArgs)`，显式 edge 参数不再绑定到 layer API。系统层的显式 args wrapper 暂时保留，作为 layer 命名空间便捷入口。
- 验证：`xmake b/r ya-gui-closure-test`（315/315）、`xmake b GUIWorkbench`、`xmake b ya-gui-minimal-host`、`xmake b ya-runtime` 均通过。
- 未完成：pending slot initializer、builder `.child()` 默认重载、旧 schema 负向清零与最终 self-positioned fallback 审计仍待后续 checkpoint。

## 2026-08-31 — 方向纠偏：builder pending bridge 不能局部删除

- 误差审计确认：尝试把 pending initializer 从 `UIElement` 局部移入 builder 会破坏大量现有 `builder.release()` 后再 `tree.attach()` 的调用点，导致 builder 几何意图丢失并触发 19 个布局测试回归。
- 该方向已回退，未保留错误的局部迁移；`xmake b/r ya-gui-closure-test` 恢复为 **315/315**。
- 正确方向：先批量迁移 release→外部 attach 的调用点到 `ui::build` / `parent.child(builder)` / 显式 slot attach，形成全量 materialization 闭环，再删除 `UIElement::_pendingSlotInitializer`。
- floating window 不受此问题影响，已独立通过 `onAttached()` 将 `_windowRect` 写入 host-owned `UICanvasSlot`。

## 2026-08-31 — CP2 示例迁移：GUIFrameworkSmoke 删除 pending slot helper

- 误差审计确认：GUIFrameworkSmoke 的父节点关系在 `buildDemoContent()` 内全部已知，不需要把几何意图暂存到 child。
- 删除 `setPendingSlotPosition/setPendingSlotSize`，新增局部 `attachCanvasChild()`：先显式 attach，再通过 parent `initializeChildSlot()` 配置 `UICanvasSlot`。
- panel 根节点继续使用显式 `attachToLayer(..., FCanvasSlotArgs)`；标题、计数器、按钮及按钮文字均由各自 parent edge 配置。
- 验证：`xmake b ya-gui-minimal-host` 通过。
- 未完成：GUIWorkbench 与测试 helper 的 pending bridge、builder release 外部 attach 全量迁移，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 Workbench render demo edge intent 迁移

- 误差审计确认：`buildRenderDemo()` 中 form、marker row、marker cell、image 的 parent 关系在构造时已确定，继续调用 pending helper 只是在延迟写入已知 parent edge。
- marker row / cell / image 改为 attach 后调用 `ui::attachLayout(..., ui::layout().size(...))`，尺寸直接落到 `UIBoxSlot`；form 的零尺寸 pending 写入删除。
- 未触及仍被多个页面共享的 `makeDemoButton()` 与动态 drag/input 控件 helper，避免把不完整的局部迁移伪装成全量完成。
- 验证：`xmake b GUIWorkbench` 通过。
- 未完成：GUIWorkbench 其余 pending helper、测试 helper、builder release 外部 attach 全量迁移，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 Workbench modal edges 迁移

- 误差审计确认：modal 的 dialog、stack、nameField parent 关系均在同一构造闭包内确定；dialog canvas size 与 nameField box preferred size 不需要 child pending 状态。
- dialog 改为 `overlay->addDetachedChild(dialog, initializer)`，nameField 改为 `stack->addDetachedChild(nameField, initializer)`；stack 本身继续由 dialog `attachLayout(...fill())` 管理。
- 验证：`xmake b GUIWorkbench` 通过。
- 未完成：`makeDemoButton()` 及其余动态控件 helper、测试 helper、builder release 外部 attach 全量迁移，最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 Workbench drag/drop edges 迁移

- 误差审计确认：drag source row 与 drop zone 都在同一 DSL 构造段中拥有确定 parent，source tile 不应先把尺寸暂存到 child。
- source tile 改为 `sourceRow[ui::layout().size(...) >> item]`；drop zone 删除 pending helper，继续由 column 的显式 size spec 管理。
- 验证：`xmake b GUIWorkbench` 通过。
- 未完成：共享按钮工厂、动态输入控件 helper、测试 helper、builder release 外部 attach 全量迁移，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 child() 直接消费 placed-child attachment

- 误差审计确认：输入控件的 parent 在 row builder 内已确定，继续让裸 shared_ptr 依赖 child pending size 是 DSL 表达能力不足，而不是业务需要。
- `TUIWidgetChildrenBuilder::child()` 新增 `TUILayoutAttachment` 重载，复用既有 `applyLayout()`，保持 `parent[spec >> child]` 与 `.child(spec >> child)` 语义一致。
- Gallery 的 DragFloat、SpinBox、RadioButton、ColorEdit、SearchComboBox 改用 `ui::layout().size(...) >> widget`，删除对应 pending size helper 调用。
- 验证：`xmake b/r ya-gui-closure-test`（315/315）与 `xmake b GUIWorkbench` 均通过。
- 未完成：共享按钮工厂、测试 helper、builder release 外部 attach 全量迁移，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 Workbench button factory edge 迁移

- 误差审计确认：`makeDemoButton()` 的 width 参数只是调用点 parent edge 的尺寸意图，不应由按钮工厂暂存到 child。
- 工厂删除 pending size 写入；RenderProbe 由 form canvas/box edge 显式配置，Modal OK/Cancel 由 buttons box slots 显式配置。
- width<=0 的按钮仍保留 content padding 语义，不混入布局迁移。
- 验证：`xmake b GUIWorkbench` 通过。
- 未完成：Interactions 内部 builder helper、测试 helper、剩余动态控件 pending 调用，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 Workbench gallery drag/input edges 迁移

- 误差审计确认：Gallery drag source row 与 drop result row 的 parent 都由同一 DSL 构造段确定，pending size 没有必要。
- source / drop zone 全部改为 `child(ui::layout().size(...) >> widget)`，WorkBenchDemoPages 中只剩 helper 定义本身，不再有调用点。
- 验证：`xmake b GUIWorkbench` 通过。
- 未完成：清理无调用的 Workbench pending helper、测试 helper、Interactions 内部 builder bridge，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 统一 attach edge initializer contract

- 误差审计确认：现有 `attach()` 后再 `initializeChildSlot()` 会让不同 materialization 路径各自实现 edge 初始化，后续删除 pending bridge 时容易产生分叉。
- 新增 `WidgetTree::attach(parent, widget, FChildSlotInitializer)`，统一表达“attach 并配置 parent-owned slot”；实现复用现有单 parent 校验和 slot 回指生命周期。
- 新增 `WidgetTreeTest.AttachWithEdgeInitializerConfiguresParentOwnedSlot`，验证 initializer 直接落到 typed canvas slot。
- 验证：`xmake b ya-gui-closure-test` 与定向测试通过。
- 未完成：迁移 builder `release()` 外部 attach、测试 helper 和最终删除 UIElement pending bridge。

## 2026-08-31 — CP2 smoke 迁移到统一 attach initializer

- GUIFrameworkSmoke 的 canvas child helper 改为直接调用 `WidgetTree::attach(parent, child, initializer)`，删除 attach 后重复 `initializeChildSlot`。
- 验证：`xmake b ya-gui-minimal-host` 通过。

## 2026-08-31 — CP2 清理 Workbench pending helper 死代码

- 误差审计确认：`WorkbenchDemoPages.cpp` 中 `setPendingSlotSize()` 已无任何调用点，继续保留会误导后续迁移并扩大 legacy 表面。
- 删除无调用 helper；不改变剩余测试 helper 与 UIElement pending bridge。
- 验证：`xmake b GUIWorkbench` 通过。

## 2026-08-31 — CP2 GUIHeadlessHostTest 显式 canvas attach

- 误差审计确认：HeadlessHost fixture 的 layer parent 在 `buildUI()` 中明确可得，panel/menu bar 不需要先 detached 暂存几何。
- 两个 fixture 改为直接构造 `FCanvasSlotArgs` 并调用显式 canvas attach；移除 `GUITestLayoutHelpers.h` 依赖。
- 验证：closure target 构建通过。
- 未完成：其余测试 helper 仍有大量 detached 构造，需继续按 fixture parent 分组迁移。

## 2026-08-31 — CP2 UIFrameSnapshot paint-order fixture 迁移

- 误差审计确认：paint-order 用例的 behind/front 都直接挂到 Content layer，几何 parent 明确。
- 删除该用例的 `authorSlotPosition/authorSlotSize` 调用，改为两个显式 `FCanvasSlotArgs` attach。
- 验证：`UIFrameSnapshotTest.BuildResolvesItemsToRenderPixelsInPaintOrder` 通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot clip fixture 迁移

- `ContainerClipResolvesOnChildren` 的 clip container 改为显式 `FCanvasSlotArgs` attach；child 原有 box slot initializer 保持 parent-owned。
- 验证：定向测试通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot 基础 geometry fixtures 批量迁移

- 误差审计确认：四个基础 snapshot 用例的 widget 都直接挂 Content layer，parent 与 canvas edge 明确。
- `SnapshotSurvivesImmediateDetach`、`TextItemsCarryFontAndText`、`LayoutRunsWhenDirtyDuringSnapshot`、`PanelCornerRadiusScalesIntoDrawItem` 删除测试 helper pending 写入，改用显式 `FCanvasSlotArgs` attach。
- 验证：`UIFrameSnapshotTest.*` 62/62 通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot perf/theme/mutation fixtures 迁移

- 误差审计确认：三个用例中的顶层 widget 都直接挂 Content layer，且移动测试在 attach 后已有稳定 slot。
- `PerfStatsCountPaintWalkAndDrawItems`、`PanelResolvesThemeStyleAndRepaintsOnThemeSwitch` 改用显式 `FCanvasSlotArgs`；`LayoutChangeRebuildsMovedWidgetDrawItems` 的移动改为直接更新当前 `UICanvasSlot`。
- 验证：三个定向测试通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot cache fixtures 迁移

- `CleanTreeOffsetChangeRebuildsResolvedItems`、`CleanTreeUiScaleChangeRebuildsResolvedItems`、`CleanTreeGenerationChangeDropsCache` 的 panel geometry 改为显式 `FCanvasSlotArgs` attach。
- 保持测试关注点在 cache invalidation，不再通过测试 pending helper 设定顶层 authored geometry。
- 验证：3 个定向测试通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot layout-host fixtures 迁移

- 误差审计确认：scroll viewport、split pane、clip container 的 parent edge 在各用例中明确；child 尺寸也可直接写入 box/overlay slot。
- 迁移 `ScrollViewportClipsContentToViewportRect`、`SplitPaneClipsChildrenToOwnPaneRect`、`LayoutHostsReuseSelfSegmentWhenClean`、`ContainerClipResizeInvalidatesChildSegments`，运行时 resize 改为直接更新 `UICanvasSlot`。
- 验证：4 个定向测试通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot style fixtures 迁移

- 误差审计确认：主题/style 用例的按钮与面板均直接挂 Content layer，位置和尺寸属于 layer-owned canvas edge。
- `AuthoredButtonStyleWinsOverThemeAndIgnoresThemeSwitch`、`ThemeAttachAfterUnthemedBuildRepaintsKeyedButton`、`SetColorWritesAuthoredStyleAndBeatsTheme`、`SameAuthoredStyleDoesNotDirty`、`SparseStyleFieldInheritsUnpatchedFieldsOnThemeSwitch` 改用显式 `FCanvasSlotArgs` attach。
- 验证：5 个定向测试通过，closure target 构建通过。

## 2026-08-31 — CP2 UIFrameSnapshot interaction fixtures 迁移

- 误差审计确认：Image、TreeView、DragDrop source/target 均直接挂 Content layer，parent edge 明确。
- `ImagePlaceholderAndModalPopupFollowTheme`、`TreeViewSelectionFollowsTheme`、`DragDropTilesFollowTheme` 删除测试 pending helper，改用显式 `FCanvasSlotArgs` attach。
- 验证：`UIFrameSnapshotTest.*` 62/62 通过，closure target 构建通过。

## 2026-08-31 — CP2 Workbench gallery menu/vector edges 迁移

- 误差审计确认：Gallery menu bar 与 vector canvas 均由 `form` 直接承载，尺寸不需要 child pending 状态。
- menu bar 改为 `form.child(ui::layout().size(...) >> localBar)`；vector canvas 使用现有 `FBoxSlotArgs.preferredSize`。
- 验证：`xmake b GUIWorkbench` 通过。
- 未完成：共享按钮工厂、测试 helper、builder release 外部 attach 全量迁移，以及最终删除 `UIElement::_pendingSlotInitializer`。

## 2026-08-31 — CP2 floating-window edge intent 收口：删除 detached pending 依赖

- 误差审计确认：浮动窗口的矩形本来就由 `UIDockFloatingHost` 的 canvas edge 所有；detached 阶段把 initializer 暂存在 `UIElement` 只是施工桥。
- `UIDockFloatingWindow::setWindowRect()` detached 时仅更新 `_windowRect`，挂载生命周期通过 `onAttached()` 重新将矩形写入 parent-owned `UICanvasSlot`。
- 保持首次 attach 的语义：`onAttached()` 在 tree membership 建立后执行，slot 已创建，可在第一次 layout 前完成 host edge 初始化。
- 验证：floating window geometry / resize-handle / dock merge 定向测试全部通过；`xmake b ya-gui-closure-test` 通过。
- 未完成：示例中的 imperative `setPendingSlotInitializer` helper、`child(node)` 默认重载、旧 schema 负向清零和最终 self-positioned fallback。

## 2026-08-31 — CP2 BindingContract fixture edge migration

- 误差审计确认：BindingContract 中 TreeView、TableGrid、MenuBar 以及 drag/drop source/target 都有明确的 Content layer parent；继续通过测试 helper 暂存 child geometry 没有必要。
- 迁移方式：删除 `authorSlotPosition/authorSlotSize/authorSlotAnchors` 调用，直接构造 `FCanvasSlotArgs` 并在 `WidgetTree::attach(parent, child, args)` 时初始化 parent-owned canvas edge。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='BindingContractTest.*'`，10/10 通过。
- 未完成：builder `.setSize/.setPosition/.setAutoSize` 与 pending bridge 仍待 release→external attach 全量迁移后删除。

## 2026-08-31 — CP2 WidgetLayout explicit canvas edge fixture

- 误差审计确认：`BuilderGeometryIntentIsConsumedByTheCanvasSlot` 实际测试的是 detached child 的 pending builder bridge，而不是 UIElement 几何 API 本身；在删除 builder bridge 前应先把 fixture 改成明确的 parent-owned edge contract。
- 迁移方式：panel root 使用显式 `FCanvasSlotArgs` 挂到 Content layer，child 使用 `addDetachedChild(..., initializer)` 直接设置 `UICanvasSlot` offset/fixedSize；测试重命名为 `ExplicitCanvasEdgeIntentIsConsumedByTheCanvasSlot`。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='WidgetLayoutTest.ExplicitCanvasEdgeIntentIsConsumedByTheCanvasSlot'` 通过。
- 未完成：其余 `builder.release()` 外部 attach fixture 仍待按 parent 分组迁移；builder 几何 sugar 暂不能删除。

## 2026-08-31 — CP2 WidgetLayout single-child host root edges

- 误差审计确认：这一组 `WidgetLayoutTest` 的关注点是 `SizeBox/ScrollViewport` 的 single-child slot 与 unified layout spec，而不是 host root 通过 builder `.setSize()` 暂存几何。继续保留 builder root size 只会掩盖 parent-owned root edge contract。
- 迁移方式：`EdgeLayoutSpecAppliesToTheChildNotTheParent`、`SingleChildSlotDefaultsToFillReproducingStretch`、`SingleChildSlotAlignKeepsDesiredSizeAndCenters`、`UnifiedLayoutSpecAppliesToSingleChildSlot`、`UnifiedLayoutSpecAppliesToScrollViewportSingleChildSlot` 改为在 attach 到 Content layer 时显式传入 `FCanvasSlotArgs.fixedSize`。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='WidgetLayoutTest.EdgeLayoutSpecAppliesToTheChildNotTheParent:WidgetLayoutTest.SingleChildSlotDefaultsToFillReproducingStretch:WidgetLayoutTest.SingleChildSlotAlignKeepsDesiredSizeAndCenters:WidgetLayoutTest.UnifiedLayoutSpecAppliesToSingleChildSlot:WidgetLayoutTest.UnifiedLayoutSpecAppliesToScrollViewportSingleChildSlot'` 通过。
- 未完成：仍有更多 `WidgetLayoutTest` / `DeclarativeContractTest` 使用 builder `.setSize()` 作为 detached root 或 child edge bridge，需要继续按语义分组迁移。

## 2026-08-31 — CP2 WidgetLayout root host edges (canvas/box/selectable-row)

- 误差审计确认：这一组 `WidgetLayoutTest` 的断言对象是 canvas slot、box slot、single-child slot、reparent rebuild 与 layout spec 初始化；host 自身通过 builder `.setSize()` 获得根尺寸并不是测试目标，只是旧 root edge 入口。
- 迁移方式：将 host root 的 `setSize()` 改为 `WidgetTree::attach(parent, host, FCanvasSlotArgs{.fixedSize=...})`，覆盖 panel/canvas/column/selectable-row 以及 reparent host 场景；保留 child `.setSize()` 的少数用例，仅限那些仍在验证 pending bridge 或 spec override 的语义。
- 覆盖用例：`CanvasLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry`、`StretchXFixedHeightChromeLivesOnTheCanvasSlot`、`CanvasSlotOffsetAndFixedSizeCanBeUpdatedAfterAttach`、`BuildWithLayoutSpecInitializesTheCanvasSlot`、`CanvasHostSetSizeBridgesToTheCanvasSlotFixedSize`、`CanvasHostIsNotBoundToThePanelVisuals`、`ReparentingBetweenHostsRebuildsTheSlotForTheNewHost`、`ReparentingAcrossHostsDoesNotLeakTheOldHostIntent`、`BoxHostSetSizeBridgesToTheBoxSlotPreferredSize`、`BuildWithLayoutAttachmentInitializesTheBoxSlot`、`UnifiedLayoutSpecAppliesToSelectableRowSingleChildSlot`、`AttachDoesNotSeedDefaultChildSizeOntoTheBoxSlot`、`CanvasLayoutIgnoresCorruptedChildSizeAfterAttach`。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='WidgetLayoutTest.CanvasLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry:WidgetLayoutTest.StretchXFixedHeightChromeLivesOnTheCanvasSlot:WidgetLayoutTest.CanvasSlotOffsetAndFixedSizeCanBeUpdatedAfterAttach:WidgetLayoutTest.BuildWithLayoutSpecInitializesTheCanvasSlot:WidgetLayoutTest.CanvasHostSetSizeBridgesToTheCanvasSlotFixedSize:WidgetLayoutTest.CanvasHostIsNotBoundToThePanelVisuals:WidgetLayoutTest.ReparentingBetweenHostsRebuildsTheSlotForTheNewHost:WidgetLayoutTest.ReparentingAcrossHostsDoesNotLeakTheOldHostIntent:WidgetLayoutTest.BoxHostSetSizeBridgesToTheBoxSlotPreferredSize:WidgetLayoutTest.BuildWithLayoutAttachmentInitializesTheBoxSlot:WidgetLayoutTest.UnifiedLayoutSpecAppliesToSelectableRowSingleChildSlot:WidgetLayoutTest.AttachDoesNotSeedDefaultChildSizeOntoTheBoxSlot:WidgetLayoutTest.CanvasLayoutIgnoresCorruptedChildSizeAfterAttach'` 通过。
- 未完成：`DeclarativeContractTest` 与剩余 child builder `.setSize/.setAutoSize` 仍是删除 pending bridge 前的主要收口对象。

## 2026-08-31 — CP2 Declarative root edge build contract

- 误差审计确认：这一批 `DeclarativeContractTest` 的核心断言是 declarative live-construct 的 widget identity、theme/style、focus/capture、container/split/overlay contract；builder 上的 root `.setSize()` 只是旧的 layer root 几何入口。
- 迁移方式：对 root 尺寸明确的 DSL 页面，改为 `ui::build(tree, parent, builder, FCanvasSlotArgs)`，让 root geometry 直接落在 parent-owned canvas edge；保留 child `.setSize()` 用于仍在验证 child slot/preferred-size 行为的用例。
- 覆盖用例：`DslSetStyleOverridesTheme`、`DslSetStyleFieldInheritsUnpatchedThemeFields`、`DirectConstructSnapshotIsStableAcrossRepeatedBuilds`、`DirectConstructButtonLabelIsContentChild`、`DirectConstructTextFieldKeepsFocusAcrossSetText`、`DirectConstructExternalPatchClampsFocusedTextFieldCursorWithoutCommit`、`DirectConstructExternalPatchKeepsPressedButtonSession`、`DirectConstructExternalPatchCanReplaceButtonLabelSubtreeMidPress`、`DirectConstructContainerLayoutHonorsAuthoredSizeAndClip`、`DirectConstructContainerClipAndMainAxisAlignment`、`DirectConstructPanelCornerRadiusAndAnchors`、`DirectConstructSplitScrollAndFillSlot`、`DirectConstructBoxSlotOverlayAndSizeBox`、`DirectConstructDetachStopsButtonClicks`。
- 额外审计：`FTestCompoundWidget::construct()` 中 composition root 的 builder `.setSize()` 已删除，避免 compound 内部继续制造 detached geometry 真值。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='DeclarativeContractTest.DslSetStyleOverridesTheme:DeclarativeContractTest.DslSetStyleFieldInheritsUnpatchedThemeFields:DeclarativeContractTest.DirectConstructSnapshotIsStableAcrossRepeatedBuilds:DeclarativeContractTest.DirectConstructButtonLabelIsContentChild:DeclarativeContractTest.DirectConstructTextFieldKeepsFocusAcrossSetText:DeclarativeContractTest.DirectConstructExternalPatchClampsFocusedTextFieldCursorWithoutCommit:DeclarativeContractTest.DirectConstructExternalPatchKeepsPressedButtonSession:DeclarativeContractTest.DirectConstructExternalPatchCanReplaceButtonLabelSubtreeMidPress:DeclarativeContractTest.DirectConstructContainerLayoutHonorsAuthoredSizeAndClip:DeclarativeContractTest.DirectConstructContainerClipAndMainAxisAlignment:DeclarativeContractTest.DirectConstructPanelCornerRadiusAndAnchors:DeclarativeContractTest.DirectConstructSplitScrollAndFillSlot:DeclarativeContractTest.DirectConstructBoxSlotOverlayAndSizeBox:DeclarativeContractTest.DirectConstructDetachStopsButtonClicks'` 通过。
- 未完成：`DeclarativeContractTest` 里仍有不少 child `.setSize()` 用例，它们大多在验证 child preferred/fixed size contract，不能和 root edge 清理混为一谈；删除 builder pending bridge 之前还需继续分组审计。

## 2026-08-31 — CP2 Declarative child edge intent migration

- 误差审计确认：本批剩余 child `.setSize()` 中，部分确实是 parent->child edge intent，继续保留会让 builder bridge 看起来像布局真值；应改为统一 `ui::layout().size(...) >> child`。
- 迁移方式：`DirectConstructSnapshotIsStableAcrossRepeatedBuilds`、`DirectConstructContainerLayoutHonorsAuthoredSizeAndClip`、`DirectConstructInputWidgetsCarryRegistryTypeId`、`DirectConstructContainerClipAndMainAxisAlignment`、`DirectConstructSplitScrollAndFillSlot`、`DirectConstructBoxSlotOverlayAndSizeBox` 改用 layout attachment；Overlay badge 的 alignment/padding/size 使用 `FOverlaySlotArgs.preferredSize`，因为当前 overlay builder 没有 attachment 与 typed args 的组合重载。
- 方向修正：没有为单个缺口新增平行 DSL；能力不完整之处记录为待后续统一扩展，而不是通过隐式回写或兼容桥绕过。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='DeclarativeContractTest.DirectConstructSnapshotIsStableAcrossRepeatedBuilds:DeclarativeContractTest.DirectConstructContainerLayoutHonorsAuthoredSizeAndClip:DeclarativeContractTest.DirectConstructInputWidgetsCarryRegistryTypeId:DeclarativeContractTest.DirectConstructContainerClipAndMainAxisAlignment:DeclarativeContractTest.DirectConstructSplitScrollAndFillSlot:DeclarativeContractTest.DirectConstructBoxSlotOverlayAndSizeBox'`，6/6 通过。
- 未完成：仍有 child `.setSize()` 出现在 `DeclarativeContractTest` 的 box/overlay 内容测试中；需要继续判断是否应直接写 typed args，并同步推进匿名节点默认命名的 DSL 设计。

## 2026-08-31 — CP2 Declarative child edge completion and compound correction

- 误差审计确认：child edge 迁移后全量 `DeclarativeContractTest.*` 首次回归暴露 `FTestCompoundWidget` composition root desired-size 丢失；原因是删除 builder `.setSize()` 时未同步把尺寸写入 compound-owned `UIOverlaySlot`。
- 纠偏方式：`FTestCompoundWidget::construct()` 使用 `addDetachedChild(..., initializer)`，将 `{123,45}` 写入 composition root 的 overlay slot `preferredSize`，恢复 compound desired-size contract；没有恢复 child geometry 或扩大兼容桥。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='DeclarativeContractTest.*'`，32/32 通过。
- 未完成：`DeclarativeContractTest` 中剩余少量 child `.setSize()` 仅作为测试 builder bridge/typed edge 语义的候选点；匿名节点默认命名仍是独立 DSL ergonomics 任务。

## 2026-08-31 — CP2 ToolControls stack edge migration

- 误差审计确认：Stack/Container 四个用例中的 root `authorSlot*` 是已知 Content layer edge，child 尺寸则由 `UIBoxSlot::preferredSize` initializer 承担；测试目标是 spacing/padding/visibility/main-axis/stretch，不需要测试 pending geometry helper。
- 迁移方式：stack/box root 改为显式 `FCanvasSlotArgs` attach；移除对应 child `authorSlotSize`，继续使用 `attachPreferredSize` 直接初始化 parent-owned box slot。首个带位置的 stack 用 root canvas `offset={20,20}` 保持原坐标。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.StackLaysOutChildrenWithGapAndPadding:ToolControlsTest.StackCollapsedSkipsSpaceHiddenKeepsSpace:ToolControlsTest.StackMainAxisAlignmentOffsetsThePack:ToolControlsTest.ContainerStretchLastChildFillsRemainingSpace'`，4/4 通过。
- 未完成：ToolControls 剩余 split/toolbar/scroll/row/menu fixtures 仍有大量 helper 调用；需继续按 parent edge 语义分组迁移。

## 2026-08-31 — CP2 ToolControls split root edge migration

- 误差审计确认：split pane divider layout/drag/hover 三个用例只依赖 split root 的 Content layer canvas edge；divider ratio、capture、cursor 行为不是 child geometry bridge。
- 迁移方式：删除 split root 的 `authorSlotPosition/authorSlotSize`，改为 `FCanvasSlotArgs.fixedSize` 在 `WidgetTree::attach()` 时初始化 parent-owned root edge。
- 覆盖用例：`SplitPaneLaysOutTwoPanesAroundDivider`、`SplitPaneDividerDragChangesRatioAndEndsSession`、`SplitPaneDividerHoverRequestsResizeCursor`。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.SplitPaneLaysOutTwoPanesAroundDivider:ToolControlsTest.SplitPaneDividerDragChangesRatioAndEndsSession:ToolControlsTest.SplitPaneDividerHoverRequestsResizeCursor'`，3/3 通过。
- 未完成：split pane 的 child/button edge、toolbar、scroll、row、menu fixtures 仍待分组迁移。

## 2026-08-31 — CP2 ToolControls toolbar root edges

- 误差审计确认：toolbar hover 用例的绝对位置、横向 box、stretch canvas 三种 root 几何都属于 Content layer parent-owned canvas edge；按钮尺寸由 toolbar-owned `UIBoxSlot` preferredSize 承担，hover/cursor 断言不依赖 child geometry。
- 迁移方式：`ButtonHoverClearsOnPointerLeave` 使用显式 button canvas args；`ToolbarSiblingHoverSwitchesAndClears` 使用 offset/fixedSize canvas root；`ToolbarAutoSizeButtonWithLabelHoverClears` 使用 anchor/offset/fixedSize canvas root，保留 toolbar child attach 的 typed box/autosize 语义。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.ButtonHoverClearsOnPointerLeave:ToolControlsTest.ToolbarSiblingHoverSwitchesAndClears:ToolControlsTest.ToolbarAutoSizeButtonWithLabelHoverClears'`，3/3 通过。
- 未完成：ToolControls 剩余 split overlap、scroll、row、menu fixtures 仍有 helper 调用。

## 2026-08-31 — CP2 ToolControls scroll edge migration

- 误差审计确认：scroll viewport 的 root 尺寸属于 Content layer canvas edge，content extent 属于 scroll-owned child slot；scroll offset/clamp/hit culling/nested split 是布局行为，不应继续依赖 detached geometry helper。
- 迁移方式：`ScrollViewportShiftsContentByOffset`、`ScrollViewportWheelConsumesWhenScrollableBubblesAtLimit`、`ScrollViewportCullsChildHitsOutsideViewport`、`ScrollViewportNestedInsideSplitKeepsCustomLayout` 改用显式 viewport `FCanvasSlotArgs`；content 使用 `attachPreferredSize` 或由 split typed edge 接管。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.ScrollViewportShiftsContentByOffset:ToolControlsTest.ScrollViewportWheelConsumesWhenScrollableBubblesAtLimit:ToolControlsTest.ScrollViewportCullsChildHitsOutsideViewport:ToolControlsTest.ScrollViewportNestedInsideSplitKeepsCustomLayout'`，4/4 通过。
- 未完成：ToolControls 剩余 split overlap、row、menu fixtures 仍有 helper 调用。

## 2026-08-31 — CP2 ToolControls selectable-row edge migration

- 误差审计确认：selectable-row press/keyboard/tab/drag/hover/theme 用例的 root row 几何属于 Content layer canvas edge；label child 几何属于 row-owned overlay slot，不应继续由测试 helper 暂存。
- 迁移方式：顶层 row/source/target 改显式 `FCanvasSlotArgs` attach；row label 使用 `addDetachedChild(..., UIOverlaySlot preferredSize)`，保留 row 的 single-child layout 与输入行为。
- 覆盖用例：`SelectableRowPressSelectsReleaseActivates`、`SelectableRowEnterActivatesFocusedRow`、`SelectableRowParticipatesInTabTraversal`、`SelectableRowDraggableRowsUseBehaviorBackedDragDrop`、`SelectableRowHoverRepaintsWithHoveredColor`、`SelectableRowWithLabelChildHoverStillHighlightsRow`、`SelectableRowHoverUsesThemeFill`。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.SelectableRowPressSelectsReleaseActivates:ToolControlsTest.SelectableRowEnterActivatesFocusedRow:ToolControlsTest.SelectableRowParticipatesInTabTraversal:ToolControlsTest.SelectableRowDraggableRowsUseBehaviorBackedDragDrop:ToolControlsTest.SelectableRowHoverRepaintsWithHoveredColor:ToolControlsTest.SelectableRowWithLabelChildHoverStillHighlightsRow:ToolControlsTest.SelectableRowHoverUsesThemeFill'`，7/7 通过。
- 未完成：ToolControls 剩余 split overlap、TreeView/TextField/Menu/MenuBar fixtures 仍有 helper 调用。

## 2026-08-31 — CP2 ToolControls TreeView/TextField root edges

- 误差审计确认：TreeView reorder 与 TextField 输入/焦点/光标/提交用例的几何都属于 Content layer root canvas edge；测试目标不依赖 child-owned geometry。
- 迁移方式：TreeView 使用显式 offset/fixedSize canvas attach；五个 TextField fixture 使用显式 fixedSize canvas attach，删除 root 的 `authorSlotPosition/authorSlotSize` pending helper 调用。
- 覆盖用例：`TreeViewReorderUsesBehaviorBackedDragDrop`、`TextFieldTypedTextAppendsAndFiresChanged`、`TextFieldBackspaceAndCursorNavigation`、`TextFieldEnterAndFocusLossCommit`、`TextFieldPressRequestsFocusAndPlacesCaret`、`TextFieldDoesNotConsumeForeignKeys`。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.TreeViewReorderUsesBehaviorBackedDragDrop:ToolControlsTest.TextFieldTypedTextAppendsAndFiresChanged:ToolControlsTest.TextFieldBackspaceAndCursorNavigation:ToolControlsTest.TextFieldEnterAndFocusLossCommit:ToolControlsTest.TextFieldPressRequestsFocusAndPlacesCaret:ToolControlsTest.TextFieldDoesNotConsumeForeignKeys'`，6/6 通过。
- 未完成：ToolControls 剩余 split overlap、menu/menu bar 以及少量 specialized fixture helper 调用。

## 2026-08-31 — CP2 ToolControls Menu/MenuBar root edges

- 误差审计确认：Menu popup content 的尺寸已由 menu-owned canvas slot 负责，本批只处理 MenuBar 测试 root edge，不重复实现菜单尺寸逻辑。
- 迁移方式：`MenuBarHoverSwitchesOpenMenu` 使用 anchor/fixedSize `FCanvasSlotArgs`；`MenuBarPaintsBottomSeparator` 使用 offset/fixedSize `FCanvasSlotArgs`；`MenuSizesPanelFromItemLabels` 保持现有 popup-owned slot 断言。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.MenuSizesPanelFromItemLabels:ToolControlsTest.MenuBarHoverSwitchesOpenMenu:ToolControlsTest.MenuBarPaintsBottomSeparator'`，3/3 通过。
- 未完成：ToolControls 剩余 split overlap、以及其他测试文件中的 helper/pending 调用仍待审计。

## 2026-08-31 — CP2 DSL anonymous node identity

- 误差审计确认：匿名节点只应省略用户身份，不应生成自动 stable key；否则会把诊断标签错误地升级为 reconciliation identity，并与 slot-owned layout 无关地引入隐藏状态。
- 迁移方式：所有内置 DSL 工厂增加无参匿名入口；registry 默认类型名保留为 `_name`，`_stableKey` 保持为空。builder 新增 `.key(...)` 与 `.displayName(...)`，分别表达稳定定位和显示/诊断标签；显式 key 未指定 displayName 时仍作为 name。
- 验证：新增 `DeclarativeContractTest.DslNodesAreAnonymousUnlessIdentityIsRequested`，确认匿名 root/child、显式 displayName、显式 key 的身份字段契约；`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='DeclarativeContractTest.DslNodesAreAnonymousUnlessIdentityIsRequested'` 通过。
- 未完成：compound 自定义 builder 的匿名便捷入口、匿名节点在 dump/LSP 中的专门标记仍可按实际工具需求补充；本批不制造自动 key 或 legacy identity 兼容层。

## 2026-08-31 — CP2 ToolControls split-overlap/specialized dump edges

- 误差审计确认：`SplitPaneDoesNotStealHoverFromOverlappingButton` 的 panel 根几何属于 Content layer canvas edge；toolbar/add 尺寸属于 panel/toolbar-owned `UIBoxSlot`，不能继续用 detached pending helper。`SpecializedLayoutsAppearInTreeDump` 的 split 根和 scroll content 尺寸同样应落在 parent-owned typed slot。
- 迁移方式：panel/split 使用显式 `FCanvasSlotArgs` attach；toolbar/add 使用 `UIBoxSlot.preferredSize`；scroll content 使用 typed `UICanvasSlot` fixed size。没有恢复 child geometry 或隐式 fallback。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test --gtest_filter='ToolControlsTest.SplitPaneDoesNotStealHoverFromOverlappingButton:ToolControlsTest.SpecializedLayoutsAppearInTreeDump'`，2/2 通过。
- 未完成：其他测试文件仍可能有 helper/pending 调用，需继续按文件和语义分组审计。

## 2026-08-31 — CP2 host/event/texture fixture edges

- 误差审计确认：GameUIHost 输入映射、GuiEventDriver hover route、UIFrameTextureLifetime snapshot retention 的根几何都是 Content layer parent-owned canvas edge，不依赖 child geometry。
- 迁移方式：三组 fixture 使用显式 `FCanvasSlotArgs` attach；GameUIHost 复用 `addToWorld(..., args)`，事件按钮分别持有各自 offset/fixedSize，纹理生命周期测试保持 resolver/strong-reference 断言不变。
- 验证：`xmake b ya-gui-closure-test` 通过；`GuiEventDriverTest.ScenarioDrivesWidgetTreeAndDumpAssertsHoverContract` 通过（其余筛选目标未发现编译/链接问题，后续全量回归覆盖）。
- 未完成：Declarative/WidgetTree/UIDocument/UIDesigner 等文件仍有 helper/pending 调用。

## 2026-08-31 — CP2 DeclarativeContract edge cleanup

- 误差审计确认：该文件中的剩余 helper 都是 Content layer 根节点或 adapter mount 后的当前 parent slot；没有需要保留 child-owned geometry 的特殊情况。
- 迁移方式：NoTheme/Appearance/Snapshot/Detach 等根 fixture 使用显式 `FCanvasSlotArgs` attach；同值 dirty 测试直接写当前 `UICanvasSlot`；adapter fixture 在 mount 后通过 `getSlotForChild` 应用 typed args。
- 验证：相关 5 项回归测试全部通过，且 `DeclarativeContractTest.cpp` 已不再调用 `authorSlot*`。
- 未完成：`GUITestLayoutHelpers.h` 仍被其他测试文件使用，pending bridge 尚不能删除。

## 2026-08-31 — CP2 UIDesigner detached-root audit

- 误差审计确认：`UIDesignerPanelTest` 中 root 在 `UIDocument::fromWidget` 前是 detached，且 Designer `openDocument` 会以 fill canvas edge 挂载预览根；root 的 pending 尺寸既不会进入文档，也不是预览布局输入。保留它会制造错误的 detached geometry 语义。
- 迁移方式：删除 UIDesigner 测试 root 的 `authorSlotSize`；child 的初始位置/尺寸继续通过 `addDetachedChild` 写入 parent-owned `UICanvasSlot`，由 Designer resize path 读取和更新。
- 验证：`xmake b ya-gui-closure-test` 编译通过；该测试源不在 closure test 注册列表中，需由对应 editor test target 单独执行。
- 未完成：WidgetLayout/WidgetTree/UIDocument 仍有大量 helper 调用，继续按语义分批清理。

## 2026-08-31 — CP2 UIDocument layout edges

- 误差审计确认：`FromWidgetRoundtripsFieldsAndChildren` 中 container 与 child 的 detached geometry 若不经 parent edge attach 不会进入 UIDocument；container root 和 JsonRoundtrip panel position、AuthoredPanel root size、Deserialize source root size 均不是文档布局输入。
- 迁移方式：删除这些无效 pending helper；box container 的 child 尺寸改在 `addDetachedChild` initializer 中写入 `UIBoxSlot.preferredSize`，保持文档 `childSlots` roundtrip 仍验证 parent-owned intent。
- 验证：`xmake b ya-gui-closure-test` 通过；UIDocument 关键回归 `FromWidgetRoundtripsFieldsAndChildren`、`JsonRoundtrip`、`AuthoredPanelFillSurvivesThemeAfterReload`、`DeserializeOnAttachedWidgetAggregatesSingleInvalidation` 4/4 通过。
- 未完成：`UIDocumentTest.cpp` 已无 `authorSlot*` 调用；WidgetLayout/WidgetTree 仍待大批量迁移。

## 2026-08-31 — CP2 WidgetLayout basic size/box edges

- 误差审计确认：`TextWithoutAutoSizeStillMeasuresGlyphs` 的 detached helper 不参与 desired-size 测量；`AnchorLayoutResolvesStretchOverAutoOverSize` 的 999 尺寸被显式 Auto slot 覆盖；toolbar/row 根尺寸由已有 `FCanvasSlotArgs` 提供；button box child 尺寸已由 `UIBoxSlot.preferredSize` initializer 提供。
- 迁移方式：删除这些无效或重复的 `authorSlot*` 调用，没有把 geometry 写回 UIElement；保留 parent-owned canvas/box slot 断言和 layout 行为。
- 验证：`xmake b ya-gui-closure-test`；`TextWithoutAutoSizeStillMeasuresGlyphs`、`AnchorLayoutResolvesStretchOverAutoOverSize`、`ButtonAutoSizeInContainerPacksAndFills`、`ButtonExplicitSizeInContainerKeepsItsWidth`、`ButtonExplicitSizeIgnoresContentWidth` 5/5 通过。
- 未完成：WidgetLayout 中段及 WidgetTree 仍有大量 helper，需要继续按父布局语义分组。

## 2026-08-31 — CP2 WidgetLayout container/box edge ownership

- 误差审计确认：NestedContainers 的 spacer、BoxSlot reparent/reorder、fill/margin/hidden fixtures 中，root 尺寸应由 layer-owned canvas slot 提供，child 尺寸应由 parent-owned box slot 或 desired size 提供；detached child 上的 `authorSlotSize` 不会成为 box layout 输入。
- 迁移方式：删除重复/无效 child helper；需要固定 root 几何的 First/Second/Box/Outer 使用显式 `FCanvasSlotArgs` attach；box child 继续通过 `UIBoxSlot.preferredSize` initializer 或 attach 后 slot setter 表达。
- 验证：`xmake b ya-gui-closure-test`；`NestedContainersPropagateDesiredSizes`、`BoxSlotsAreParentOwnedAndRecreatedOnReparent`、`BoxSlotFillMarginAndCrossAlignmentArrangeWithoutContainerFields`、`BoxSlotsKeepEdgeStateLocalAcrossNestedReparent`、`SameParentReorderPreservesExistingBoxSlotState`、`BoxSlotsControlHiddenParticipationAndFillBounds` 6/6 通过。
- 未完成：WidgetLayout overlay/specialized 后段及 WidgetTree 大量 helper 仍待迁移。

## 2026-08-31 — CP2 WidgetLayout overlay/single-child edges

- 误差审计确认：overlay Host root 几何属于 Content layer canvas edge；Fill/Badge 与 SizeBox Inner 的尺寸由 parent-owned `UIOverlaySlot` 承担；Box 四边 margin 测试中的 left/right detached 尺寸由 box slot preferred size 承担。
- 迁移方式：删除 child `authorSlotSize`；overlay/size-box root 使用显式 `FCanvasSlotArgs`，保留 typed slot alignment/preferredSize 与 edge layout spec 断言。
- 验证：`xmake b ya-gui-closure-test`；`BoxSlotFourSideMarginIsNotSymmetric`、`OverlaySlotAlignsWithoutChildAnchors`、`SizeBoxPadsChildAndHonorsWidthOverride`、`EdgeLayoutSpecAppliesToTheChildNotTheParent` 4/4 通过。
- 未完成：WidgetLayout specialized 后段和 WidgetTree 仍待迁移。

## 2026-08-31 — CP2 WidgetLayout slot-proof/specialized edges

- 误差审计确认：fixed text binding 的固定尺寸必须来自 layer canvas slot；corrupted child size 测试应只验证 attach 后 parent-owned typed slot，不应再预置 child geometry；SelectableRow/CheckBox/Dock projection 的根尺寸同样来自显式 canvas edge。
- 迁移方式：移除 WidgetLayout 后段剩余 `authorSlot*` 调用；fixed binding 使用 `FCanvasSlotArgs` attach；box/overlay corrupted-size fixture 保留 typed slot initializer；SelectableRow、CheckBox、Overlay、Dock root 使用显式 canvas args。
- 验证：`xmake b ya-gui-closure-test`；fixed binding、canvas/box/overlay slot-proof、single-child 与 dock projection 共 9/9 通过；`WidgetLayoutTest.cpp` 已无 `authorSlot*` 调用。
- 未完成：WidgetTree 的 `makeButton` helper 仍封装 pending slot initializer，需下一批整体改为纯 widget 工厂 + 显式 attach args，随后才能删除测试 helper/pending bridge。

## 2026-08-31 — CP2 WidgetTree drag/behavior edges

- 误差审计确认：拖拽目标/source、ghost source、behavior host、preview/bubble route、behavior drag source/target 的几何均属于 Content layer root canvas edge；child route 的局部几何已经通过 parent-owned canvas initializer 表达。
- 迁移方式：删除这些 fixture 的 `authorSlotPosition/authorSlotSize`，改用显式 `FCanvasSlotArgs` attach；没有改变 drag session、capture、focus 或 route policy。
- 验证：`xmake b ya-gui-closure-test`；6 个 WidgetTree 回归（drag target/source/ghost、behavior lifecycle/route/drag-drop）6/6 通过。
- 未完成：WidgetTree 仍有大量 helper，尤其 focus/capture、层级、dock/drag-drop 后段，需继续分组迁移。

## 2026-08-31 — CP2 WidgetTree focus/capture/route edges

- 误差审计确认：`ChildAddedToAttachedParentJoinsItsTree`、`AttachToLayerKeepsChildAbsoluteGeometrySemantics`、`LayerCanvasSlotTracksPositionUpdatesAfterAttach` 与 `PointerRouteDeliversPreviewTargetThenBubble` 的 root 几何都属于 parent-owned canvas edge；attached child 的局部位置通过 `addDetachedChild/attach` 的 edge initializer 表达。
- 迁移方式：root 使用显式 `FCanvasSlotArgs` attach；运行时位置更新直接修改当前 `UICanvasSlot`；route child 不再通过 child geometry helper 暂存。没有改变 focus/capture/route 逻辑。
- 验证：`xmake b ya-gui-closure-test`；上述 4 项 WidgetTree 回归 4/4 通过。
- 未完成：WidgetTree helper 仍集中在 focus/capture、层级、dock 与后段 drag-drop fixture；`makeButton` 测试辅助仍需后续拆为显式 attach 参数。

## 2026-08-31 — CP2 WidgetTree detach/path edges

- 误差审计确认：`DetachedWidgetDoesNotParticipate` 的 detached root 几何没有任何布局消费者，应直接删除；`WeakPointerPathsSurviveDetachWithoutDangling` 的 panel root 位置/尺寸属于 Content layer canvas edge，child 局部几何继续由 parent-owned canvas initializer 提供。
- 迁移方式：删除 detached fixture 的 pending helper；weak path fixture 使用显式 `FCanvasSlotArgs` attach，并保留 detach 后 focus/pointer path 清理断言。
- 验证：`xmake b ya-gui-closure-test`；`DetachedWidgetDoesNotParticipate`、`WeakPointerPathsSurviveDetachWithoutDangling` 2/2 通过。
- 未完成：`makeButton` helper 仍将位置/尺寸暂存在 pending bridge，后续需整体替换为显式 attach 参数或局部 slot initializer。

## 2026-08-31 — CP2 WidgetTree dock/drag observer edges

- 误差审计确认：DockSpace 根、拖拽 source、observer target 的几何均是 Content layer parent-owned canvas edge；source 在拖拽期间移动也应修改当前 canvas slot，而不是 child geometry。
- 迁移方式：dock root 使用显式 anchor canvas args；source/target 使用显式 offset/fixedSize attach；拖拽跟随指针通过当前 slot 更新。
- 验证：`xmake b ya-gui-closure-test`；Dock preview/merge、floating-window session、tab tear-off、drag observer 6/6 通过。测试中已有的 `addDetachedChild` 日志来自 DockWorkspace 对已挂载 panel 的既有装配路径，本批未扩大该行为范围，后续应单独修复为明确的 reparent/ownership API。
- 未完成：WidgetTree `makeButton` helper、focus/capture 后段及 dock 既有装配日志仍待处理。

## 2026-08-31 — CP2 WidgetTree layer/hit/capture edges

- 误差审计确认：z-order、system layer precedence、pass/hidden hit policy 与 pointer capture 测试中的几何都是 layer-owned canvas edge；这些用例不需要 detached widget 自带 geometry。
- 迁移方式：移除 `makeButton` 在本组用例中的 pending 几何，改为直接构造 `UIButton` 并在各 layer attach 时传入显式 `FCanvasSlotArgs`；保留层级、命中、capture 行为断言。
- 验证：`xmake b ya-gui-closure-test`；`ZOrderDefinesHitOrderWithinLayer`、system layer click/hover、pass、hidden、pointer capture 共 6/6 通过。
- 未完成：WidgetTree 仍有大量 `makeButton` 使用，后续继续迁移 focus traversal、popup/modal 与 drag-drop 后段。

## 2026-08-31 — CP2 WidgetTree route-state root edge

- 误差审计确认：`RouteStateTracksPointerCaptureAndFocusPaths` 的 panel 几何是 Content layer canvas edge；button 局部 edge 已通过 `addDetachedChild` initializer 表达。
- 迁移方式：panel root 使用显式 `FCanvasSlotArgs` attach，未改变 pointer/focus route trace 契约。
- 验证：`xmake b ya-gui-closure-test`；`RouteStateTracksPointerCaptureAndFocusPaths` 通过。
- 未完成：`makeButton` 仍是其他 focus traversal/popup fixture 的 pending 来源，后续继续批量替换。

## 2026-08-31 — CP2 WidgetTree makeButton migration

- 误差审计确认：测试辅助 `makeButton(name,pos,size)` 将 parent-owned geometry 错误地暂存在 detached child，是 pending bridge 的主要残留来源；位置/尺寸只有在 attach 到 layer 时才有意义。
- 迁移方式：`makeButton` 改为纯 widget 工厂，新增 `makeButtonSlot(pos,size)` 返回 `FCanvasSlotArgs`；Dump、Modal、Tab、Button press/drag/focus、DragObserver 等 10 个命中/布局 fixture 在 attach 时显式传入 slot。
- 验证：`xmake b ya-gui-closure-test`；上述 10 项回归 10/10 通过。
- 未完成：WidgetTreeTest 中仍有少量 makeButton 用于 parent child edge 或其他后段 fixture，需继续迁移后才能删除 `GUITestLayoutHelpers.h`。

## 2026-08-31 — CP2 ToolControls split child edge

- 误差审计确认：`SplitPanePressOnPaneFallsThroughToChild` 的 split root 几何属于 Content layer canvas edge；button 的尺寸属于 left pane 自身 child edge，应写入 `UIBoxSlot.preferredSize`，位置不应写入 child geometry。
- 迁移方式：split 使用显式 `FCanvasSlotArgs` attach；button 改为 `addDetachedChild` + typed box slot initializer，删除 `authorSlotPosition/authorSlotSize`。
- 验证：`xmake b ya-gui-closure-test`；`ToolControlsTest.SplitPanePressOnPaneFallsThroughToChild` 通过。
- 未完成：`ToolControlsTest.cpp` 已无直接 `authorSlot*` 之外的残留将继续审计；`GUITestLayoutHelpers.h` 仍被 WidgetTree/其他测试使用。

## 2026-08-31 — CP2 remove obsolete test geometry helper

- 误差审计确认：全局搜索显示所有测试源已不再调用 `authorSlotPosition/authorSlotSize/authorSlotAnchors`；helper 仅剩 include，删除不会移除任何测试语义。
- 迁移方式：删除 `Engine/Test/Source/GUITestLayoutHelpers.h` 及全部 include；测试布局意图现在只通过显式 `FCanvasSlotArgs`、typed slot initializer 或当前 slot setter 表达。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`，全量 317/317 通过。
- 未完成：生产 DSL builder 仍有 `_pendingSlotInitializer` 过渡桥及 `.setSize/.setPosition/.setAutoSize` 几何 sugar；需要继续迁移所有 `release()` 外部 attach 后才能删除。

## 2026-08-31 — CP2 WidgetLayout builder root edges

- 误差审计确认：WidgetLayout 中 5 组 layout contract 的 builder `.setSize` 只是在设置 root host 几何；真正的 child intent 已位于 `ui::layout()`/typed slot。将 root 尺寸迁移到 layer-owned `FCanvasSlotArgs` 不改变 layout contract。
- 迁移方式：移除 `PathAFill`、Overlay、Canvas pivot/Auto、Box layout spec 等测试 root `.setSize`，改显式 canvas attach；未删除仍用于验证 reparent bridge 的 child builder `.setSize`。
- 验证：`xmake b ya-gui-closure-test`；`BoxLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry`、`PathAFillIsExpressedOnTheSlotNotTheChild`、`UnifiedLayoutSpecAppliesToOverlaySlot`、`CanvasPivotCentresAChildOnItsAnchoredPosition`、`CanvasPreferredSizeDrivesAnAutoAxis` 5/5 通过。
- 未完成：生产/示例中仍有大量 builder geometry sugar；需继续按 attach 生命周期分组迁移。

## 2026-08-31 — CP2 WidgetLayout canvas/box capability roots

- 误差审计确认：剩余 Canvas offsets/Auto/alignment、Box slot preferred-size 与 corrupted-size contract 中的 root `.setSize()` 仍只是 host 几何；保留它会继续掩盖 root-slot contract。
- 迁移方式：root host 统一通过显式 `FCanvasSlotArgs.fixedSize` attach；child `.setSize()` 仅保留在专门验证 authored-size bridge / layout-spec 覆盖的测试。
- 验证：`xmake b ya-gui-closure-test && xmake r ya-gui-closure-test`，全量 317/317 通过。
- 未完成：生产/示例中仍有大量 builder geometry sugar；pending slot bridge 仍待所有外部调用迁移后删除。

## 2026-08-31 — CP2 WidgetTree attach/reparent/detach edges

- 误差审计确认：AttachTwice/CrossTree/Reparent/Sibling/Detach 语义测试只验证树归属与生命周期；大量 `makeButton(..., {}, {})` 的 detached 几何没有布局消费者，应移除 pending helper。需要命中/hover 的按钮则改显式 layer canvas args。
- 迁移方式：基础 attach/reparent/sibling/detach fixture 改为直接构造 widget，并在需要时通过 `FCanvasSlotArgs` attach；ButtonText/PopupShield/DetachFocus 等命中测试保留明确 root canvas 几何。
- 验证：`xmake b ya-gui-closure-test`；attach、cross-tree、reparent、detach、hover、tree destruction 共 15/15 通过。
- 未完成：`makeButton` helper 仍被命中测试大量使用，需后续改为返回 widget + 显式 args，最终删除 `GUITestLayoutHelpers` pending bridge。
