## Dock 权责

划分按责任，不按目录。三层：

```text
GUI Framework     怎么开窗、投影 stack、拖拽、跨窗迁移
GameEditor        这个 tab 是什么、属于谁、能不能放这里、关闭怎么办
EditorSurface     这一扇 editor 窗的 chrome / viewport / dialog / snapshot 编排
```

| 角色 | 当前类型 | Framework 负责 | 不负责 |
| --- | --- | --- | --- |
| Window | `GUIWindowManager` / `IGUIWindowSession` | NativeWindow、WidgetTree、surface/present、input/focus/DPI | tab 业务、editor owner、`FDockContext` |
| Tab registry | `FDockContext::FTabRegistry` / `FPanel` | TabId、title、stableKey、content widget、opaque owner/document | `EditorRootId`、spawn factory |
| Dock layout | `FDockTreeModel` (`layout()`) | Split / Stack、tab 顺序、active、split 比、persist | OS window、editor 类型 |
| Dock session | `FDockContext` | TabRegistry + layout + overlay/native placement + opaque policy + `commitDrop` | 创建 OS window、听 SDL |
| Area 投影 | `UIDockSpace` | 物化 split、stack 投影生命周期、area overlay + `applyDrop` | editor policy、leaf hit |
| Stack / Well | `UIDockTabStack` / `UIDockTabWell` | merge/split/tab 插入的 drop target、tab 重排/发起 drag | 改 layout、创建 native window |
| Drag | `GUIDragRouter` + `WidgetTree` D&D | 跨窗路由、ghost、keep-alive、`FDockDropTarget` | Inspector 能不能进 Material |
| Overlay floating | `UIDockFloatingHost` / `UIDockFloatingWindow` | 同树 Popup 投影；drop 走同一套 target | 第二套 floating dock 模型 |

GameEditor：`FEditorTabSpawner` / `FEditorTabSpawnContext`（typed factory）；`canTearOffEditorTab` / `canAcceptEditorDrop` 注入 `canAdoptPanel`；`EditorNativeTearOff` 只调 coordinator + `transferPanelTo`；`EditorWindowSession` 绑 GUI session；`EditorDockWorkspace` 做 factory JSON / persist，不创建 OS window、不算几何。

禁止：每个 TabWell 自持 `FDockContext`；删掉窗口级 layout；Framework include `EditorRootId`；`UIDockFloatingWindow` 另搞一套 drop；`EditorDockWorkspace` 当 window manager。

`UIDockTabWell` / `UIDockTabStack` 是 leaf drop target。GameEditor policy 仍用 `canAdoptOntoLeaf` 签名。`UIDockSpace` 保留作 Area 投影名。

