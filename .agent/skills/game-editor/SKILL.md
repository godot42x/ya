---
name: game-editor
description: GameEditor（Application 层）的窗口/工作区/chrome 结构与编辑会话契约：WindowRootEditor、nested owned tools、canvas、主题、输入与撤销。
---

## 适用场景

- 改 `Engine/Source/Applications/GameEditor/` 的窗口、工作区、tab、chrome
- 排查编辑器 tab 归属 / dock placement / nested tool 可见性
- 改编辑器 theme、Inspector 属性投影、Content Browser 或 UI Designer 画布

## 边界

这是 **Application 层**产品内容（`Engine/Source/Applications/GameEditor/`），
不是 GUI framework。framework 侧的 WidgetTree / 控件 / 布局 / Dock 权责见
`../gui-framework/SKILL.md`；本 skill 只讲编辑器如何**用**这些能力组装产品。

## GameEditor chrome

- Module 经 `EditorWindowRegistry::find(kDefaultEditorWindowId)` 调
  `EditorWindowSession::tick`；Surface 仍做窗口内编排：`rebuild-if-needed` → window metrics →
  `WidgetTree::tick` → shell dialogs → push viewport display → `buildSnapshot` →
  viewport host bridge。禁止 `tab->sync`，禁止 Surface 持有 Tab 控件指针。
  `selection` / `actions` 在窗口的 `EditorRootSession`（Level / UI / Material / Script）；
  窗口 `activeRoot()` 仍是 Level。`undo` 在绑定的 `EditorDocumentSession`（无 document 时回落到
  RootSession 本地栈）。`EditorDocumentRegistry` 归 `EditorModule`，按
  `(kind, key)` 单例；两扇窗绑同一 scene key 共享 undo。Surface / WindowSession
  不拥有 registry。UI Designer 是 `WindowRootEditor`（`kUIEditorRootId`），Preview /
  Palette / Tree / Inspector 是 nested owned tools，走 UI document 的 dirty /
  RejectIfDirty close / per-kind preview claim（不是 Camera）。画布是 UI Designer 自己的
  Canvas tab（`EditorUICanvasTab`，stable key `ui-preview`）：pan/zoom/extent/image 在
  `EditorUIDesignerSession::canvas()`，手势在 tab 的 `handleInputEvent`（pointer capture），
  `EditorUICanvasCompositor` 只在 tab attach 时录制；Level 视口没有 2D 模式，
  `EditorLayer` / `EditorInputNode` 不含任何画布分支。Material/Script 同样是 WindowRootEditor + nested document tools
  （identity/dirty/undo chrome，还不是 material graph / script AST）。Owned tool 带
  `ownerEditorId`，dock 政策是目标 dock scope + owner。`canSpawnEditorTab` 管
  Window-menu / invoke / drop / redock / layout restore，是唯一一处 placement 谓词：
  window root 是**页签井**，只收 `WindowRootEditor`（Level / UI / Material / Script）；
  owned tool 只进同 owner 的 nested dock。**不存在窗口级 tool scope**——那正是
  「切到 UI 页仍看到 Level 工具面板」的根因：窗口级工具住在 window-root 的 tools 叶里，
  与当前页签无关。Level 的 Content/Stats/Runtime/Render/Assets/Debug/Fonts 因此是
  Level-owned tool，跟 Viewport/Hierarchy/Inspector 一起住在 Level 自己的 nested dock；
  切页签＝整块工作区一起换。保存布局若把非页签面板留在页签井（老布局），
  `repairPlacement` 关闭它们而不是再造一个 tools 叶：页签井不承载 tool，也没有可
  re-home 的窗口级 dock。`FDockContext`
  不认识 editor root。UI/Material/Script 的 nested dock 由 `EditorNestedDockHost`
  持有，不把 Surface 做成 dock manager；这些 nested dock 关闭 floating/tear-off（C7）。
  C5 起 owned tool 只进同 owner 的 nested `FDockContext`（window-root 不再扁平物化）。
  Level Editor 是 Locked、不可关闭的 window-root tab，内部 `UIDockSpace` 投影 nested dock。
  `editor.dockLayout` v5 信封为 `{version:5, windows:[{role, bounds, monitor, maximized, windowRoot, ownedNested, ...}]}`。
  每个 record 内嵌 v2 dock 文档：`tree` 只放节点结构（split + `{kind:"leaf",id}` 引用），
  `dockSpace` 按叶 id 放栈数据（role/hideTabBar/persistentEmpty/panels/selected），
  `floating[]`/`windows[]` 仍是 placement 记录。v1–v4 旧文档不再映射，直接回退工厂布局。
  Dock overlay `floating[]` 坐标仍是 tree-local。
  Extra OS window 只经 `IGUIWindowCoordinator` restore（`restoreEditorExtraWindows`）；
  GameEditor 不创建 SDL 窗。坏 monitor 经 `recoverWindowScreenPlacement` 迁到可用屏
  （按 index，否则按 name，否则 primary）；未知 origin（index<0 且无名）只改 size。
  缺失 documentKey / 未知 ownerEditorId 丢弃该 extra，不改绑到其他 root。
  `closing: true` 与空 torn-off extra 不恢复。产品路径仍只 tick/present 默认 native
  window；主窗启动会 recover 已持久化的 screen placement。
  `FEditorTabSpawnContext` 是 identity（windowId / scope / optional
  owner / document / placement / detach）加来自 owner session 的可选指针，以及
  窗口注入的 `app` / `presentSurface`（不是 `App::get()` / `primarySwapchain()`）。factory
  只创建 UI content，不持有 `EditorSurface`、不拥有 tree、不调用 `tick()`。Viewport spawn
  需要 viewportHost；Inspector spawn 需要 layer+SelectionModel。Content 经 Layer
  `cmdLoadScene`（`.scene.json`）或 `openDocumentEditor`（`.lua` / `.mat`）；Runtime Tools
  经 ActionMap + present surface。第二扇
  `EditorWindowSession` 只走 MW-401（独立 tree/dock，禁止两树一窗、禁止每窗
  `IRender::create`）。禁止把这些模型倒进 WindowSession god object。
  Tab 经 `EditorTabSpawnerRegistry` 注册，`EditorDockWorkspace::invokeTab` 按 stable key
  激活或 spawn（owned tool 若 owner 不是当前 host，先打开对应 WindowRootEditor 再进其 nested）。layout 工厂是
  `EditorDockWorkspace.cpp` 顶部的 JSON 字面量（唯一事实源，运行时不读磁盘布局文件）：
  window-root 与 Level nested（hierarchy | play-toolbar 在 viewport 上 | inspector），
  外加 UI/Material/Script 的 nested factory。Window 菜单
  checkbox 切换已注册 tab；Layout → Default 一键恢复工厂布局（先
  `setPanelClosable(true)` 再关 Locked tab）。rebuild 期
  dock/workspace 政策在 `EditorDockWorkspace`，ActionMap 目录在
  `registerEditorActions`。`onAttached` 拉权威状态并订阅所属边界的 `MulticastDelegate`，
  `onDetached` 按 handle 退订。未选中 dock tab 是 detached subtree，不会 tick。
  不要再引入 `EditorPanel`、中心 MessageBus，或 `EditorTabRegistry` 那种 `std::function`
  袋子。结构见 `.agent/plan/archive/gui-editor-tab-lifecycle/`。
