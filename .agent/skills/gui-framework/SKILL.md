---
name: gui-framework
description: YA GUI 框架（WidgetTree / 控件 / layout / Render2D pass slot / host 诊断）的模块地图与稳定契约。
---

## 适用场景

- 在 `Engine/Source/Framework/GUI/` 内改控件、布局、事件、快照、合成
- 开发 GUIWorkbench demo / 编辑器内嵌 panel
- 排查 GUI 渲染、布局、生命周期问题（GPU 资源 teardown、pass slot、clip/scissor）

## 计划与提交门禁

- GUI 迁移按用户指定的 feature/tab 闭环推进；不得用 registry、placeholder、纯拆文件或仅补 contract 文档冒充 feature migration。
- 一个 checkpoint 只能对应一个可运行、可验证的架构目标；代码、测试和 plan/progress 必须在同一提交中，且提交说明必须写清未完成项。
- `EditorSurface`、`UICompoundWidget`、WidgetTree 和 DSL 的边界若尚未验证，不得继续向宿主文件堆实现；先停下来做边界审计。

## 模块地图

```text
Framework/GUI/
  Runtime/Resource/   ya-gui-resources   Font/glyph、texture-slot（FontManager::registerFont 可注入合成字体做无 GPU 测试）
  Runtime/Draw2D/     ya-gui-draw2d      Render2D：screen/world 精灵+文本+线条批处理；pass slot 资源池
  Runtime/Widgets/    ya-gui-widgets     UIElement / WidgetTree / UIFrameSnapshot / 控件
  Runtime/Compose/    ya-gui-compose     共享 2D 合成 pass（UIFrameSnapshot -> Render2D）
  Tooling/            ya-gui-tooling     WorkbenchSurface / Workspace（工具 UI 外壳，demo 无关）
  App/                ya-gui-app-host    standalone 宿主：SDL 窗口、Vulkan、帧循环、automation 入口
Example/GUIWorkbench/                    retain-mode demo app（页面注册进 FWorkbenchSurface）
```

`ya-gui-framework` 是聚合 meta target（widgets+compose+tooling 等）；GUI 测试只链 GUI closure
（`ya-gui-closure-test` 不依赖 Scene/ECS/Render3D/Host/Editor）。

## WidgetTree 模型

- 层：`Content / Popup / Tooltip / DragIme`（项目内容不能覆盖系统层 zOrder）。
- 单视觉父契约：`attach`（新成员）/ `reparent`（显式迁移）/ `detach`（递归、清 transient state）。
- 输入：`dispatchEvent` 用显式 route：topmost candidate discovery 后执行 Preview
  (root -> parent) -> Target -> Bubble (parent -> root)。`Stop` 短路，`Pass` 继续 lower
  candidate；capture/focus/popup/modal/drag 都是 tree 级 route policy。`WidgetTree` 持有
  persistent pointer state、pointer path、focus path 和 route trace；`WidgetTreeDump`
  输出 `pointer`、`focusPath`、`lastRoute`（policy/path/phase/handled/result）。route callback
  可 detach 自身，executor 会持有 path 并重查 membership。drag&drop 会话
  （`beginDrag/updateDrag/endDrag/cancelDrag`，payload 为 string）由树管理，目标控件实现
  `canAcceptDrop/onDrop/setDropHighlight`。
- 快照：`buildSnapshot`（layout dirty 时才 layout + paint）→ 不可变 `UIFrameSnapshot`；
  录制只消费快照。命令录制期绝不读 live tree。业务代码不得在 paint/layout
  回调中直接修改 tree 结构；tooltip/drag 等 framework maintenance 只在显式 pass boundary 执行。
  编辑器规模基线在 `EditorScaleBaselineTest`：Hierarchy 只 paint 视口窗口、Content 目录
  `computeKeyedVisibleWindow` 与 catalog 规模无关、Inspector 列第二次干净 snapshot `rebuiltWidgets==0`。
  长时结构 soak 在 `EditorLongRunSoakTest`：同 subtree attach/detach、destroy/recreate、theme 切换、
  deferred texture generation。GPU/offscreen 像素门禁仍属 Phase 9。

## 布局契约（SizeToContent）

- SizeToContent / Slate DesiredSize 模型完全由 parent-owned slot 表达：canvas 在 attach 时把 Auto 种到 `UICanvasSlot` size mode。每轴解析优先级
  `anchor span（stretch）> Auto（computeDesiredSize 内容测量）> slot authored size（fixedSize / preferredSize）`。
  child geometry 永远不是 layout 输入；`computeDesiredSize` / `computeIntrinsicSize` 只报告内容。不存在仍读取 child authored geometry 的 path-B。
