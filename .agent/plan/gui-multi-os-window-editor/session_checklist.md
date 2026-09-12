# GUI 多 OS Window / GameEditor 多 Editor Session Checklist

## 开工前

1. 读取根 `AGENTS.md`、`.agent/skills/gui-framework/SKILL.md`；涉及 RHI 时读取 `render-arch`、`ya-build`。
2. 读取本目录 `plan.md`（先读「冻结」与不可上屏段）、`todo.md`、`progress.md`、`feature_matrix.json`；RHI 细节再读 `c2_present_compose_model.md`、`c2_view_model.md`、`c2_unpresentable_surface.md`。
3. 运行 `git status --short`，逐文件确认共享工作区改动归属。
4. 复述当前单一 checkpoint 的目标、层级、依赖、非目标和回退边界。
5. 同时最多一个 TODO 为 `[-]`，同步 `feature_matrix.active_task`。

## C0 审计

1. 先找真实调用方与 ownership，再设计 API。
2. 记录 surface/swapchain/frame resource 的 owner、创建和销毁时机。
3. 记录 `getSwapchain()`、全局 frame index、Render2D static session 等单实例假设。
4. 登记旧 tab 的 spawn、tick、destroy、document、viewport、selection、undo 依赖。
5. 不因未来多窗口提前实现 opaque handle、parallel recorder 或中心 event bus。

## Render boundary follow-up

1. 先区分 NativeWindow、PresentSurface、Camera/WorldView、BaseRenderPipeline、ViewCompose、DisplayCompose、Present；任何新 API 必须标明 owner。
2. view/projection/viewProjection/extent 必须在 graph build 前进入 immutable CameraFrameInput；pipeline 不得从 swapchain/window 反查。
3. graphics/UI/view compose 只能写 Camera 离屏 RT；display compose 才能写 swapchain image；acquire/present 只能由 surface/present coordinator 调用。
4. R-1～R-4 未完成前，不实现 N Camera、FRenderViewDesc、第二次 submit 或复制 RenderRuntime。
5. 每次重构后运行静态 grep、单 Camera golden、双 surface smoke 和 surface-only teardown；记录 trace 中的 Camera/Display/Present 阶段。

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
10. 明确区分 `DockNode`、`FDockFloatingPlacement`、`UIDockFloatingHost`（in-process overlay）和 native `GUIWindowSession`；只有后者才是 OS window。
11. 一个 native window 只绑定一个 WidgetTree；跨窗迁移必须 detach/transfer/attach，禁止同一 live widget 双挂载或用一棵树管理多窗 focus/popup/IME。
12. Window chrome 走 capability API：macOS 默认 Hybrid、Windows 可选 ClientDrawn；Editor/Dock 层不得直接依赖 NSWindow/Win32 non-client API。菜单/hosted dock 消费 `queryWindowChromeLayout`（traffic-light safe-zone + title Client + trailing drag gutter），不要用生 SDL safe-area。
13. DockSpace 的 split/tab/reorder/drop preview、floating projection 和 cross-window drag router 必须归 GUI Framework；GameEditor 只能提供 typed payload 与 placement policy。
14. GameEditor 不得重新监听 SDL/平台事件实现跨窗拖拽，也不得直接创建 native window；所有迁移经 GUI coordinator/session。
15. 指针拖拽 session 对每个 input universe 唯一，由 `GUIDragRouter` 持有 source/hover window、capture 所属窗、IME 窗与 app-modal。`WidgetTree` 只保留 payload/ghost/observer/capture widget。不是 `GUIApp`、也不是进程单例。`WindowFocusLost` 不得对正在 capture/drag 的树注入远指针。extra 窗 `bindSdlClipboard`。

## RHI / presentation