- 启动时 **WidgetTree 唯一 chrome**：整窗 default `EditorWindowSession`（持有 `EditorSurface`）+ `replayUIFrameSnapshot`；3D 仍离屏
  compose，树只采样那张 RT。`--editor-chrome=imgui` / `editor.chrome.host=imgui` 会被忽略并打 WARN。
- WidgetTree 输入：`EditorInputNode` 绑 `EditorWindowSession*` → session `dispatchEvent` →
  `WidgetTree::dispatchEvent`。
  所有权分层（不要在 Router 里每条事件 `cancelHeldKeys`）：
  1. Session 先把事件交给 chrome 树。Viewport gizmo overlay **只在 LMB drag** 时 Exclusive；hover 一条轴不得吞 MouseMoved，否则树的 hover 冻在 viewport 上，dock/split 收不到 press。
  2. Overlay 命中用 **指针是否在 viewport imageRect 内**（或 gizmo 正在 capture），不要用 stale 的 hover/focus。
  3. 点在 viewport image 上 `takeKeyboardFocus()`。Image 必须 `Focusable`，否则 WidgetTree 会 `setFocus(nullptr)`，WASD 只剩 hover 碰巧有效。
  4. Viewport 3D 右键菜单在 **release** 打开（press 记 pending；移动超过 ~4px 视为 look）。同一时间一个 viewport menu。
  5. RMB look 是 `EditorInputNode` 上的会话（`_bLooking`），不是“本帧 IM 里 RMB 还在 + stale viewportMouse”。按住期间 MouseMoved/WASD 继续进 `InputManager`，即使指针离开 image。
  6. 只把 **IM 当前按住的** KeyReleased / MouseButtonReleased 回写 IM。键盘所有权从相机丢掉的那一次（焦点/hover/look 全无）才 `cancelHeldKeys()`。
  7. `shouldCaptureInput()` = viewport hover/focus **或** RMB held。`FreeCameraController` 只读 IM，不读 WidgetTree。
  8. `UISplitPane` 分隔条双击（400ms / 6px，与 SpinBox 相同时钟）把 ratio 重置为 0.5，并写回 binding / dock callback。