- `FDockContext` 是共享会话，不是 widget，也不是 OS window。NativeWindow placement 只是 coordinator 记录（MW-702/703），本对象不创建 native window。Overlay 的 `targetWindowId` 是 host window；`bindFloatingTargetWindow` 拒绝 overlay。跨树迁移走 `extractPanel` / `adoptPanel` / `transferPanelTo`（禁止 live widget 双挂载）。Policy 只吃 opaque `stableKey` / `ownerEditorId` / `documentKey`。`FDockPanelDragDropOp` 携带 `sourceContext`。Drop 命中由 Well/Stack 产生 `FDockDropTarget`。
- 任何在 `WidgetTree*` 之外缓存裸树指针的宿主/路由（`GUIDragRouter` 的 primary/source/hover/capture/window 绑定即此类）必须订阅 `WidgetTree::onDestroyed`（析构首行广播）并丢弃指向该树的全部缓存状态。重建自家 WidgetTree 的宿主（GameEditor chrome rebuild）正是依赖此契约：重建到下一次 `bindPrimary` 之间的事件路径（如 `InputRouter::updateCursor` → `EditorInputNode::getCursor`）不允许解引用旧树。
- 指针拖拽 session 在进程里对每个 input universe（`GUIApp` 或 GameEditor 的 `AppKernel`）是唯一的：`GUIDragRouter` 记录 source/hover window，以及 pointer capture 所属窗、key-focus IME 窗、树内 modal 的 app-modal 范围。`WidgetTree` 只持有 source-local 的 payload、ghost、observer、capture widget、hover/tooltip。通用 D&D（TreeView / Designer / Asset / Dock）都从 topmost `acceptsDrop()` 发现可提交目标；同树 hover 与跨窗一样走 `previewsDrop()` / `findDropHoverTarget`（dock chooser）。OS cursor 问 router（capture 窗或当前指针窗的 hovered）。GameEditor 不得再自己听 SDL 做跨窗拖拽；`EditorInputNode` / `EditorModule::onEvent` 共用同一 router。同一窗与跨窗 dock tab 拖拽共用一条路径：source 窗保留自己的 tree，ghost 是 DragIme 上的小 tile，目标树走 `setExternalDropHover`。禁止把 extra tree 偷到全屏/等大 pickup overlay（macOS Vulkan swapchain 往往不透明，全屏 overlay 会挡住 drop 目标）。指针不在任何可见窗内时才用 ~168×32 的 click-through desktop overlay 画 tab ghost。标题 TabBar 整条 rect 必须注册为 Client；只有 trailing gutter 是 Drag。从顶部 tabwell 拖出 tab 是 tab 手势，不是 OS 拖窗。最后一个可关闭 extra 页签一旦离开源窗，`bHideSourceWindowOnLeave` 立刻 `INativeWindow::hide()`，源窗不得跟着指针走；drop 到 dock 后 reclaim，NoTarget 再 show/挪到落点或开新窗。不要用 `SDL_HITTEST_DRAGGABLE` / `performWindowDragWithEvent` 去“拖 tab”。drag 期间 `SDL_CaptureMouse`；source-tagged 的 move/release 用 `OsEventPump::queryGlobalMouse` 做窗口 hit-test（capture 会把事件钉在源窗并可能钳制局部坐标），显式 foreign window id 仍信任事件坐标；hit-test 跳过 hidden/minimized。`WindowFocusLost` 必须清掉该树的 hover / tooltip / 普通 pointer-over；正在 capture/drag 的树仍不得注入远指针（capture 与 drag session 可以跨窗保留）。
- GameEditor 经 `FEditorTabDragPayload` / `canTearOffEditorTab` / `canAcceptEditorDrop` / `canRedockEditorTab` 做 placement policy。`tearOffEditorPanelToNativeWindow` / `handleDockNoTargetTearOff` / `redockEditorPanelToOwner` / `closeEditorWindow` 只调用 coordinator + `transferPanelTo`；空 extra 窗 `reclaimEditorWindowIfEmpty`。Locked / Level / 默认窗不能关或迁走。`EditorDockWorkspace` 不创建 native window、不 include `IGUIWindowCoordinator`。`EditorSurface` 只挂 generic `realizeNoTargetTearOff` 回调，不 include coordinator。
- DockSpace NoTarget：若 `FDockContext::realizeNoTargetTearOff` 返回 true，不创建 overlay；未接线或返回 false 时仍 `InProcessOverlay`。GameEditor 产品路径走 native OS window。`UIDockFloatingHost` 仍只投影 overlay。Host 适配器 `realizeNativeDockPlacement` 只把 `geometrySpace == Screen` 的 pos 写成 host origin；`createSession` 在 `monitorIndex < 0` 时保留窗口已查询的 monitor，避免 recover 丢掉 origin。TreeLocal 不得升格为 OS origin。
- `UIDockSpace` 是 DockArea 投影：把 context 的 docked tree 物化成 nested split + `UIDockTabStack` / `UIDockTabWell`。不拥有 model。内部投影是 `FDockStackView`。**唯一入口**是 `syncProjection(EDockProjectionSync)`：`Structure` 重建拓扑（drop / tear-off / `fireDockUpdated` / 空 Area 的首次 layout）；`Stack` 只灌一个 stack 的 tab+graft（`addPanel` / `activatePanel`）；`Chrome` 只改 well 可见性。禁止在 Area 外直接调 `rebuildProjection` / `rebuildStack`。drop 语义收口为 `FDockDropTarget`；布局变更走 `FDockContext::commitDrop`。Well/Stack 的 `canAcceptDrop` 命中 leaf；Area 处理 split gutter 回退，并拥有 chooser overlay / `applyDrop`。overlay floating 自己产生 `FloatingTabWell`。模型节点是 `EDockNodeKind::Split` / `Stack`（JSON `"leaf"` 仍可读）。同一树里 nested `UIDockSpace`（Level Viewport）盖住 window-root page leaf：同树 hover 走 `findDropHoverTarget`（chooser 是 preview-only）；外层 `resolveDropPreview` 在 innermost dock 不是自己时返回空，禁止把整块上半窗画成 page chooser。
- 禁止可见的空 dock stack（`DockStackN (drop tabs here)`）。Generic/Tools 最后一个 tab 离开后立刻 collapse，即使 `persistentEmptyLeaf` 曾为占位而设。Page well 可以空，但 chrome-only、不画 inner tab well。`addPanel` 必须清掉 persistent 标记；`extractPanel` / `commitDrop` 之后 `pruneEmptyGenericLeaves`。空 Tools well 需要时由 `ensureToolsLeaf` 再造，不能留在屏幕上当 drop 槽。
- `UIDockFloatingHost` 只投影 `InProcessOverlay` placement（同一 native window / 同一 WidgetTree 的 Popup 层）。它不是 OS window；`NativeWindow` placement 必须跳过。浮窗 merge 走 `FloatingTabWell`，不要再加一套 `targetFloatingId` bool。
- 绑定 API：`UIDockSpace::setContext` / `UIDockFloatingHost::bindContext`。不要再引入 `UIDockWorkspace` 这种与 Space 近义、还带 `UI` 前缀的会话类型。
- 源码与公开头收在 `Runtime/Widgets/Controls/DockSpace/`；include 为 `GUI/Widgets/Controls/DockSpace/...`。TabBar 仍是通用控件，不进这个目录。
- 停靠 tab 拖动（`FDockSpacePanelDrag`：ghost + 无目标时 tear-off）和浮窗标题拖动（`FDockFloatingWindowPanelDrag`：窗体跟随指针、skip-source hit-test、sticky preview）不是同一套手势。不要抽共享 helper。


