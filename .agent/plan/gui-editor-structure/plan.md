# GUI + Editor 结构收口（克制版）

## 目的

让 GUI Framework + GameEditor 能顺着读完整 orchestration：`EditorSurface` 只做编排，tab 各自有 owner，目录不再说谎。不合并 `GUIApp` 与 `ya::App`。

> 目录那一半已单独落地：`GameEditor/UI` 的 89 个平铺文件按关切拆成
> `Shell/ Dock/ Tabs/ Sections/ Viewport/ Dialogs/ Ops/`，公开路径变成
> `GameEditor/UI/<Group>/<Name>.h`。见 `.agent/plan/editor-ui-grouping/`。
> 本线继续负责剩下的一半：谁在驱动、谁拥有 tab。

## 硬边界

1. 不合并 `GUIWindowHost` 与 `ya::App` 的 present / 输入栈。两条产品线有意分叉：Workbench 保持 GUI closure；Editor 吃 3D viewport + swapchain。
2. 不按行数拆 `WidgetTree` / `UILayout` / `GUIAppHost`。它们的价值是顺着读完整时序。
3. 不接 Phase 10F PropertyHandle、Windows/MSVC、OpenGL presentation。
4. 不把 `UIDesignerPanel` 预览文档模型塞进 widget。
5. 不为 1–2 个文件发明新目录。
6. 不把 `UICompoundWidget` 当成 specialized control 的归宿。
7. 不把无关工作区改动（尤其 PropertyHandle）卷进本线提交。

## 产品循环（当前事实）

- 唯一 while-loop：`AppKernel`
- GUI-only：`GUIApp` + `GUIWindowHost`（`Framework/GUI/Host/`，target `ya-gui-host`）
- 游戏/编辑器：`ya::App` + `GameRuntimeFrameOrchestrator`；编辑器是 module，经 `onPresentation` 挂 `EditorSurface`

```text
AppKernel
  ├─ GUIWindowHost  → FWorkbenchSurface → WidgetTree::buildSnapshot → compose/present
  └─ ya::App → GameRuntimeFrameOrchestrator → EditorModule::onPresentation
       → EditorSurface::tick → WidgetTree::buildSnapshot → replayUIFrameSnapshot
```

## EditorSurface 目标契约

```text
tick: rebuild-if-needed -> window metrics -> sync tabs/chrome -> buildSnapshot -> viewport host
rebuild: new tree/theme/dock -> shell chrome -> tab.build() 挂进 FDockContext
```

Hierarchy / Viewport / Menu / Dock persist / dialogs 留在 Surface（chrome 编排）。Content / Asset / UI Designer / Runtime Tools 必须是独立 tab owner。

结构面 C0–C3 已完成。内核手感与 ImGui 工作流接线见 `.agent/plan/gui-kernel-ux-parity/`。

## C4–C5：把 Surface 的 push 编排与 page tab 投影消掉（2026-09-19 复查）

C0–C3 把 tab owner 抽出来了，但 C2 明确保留了"Hierarchy / Viewport / Menu / Dock persist / dialogs
仍在 Surface（chrome 编排）"。复查确认这条保留项就是**本线最初那个问题的残余**：Surface 仍在每个
tick 里手写把状态推给各个子 UI。

### C4：`EditorSurface::tick` 从 push 改成子 UI 自己读

现状（`EditorSurface.cpp` 1322 行 / 36 个成员函数）：`tick` 是"Dock 模型 / 度量 → 一串 push →
buildSnapshot"的混排：

~~~cpp
applyWindowMetrics(context.metrics);
_tree->tick(dt);
syncShellDialogs();          // 把 *_filePicker / *_settings 的状态推进对话框
pushViewportDisplay();       // 把 viewport 显示参数推给显示侧
_snapshot = _tree->buildSnapshot(snapshotCtx);
publishTitleClientHits();
publishViewportRect();
syncViewportHostState(context);   // 组装 FEditorViewportHostState 再推给 overlay host
~~~

另有 `_dockContext->appendOnDockUpdated([this]{ syncPageTabs(); })`：一个模型变更回调，用来驱动
Surface 侧的再投影。整个类是 8 个 push / 投影 helper：`applyWindowMetrics`、`syncShellDialogs`、
`pushViewportDisplay`、`publishTitleClientHits`、`publishViewportRect`、`syncViewportHostState`、
`syncPageTabs`（+`refreshProjectBrowserRows`）。