- Viewport gizmo 已改为 retained host + native math controller：`EditorViewportGizmoOverlay`
 只路由输入，`EditorViewportGizmoController` 负责世界空间 translate/rotate/scale 与 undo，绘制在
 `EditorModule` 的 viewport compose callback 中走 `Render2D`（`layer.gizmo().recordOverlay()`）。
- Camera preview 是 **chrome，不是 runtime compose inset**：`EditorViewProducer` 声明的 preview
  View（`previewKey()`，owner 2 + local 2）只渲染到自己的 RT，`composeRect` 为空，所以 runtime 不把它
  blit 到世界 RT（那样世界 overlay——x-z 网格 / manipulator / 相机视锥线框——会盖在预览上，因为它们
  是世界空间内容，而 inset 是后画的）。同帧顺序不变：`onViewportCompose` 里 `EditorModule` 读
  `getViewOutput(producer.previewKey().viewId())` → `EditorLayer::setViewportPreviewImage` →
  `EditorSurface::pushViewportDisplay`（device → layer → chrome 三段，和世界 viewport 同一条）→
  `EditorViewportTab::setPreviewImage`；Tab 内部是 `UIOverlay` 叠两兄弟：世界 `UIImage` 占满，
  `UIBorder`（`panel` 主题 + 1px padding）包住预览 `UIImage`，End/End 对齐、位置由 slot 的
  padding + preferredSize 表达（不要手写 rect）。preview View 的 features 只含 `Game`：
  预览=该相机所见，不画编辑器 gizmo。面板尺寸/位置唯一拼写是
  `EditorViewProducer::previewRect(imageRect)`（Tab 与 Layer 都从它取），所以 chrome 摆放与 Layer
  的输入排除不会各自漂移。**世界交互必须先问宿主**：`EditorViewportTab::isWorldPoint`（= `pickAt(point)
  == ViewportImage`）为 false 时 `screenToViewport` 直接返回 false、overlay candidate 也不成立，
  否则点在预览面板上会去 pick 面板下面的世界对象。收起预览 = 推 null 纹理（Collapsed），不保留空面板。
- **视口不在屏上就不声明 View**。视口是 dock tab，`UIDockSpace::rebuildStack` 在非选中时把它的 widget 从树上 detach（detach 递归到持有该 stack 的 level editor tab），所以“用户在看 Inspector”是常规情况而不是错误态。可见性由 widget 自己的 attach/detach 边写进 `EditorLayer`（`_shownViewportCount`，**计数不是 bool**——第二个编辑器窗口有自己的 chrome 和视口，一个窗口切 tab 不能停掉另一个），`EditorViewProducer::collectSceneViews` 先读 `isViewportShown()`，为假直接 return。没声明就没有 View，也就没有 family / target / graph——**不要**让各 pass 自己去跳过，也不要用退化几何（extent 0）表达隐藏：可见性是 widget 树的事实，几何是布局的事实，混在一起每个读点都得重新猜，而且折叠 / 未布局 / `describesPixels` 刚失败三种情况都会产生退化几何，判不准。detach 时同时清 hover/focus，否则编辑器相机会继续吃本该给新 tab 的输入。
- `onImGuiRender` 编辑器 chrome shell（menu/toolbar/dockspace/viewport/debug/settings/project browser）已删除。
- GUIWorkbench 是独立 Feature Gallery（`FWorkbenchSurface` 挂 GUIApp）。左侧 rail 是
  分组 `UISelectableRow` 列表（`UIScrollViewport` + group `UIText`），**不是**垂直 `UITabBar`。
  `--start-page` / smoke 一律按页名 `selectPageByName`，禁止 `tabs[N]`。
  分组与页名：
  Diagnostics=`Render`；Controls=`Widgets`/`Inputs`；Layout=`Box`/`Hosts`/`ScrollSplit`；
  Paint=`Brush`；Text=`Text`/`Fonts`；Style=`Theme`；Overlays=`Menus`/`Dialog`；
  Interaction=`DragDrop`/`Enable`；Data=`Binding`/`Tree`/`Table`；
  Composition=`Dock`/`DSL`/`Editor`。
  旧名别名：`Layout`→`Box`，`Gallery`→`Binding`，`Modal`/`Interactions`→`Dialog`，
  `Unicode`/`中文测试`→`Fonts`，`RoundedRect`→`Brush`。
  页源码在 `Example/GUIWorkbench/Source/Pages/`，builder 仍从 `WorkbenchDemoPages.h` 转发。
  GameEditor 不把 Workbench 做成 dock tab。
  Dock 只把**当前选中 tab** 的
  panel widget `addDetachedChild` 进树；未选中的 panel 是 detached subtree。
