# Todo

- [x] C0：口径与编排图（skill Host 地图 + 两条产品循环；交叉 10E）
- [x] C1：删除 GUIWorkbenchPanel / FrameStatsPanel；搬迁 RuntimeToolsPanel leftover；审计 ContentBrowserPanel；删空 Layout/
- [x] C2：抽出 Content / Asset / UIDesigner / RuntimeTools tab owner；EditorSurface 只编排；去掉 EditorTabRegistry callback 袋
- [x] C3：拆 InputExtras 为单类文件；Dock drag 仅在真重复时抽 helper；不拆 kernel 主链路
- [ ] C4：`EditorSurface::tick` 去 push——子 UI 改成按需读；删 8 个 `sync*`/`push*`/`publish*` helper 与 `FEditorViewportHostState` 这类"Surface 拼出来的推入结构"；`EditorSurface.h` 不再持有对话框 / overlay 状态
- [ ] C5：page tab 交还 dock 侧——删 `_pageTabBar` / `_pageTabKeys` / `beginPageTabDrag` / `acceptPageTabDrop` / `dropOntoPageTabs` 与 `syncPageTabs` 的并列向量投影，以及为它强制改写的 `bHideTabBar`
