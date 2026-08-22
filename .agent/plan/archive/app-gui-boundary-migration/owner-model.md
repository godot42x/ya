# Phase A Owner Model — AppKernel / GUIApp / GUIWindowHost / WidgetTree

> 状态：Phase A 的规范基线。本文区分**目标对象模型**、**当前落点**与**迁移方向**；
> 不把过渡实现误写成终局。更新时间：2026-08-13。

## 1. 一句话职责图

```text
AppKernel                         唯一 loop / event-source / frame-time / base exit policy
  └─ GUIApp                       GUI 应用装配、native window manager、跨窗口策略
       └─ GUIWindowHost[n]        一窗口一 tree、native window、presenter、pointer context
            └─ WidgetTree         单窗口 live visual tree + per-tree interaction state
                 ├─ Content
                 ├─ Popup
                 ├─ Tooltip
                 └─ DragIme
```

`AppKernel` 不知道 widget、layout、RHI target 或具体产品页面。`GUIApp` 不是第二个
主循环；它只是作为 `AppKernel` delegate/模块装配 GUI window hosts。`GUIWindowHost`
不拥有整个 workspace；`WidgetTree` 更不代表整个应用。

## 2. 当前实现与目标映射

| 当前类型 / 位置 | 当前事实 | 目标去向 | 分类 |
|---|---|---|---|
| `Foundation/Core/Application/AppKernel` | GUI windowed/headless hosts 与 `Product/Host/AppFrameLoop::run` 都已调用；提供 event source、tick 与基础 exit policy | 保留为唯一共享 loop 内核 | 保留 |
| `Framework/GUI/App/GUIApp` | standalone GUI entry 的装配层；当前持有一个 primary window 并创建 `AppKernel` | 保留；后续在这里增长 `NativeWindowManager` 级策略与多窗口编排 | 保留 |
| `Framework/GUI/App/GUIWindowHost` | 当前持有 SDL window、Vulkan presenter、WidgetTree、pointer state、control server | 保留为 concrete one-window owner | 保留 |
| `Foundation/RHI/NativeWindow*` | 当前把窗口对象语义与 Vulkan/OpenGL present bridge 混在一起 | 拆成 `INativeWindow` + `IPresentSurfaceSource`（命名待最终拍板） | 过渡 / 拆分 |
| `Framework/GUI/App/GUIAppHost` | `GUIApp` 的 compatibility alias | 只为旧 include/调用兼容；新代码不再使用 | 过渡 / 删除 |
| `Framework/GUI/App/GUIHeadlessHost` | 无 SDL/RHI 的 AppKernel + WidgetTree + snapshot host | 保留为 GUIApp/WindowHost 的无 native-surface 验证形态；不成为另一条 product loop | 保留 / 过渡 |
| `Framework/GUI/App/GUIRenderSurface` | 统一 imported swapchain 与 owned offscreen RenderImage 的 compose/final-layout 边界 | `GUIWindowHost` presenter/target 的底层资源对象 | 保留 |
| `Product/Host/App` | 游戏/runtime/editor 产品状态、scene/module/render services | 保留为 game/editor application module；经 AppKernel adapter 运行 | 保留 |
| `Product/Host/Lifecycle/AppFrameLoop` | SDL event source + product frame work（logic/render/callback/advanced automation） | 自有 while 已删除；`run()` 仅装配 AppKernel adapter | 过渡 / 拆分 |
| `Product/Host/GUI/GameUI/GameUIHost` | 一个 game presentation area 的 WidgetTree + scene/input/snapshot adapter | 保留为 Product Host 的 viewport UI adapter；不是 generic GUIApp | 保留 |
| `Product/Editor/Panels/GUIWorkbenchPanel` | ImGui panel 内的 workbench tree/extent/input adapter | 保留为 editor embedding adapter；不拥有 native window 或 loop | 保留 |
| `Product/Editor/EditorToolSurfaceCompositor` | editor panel snapshot -> owned offscreen `GUIRenderSurface` | 保留为 editor caller-side compose policy；不另造 surface resource model | 保留 |
| `Tooling/Workbench/WorkbenchSurface` | menu/tab/status/workspace shell | 保留为 tooling shell；非 app/host/loop | 保留 |
| `Example/GUIWorkbench/FWorkbenchApp` | feature gallery 页面与 smoke state | 保留为 example delegate；非 framework app kernel | 保留 |