- WidgetTree chrome teardown：`EditorSurface::shutdown` 必须在 compositor / VMA
  之前丢掉 tree、snapshot、viewport wrap；随后 `FontManager::clearCache()`，
  否则 RuntimeDefault atlas 会以 dedicated allocation 活过 allocator Destroy。
- 原 ImGui editor chrome shell（`onImGuiRender` / menu / toolbar / dockspace / viewport window）已删除。
  `TypeRenderer` / `FilePicker::render` / `FileExplorer::render` / editor ImGui texture
  bridge / GameRuntime `GuiSystem`/`ImGuiSystem` 已删。进程不再链接 `imgui-local`。
  不要再引入 ImGui-shaped `IGuiBackend` 或强迫 EditorSurface 走它。
- WidgetTree chrome 的 theme 走 `buildEditorTheme`（`GameEditor/UI/EditorTheme.h`），
  不要直接调 `buildWorkbenchTheme`。Workbench `text.header` 是 page title（`gui_type::kTitle` 28）。
  Editor 字号走 `editor_type`（`kHeader` 14 / `kBody` 12 / `kCaption` 11），由
  `buildEditorTheme` 写进 family 文案角色（`text.header` / `text.muted` / …）和
  `editor.<key>` overlay。Inspector 输入用 `editor.textfield` / `editor.dragfloat` /
  `editor.coloredit` / `editor.combobox`，**不要** `setFontSize`，也不要盖掉 family
  `textfield`（Designer canvas 仍是 gallery body 16）。Workbench 字号仍走 `gui_type`
  与 `textfield.compact`。Chrome 文案用 `text.header` / `text.muted` /
  `text.error` / `text.eyebrow` / `text.small` / `text.caption`，不要 `setColor` 字面量。
  Inspector ColorEdit / DragFloat 的内边距是 `FColorEditStyle::padding`（host row）/
  `FDragFloatStyle::padding`（ImGui FramePadding / ItemInnerSpacing），不要把
  `"R 0.60"` 画成一串居中字符串。`UIColorEdit` 是 `UICompoundWidget`：色板 + 每通道
  `UIDragFloat`（`_prefix` 为 R/G/B/A，前缀在格内左侧，不是旁边再叠一个 `UIText`）。
  通道 hover/drag 填色只走 `FDragFloatStyle`，不要在 `FColorEditStyle` 再复制一套。
  SV picker 仍是 paint leaf（两层 1D 顶点色），不是更多 widget。DragFloat
  `isHoverable()`，拖动时 `ResizeEastWest`。`UITextField` 同样有 `FTextFieldStyle::padding`，
  paint 把文字/caret clip 到 padded inner rect，并在焦点下横向滚到 caret；不要自适应字号，
  也不要为了长路径撑开 Fill 表单行。`TextureRef` 是「path 填满一行 + Browse + Show」，预览在下一行；
  禁止把缩略图、路径框、Browse 塞进同一行。Show 经 `EditorRevealAssetCallback`
  （`EditorLayer::revealInContentBrowser` → `EditorSurface::showContentBrowser` / `invokeTab("content-browser")`
  → `FileExplorer::setSelectedPath`）。`invokeTab` 必须先激活 **本窗已有实例**（window-root 或
  Level nested，用户把 Content 拖进内层 dock 之后仍在），禁止只查当前 dock 再 spawn 第二份。
  `propertyLabelFromPath` 的 `" / "` group 是 expander **路径**，
  Sampler Config 嵌在 Texture Slot 里，不要做成同级 collapsing header。
