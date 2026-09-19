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

**C4a 已落地 2026-09-19（对话框自持刷新）**：`syncShellDialogs()` 从 `tick` 里消失，
`EditorSurface` 不再每帧推对话框。做法是补上框架缺的那件小事：新增 `UITickBehavior`
（`UIBehavior` 子类，带一个 `std::function<void(UIElement&, float)>`），两个对话框在 `open()` 时把它
挂到自己的 overlay 上，刷新由 `WidgetTree` 的子树 tick 驱动。

关键收尾：**`EditorFilePickerDialog::sync` / `EditorSettingsDialog::sync` 改为 private**。这是本刀
真正的验收——契约不能只是"现在没人调"，而是"别人调不到"。在此之前两个 `sync` 是公开的，等于公开邀请
shell 回来推；改完之后 shell 想推也没有入口。

**为什么 tick 驱动的刷新是安全的**：对话框只有在打开时才存在，打开即成树、即可见，
`WidgetTree::tickSubtree` 只在 `isVisibleInTree()` 时 skips —— 对一个"打开就可见"的 modal 来说
不存在"已打开但跳过 tick"的窗口。（这条推理只对对话框成立，对**会被折叠的 dock tab 不成立**，
见下面 C4b 的注意事项。）

**验证口径的加强**：原来的对话框测试直接调 `dialog.sync(tree)`，所以**即使接线断了也会通过**——
这正是不值得信任的那类测试。现在改成 `tree.tick(dt)`，测试走的是产品路径。其中
`EditorSettingsDialogTest` 里"改绑定 → tick → `isApplyEnabled()` 翻转"是真实守卫：
`_applyButton->setEnabled(dirty)` 只在 `sync()` 里写过（这一刀之前也一样），所以接线断了第二次断言必挂。

验证：`ya-gui-widgets` / `ya-game-editor` / `ya-testing` build ok；对话框用例 8/8；渲染/编辑器+GUI
滤镜 547 passed / 3 failed（与基线同 3 个 pre-existing）；两张 smoke 截图逐字节相同；编辑器 smoke 六步全过。

**C4b–C4d 未做**（原样保留在下面），并新增一条本刀查到的约束：

> `WidgetTree::tickSubtree` 对不可见子树直接 return。所以"把 viewport tab 的显示图从 push 改成
> 它自己 tick 时拉取"会引入一个真实差异：viewport tab 在被同 leaf 的其它 tab 顶掉时会变成不可见而
> **停止拉取**，而今天的 push 不管可见性每帧都更新纹理。切回来的那一帧需要"先变可见、再 tick、
> 再 buildSnapshot"的顺序真的成立才不出旧帧。改 C4b 前必须先把这条时序验证清楚，不能靠推理。

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
