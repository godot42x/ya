# Todo

- [x] C0：口径与编排图（skill Host 地图 + 两条产品循环；交叉 10E）
- [x] C1：删除 GUIWorkbenchPanel / FrameStatsPanel；搬迁 RuntimeToolsPanel leftover；审计 ContentBrowserPanel；删空 Layout/
- [x] C2：抽出 Content / Asset / UIDesigner / RuntimeTools tab owner；EditorSurface 只编排；去掉 EditorTabRegistry callback 袋
- [x] C3：拆 InputExtras 为单类文件；Dock drag 仅在真重复时抽 helper；不拆 kernel 主链路
- [x] C4a：对话框自持刷新。新增 `UITickBehavior`；两个对话框 `open()` 时挂到自己的 overlay，刷新由 `WidgetTree` 子树 tick 驱动；`syncShellDialogs()` 与 `tick` 里的调用删除；两个 `sync` 改 **private**（契约"别人调不到"，不只是"现在没人调"）；对话框测试改为驱动 `tree.tick()` 而非直接调 `sync`
- [ ] C4b：viewport 显示改为 tab 自持（`pushViewportDisplay` + `previewPanelLocalRect` + Surface 的 `_viewportTexture`/`_viewportImageResource`/`_viewportImageView` 一并搬到 `EditorViewportTab`）。**先决条件**：验证"折叠中的 tab 不 tick"不会让切回来的首帧显示旧图（`WidgetTree::tickSubtree` 对不可见子树 return）
- [ ] C4c：`syncViewportHostState` 的 `FEditorViewportHostState` 消失——overlay host 从 viewport host + 相机来源直接读，而不是 Surface 每帧拼一个推入结构
- [x] C4d-1：**视口不在屏上时不声明 View**（2026-09-19）。`viewportRect` 一个字段原本同时表示"视口在哪"与"上次有效几何"，于是"视口被切走"被 fallback 当成"视口还没布局"，整条 3D 图照跑。现在把可见性变成 layer 能回答的事实：`EditorLayer::_shownViewportCount`（计数，不是 bool——第二个编辑器窗口有自己的 chrome 和视口，一个窗口切 tab 不能停掉另一个），由 `EditorSurface::setViewportHost` 在 widget 的 attach/detach 边写入；`EditorViewProducer` 在 `collectSceneViews` 里先读它，为假就什么都不声明（和既有 `isViewportMode2D` 同一条声明式路径）。detach 同时清掉 hover/focus，否则编辑器相机会继续吃本该给新 tab 的输入。证据：`EditorViewProducerTest.HidingTheViewportDeclaresNoEditorViewAtAll` / `DockTabDetachIsWhatHidesTheViewport` / `OneWindowHidingItsViewportDoesNotHideTheOtherWindows` / `AHiddenViewportStopsCapturingViewportInput`。**未做**：`EditorViewportCompositor` 在无声明时仍录一个 compose pass（画的是 fallback 空图），下一条收。
- [ ] C4d-2：视口不可见时 `composeAuthoringViewport` 不再录 compose。今天无 View 声明时 `snapshot.viewportImageOwner` 为空 → `composeWorldFallback` 仍录一个 pass 到 `_composedViewportImage`；`EditorSurface::pushViewportDisplay` 不为空时还会把它推给已经不在树上的 widget。要么让 compose 在 `!layer.isViewportShown()` 时直接 return（并清 `_layer->setViewportDisplayImage(nullptr)`，让切回来那一帧走 `missing` 而不是旧图），要么证明保留它是必要的。
- [~] C4d-3：`publishViewportRect` 仍是"last-valid viewport geometry"的唯一快照点（widget 折叠时报退化 rect，`notifyViewportWidgetRect` 的 `describesPixels` 守卫就是为此而设），拉取只会把状态搬到约 20 个读点。C4d-1 之后待决问题缩小为"折叠的视口 tab 算隐藏还是算显示中未布局"——选隐藏则 last-valid 基本可以不要。`publishTitleClientHits` 归 C5
- [ ] C5：page tab 交还 dock 侧——删 `_pageTabBar` / `_pageTabKeys` / `beginPageTabDrag` / `acceptPageTabDrop` / `dropOntoPageTabs` 与 `syncPageTabs` 的并列向量投影，以及为它强制改写的 `bHideTabBar`