- Canvas `fill()` / `anchor({0,0},{1,1})` 的 `offset({x,y})` **只移动 min 角**，span 仍是父矩形全高/全宽
  （`resolveCanvasRect`）。菜单下的 Dock 必须用
  `insets(FMargin{left, top, right, bottom})` 收缩 fill，不能 `fill().offset({0, chromeTop})`。
  Hierarchy 过滤条与 tree 是 column 兄弟，不要 canvas overlap 再靠 insets 让位。
  Play/Stop/Simulate 是 Level owned tool tab（`play-toolbar`），工厂布局在 Viewport
  上方；不是 chrome strip，也不并进 Runtime Tools。
  合同：`WidgetLayoutTest.CanvasFillOffsetDoesNotShrinkTheChild` vs
  `CanvasFourSideOffsetsInsetTheChildWithoutAnExplicitSize`。
- `SelectionModel` 是 identity 选择源（`GUI/Binding/SelectionModel.h`）：selected 有序集合 + primary（空或不在集合外）+ hover/active/focus。不持有 Entity*。控件绑 `primaryRef()`；多选走 `add`/`toggle`；`replace` 批量同步。`EditorHierarchyTab` 在 `onAttached` 拉一次 Layer 选择，之后只订 `EditorLayer::onSelectionChanged`。
- `ActionMap` 是 identity 命令表（`GUI/Binding/ActionMap.h`）：菜单、快捷键、toolbar 都 `execute(id)`。`FActionChord::primary` 在 macOS 是 Cmd、别处是 Ctrl。WidgetTree 未处理的 KeyPressed 才走 shortcut；文本焦点下只匹配带 modifier 的 chord。`UIMenu::FItem::fromAction` 生成同一 execute 的菜单行。
- `UndoStack` 是 identity 撤销历史（`GUI/Binding/UndoStack.h`）：`push` 记录已应用的 undo/redo 闭包，不在 push 时调用 redo。`beginMerge`/`endMerge` 把同一 `mergeKey` 的连续 push 收成一步（拖动）；`UndoTransaction` 把嵌套 push 收成一步。栈不持有 Entity*。`edit.undo` / `edit.redo` 走 ActionMap（macOS Redo 是 Cmd+Shift+Z，别处 Ctrl+Y）。Inspector 拖动 `UIDragFloat` 在 `_onDragBegan/Ended` 开闭 merge；`setValue(..., false)` 是 sync，不进 undo。Gizmo / viewport 选择仍未接入。
- `PropertyGraph::project` 是反射字段 → editor field model 的入口（`PropertyAccessor::collectLeaves` + `PropertyProjectionRegistry`）。单实例 typed get/set/equals/validation 在 `Core/Reflection/PropertyAccessor`；`PropertyHandle` 只做多选 mixed、undo copy/restore、asset picker kind 和 owner callback。**写回必须标脏：** `project()` 对已注册 ECS 组件默认装 `IComponent::onEdit()` change hook（Mesh/Model/Billboard/Skybox/Environment/Terrain/SimpleMaterial 在 `onEdit` 里 `invalidate()`）。材质 projection 覆盖为 `onPropertyChanged(path)`；Transform projection 只装 `setPosition/setRotation/setScale`。禁止只 poke 反射字段、不丢 runtime cache——那是 imgui `DetailsView` 在 `hasModifications()` 后 `invalidate()` 的合同。`build()` 无 hook，不是 Inspector 入口。Inspector 对多选的 **交集** component 物化 `EditorAutoPropertySection`；`UIDragFloat` mixed 显示 "—"，编辑写回全部 instance，undo 按 instance 快照恢复。enum 字段走 `UIComboBox`；`.color()` 元数据的 `glm::vec3`/`glm::vec4` 走 `UIColorEdit`（非 color vec3 仍走 DragFloat）。`TextureRef`/`ModelRef`/`MeshRef` 走 path `UITextField` + Browse + Show；Browse 经 `EditorAssetPickerCallback`（widgettree：`EditorLayer::setAssetPickerHandler` → `EditorSurface::openAssetPickerDialog`；handler 缺失时 `FilePicker::open*` 仍作 fallback，无 ImGui `render`）。Show 经 `EditorRevealAssetCallback`（`EditorLayer::revealInContentBrowser` → activate `content-browser`）。`EditorAutoPropertySection` 把 `" / "` group path 展成嵌套 `UIExpander`（`Diffuse Slot` 含 `Sampler Config`），不要平铺成同级 header。`PropertyHandle::validationError` 转调 `PropertyAccessor` 的 manipulate spec 范围；`hasAssetResolveError` 对 failed resolve 画 error fill；`UIDragFloat`/`UITextField` `setError` 画 error fill。`UIImage` 对缺失 asset / `setResourceMissing` 画 error fill。没有 retained 可编辑字段的类型跳过。ImGui `DetailsView` / `TypeRenderer` 已删；`EditorInspectorTab` 是实体/component 唯一正式 Inspector UI，并显示 Game UI Entry 摘要 + Open in UI Designer。
- `EditorSurface` Content Browser：`EditorContentBrowserTab` 持有 `FileExplorer` 与 keyed window。
  `FileExplorer::contentGeneration()` 才是 catalog 身份（mount/dir/search/view/filter）；
  tick **禁止**每帧 `collectEntries` 拼 fingerprint。tick 只做 O(1)：generation +
  选中 path + scroll/viewport 像素。`collectEntries` 只发生在 keyed window rebuild。
  磁盘变更没有 watcher；navigate/search 才会看到新文件。选中纹理调 `inspectAsset`。
  行列图标走 `editor_icons` 资产路径，不再经 ImGui texture cache。
  construct 之后的 widget / explorer 指针是不变量：不要 `if (!_pathText)`；create/attach 失败应崩溃。
