# Progress

## C6 当前 checkpoint（2026-09-25）

**目标**：删掉窗口级 tool scope，让每个 major editor 只显示/停靠自己的 tab。

**根因**：`EEditorTabScope::WindowTool` 不属于任何 editor，住在 window-root 的 `tools`
叶里，因此切页签只换 page 叶内容，Level 的工具面板恒在——这就是 UI 页里看到 Level
Hierarchy/Viewport/Content/Stats 等面板的来源。

**改动**：

- 删除 `WindowTool`；`EEditorTabScope` 只剩 `WindowRootEditor` / `EditorOwnedTool`。
- `canSpawnEditorTab` 成为唯一 placement 谓词（spawn/drop/redock/restore 共用）；
  删 `canDockEditorTab`、`isLevelEditorSharedDockTab`、`materializeTab` 的
  `bRestoreLayout` 参数。
- 窗口 root 工厂布局改为单页签叶；Level owned nested 工厂新增 `tools` 叶，装 Level
  的全部工具（content-browser / stats / runtime / render / assets / debug / fonts）。
- 注册表里那 7 个工具改为 Level-owned，placement 改 `EditorOwnedNested`。
- `repairPlacement`：页签井里的非页签面板关闭，不再造窗口级 tools 叶。
- `makeSpawnContext` 的 documentKey 继承收窄到 Scene-kind root。

**验证**：

- `xmake b ya-game-editor / ya-testing` 全绿。
- `xmake r ya-testing` 全量 1319 passed / 0 failed。
- 新增 `PreOwnershipWindowRootLayoutMigratesToPageWellOnly`：老布局（page + tools）
  恢复后 tools 叶被清除，只剩页签井。
- 编辑器 smoke 截图：Level 页显示 Hierarchy/Viewport/Inspector + Content/Stats/Runtime/
  Render，UI 页只显示 Palette/UI Preview/UI Tree/UI Inspector。

**未做**：C5（page tab 由 dock 侧渲染）。

## C4d-2 当前 checkpoint（2026-09-19）

**目标**：视口不在屏上时，compose 也不录。

**根因**：C4d-1 之后渲染侧已经不为隐藏视口声明 View，但 `composeAuthoringViewport` 照旧跑：`snapshot.viewportImageOwner` 为空 → `composeWorldFallback` 把 fallback 空图录进 `_composedViewportImage`，然后被推给一个已经不在树上的 widget。等于白录一个 pass，还在往一个看不见的控件推数据。

**改动**：`composeAuthoringViewport` 开头读 `_layer->isViewportShown()`，为假就清掉 `setViewportDisplayImage(nullptr)` / `setViewportPreviewImage(nullptr)` 并返回，不录任何 pass。清而不是留着，是为了让重新显示时不可能捡到上一次可见时的 handle。

**切回来那一帧为什么不会缺图**：重新挂上发生在输入回调（dock rebuild）里，早于本 tick 的 `declareViews`；本 tick 的 compose 又跑在 chrome `pushViewportDisplay` 之前。所以首个可见帧拿到的是本 tick 的图，不是旧图也不是占位。

**验证**：

- 默认编辑器 smoke 与改动前逐字节相同（`174c44cb`）——视口可见时门禁不触发，是 no-op。
- runtime smoke 不变（`c775245a`）。
- `ya-game-editor` / `ya-testing` / `ya-runtime` build ok。
- 滤镜跑出 5 个失败，但**没有一个来自本轮**：3 个是既有基线（`EditorPropertyGraphTest` 两例 + `GameUIHostTest.BuildSnapshotComposesMountedWidgets`），另 2 个（`EditorDockWorkspaceTest.ToolsOnlyWindowAdoptsFullLeafWithoutPageSplit`、`WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots`）用 `git stash` 去掉本轮改动后**同样失败**，属并发作者正在做的 dock 改动（工作区里 `DockNode.cpp` / `DockHideTabBarAffordance.h` 处于编辑中）。

