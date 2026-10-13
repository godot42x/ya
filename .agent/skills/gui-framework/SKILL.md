---
name: gui-framework
description: YA GUI 框架（WidgetTree / 控件 / layout / Render2D pass slot / host 诊断）的模块地图与稳定契约。
---

## 适用场景

- 在 `Engine/Source/Framework/GUI/` 内改控件、布局、事件、快照、合成
- 开发 GUIWorkbench demo / GameEditor chrome
- 排查 GUI 渲染、布局、生命周期问题（GPU 资源 teardown、pass slot、clip/scissor）


## 计划与提交门禁

- GUI 迁移按用户指定的 feature/tab 闭环推进；不得用 registry、placeholder、纯拆文件或仅补 contract 文档冒充 feature migration。
- 一个 checkpoint 只能对应一个可运行、可验证的架构目标；代码、测试和 plan/progress 必须在同一提交中，且提交说明必须写清未完成项。
- `EditorSurface`、`UICompoundWidget`、WidgetTree 和 DSL 的边界若尚未验证，不得继续向宿主文件堆实现；先停下来做边界审计。


## 模块地图

```text
Framework/GUI/
  Runtime/Widgets/    ya-gui-widgets     UIElement / WidgetTree / UIFrameSnapshot / 控件
                                         （Layout / Binding / Declarative 编进本 target）
  Runtime/Compose/    ya-gui-compose     共享 2D 合成 pass（UIFrameSnapshot -> Render2D）
  Tooling/            ya-gui-tooling     WorkbenchSurface / Workspace（工具 UI 外壳，demo 无关）
  Host/               ya-gui-host        standalone 宿主：SDL 窗口、Vulkan、帧循环、automation
Framework/Render/                        ya-render-2d / ya-render-resources（Font/glyph、Render2D）
Framework/App/Kernel  ya-app-kernel      唯一 while-loop；GUI 与 GameRuntime 共用
Example/GUIWorkbench/                    Feature Gallery（FWorkbenchSurface 分组 rail：
                                         Diagnostics/Controls/Layout/Paint/Text/Style/
                                         Overlays/Interaction/Data/Composition）
Applications/GameRuntime                 ya::App 产品壳（scene / RenderRuntime / modules）
Applications/GameEditor                  EditorModule + EditorWindowRegistry / EditorWindowSession
                                         （session 持有 EditorSurface；不是独立主循环）
```

`ya-gui-framework` 是聚合 meta target（widgets+compose+tooling 等），**不含** host。
GUI 测试只链 GUI closure（`ya-gui-closure-test` 不依赖 Scene/ECS/Render3D/Editor）。
standalone GUI 可执行文件显式链 `ya-gui-host`。

不要合并 `GUIWindowHost` 与 `ya::App` 的 present / 输入栈：Workbench 必须保持 GUI
closure；Editor 必须吃 3D viewport + swapchain。结构整理优先让 `EditorSurface`
成为可阅读 orchestrator，而不是再造一条产品宿主。


## 责任规则（不按目录搬文件）

划分按责任。禁止用物理搬家冒充边界收口。

1. `GUI/Window` 不 include GameEditor，**也不出现 `FDockContext` / DockPlacement 类型**。`IGUIWindowCoordinator` 只做 create/destroy/find session。Native dock placement 是 Host 适配器 `realizeNativeDockPlacement(coordinator, dock, …)`（`GUIDockNativePlacement.h`），不是 coordinator 方法。
2. `GUI/Docking` 只做通用 Tab/Dock/Layout/Drag transaction；要 OS 窗就调 `createSession`，自己不 `IRender::create`。
3. `GameEditor/Shell` 只编排 Surface / WindowRegistry / WindowLayout。
4. `GameEditor/Tabs` 只做 tab 内容。
5. `GameEditor/Docking` 只做 editor placement/ownership policy 和 payload。
6. `EditorLayer` 属于 editor runtime/domain，不是 GUI 宿主。
7. Workbench 不是 GUI Runtime 核心抽象；**EditorTheme 不得 include WorkbenchTheme**。共享 chrome 值在 `buildDefaultChromeTheme`。
8. 跨窗拖拽只保留 `GUIDragRouter`。`GUIApp` 的 `bindDragRouter` / `routeCrossWindowDrag` 是 Router 转发，禁止再加第二入口文件。

Primary 仍是 `GUIWindowHost`，extra 仍是 `GUIWindowManager`；并成一种 session 之前不要假装只有一套。


## 产品循环

唯一 while-loop 是 `AppKernel`。两条产品线在 kernel 之下分叉，不要读成「GameApp vs GuiApp」类型对。

**AppKernel 是共享控制面，不是应用基类——不要为了「让独立 GUI 更轻」而删它。**
它只持有三件每条产品线都需要、且各自实现会失真的东西：单实例 `OsProcessLock`、`AppAutomationRunController`
（exit-after-frame / 墙钟上限 / 远端退出）、以及唯一的事件泵 + 帧计时 + `exit` 判定。
实现 `IAppLoopDelegate` 的成本是 5 个虚函数，而 `GUIWindowHost` / `GUIApp` / `GUIHeadlessHost`
各自只在 `run()` 里写 4 行来构造它。