- `UITreeView` 在 `UIScrollViewport` 内只 paint 可见行窗口（`computeKeyedVisibleWindow` + `getPaintedRowCount`）；`EditorHierarchyTab` 用 scroll 包裹。flatten/hit-test 仍读全量可见行；无 per-row widget。**没有** UE `SListView` / `generateWidgetForItem`：行不是 child widget，图标走 `FNode::icon`（`FBrush`），展开钮走共享 `paintDisclosureButton`。行 leading 必须走 `layoutDisclosureLeading`（button + icon + title 同一条 HBox，垂直居中），不要按钮一套几何、文字另用整行高度。`FDisclosureSpec` / `EDisclosureKind`：`PlusMinus`（默认带框 +/-）、`Chevron`（同一条折线在 Y-down 下旋转 90°，不是 ASCII `>`/`v`）、`Glyph`（自定文字对）、`Image`（collapsed 图，缺 expanded 时 UV 翻转 180°）、`Hidden`（只留 icon）。`bindFilter` + `HierarchyFilter` 搜索框过滤节点；`setReorderable` + `FTreeReorderDragDropOp`：Hierarchy 走 `moveEditorHierarchyEntity`（`ui:` 条目仍不可重排）；UI Designer 树接到 `UIDesignerPanel::applyWidgetDrop`。结构变化走 `EditorLayer::onHierarchyChanged`，不在 Surface 轮询 fingerprint。ImGui `SceneHierarchyPanel::sceneTree` 已删（Phase 8O）；`SceneHierarchyPanel` 仅保留 viewport 选择总线 API。
- `UIExpander` 是 ImGui `TreeNode`：折叠 **widget 子树** 的 layout host（Details 组件段 / 属性组）。`ui::collapsingHeader` 只是 `ui::treeNode().setFramed(true)`（`TreeNodeFlags_Framed`），不是第二种控件。Header 默认带框 `+`/`-`；`setDisclosureKind` / `setDisclosureGlyphs` / `setDisclosureImages` 与 TreeView 共用 `FDisclosureSpec`；`setIcon` 画出 `mark icon Name`，`Hidden` 时只留 icon。不要用 `UITreeView` 做 Details 折叠：TreeView 是数据行列表，Expander 的 children 才是属性行。不要为 TreeView 造 per-row child factory。
- Viewport overlay：`FEditorViewportHostState` / `IEditorViewportOverlay` / `EditorViewportOverlayHost`；
  `EditorSurface::syncViewportHostState` + hover/focus overlay dispatch；gizmo 绘制不再经
  `GuiSystem`，而是在 viewport compose 中直接发 `Render2D` world-line/screen-handle。



## 编辑器内嵌（已废弃）

- 旧路径曾把 WidgetTree 合成到离屏 RT 再 `ImGui::Image`。那些 panel 类型已删除；
  GameEditor chrome 只走整窗 `EditorSurface`，不要再扩离屏桥。
- `EditorToolSurfaceCompositor` 仍保留 shutdown，但 presentation 不再 compose
  workbench 离屏图。