- `UIText`：desired / intrinsic = `font.measureText(text) × lineHeight`（与 AutoSize 无关）；字体经
  FontManager 解析，closure 测试用 `registerFont` 注入合成字体。显式尺寸在 parent-owned slot 上。
- `UIButton` / `UISelectableRow` / `UICheckBox`（Content-Slot）：单 child 容器。标签是内容槽里的 `UIText`
  子节点（DSL：`.child(ui::text(...).setText(...))`）。`UISingleChildLayout` padding + 内容子节点填入
  内缩 rect（`layoutAssigned`，非 child `setPosition`）。CheckBox 的左 padding = `_boxSize + _labelSpacing`。
  desired = 内容子节点 + padding；显式尺寸在 parent slot 上。行缩进用
  `setContentPadding(FMargin{indent, 0, 0, 0})`。
- `UICompoundWidget` 是 single-child host：`construct()` 挂上的第一个 child 经 `UISingleChildSlot` 填满 compound rect，不再手写 `layoutAssigned`。
- 布局正式分为 `UIElement / UILayout / UISlot`：`UIContainer` 只是第一个 layout host，
  持有 `UIBoxLayout`；它不再持有 `_direction/_spacing/_padding/...` 这类 box 字段。
  `UILayout` 只负责 measure/arrange，`UISlot` 是 parent-owned parent-child 边对象。
  `installLayout()` 的 host 由 `UIElement::createSlotForChild()` 直接问 layout 要 typed slot，不必再覆写工厂（Panel / TreeRoot / DockSpace / DockFloatingHost）。成员持有 layout 的 host（Button / CheckBox / Compound / SizeBox / Split / Scroll / Overlay / Container）仍自己转发 `createSlot`。
- `UIBoxSlot` 承载每 child 的 `Auto/Fill`、weight、**四边 `FMargin`**、cross alignment、
  min/max/preferred size 与 layout participation；slot setter 会使所属 tree 的 layout 失效。
  Fill 按权重分配剩余主轴空间且遵守 max size；Hidden 默认保留空间，可由 slot 明确关闭。
  Construct：`column.child(node, FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill, .margin = FMargin::all(8), .preferredSize = {120, 22}})`；
  `childFill` 仍是只标 Fill 的简写。`setMargin({x, y})` 走 `glm::vec2` → 左右/上下对称
  （`FMargin` 不是 aggregate，两元素列表不会变成 left/top、right/bottom=0）。
  `FBoxSlotArgs::preferredSize` 非零轴覆盖 child desired；`ui::boxSlot().preferredSize({w,h})` 是 construct-time 写法。
- UIElement 不提供 `setSize/setPosition/getSize/getPosition` 或 authored geometry shadow；运行时与 imperative 构造代码必须先取得当前 `UISlot`，再显式修改 `UICanvasSlot/ UIBoxSlot/ UIOverlaySlot`；detached 构造使用 `addDetachedChild(..., slotInitializer)` 或 builder 的 pending edge intent。最终 rect 通过 `getLayoutRect()` 读取。
- attach 不再从 child geometry 推断 slot；显式 `FCanvasSlotArgs` / typed slot initializer 才是 edge 的唯一 authored placement 来源。默认构造尺寸不是布局输入。
- `UIPopupOverlay::_contentExtent` 是 popup-owned canvas edge 的内容尺寸；基类 Auto + preferredSize，Menu 覆盖为 fixedSize，Dialog 走 preferredSize。不要再通过 child geometry API 写内容尺寸。
- child 用 `getSlot()` 读取当前边，parent 用 `getSlotForChild()` 查询；reparent/detach 时旧 parent 销毁旧 slot，新 parent 创建默认 slot。不要缓存 slot。层挂载默认使用 `attach(*tree.getLayer(layer), widget)`；带几何意图使用 `attach(parent, widget, FCanvasSlotArgs)` 或显式 layer args。
  裸指针跨越 reparent/detach。
- `UIBoxLayout` 主轴按 desired/slot 排列，cross 轴默认 stretch；`computeDesiredSize` 聚合
  child + margin + spacing + padding。scroll/split 仍读取内容 desired，specialized layout
  已收口为 `UIScrollLayout` / `UISplitLayout` / `UIOverlayLayout`；`UIButton`、`UISelectableRow`、`UICheckBox`、`UICompoundWidget` 与 `UISizeBox`
  使用 `UISingleChildLayout`。  `UIDockSpace` 也是 single-child host：投影根填满 dock。  `FDockTreeModel::exportLayoutJson` /
  `importLayoutJson` 按 panel `stableKey` 持久化 split/leaf 树（不持久化 NodeId）；
  `UIDockWorkspace::exportLayoutJson` / `importLayoutJson` 在同一 JSON 上附加
  `floating[]`（panel keys + pos/size + selected tab）。Editor 经
  `ConfigManager` `editor.dockLayout` 恢复，`UIDockWorkspace::appendOnDockUpdated`
  与 `appendOnFloatingUpdated` 写回。Editor chrome 打开 `bAllowFloating` /
  `bAllowTearOff`，`UIDockFloatingHost` 挂在 Popup 层。
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

