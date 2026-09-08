# GUI 多 OS Window / GameEditor 多 Editor Session Checklist

## 开工前

1. 读取根 `AGENTS.md`、`.agent/skills/gui-framework/SKILL.md`；涉及 RHI 时读取 `render-arch`、`ya-build`。
2. 读取本目录 `plan.md`、`todo.md`、`progress.md`、`feature_matrix.json`。
3. 运行 `git status --short`，逐文件确认共享工作区改动归属。
4. 复述当前单一 checkpoint 的目标、层级、依赖、非目标和回退边界。
5. 同时最多一个 TODO 为 `[-]`，同步 `feature_matrix.active_task`。

## C0 审计

1. 先找真实调用方与 ownership，再设计 API。
2. 记录 surface/swapchain/frame resource 的 owner、创建和销毁时机。
3. 记录 `getSwapchain()`、全局 frame index、Render2D static session 等单实例假设。
4. 登记旧 tab 的 spawn、tick、destroy、document、viewport、selection、undo 依赖。
5. 不因未来多窗口提前实现 opaque handle、parallel recorder 或中心 event bus。

## GUI Framework 实施

1. 先完成两个空白 native window，再接 editor。
2. 一个 window 一个 WidgetTree、snapshot、surface/presentation state。
3. create/destroy/swapchain rebuild 延迟到 frame boundary。
4. 输入先按 native window id 路由，再进入对应 WidgetTree。
5. shared GPU resource 必须活到 queue submit 完成。
6. 每窗口 Render2D pass slot/descriptor/vertex resource 不得互相覆盖。
7. GUI Framework 不 include GameEditor、EditorLayer、Scene/ECS。
8. C1/C2 空白双窗之后，必须用 Feature Gallery `Windows` 页作为第一份真实多窗消费者，再进 C3 drag 或 GameEditor。
9. Gallery 额外窗口经 `GUIApp::openWindow`；禁止 `FWorkbenchSurface` 变成 window manager，禁止用 dock floating 冒充 OS window。

## GameEditor 实施

1. `FDockContext` 只管理一个 window-root 或 editor-owned dock model，不创建 native window。
2. `EditorTabSpawnerRegistry` 只注册 factory，不拥有 tree/widget。
3. 每个 tab 先分类为 `WindowRootEditor`、`EditorOwnedTool` 或 `WindowTool`；owned tool 必须记录 `ownerEditorId`。
4. Level Editor 是 main-window non-closable root editor。
5. document/dirty/undo/selection 按 editor/document owner 管理。
6. 跨窗迁移必须 detach → keep-alive → attach → focus transfer。
7. source 不保留 detached tab parent edge；target 不重复 attach。
8. 旧 `editor.dockLayout` 只恢复 main window；新增 topology 使用 versioned envelope。
9. root editor 和允许 detachable 的 owned tool 都能 tear-off；owned tool 不能 dock 到其他 root editor/tab，独立窗口仍保留 ownerEditorId。
10. `EditorSurface` 只做当前窗口 UI 编排；禁止改成 window manager，也禁止把字段整体倒进 `EditorWindowSession` god object。
11. 新代码只走 `EditorWindowSession::tick` + `FEditorSurfaceContext`；`tick(App&)` 仅迁移期 forwarding，ES-5 删除。
12. 文本输入走 `WidgetTree::wantsTextInput()`，禁止 Surface 特判具体 tab 类型。
13. C4 按 ES-1 → ES-5 顺序；单元素 registry 稳定前不得创建第二扇 editor window。

## 收尾

1. 运行本 checkpoint 定向构建和测试。
2. 至少按影响运行 `xmake b ya-gui-host`、`xmake b ya-gui-closure-test`、`xmake b ya-game-runtime`、`xmake b ya-game-editor`。C2G 额外 `xmake b GUIWorkbench`，并用 `--start-page Windows` / scenario 验收多窗。
3. 涉及 GPU/presentation 时运行 `python3 Script/automation/gui/run_workbench_gpu_parity.py`。
4. 补充双窗、resize、close、focus、tear-off、re-dock 证据。
5. 运行 `git diff --check` 和 `python3 -m json.tool .agent/plan/gui-multi-os-window-editor/feature_matrix.json`。
6. 检查 staged diff，排除其他 GUI/Editor/Workbench 改动。
7. 同步 todo/progress/matrix；checkpoint 闭环后代码、测试、计划同一 commit。