## 2.1 共享能力与应用形态的 owner 口径

Owner 模型默认同时遵守两条轴：

- 共享能力 owner：`AppKernel`、`GUIApp`、`GUIWindowHost`、`WidgetTree`、未来的 `Render/Runtime`、`Scene`、`Physics`、`Scripting` 等；
- 应用形态 owner：`GameRuntime`、`GameEditor`、`GuiWorkbench`、未来的 `DccEditor`、`ModelViewer`、`CLI`、`RenderServer` 等。

这意味着：

- `Game` / `Editor` 可以是 app-form owner，但不是 physics / scripting / scene / render runtime 的默认 owner；
- 如果某个能力被多个 app form 共享，它就必须先在共享能力轴上找到 owner，再由上层 app form 组合；
- `Product/Host/App` 这类历史壳后续只允许保留 app-form 组合语义，不能继续藏共享能力。

## 3. 状态归属

| 状态 | 正式 owner | 当前落点 | 迁移约束 |
|---|---|---|---|
| native window / swapchain acquire-present / presentation target | `GUIWindowHost` | `GUIAppHost` | 一窗口一组 target；resize 只在 frame boundary rebuild |
| native window identity / title / size / raw handle | `INativeWindow`（被 `GUIWindowHost` 持有） | `INativeWindow` | 不再把这组语义直接暴露为 RHI window provider 合同 |
| present surface create/destroy / required instance extensions / present-source binding | `IPresentSurfaceSource`（通常与 `INativeWindow` 同 concrete 对象共存） | `INativeWindow` 上的 Vulkan hooks | RHI 最终只依赖最小 present bridge，而不是完整 window owner 接口 |
| logical extent / pointer position / pointer buttons | `GUIWindowHost` | `GUIAppHost` 的 last mouse；`GameUIHost` 的 viewport mapping | 上层业务不得反复携带“当前鼠标点”作为隐式状态 |
| focus / hover / pointer capture / popup / tooltip / drag overlay | `WidgetTree`（per window） | `WidgetTree` | 未来升级为 focus/pointer path，不升级为 app-global widget pointer |
| per-window modal gating | `GUIWindowHost` + tree route policy | popup/modal 控件局部 + tree layer | Phase C 前必须明确 route policy，不把 modal 状态塞进 leaf |
| whole-app modal stack / active window / activation order | `GUIApp` | 尚未实现 | 预留 app registry，不在单 tree 中伪造全局状态 |
| cross-window drag session | `GUIApp` | 尚未实现；tree 有单窗口 drag session | source/target/commit/cancel 由 app 协调，tree 只处理本窗口 hit/path |
| immutable UI frame snapshot | `WidgetTree` build，surface consumer retain | `WidgetTree::buildSnapshot` / `GUIRenderSurface` | command recording 只消费 snapshot，绝不回读 live tree |
| app-specific workspace/page state | 产品 / tooling delegate | `FWorkbenchApp` / `FWorkbenchSurface` | 不下沉到 WidgetTree 或 GUIWindowHost |

## 4. 生命周期清单

| Owner | 创建 | 每帧 | resize / restore | shutdown | dump / control |
|---|---|---|---|---|---|
| `AppKernel` | caller 构造，接收 event source + delegate | poll -> delegate tick -> exit policy | 不解释 GUI resize | 调 delegate shutdown | base frame stepping / exit policy |
| `GUIApp`（目标） | 产品入口装配 GUI services + windows | 选择 active window / 跨窗口 policy | 创建/销毁 window host | 先停止 windows，再释放 app services | window-id 路由、app-scope scenario |
| `GUIWindowHost`（目标） | 创建 native window、presenter、tree | native event -> tree; snapshot -> surface -> submit/present | wait safe point 后重建 imported targets | 先 release command retention/surface/tree，再 destroy render/window | GPU shot、surface parity、per-window tree/snapshot dump |
| `INativeWindow` / `IPresentSurfaceSource` | GUI host 创建具体 window 对象；present bridge 可与 window 同 concrete 实现 | window message、surface source 查询、size/handle 访问 | surface rebuild 随 host/presenter 在安全点完成 | 先释放 presenter/swapchain，再销毁 native window 与 surface source | RHI 侧只看 present-source 证据，GUI 侧看 window identity/state 证据 |
| `WidgetTree` | window host 创建 | layout dirty 时 layout + build snapshot；route event | logical extent 改动导致 layout invalidation | detach clears transient references | tree / route / hover / focus dump |
| `GUIRenderSurface` | window host 或 editor caller于 frame boundary 创建 | prepare + record immutable snapshot | owner 替换旧 surface 前 wait submit | 在 RHI/VMA teardown 前释放 | offscreen/windowed BMP parity |