真正会让人误判的是 `ya-gui-framework` 这个**聚合目标**：它公开拉入 `ya-app-kernel` /
`ya-app-control` / `ya-hierarchy`，于是"GUI framework"看起来等于 GUI + app shell + module system。
要修的是这个聚合边界（见 `.agent/plan/source-layout-subtraction` S2），不是 Kernel 本身。
删 Kernel 会把上面三件事复制到 3 条产品线上，而它们各有 memory
（`control_instance_lifecycle`、`app_teardown_order_and_instance_lock`）记录过坑。

```text
AppKernel
  ├─ GUIApp / GUIWindowHost     GUI-only（无 Scene / ECS / Render3D）
  │    SDL → dispatchEvent → tree.tick → delegate.updateUI
  │    → buildSnapshot → compose → present
  │    GUIWorkbench：IGUIAppDelegate 挂 FWorkbenchSurface
  └─ ya::App                    游戏 / 编辑器产品壳
       GameRuntimeFrameOrchestrator
         tickLogic → EditorModule::onLogic
           scene context | editor camera | prepare compose pipelines
           EditorLayer::onUpdate | pending viewport resize
         tickRender → RenderRuntime world graph
           EditorModule::onViewportCompose
             EditorViewportCompositor
               world RT + recordEditorWorldViewportOverlays
             setViewportDisplayImage
             EditorUICanvasCompositor（仅 Canvas tab attach 时）
               preview tree + grid + recordEditorCanvasSelectionOverlay
               → designer.canvas().image（Canvas tab tick 时包成 Texture）
           EditorModule::onPresentation
             EditorWindowSession::tick（default window）
               → EditorSurface::tick
               rebuild-if-needed → window metrics
               → WidgetTree::tick → shell dialogs
               → pushViewportDisplay → buildSnapshot
               → publishViewportRect → viewport overlay host
             replayUIFrameSnapshot(..., EditorToolSurface)
           submitPresentFrame
         EditorModule::onAfterPresent
           close extras | reclaim empty | orphan GUI sessions
           GUIWindowManager::tickTrees + renderAll
```

Editor 全链路以 `Applications/GameEditor/EditorModule.cpp` 文件头注释为准；不要再造第二条产品宿主。
`EditorWindowSession::tick` 是 Module 侧 chrome 入口；`EditorSurface::tick` 仍是窗口内编排。Tab 由 `EditorTabSpawnerRegistry`
spawn，root 是 `UIElement` / `UICompoundWidget`；attach/detach/tick 只由 `WidgetTree`
驱动。Surface 只编排 shell、dock persist、viewport host bridge 和 dialogs。
禁止 `tab->sync`、禁止 Surface 持有 Tab 控件指针。不要再引入 `EditorPanel` 或中心
事件总线。长线见 `.agent/plan/archive/gui-editor-tab-lifecycle/`。


## 细节参考（按需读，不要全载）

L4 细节按主题切在 `references/`，只在需要展开时读对应的一份：

| 何时读 | 文件 |
| --- | --- |
| 改 WidgetTree（层/输入路由/pointer session/拖放/行为模型/快照/帧生命周期） | `references/widget-tree.md` |
| 改控件、布局契约、SizeToContent、slot 族、UICompoundWidget | `references/layout-and-controls.md` |
| 改动画（tween / 可动画属性 / render transform）、写 DSL 构造 | `references/animation-and-dsl.md` |
| 改 Style / Theme、调色板、画笔、style 失效粒度 | `references/style-theme.md` |
| 改 Dock / 浮窗 / 拖拽 / tear-off 权责 | `references/dock.md` |
| 改 Render2D pass slot、compose surface、Host 窗口与生命周期 | `references/host-and-render.md` |
| 写控件交互行为、做 UX 验收、改自动化场景 | `references/interaction-contract.md` |

GameEditor 是 Application 层，不在本 skill：见 `../game-editor/SKILL.md`。

## 构建 / 测试

```bash
make b t=GUIWorkbench && make r t=GUIWorkbench          # standalone demo
make r t=GUIWorkbench ARGS="--smoke-actions"            # 端到端自动化
make run t=HelloMaterial / make run-editor t=HelloMaterial
xmake b ya-gui-closure-test && xmake r ya-gui-closure-test
xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test
xmake b ya-gui-headless-host-test && xmake r ya-gui-headless-host-test
xmake b ya-gui-workbench-workspace-test && xmake r ya-gui-workbench-workspace-test
# windowed frame / process-memory / VRAM summary (same bench_summary.json as the game runtime)
# GUIWorkbench --benchmark Example/GUIWorkbench/Benchmark/GUIWorkbench.bench.json
```

macOS / MoltenVK convergence gate (must be run on macOS, not emulated from a
Windows runner):

```bash
python3 Script/gui_convergence_macos_validation.py
```