## 静态 DSL（live construct）

- 默认路径：`ui::column/row/text/button/checkBox/slider/comboBox/image/textField/panel/splitPane/scroll/overlay/sizeBox/...` 组好 builder，再单独 `ui::build(tree, parent, std::move(page))` 物化 live `UIElement`（Slate `SNew`）。不要把整棵 DSL 包进 `ui::build(...)`。`setAnchors` / `fillParent` / panel `setCornerRadius` / panel+text `setStyleKey` / `setStyle`（freeze）/ `setStyleField`（单键 inherit）/ container `childFill` 与 `child(node, FBoxSlotArgs)` / overlay `child(node, FOverlaySlotArgs)` 在 Construct 时写到 live widget。`setTooltip` 写在 base builder；Text `setWrap` / `setMaxWrapWidth` 控制折行；base `setVisibility`；split `setPadding`。`ui::button` 没有 `setText`；文字走内部 `UIText` 子 widget。值更新走 `Reactive<T>`；已知结构走 `attach`/`detach`/`setVisible`。自定义 / 复杂 demo widget（MenuBar、TreeView、TableGrid、InputExtras、UIDragDropTile、DockSpace、SelectableRow）用 `child(UIElementRef)` 挂进 DSL 壳，不要为此扩 Construct。Gallery / Interactions / Dock / Workbench 内置 Editor demo（`FWorkbenchSurface::buildEditorDemo`）已是一次 `ui::build`。Editor 的 `rebuildItemRows()` 仍是事件期 live attach/detach `UISelectableRow`。弹层（Menu / Modal / Dialog）仍在点击时 live 组装。Dock floating host 仍 `attachToLayer(Popup, host, fill canvas args)`。Render 仍是 raw retained 对照。GameEditor chrome 已切到 `EditorSurface`（整窗 WidgetTree，不是 ImGui 内嵌 panel）。
- `UIDescription` / `UIReconciler` / `UIRenderController` / apply hook **已删除**。不要恢复 Description → apply → widget 转发层。
- Document/script：`UIDocument::instantiate()`（registry factory）只实例化一次。变长集合走列表控件 + `ReactiveList`，不是整页 re-run。
- `UIScreen` 是挂卸 / z-order / input blocking，不是每帧 `render()` owner。Gallery / Editor 不使用它；接到游戏多表面（HUD/模态）之前保持搁置。

## Render2D pass slot

- `Render2D` 不认识 game/editor pass；调用方经 `Render2D::acquirePassSlot()` 获取不透明
  `Render2DPassSlot`（每进程静态递增，上限 `FQuadRender::kMaxPassSlots = 8`），资源按 slot
  懒分配（GUI app 只用自己需要的 slot）。
- 映射位置：`Render2DComposePass::composePassSlot(kind)`（compose kind 各占一个 slot）、
  `RenderOverlay::viewportOverlayPassSlot()`。
- 管线 prep 必须在录制前：depthless（depthFormat==Undefined → uiPipeline）与 depth 变体
  （screenPipeline）按目标附件格式缓存。**运行时 UI 复合管线必须在首帧 world 渲染前 prep**
  （display image 在帧图执行期才创建；见 memory：first-frame prep 时序坑）。
- 单帧多批次 flush 共享一块 host-visible 顶点缓冲：每批次写不同区域 + `vertexOffset` 定位，
  容量 `MaxVertexCount × kFrameFlushSlots`，超限 assert（见 memory：multi-flush 覆盖坑）。
  kind 切换也算一次该 backend 的 flush，连续同类仍合批。
- Session 是 state-change batcher：`pendingKind ∈ {None, ScreenQuad, WorldQuad, Line}`。
  `makeSprite` / `makeText` / `drawRoundedRect` → ScreenQuad，`makeWorldSprite` → WorldQuad，
  `makeWorldLine` / wire → Line。kind 一变先 `flushPending()` 再切换，GPU draw 顺序 = emit
  顺序。禁止在 `end()` 里按 world→screen→line 固定倒空。clip 改栈 flush 的是 pending
  kind（三条 backend），不是只 flush screen quad。