`FEditorSurfaceContext` 也是 push 形状：`app` / `presentSurface` / `metrics` / `view` / `projection`
一次性交进来，Surface 再把其中几项组装成别的结构推下去。

目标契约（与最初那次讨论一致）：**子 UI 按需读，Surface 只提供 extension point 与生命周期**。
具体做法（每条可独立验收）：

1. 需要"当前度量 / 相机"的子 UI 改成构造时拿到一个只读来源（或订阅一个变更信号），不再由 Surface
   每个 tick 组装值对象推下去；`FEditorViewportHostState` 这种"Surface 拼出来的推入结构"应消失，
   由 viewport host 从它需要的来源直接读。
2. `publishTitleClientHits` / `publishViewportRect` 是"把 Surface 的几何发布给别处"——发布物应改为
   树层面的查询（与 `WidgetTree::wantsTextInput()` 同类：能力补齐后由树回答，而不是由 Shell 广播）。
3. `syncShellDialogs` 的 `->sync(*_tree)` 若只是把对话框挂上/摘下树，就归到对话框自己的生命周期；
   若含真实状态搬运，说明对话框的状态该由对话框自己持有。

验收：`EditorSurface::tick` 只剩"生命周期 + extension point"（rebuild / tick tree / buildSnapshot /
dispatch），不再出现 `sync*` / `push*` / `publish*` 调用；`EditorSurface.h` 不再持有对话框与
viewport overlay 的状态对象。

### C5：page tab 是 dock 模型的一部分，不该由 Surface 手工投影

`syncPageTabs()` 现在做的是：在 dock 模型里找 `EDockLeafRole::Page` 那个 leaf → 把它的
`bHideTabBar` 强制设成 true → `_dockSpace->syncTabBarVisibility()` → 再从 leaf 的 `panelIds` 里把
每条 `FDockPanelRecord` 抄成并列的 `keys` / `titles` / `closable` / `selected` 向量（外加成员
`_pageTabKeys`），喂给**第二个 `UITabBar`**（`_pageTabBar`）。那个 bar 还自己带 4 个回调
（`_onTabSelected` / `_onTabDragBegin` / `_onTabReordered` / `_canBeginTabDrag`）与
`beginPageTabDrag` / `acceptPageTabDrop` / `dropOntoPageTabs` 三个拖放处理。

即"页面"这个概念**已经在模型里**（`EDockLeafRole::Page` 有真实查询），但它的 UI 是 Shell 手工抄出来
的第二份表示。这与 dockspace 那条线的结论一致：tab 的列表与拖放属于 dock 控件，不属于 Shell。

目标契约：page tab 由 dock 侧渲染（leaf 的 tab bar 渲染在 chrome 的位置，或由模型查询直接绑定），
Shell 不再持有 `_pageTabBar` / `_pageTabKeys` / 三个拖放函数；`bHideTabBar` 的强制改写随之一并消失
（它是"模型被绕开"留下的补丁）。

顺序上先做 C4 再做 C5：C4 让 Surface 不再推状态，C5 才有地方把 page tab 交还给 dock 侧。

验证（两条共同）：`xmake b ya-game-editor`；`ya-gui-closure-test` 的 `DockNodeTest.*` /
`WidgetLayoutTest.Dock*`；编辑器 smoke（`Script/automation/editor/run_widgettree_editor_smoke.py`）
六步全过；`run-editor` viewport 截图与基线逐字节相同。

## Checkpoints

- **C0**：口径与编排图（skill + 本计划工件，不改 C++）
- **C1**：删除说谎 leftover panel；搬迁 `migrateLegacyRuntimeSettings`
- **C2**：抽出 tab owner；去掉 `EditorTabRegistry` callback 袋
- **C3**：拆 `InputExtras`；Dock drag 仅在真重复时抽 helper

每个 checkpoint 必须有代码或文档闭环、验证和一次 `[gui] ...` 提交。

结构面 C0–C3 已完成。内核手感与 ImGui 工作流接线见 `.agent/plan/gui-kernel-ux-parity/`。