## 5. control plane 归位

基础能力归 `Foundation/Core/Application`：

- `AppKernel`：event-source sequencing、frame timing、shared exit policy；
- `AppControlRunOptions`：exit-after-frame / control port / stdin 命令入口等基础选项；
- `GuiEventDriver`：JSONL mouse/key/wheel/drag/resize/checkpoint step 语义；
- `BmpDiff` / `ControlServer`：通用 capture/diff/RPC 基础。

这里的 automation 现在被降级为 `App/Control` 下的一个子能力；更大的总概念是 control plane：

- CLI / command dispatch；
- 交互式 stdin / shell 控制；
- remote / agent control；
- scenario / replay / frame stepping；
- capture / diff / inspection。

产品扩展不重写 loop：

- standalone GUI：`GUIAppHost`（过渡期）/ future `GUIApp` 注册 window surface 与
  GUI-specific RPC；
- game/editor：`Product/Host/App` 注册 scene/render/screenshot/editor RPC；
- Workbench：只注册 feature actions/scenarios，不再拥有 timing/event pump。

当前已知迁移缺口：`Product/Host/AppFrameLoop` 已经不再持有 while loop；它保留 SDL
event source 和 product frame work。高级 screenshot、RenderDoc、scene-stability
control 扩展仍属于 Product Host extension，暂不错误下沉到 GUI 或 AppKernel 基础层。

当前已知迁移缺口补充：`NativeWindow` 仍然是 window object 与 present bridge 的混合接口；在真实目录迁移前，必须先给出“哪些成员留在 `INativeWindow`、哪些下沉到 `IPresentSurfaceSource`、哪些调用者只该依赖 presenter/host”这张三分表。

## 6. 多窗口默认语义（先定，不实现）

1. `GUIApp` 维护 `NativeWindowManager` 与 active window；每个 `GUIWindowHost` 一棵 tree。
2. popup / tooltip / drag overlay 默认 per-window。
3. focus 基础归属 per-window；active-window 切换整体切换/失效 focus path，不保存裸
   global focused widget。
4. dragdrop 允许跨窗口：GUIApp 保存 source window + payload，hover target window
   由 pointer hit window 决定，drop commit 只在目标 tree 接受后发生；cancel 恢复 source
   window transient state。
5. modal 明确区分 per-window 与 whole-app。whole-app modal 由 GUIApp gate windows，
   不能用普通 popup layer 冒充。
6. control 注入默认携带 target window id；单窗口 CLI 可以省略，解析为 default
   window。

## 7. Phase A 实施顺序

1. [x] `Product/Host/AppFrameLoop` 改为 AppKernel-backed adapter，保留已有 frame
   work / advanced product control extension，删除第二个 while/event-pump 主循环。
2. [x] 把 `GUIAppHost` 的 concrete single-window owner 命名收口到
   `GUIWindowHost`；旧名是 `GUIApp` compatibility alias。
3. [x] 新建最小 `GUIApp` primary-window assembly，让 AppKernel 在 GUIApp 层创建；
   v1 只有一个 primary slot，未来多窗口编排与 `NativeWindowManager` 策略在此增长，不在此阶段实现 docking
   或 cross-window drag。
4. [ ] 在 Phase C 前把 current pointer/focus/capture 从 host 传参收口为
   window/tree state。

## 8. 禁止事项

- 不再为 GUI、game、editor 复制新的 `while (running)` 或 SDL poll loop。
- 不把 app-global modal / drag / active-window 状态藏进 `WidgetTree` 或 leaf widget。
- 不把 WorkbenchSurface、GUIWorkbenchPanel 或 GameUIHost 重命名成 GUIApp；它们分别是
  shell、editor embedding、game viewport adapter。
- 不因“未来多窗口”提前实现 docking/window manager；现在只立 owner boundary。