- GUI snapshot 的 Line 仍 tessellate 成 sprite，不走 `FLineRender`。`FLineRender` 只用于
  对场景 depth 的 debug 线。2D 半透明叠放靠 painter’s algorithm，不要用 `pos.z` / depth
  解决 chrome 遮盖。
- clip 栈改动必须"先 flush 再改栈"；scissor 防御性 clamp 到窗口边界。

## GUI render surface

- `GUIRenderSurface`（`Runtime/Compose`）是 GUI compose target 的唯一资源边界：
  `createOffscreen()` 创建 Framework-owned `RenderImage`，`wrapExternal()` 包装
  imported swapchain image；二者都经同一 `prepare()` / `record()` 调用
  `Render2DComposePass`。
- surface 只拥有/保留目标 image、format 与最终 layout；**不** pump event、acquire
  swapchain image、present 或访问 live WidgetTree。window/present 仍属于 host，
  tree/snapshot 仍属于 WidgetTree。
- 最终 layout 是 surface 的不变量：offscreen 默认 `ShaderReadOnlyOptimal`，
  swapchain surface 为 `PresentSrcKHR`。调用方不能通过 compose desc 把二者留在
  错误 layout。
- 替换/销毁 surface 必须发生在 frame boundary，且旧 command buffer 的 submit 已完成；
  command recording 仅消费不可变 snapshot 与当前 surface。
- `RuntimeUIOffscreen` 是和 `RuntimeUIComposite` 分离的 compose kind / pass slot：
  可在同一 command buffer 中把**同一 snapshot**录制到 windowed 和 offscreen target，
  不复用 vertex/descriptor frame resources。`GUIAppHost` 的
  `--gpu-shot` + `--offscreen-shot` + `--offscreen-diff` 是零容差 parity 门禁。
- `replayUIFrameSnapshot` 把 snapshot 画进**已经 begin 的 raster pass**（不
  `beginRendering` / 不转 layout）。WidgetTree chrome 走这条路径：presentation
  pass 已经打开，WidgetTree 覆盖整个 swapchain，3D viewport RT 只作为 `UIImage`
  采样。管线 prep 用 **swapchain format**，slot 用 `EditorToolSurface`。不要把
  `GUIRenderSurface::record()` 塞进 presentation（它会自己 begin pass）。

## GameEditor chrome

- 启动时 **WidgetTree 唯一 chrome**：整窗 `EditorSurface` + `replayUIFrameSnapshot`；3D 仍离屏
  compose，树只采样那张 RT。`--editor-chrome=imgui` / `editor.chrome.host=imgui` 会被忽略并打 WARN。
- WidgetTree 输入：`EditorInputNode` → `WidgetTree::dispatchEvent`。
- ImGuizmo overlay 仍经 `EditorSurface::presentViewportGizmo` 走 `GuiSystem` begin/render/submit；
  `imgui-local` 因此仍是 editor 依赖，直到 gizmo 有 retained 绘制路径。
- `onImGuiRender` 编辑器 chrome shell（menu/toolbar/dockspace/viewport/debug/settings/project browser）已删除。
- Workbench 作为 WidgetTree dock panel 嵌入时用 `FWorkbenchSurface::buildUI(tree, parent)`，
  不要 `attachToLayer(Content)` 盖掉 editor root。Dock 只把**当前选中 tab** 的
  panel widget `addDetachedChild` 进树；未选中的 panel 是 detached subtree。
  因此 `buildUI` 前要把 host 临时 `attach` 到 editor tree，建完再 `detach`，
  交给 workspace 之后再 graft。未挂上时 `updateUI` 不能再 `tree.attach`。
- WidgetTree chrome teardown：`EditorSurface::shutdown` 必须在 compositor / VMA
  之前丢掉 tree、snapshot、viewport wrap；随后 `FontManager::clearCache()`，
  否则 RuntimeDefault atlas 会以 dedicated allocation 活过 allocator Destroy。
- 原 ImGui editor chrome shell（`onImGuiRender` / menu / toolbar / dockspace / viewport window）已删除。
  `imgui-local` 仍因 ImGuizmo overlay 与 `FilePicker`/`TypeRenderer` 残留而保留。
  `IGuiBackend` 仍是 ImGui 形，不要强迫 EditorSurface 走它。
- WidgetTree chrome 的 theme 走 `buildEditorTheme`（`GameEditor/UI/EditorTheme.h`），
  不要直接调 `buildWorkbenchTheme`。Chrome 文案用 `text.header` / `text.muted` /
  `text.error` / `text.eyebrow`，不要 `setColor` 字面量（显式着色会盖掉 theme）。
