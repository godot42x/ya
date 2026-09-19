# Todo

- [x] C0：口径与编排图（skill Host 地图 + 两条产品循环；交叉 10E）
- [x] C1：删除 GUIWorkbenchPanel / FrameStatsPanel；搬迁 RuntimeToolsPanel leftover；审计 ContentBrowserPanel；删空 Layout/
- [x] C2：抽出 Content / Asset / UIDesigner / RuntimeTools tab owner；EditorSurface 只编排；去掉 EditorTabRegistry callback 袋
- [x] C3：拆 InputExtras 为单类文件；Dock drag 仅在真重复时抽 helper；不拆 kernel 主链路
- [x] C4a：对话框自持刷新。新增 `UITickBehavior`；两个对话框 `open()` 时挂到自己的 overlay，刷新由 `WidgetTree` 子树 tick 驱动；`syncShellDialogs()` 与 `tick` 里的调用删除；两个 `sync` 改 **private**（契约"别人调不到"，不只是"现在没人调"）；对话框测试改为驱动 `tree.tick()` 而非直接调 `sync`
- [ ] C4b：viewport 显示改为 tab 自持（`pushViewportDisplay` + `previewPanelLocalRect` + Surface 的 `_viewportTexture`/`_viewportImageResource`/`_viewportImageView` 一并搬到 `EditorViewportTab`）。**先决条件**：验证"折叠中的 tab 不 tick"不会让切回来的首帧显示旧图（`WidgetTree::tickSubtree` 对不可见子树 return）
- [ ] C4c：`syncViewportHostState` 的 `FEditorViewportHostState` 消失——overlay host 从 viewport host + 相机来源直接读，而不是 Surface 每帧拼一个推入结构
- [ ] C4d：`publishViewportRect` / `publishTitleClientHits` 改为树层查询，而不是 Surface 广播几何
- [ ] C5：page tab 交还 dock 侧——删 `_pageTabBar` / `_pageTabKeys` / `beginPageTabDrag` / `acceptPageTabDrop` / `dropOntoPageTabs` 与 `syncPageTabs` 的并列向量投影，以及为它强制改写的 `bHideTabBar`