**未做**：C4d-3（折叠的视口 tab 算隐藏还是算显示中未布局）仍未定。今天折叠走 `describesPixels` 失败 → fallback 默认尺寸那条老路，与本条（detach）不是同一条；折叠时视口仍会声明并渲染。

## C4d-1 当前 checkpoint（2026-09-19）

**目标**：视口被切走（或它所在的 level editor tab 被切走）时，编辑器不再向渲染侧声明作者视口，整条 3D 图不跑。

**根因**：`viewportRect` 一个字段同时表示两件事——"视口现在在哪"和"上次有效几何"。`getViewportRect()` 在 `describesPixels` 失败时回落到 `_viewportSize`，于是"视口不在屏上"和"视口在屏上但还没布局"表现成同一种样子，fallback 把前者也当后者。`publishViewportRect` 撞上 `if (!_viewportHost) return` 时**什么都没说**，layer 里的 rect 原封不动，producer 照常声明。

**改动**：

- `EditorLayer::_shownViewportCount`（计数而非 bool）。视口是 dock tab，`UIDockSpace::rebuildStack` 在非选中时把 widget 从树上 detach，detach 会递归到持有该 stack 的 level editor tab。计数是因为第二个编辑器窗口有自己的 chrome 和自己的视口，一个窗口切 tab 不该停掉另一个；`removeViewportShown()` 归零时才清 hover/focus（widget 不在树上就没人能 hover 它，留着会让编辑器相机继续吃本该给新 tab 的输入）。
- 写入点是 `EditorSurface::setViewportHost`——widget 自己的 attach/detach 边。选这里而不是 tick 时轮询：detach 发生在输入回调里，早于同一 tick 的 `declareViews`，所以重新挂上的当帧就已经声明了，不存在"多花一帧画不可见的视口"。
- `EditorViewProducer::collectSceneViews` 先读 `_layer->isViewportShown()`，为假直接 return。与既有的 `isViewportMode2D()` 早退同一条路径：**没声明就没有 View，没有 View 就没有 family、target、graph**，而不是让各个 pass 自己去跳过。

**没走的两条路**：

- 不用退化几何表达隐藏（extent 0 就不渲染）。可见性是 widget 树的事实，几何是布局的事实；混在一起每个读点都得重新猜，这正是被 4d 删掉的 `bWorldSceneRenderEnabled` 那类全局格子。而且折叠、未布局、`describesPixels` 刚失败三种情况都会产生退化几何，判不准。
- 不把可见性做成全局开关。理由同上（多窗口）。

**验证**：

- 新增 4 例：`HidingTheViewportDeclaresNoEditorViewAtAll`（隐藏时不声明，切回来读到的是面板几何而非 last-valid/默认）、`DockTabDetachIsWhatHidesTheViewport`（走产品边：dock detach → sink → layer → producer）、`OneWindowHidingItsViewportDoesNotHideTheOtherWindows`（计数语义）、`AHiddenViewportStopsCapturingViewportInput`。
- `EditorViewProducerTest.*` 9/9；`ya-render-3d-test` 175/175；渲染/编辑器滤镜 562 passed / 3 failed（与基线同 3 个 pre-existing）。
- 8 个目标 build ok。
- smoke 截图与改动前逐字节相同（runtime `c775245a`、editor `174c44cb`）。**注**：editor 那张的 hash 相对 handoff 基线 `5b8f5dd8` 变了，但用 `git stash` 去掉本轮改动后重跑仍是 `174c44cb`，且 editor 截图尺寸从 454x686 变成 579x439——是 `editor.dockLayout` 持久化状态在多次跑编辑器后漂移，不是本轮代码。runtime 那张一直是 `c775245a`（与更早的 `1c666897` 不同，差异来自并发作者已提交的 `a0a752d3`）。

**未做 / 偏离**：

- `EditorViewportCompositor` 在无声明时仍录一个 compose pass（画 fallback 空图）——C4d-2。
- "折叠的视口 tab 算隐藏还是算显示中未布局"仍未定——C4d-3。今天折叠走的是 `describesPixels` 失败 → fallback 默认尺寸这条老路，与本条（detach）不是同一条。