- `SelectionModel` 是 identity 选择源（`GUI/Binding/SelectionModel.h`）：selected 有序集合 + primary（空或不在集合外）+ hover/active/focus。不持有 Entity*。控件绑 `primaryRef()`；多选走 `add`/`toggle`；`replace` 批量同步 viewport 多选。Hierarchy 仍写 `EditorLayer`，`syncSelectionFromLayer` 按 `selectionGeneration` 把 layer 选择映射为 `e:{uuid}` / `ui:{entryId}` 写回共享 model。
- `ActionMap` 是 identity 命令表（`GUI/Binding/ActionMap.h`）：菜单、快捷键、toolbar 都 `execute(id)`。`FActionChord::primary` 在 macOS 是 Cmd、别处是 Ctrl。WidgetTree 未处理的 KeyPressed 才走 shortcut；文本焦点下只匹配带 modifier 的 chord。`UIMenu::FItem::fromAction` 生成同一 execute 的菜单行。
- `UndoStack` 是 identity 撤销历史（`GUI/Binding/UndoStack.h`）：`push` 记录已应用的 undo/redo 闭包，不在 push 时调用 redo。`beginMerge`/`endMerge` 把同一 `mergeKey` 的连续 push 收成一步（拖动）；`UndoTransaction` 把嵌套 push 收成一步。栈不持有 Entity*。`edit.undo` / `edit.redo` 走 ActionMap（macOS Redo 是 Cmd+Shift+Z，别处 Ctrl+Y）。Inspector 拖动 `UIDragFloat` 在 `_onDragBegan/Ended` 开闭 merge；`setValue(..., false)` 是 sync，不进 undo。Gizmo / viewport 选择仍未接入。
- `PropertyGraph::project` 是反射字段 → editor field model 的入口（`build` + `PropertyProjectionRegistry`）。Transform projection 负责显示名和 `setPosition/setRotation/setScale` 写回。Inspector 对多选的 **交集** component 物化 `EditorAutoPropertySection`；`UIDragFloat` mixed 显示 "—"，编辑写回全部 instance，undo 按 instance 快照恢复。enum 字段走 `UIComboBox`；`.color()` 元数据的 `glm::vec3`/`glm::vec4` 走 `UIColorEdit`（非 color vec3 仍走 DragFloat）。`TextureRef`/`ModelRef`/`MeshRef` 走 path `UITextField` + Browse；Browse 经 `EditorAssetPickerCallback`（widgettree：`EditorLayer::setAssetPickerHandler` → `EditorSurface::openAssetPickerDialog`；legacy imgui：`FilePicker`；`EditorInspectorTab` 注入，framework 不依赖 `EditorLayer`）。`PropertyHandle::validationError` 读 manipulate spec 范围；`hasAssetResolveError` 对 failed resolve 画 error fill；`UIDragFloat`/`UITextField` `setError` 画 error fill。`UIImage` 对缺失 asset / `setResourceMissing` 画 error fill。没有 retained 可编辑字段的类型跳过。ImGui `DetailsView` 实现已在 Phase 8N 删除；`EditorInspectorTab` 是实体/component 唯一正式 Inspector UI，并显示 Game UI Entry 摘要 + Open in UI Designer。
- `EditorSurface` Content Browser：`FileExplorer` 管 mount/目录/搜索枚举；`UIKeyedChildReconciler` + `EditorListRows.h` 物化 mount/entry 行；entry 列表用 `computeKeyedVisibleWindow` 窗口化。fingerprint 含 search + selected path；`selectContentItem` 写 `setSelectedPath` 并对纹理调 `inspectAsset`。ImGui `ContentBrowserPanel` 已删（Phase 8A）；`ContentBrowserPanel` 仅保留 FilePicker 图标加载。
- `UITreeView` 在 `UIScrollViewport` 内只 paint 可见行窗口（`computeKeyedVisibleWindow` + `getPaintedRowCount`）；`EditorSurface` Hierarchy 用 scroll 包裹。flatten/hit-test 仍读全量可见行；无 per-row widget。`bindFilter` + `HierarchyFilter` 搜索框过滤节点；`setReorderable` + `moveEditorHierarchyEntity` 支持 scene 实体拖放重排（`ui:` 条目仍不可重排）。ImGui `SceneHierarchyPanel::sceneTree` 已删（Phase 8O）；`SceneHierarchyPanel` 仅保留 viewport 选择总线 API。
- Viewport overlay：`FEditorViewportHostState` / `IEditorViewportOverlay` / `EditorViewportOverlayHost`；`EditorSurface::syncViewportHostState` + hover 时 overlay dispatch。ImGuizmo 仍经 `presentViewportGizmo` 绘制。