1. `IRender` 是共享 device；一扇 OS 窗一个 `IRenderSurfaceContext`。禁止每窗 `IRender::create`。
2. Camera 链写离屏 RT：graphics → UI → view compose。swapchain 只在 display compose + present。
3. 新代码不要用 `primarySwapchain()` / `primaryFrameIndex()` 当 viewport 或 recording flight。
4. Present 消费方只走 `IRenderSurfaceContext` / `ISwapchain` / `buildPresentationImages`。禁止 `as<VulkanSwapChain>()`。Vulkan 细节留在 `VulkanRenderSurfaceContext`。
5. swapchain recreate / imported image rebuild 只 wait **该** surface 的 fence（`IRenderSurfaceContext::waitInFlight`），禁止 `vkDeviceWaitIdle` 卡住其他窗。
6. `PresentationGraphService` 只服务主 world 窗；辅助 GUI 窗 window-local import + compose。
7. C2 完成前冻结 N Camera / `FRenderViewDesc`（`MW-902`）。extra present 不要顺手改 Camera 图。
8. `Render2D` 静态 session 串行；每窗唯一 pass slot（MW-203）。
9. 最小化只 delay 该 PresentSurface 的 present；禁止进程级 `_bMinimized` sleep。模型见 `c2_unpresentable_surface.md`。

## GameEditor 实施

1. `FDockContext` 只管理一个 window-root 或 editor-owned dock model，不创建 native window，也不认识 `EditorRootId` / tab scope。Editor 侧用 `canDockEditorTab(tab, targetPlacement, targetRootId)` 决定能否物化进该 dock。
2. `EditorTabSpawnerRegistry` 只注册 factory，不拥有 tree/widget。
3. 每个 tab 先分类为 `WindowRootEditor`、`EditorOwnedTool` 或 `WindowTool`；owned tool 必须记录 `ownerEditorId`。
4. Level Editor 是 main-window non-closable root editor。
5. document/dirty/undo/selection 按 editor/document owner 管理。
6. 跨窗迁移必须 detach → keep-alive → attach → focus transfer。
7. source 不保留 detached tab parent edge；target 不重复 attach。
8. 旧 `editor.dockLayout` 只恢复 main window（v1–v3）。v4 `windows[]` 是 OS 窗 topology（MW-801）；Dock JSON 的 `windows[]` 是 NativeWindow placement（MW-707）。产品启动 recover 主窗 screen placement，并经 coordinator restore extra（C9-P）；extra present 在 primary submit 之后。缺失 monitor 迁到可用屏，不把 overlay 坐标当 origin。
9. root editor 和允许 detachable 的 owned tool 都能 tear-off；owned tool 不能 dock 到其他 root editor/tab，独立窗口仍保留 ownerEditorId。
10. `EditorSurface` 只做当前窗口 UI 编排；禁止改成 window manager，也禁止把字段整体倒进 `EditorWindowSession` god object。
11. 新代码只走 `EditorWindowSession::tick` + `FEditorSurfaceContext`；`tick(App&)` 已删除（ES-5）。
12. 文本输入走 `WidgetTree::wantsTextInput()`，禁止 Surface 特判具体 tab 类型。
13. 第二扇 `EditorWindowSession` 只走 MW-401：独立 tree/dock，禁止两棵树画进同一 native window，禁止每窗 `IRender::create`。

## 收尾

1. 运行本 checkpoint 定向构建和测试。
2. 至少按影响运行 `xmake b ya-gui-host`、`xmake b ya-gui-closure-test`、`xmake b ya-game-runtime`、`xmake b ya-game-editor`。C2G 额外 `xmake b GUIWorkbench`；`--extra-window --exit-after-frame=N`、`--headless --start-page Windows --scenario Example/GUIWorkbench/Scenarios/windows_extra_os.jsonl`、`--smoke-actions`。
3. 涉及 GPU/presentation 时运行 `python3 Script/automation/gui/run_workbench_gpu_parity.py`。
4. 补充双窗、resize、close、focus、tear-off、re-dock 证据。
5. 运行 `git diff --check` 和 `python3 -m json.tool .agent/plan/gui-multi-os-window-editor/feature_matrix.json`。
6. 检查 staged diff，排除其他 GUI/Editor/Workbench 改动。
7. 同步 todo/progress/matrix；checkpoint 闭环后代码、测试、计划同一 commit。
8. 若涉及 C7，额外验证 in-process floating 与 native-window placement 不混淆，且记录每个 window 的 WidgetTree/surface/snapshot 所有权。