## C3 当前 checkpoint（2026-09-06）

- 拆 `InputExtras` 为 `DragFloat` / `SpinBox` / `RadioButton` / `ColorEdit` / `SearchComboBox` 单类文件；`Controls.h` 转发；无旧 `InputExtras` include。
- Dock drag：`FDockSpacePanelDragBehavior`（ghost + tear-off）与 `FDockFloatingWindowPanelDragBehavior`（窗体跟随、skip-source、sticky preview）有分叉，不抽 helper。
- 不拆 `WidgetTree` / `UILayout` / `GUIAppHost`。
- 验证：`xmake b ya-gui-closure-test`；`xmake r ya-gui-closure-test -- --gtest_filter='WidgetLayoutTest.*:DockNodeTest.*:WidgetTreeTest.Dock*'`。

### C3 保留项

- Hierarchy / Viewport / Menu / Dock persist / dialogs 仍在 Surface。
- domain panels 仍名 `*Panel`。
- 不合并 `GUIApp` 与 `ya::App`。
- 未宣称 retained editor ready。

## C2 当前 checkpoint（2026-09-06）

- 抽出 `EditorContentBrowserTab` / `EditorAssetInspectorTab` / `EditorUIDesignerTab` / `EditorRuntimeToolsTab`。
- 删除 `EditorTabRegistry` callback 袋；`EditorSurface::tick` / `buildEditorChrome` / `syncPresentation` 只编排 `tab->build/sync`。
- `EditorSurface.h` 不再持有 content/designer/runtime/asset 控件指针。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test -- --gtest_filter='WidgetLayoutTest.*:DockNodeTest.*:WidgetTreeTest.Dock*'`；`xmake r ya-testing -- --gtest_filter='EditorListRowsTest.*:EditorFilePickerDialogTest.*:EditorHierarchyOpsTest.*:FileExplorerNavigationTest.*'`。

### C2 保留项

- C3 InputExtras 拆分已做；Dock drag 因手势分叉未抽 helper。
- Hierarchy / Viewport / Menu / Dock persist / dialogs 仍在 Surface（chrome 编排）。
- `UIDesignerPanel` / `SceneHierarchyPanel` / `AssetInspectorPanel` 仍是 domain model 原名。
- 不合并 `GUIApp` 与 `ya::App`。

## C1 当前 checkpoint（2026-09-06）

- 删除无实例 leftover：`GUIWorkbenchPanel`、`FrameStatsPanel`、`ContentBrowserPanel` 及公开转发头。
- `migrateLegacyRuntimeSettings` 收进 `editor_runtime_settings::migrateLegacy()`；删除空壳 `RuntimeToolsPanel`。
- legacy `FilePicker` 图标改在 `EditorLayer::onAttach` 加载。
- 删除空目录 `GameEditor/Layout/`。
- 保留的 `UIDesignerPanel` / `SceneHierarchyPanel` / `AssetInspectorPanel` 注释改为 domain model，不是 retained UI。
- 验证：`xmake b ya-game-editor`。

### C1 保留项

- C2 tab owner 未做。
- 三份 domain panel 尚未改名（下轮抽 UI 后再考虑）。
- 不合并 `GUIApp` 与 `ya::App`。

## C0 当前 checkpoint（2026-09-06）

- 建立 `gui-editor-structure` 计划工件。
- 修正 `gui-framework` skill：模块地图改为 `Host/` + `ya-gui-host`；Draw2D/fonts 归 `Framework/Render`；写清 `AppKernel` 下 GUI-only 与 GameEditor 两条产品循环，以及 `EditorSurface::tick` 契约。
- editor-readiness `todo.md` 的 10E 其余 tab owner 交叉引用到本线 C2。
- 验证：文档对照；本 checkpoint 不改 C++。

### C0 保留项

- C1 leftover panel 删除未做。
- C2 tab owner 未做。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree / UILayout / GUIAppHost。
- 未宣称 retained editor ready。