## Style / Theme

- 机制在 framework：`UITheme` + `resolveThemeStyle` + generation token。值在 app：
  WorkbenchTheme（demo 壳）/ EditorTheme（GameEditor chrome）。
- Resolve：稀疏 patch（JSON 键 = 反射字段名）overlay 到 theme 的 dense `TStyle`。`setStyle(TStyle)` 写全字段 = full freeze（不登记 theme 边）；`setStyleField` / Text·Panel `setColor` 只盖出现过的键，其余 inherit，**必须**登记 generation + style Reactive。空 / null / `{}` = 无覆盖。
- Field impact：`lookupStyleFieldImpact` / `lookupStylePatchImpact` 用反射分类字段，而不是 per-type 表。`FBrush` → Paint+Resource；`fontSize` / `padding` / `minSize` → Layout；`FScrollBarStyle.width` 是 overlay 绘制厚度，不是 Layout。`setStyle` / `setStyleField` / `clearStyleField` / `clearAuthoredStyle` 默认走 catalog；`UIText::setFontSize` 等仍可显式覆盖。`bResource` 是 metadata；异步就绪不走 `invalidateProperty`。Font 由 `FontManager::resourceRevision()` 在 `WidgetTree::buildSnapshot` 消费：revision 变化则整树 `markLayoutDirty(ResourceReady)`（嵌套 fill 容器不能 skip 过期文字度量）。Texture 由 host `UIFrameBuildContext.generation` 作为 ResourceReady 丢 paint cache。Host 只 `flushPendingGlyphs`，不要再 `invalidateSubtree`。
- Visual fill matrix：`composeVisualFlags` + `FVisualChrome` + `resolveVisualFill` 是 exclusive 优先级（Disabled > DropTarget > Error > Pressed > Selected+Hovered > Selected > Hovered > Focused > Normal）。`visualChrome(style)` 覆盖 Button / SelectableRow / CheckBox / ComboBox / MenuBar / Tab / MenuItem / TableGrid / TreeView 行填充。CheckBox 的 checked 与 Tab 的 selected 映射为 Selected，且 Selected+Hovered 回落到 Selected（保持原“选中盖住 hover”）。Table/Tree 未选中且未 hover 的 normal 是透明刷，paint 仍按 alpha 跳过。SpinBox/Radio/TextField/DragFloat 等非这套 chrome 的控件不硬套。
- Key catalog：`YA_GUI_STYLE_CATALOG` / `StyleKey::*` 是 theme key + `TStyle` 的单一词汇。`lookupStyleKey` 校验 `define` / `setStyleKey` / document deserialize；未知 key 与类型不匹配记入 `StyleCatalogDiagnostics` 并 `YA_CORE_WARN`，不拒绝写入。空 key 表示不查 theme。`editor.<key>` 是同一词汇的 GameEditor overlay，不是第二套机制。`canvas` 是无 chrome 的 panel 角色 key（`ui::canvas()`）。
- 控件 paint/layout 读 `UIStyledWidget::resolvedStyle()`（dense cache）。merge（theme base + 稀疏 patch）在 dirty/recompute 时发生，不在每帧 paint 热路径。`resolveWidgetStyle` 仍是无缓存计算路径，给测试断言和非 `UIStyledWidget` 节点（如 ColorEdit 色板）用。cache 不落盘。
- 高频路径是实例 `setStyle` / `setStyleField` / `setStyleKey`（DSL 基类 builder 已暴露）；切 theme 是低频目录切换。未盖满的控件在切皮肤时未覆写字段跟着变。
- `_styleKey` 在 `UIElement` 上反射；稀疏 patch 经 `YA_GUI_AUTHORED_STYLE_IO` 虚函数写入 UIDocument 的 `_authoredStyle`（mixin 字段不能 `YA_REFLECT_FIELD`，MI 偏移不对）。缺键 = inherit；旧文档的全字段对象仍是 freeze。`FBrush`/`F*Style` 走运行时反射，merge 用 `deserializeProperty`。
- `FBrush`：纯色 = 无 resource + tint；Image 整张拉伸；NinePatch/Border 按 `margin`（纹理 px，1 tex px = 1 logical px）切成最多 9/8 个 snapshot sprite，compose 经 `uvScale`/`uvOffset` 透传。无纹理尺寸时退回整张拉伸。`sliceBrush` 是纯函数。
- `UIPanel` paint 只读 `resolvedStyle().fillColor`。无 theme 时 `_color` 是 fillColor fallback（与 `UIText` 的 `_color`/`_fontSize` 相同）；不要再走第二套 `_color` sprite。Image 是 content：无 authored overlay 的 themed panel 仍画 theme chrome。
- 无 theme / 缺纹理 / 延迟就绪的 GPU 输入就是 snapshot：miss 时 image 画 `placeholderFill`，`UIFrameBuildContext.generation` bump 后 resolver 命中才带 texture。Headless host 与 windowed compose 消费同一份 packet；windowed `--gpu-shot` 像素门禁仍是 Phase 9。
- 族 key：`panel` / `button` / `text` / `menubar` / `tab` / `split` / `scrollbar` /
  `dock` / `floating` / `image` / `popup`。角色 key：`panel.window` / `panel.canvas` / `panel.sidebar` /
  `tab.dock` / `tab.sidebar` / `text.header` / `text.muted` / `text.error` /
  `text.eyebrow` / `menu.panel` / `tooltip` / `drag.ghost` / `drag.source` /
  `drag.target`。表单 key：`tree` / `textfield` / `menu` /
  `selectable` / `dragfloat` / `checkbox` / `combobox` / `slider` / `table` /
  `spinbox` / `radio` / `coloredit` / `searchcombo`。
- `editor.*` 前缀只用于 GameEditor 显式覆盖，不是第二套词汇。
- Shell 控件（Panel/Button/Text/MenuBar/Tab/Split/Scroll/Dock/Floating）和已接线的
  表单控件（TreeView/TextField/Menu/SelectableRow/DragFloat/CheckBox/ComboBox/
  Slider/TableGrid/SpinBox/Radio/ColorEdit chrome/SearchCombo）以及 Image 占位 /
  Popup 遮罩 / DragDrop tile paint 时读 `resolvedStyle()`；几何（rowHeight/indent/thumbSize）留在 widget。
  实例覆盖走 `setStyle(TStyle)`（freeze）或 `setStyleField`（单键 inherit）；Text/Panel 的 `setColor` 只 overlay 颜色（Paint 粒度），字号等跟 theme。
  列表行标签走 `text` key，不要 `setColor` 冻色。布局宿主（Container/Overlay/SizeBox/DockFloatingHost）无 chrome paint。

## Host（ya-gui-app-host）

- 顶层命名：`GUIApp` 是 standalone GUI 的装配层（当前一个 primary
  `GUIWindowHost`）；`GUIWindowHost` 是一窗口一 tree / SDL window / presenter /
  pointer context 的真实 owner。新代码只使用 `GUIApp` / `GUIWindowHost`，不得新增或恢复
  `GUIAppHost` 兼容别名。
- 生命周期：init → run（SDL event → WidgetTree dispatch → snapshot → compose → present）→
  shutdown。resize 只在帧边界重建 presentation 资源。
- `GUIHeadlessHost` 是同一 AppKernel/WidgetTree/delegate 合同的无窗口变体：只产生
  immutable `UIFrameSnapshot`（可由 callback 检查/落盘），不创建 SDL window、RHI、
  swapchain 或第二套 run loop。它用于 automation、结构断言与 windowed/offscreen
  交叉取证。
- 诊断：`--dump-snapshot=path --dump-frame=N`（CPU 侧 BMP 光栅化快照）、
  `--dump-snapshot-json=path --dump-frame=N`（snapshot 几何/clip/text JSON + digest）、
  `--gpu-shot=path --gpu-shot-frame=N`（GPU readback BMP）。内置纹理 resolver
  （`builtin/white|black|multipixel|checkerboard`）供 image 控件在无资产系统时使用。
- Glyph flush：`flushPendingGlyphs` 仍在 snapshot 之后、command recording 之前（Core Rule 6）。缺失字形的 layout/paint 由下一帧 `WidgetTree` 消费 `FontManager::resourceRevision()` 驱动，host 不要再 `invalidateSubtree`。Texture 就绪靠 bump `UIFrameBuildContext.generation`。
- **teardown 铁律**：任何持有 GPU 资源的成员（readback buffer、shader storage、widget tree、
  command buffers、presentation targets）必须在 `delete render`（VMA 销毁）前释放
  （见 memory：VMA teardown 顺序坑）。

## 编辑器内嵌（已废弃）

- 旧路径 `GUIWorkbenchPanel` / `FrameStatsPanel` 把 WidgetTree 合成到离屏 RT 再
  `ImGui::Image`。GameEditor chrome 已切到整窗 `EditorSurface`，不要再扩这条桥。
- `EditorToolSurfaceCompositor` 仍保留 shutdown，但 presentation 不再 compose
  workbench 离屏图。

## 构建 / 测试

```bash
make b t=GUIWorkbench && make r t=GUIWorkbench          # standalone demo
make r t=GUIWorkbench ARGS="--smoke-actions"            # 端到端自动化
make run t=HelloMaterial / make run-editor t=HelloMaterial
xmake b ya-gui-closure-test && xmake r ya-gui-closure-test
make test-gui                                            # closure + widgets + workspace
```

macOS / MoltenVK convergence gate (must be run on macOS, not emulated from a
Windows runner):

```bash
python3 Script/gui_convergence_macos_validation.py
```

## 控件交互契约（编辑态 / 弹出态规则）

这些是 2026-08-18 Gallery 交互验收轮沉淀的硬规则，写新控件时逐条自查：

1. **编辑模式不吞底层交互**：进入编辑态（_bEditing）后，控件原有的业务交互
   （SpinBox 的 +/- 步进、拖拽、打开菜单）必须仍可达——编辑态 press 分支要先
   commit/cancel 编辑再执行原交互，绝不能 `return false` 把 press 吞掉。
2. **弹出控件的 dismiss 必须释放交互残留**：菜单/弹出被真实关闭（外部点击/Esc/
   选挑）时，dismiss 回调要一次清完——filter 清空 + 主动 `setFocus(nullptr)`
   （否则控件继续画 '(type to filter)' 等占位态，用户要再点一次才恢复）。
3. **可见内容集变化必须同时标 Paint**：Reactive 的 Layout 粒度通知只保证重排；
   若重排后排列 rect 不变（固定高度树/表），增量 paint 缓存会继续画旧内容。
   `setExpanded/toggleExpanded` 这类「行集变化」必须显式 `markPaintDirty()`。
   任何影响可见行/可见项的状态变化都按此处理。
4. **弹出刷新 ≠ 真实关闭**：「关旧开新」的刷新路径会触发旧菜单 dismiss 回调；
   回调里的清理逻辑（清 filter 等）必须用刷新标志（_bRefreshingMenu）隔离，
   只在真实关闭时执行。
5. **过滤/搜索的自动展开是一次性的**：过滤变化时自动展开匹配链（记录
   `_lastFilterApplied` 防重复），之后手动折叠/展开必须仍然生效——过滤
   不能持续强制展开（TreeView「filter 激活时箭头失效」即此坑）。清空
   过滤永不收拢任何东西。
6. **demo 的约束性行为要有可见文案**：选择性 accept（drop target 谓词）、禁用
   条件等「看起来像 bug」的设计，必须在控件 label / 页面说明里写明
   （如 'Zone B: only payload.2'）。

## 人肉测试前的自动化验收

按 `Example/GUIWorkbench/Scenarios/` 的 jsonl 回放做第一道闸（跑法：
`xmake run GUIWorkbench --start-page <X> --scenario <abs path> --scenario-dump-dir <dir>`，
exit 0 = 全 checkpoint 过）。写验收场景时的覆盖要求：

1. **状态 × 交互组合矩阵**：有编辑态/弹出态的控件，必须覆盖「进入状态 → 各交互」
   的关键项（编辑态 × {键入, Backspace, Enter, Esc, +/- 点击, 外部点击}；菜单开 ×
   {过滤输入, 选挑, Esc, 外部点击}）。单条 happy path 会漏掉状态组合 bug
   （SpinBox 编辑态吞 +/- 即如此）。
2. **焦点生命周期断言**：弹出类控件关闭后断言 `focusPath` 不含该控件（或为空）——
   只断言 popup 结构开合不够（SearchCombo 两次点击 bug 即漏在焦点上）。
3. **数据副作用断言**：断言要锁到值级（filter 字段、visibleRows 计数），不能只锁
   控件存在/结构（filter 被刷新清除的 bug 就溜过了只查 popup 结构的断言）。
4. **双布局变体**：TreeView/Table 的行集变化测试，autoSize 和固定高度两种布局都要
   覆盖——前者 rect 变化掩盖了「Layout≠Paint」漏画，后者才暴露。
5. **渲染级验证走真机**：scenario 模式不渲染帧（buildSnapshot 不跑），G2 校验帧
   （漏标脏告警）和像素验证必须用真机：`--automation-control-port` 启动 + sleep +
   `quit` JSON-RPC 收日志。行为断言归 scenario，渲染断言归真机，两条线分工。
6. **环境随机崩溃重试**：GUI 反复启动偶发 init 崩溃（0xC0000005，swapchain 创建
   阶段，与场景内容无关）。自动化脚本对场景运行加重试（每场景最多 4 次，间隔
   1.5s），不要在单次失败上误判回归。
