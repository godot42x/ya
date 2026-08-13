# GUI 架构收敛：详细推进方案

> 建立日期：2026-08-13
> 状态：Phase 1 harness 已闭环。Phase 2 单循环收敛已落地：AppKernel（Foundation，
> 无 RHI）+ GUIAppHost 迁移到 AppKernel + SDL/场景抽成 IAppEventSource（事件相位
> 统一，宿主在 onEvent 内记录鼠标位置）。剩余：离屏 presenter、SDF、布局增强。
> 承接：../gui-app-bootstrap/plan.md（已定义 shared foundation / GUI library /
> standalone host 三角色）；本文件不再回写 GUI app 自举本身，只规划下一步的
> "观测 harness → 单循环收敛 → SDF 字体 → 布局增强" 主线。

## 0. 结论摘要（TL;DR）

当前混乱的根因不是功能缺失，而是三层壳并存且 GUI 缺陷不可见：

1. 两个真实主循环：引擎 App（Product/Host）+ GUIAppHost（Framework/GUI/App）。
2. 编辑器是第三层壳：ImGui 窗口里嵌一棵 WidgetTree，事件从 ImGui 手工翻译回 tree。
3. 没有给 agent 的"眼睛"：只有 CPU 矩形 dump 和 GPU 截图，没有黄金 diff、树 dump、
   事件录制回放。
4. 字体按字号逐份光栅化 atlas；RHI 底层类型泄漏到应用层。

已拍板的四个决定：

- 主循环唯一：game engine / game / gui app 只往同一个循环注入逻辑；呈现不进 kernel，
  CLI / dedicated server 就是"无窗口 sink"。第一刀先迁 GUI 线。
- 先建观测 + 自动化 harness，且支持离屏 headless。
- 字体走 SDF 单图集 + ASCII 种子 + 动态追加。
- 顺序：harness → 单循环收敛 → SDF → 布局/文本；RHI 不做全面重构，只立边界 + 最小门面。

## 1. 现状事实锚点（调研结果）

### 1.1 布局与事件

- 布局入口：WidgetTree::layout()（invalidateLayout() 置脏，buildSnapshot() 内自动跑）。
- 锚点数学：UIElement::computeAnchorRect()，rect.min = parent.pos + parent.extent*anchorMin
  + position；尺寸解析优先级 anchor 拉伸 > AutoSize > _size。
- 盒式排列：UIContainer::arrangeChildren()，按 computeDesiredSize() 打包，主轴
  Start/Center/End，_spacing + _padding(vec2)；子节点 cross 轴填满、主轴取 desired。
- 专用布局：UISplitPane（双 pane + 拖拽 divider + min extent）、UIScrollViewport
  （content offset + wheel + clip + cullChildHits）、UIButton（内容槽 contentPadding）。
- 文本：UIText::computeDesiredSize() = Font::measureText x lineHeight，addText 只做
  h/v align，无换行、无省略号。
- 事件：Core Event 家族（MouseMove/Press/Release/Scrolled/KeyPressed/Released/Typed…）
  进 WidgetTree::dispatchEvent()（zOrder 分层命中、capture、focus、hover、drag session）。

编辑器级缺口：容器不能表达"拉伸填满剩余空间"（flex-grow），只能靠 magic padding
（上轮 ScrollSplit 页头遮挡正是这个坑）；无 grid、无文本换行/省略；无 docking / 多窗口
（按既定边界继续不做）。

### 1.2 双主循环与三层壳

- 引擎循环：App（Product/Host/App.h，god-object + friend 若干）→
  AppFrameLoop::run/iterate（Lifecycle/AppFrameLoop.cpp）：SDL pump（processNativeEvent
  → app.dispatchEvent）、FPSControl、tickLogic、tickRender、TaskQueue、automation
  onFrameCompleted。编辑器以模块方式挂在 ya-runtime --editor 上，编辑器内 ImGui 调用
  约 1091 处。
- GUI 循环：GUIAppHost（Framework/GUI/App/GUIAppHost.cpp）：自己的 SDL pump、swapchain
  重建、compose、present；自己内嵌 AppAutomationControlServer（ping/quit/set_window_size）。
- 第三层壳：GUIWorkbenchPanel（Product/Editor/Panels/）用 ImGui 窗口承载一棵 WidgetTree，
  routePanelInput 把 ImGui 鼠标转成 dispatchEvent，合成到离屏 RenderImage 再让 ImGui 显示。
  这是"引擎循环 → ImGui 壳 → WidgetTree"三明治。

### 1.3 自动化

- Foundation 已有：AppAutomationRunOptions/Controller（退出策略，GUI/引擎共用）、
  AppAutomationControlServer（asio TCP + JSON，Request{id,method,params} → completeRequest）。
- 引擎：AppAutomationControlService 30+ 命令（ping / set_* / capture_screenshot /
  list_scene_entities / list_commands / invoke / eval_js …）。
- GUI：GUIAppHost 自己内嵌 control server，命令在 run() 帧边界处 consume。
- WidgetTree::dispatchEvent 已接受任意合成 Event（现有 smoke 就是这么注入的），缺的是
  框架级驱动（录制/回放/脚本）+ 断言（树 dump / 黄金图），而不是每 app 再写 switch-case。

### 1.4 观测工具现状

- 已有：dumpSnapshotToBMP（CPU 光栅 draw items + 打印 item pos/size）、GPU readback
  writeRGBAtoBMP（处理 B8G8R8A8 字节序）、--smoke-actions（example 内 switch-case）。
- 缺失：元素树 JSON dump、黄金截图 diff（含差异图）、布局/命中调试 overlay、事件录制
  回放、headless 运行路径。

### 1.5 字体

- FontManager（343+242 行）：loadFont 用 FreeType 按 name:size 光栅化并 row-pack 进
  nextPow2 atlas；ensureGlyphs 对缺失字形逐字 appendStandaloneGlyph；cache 键 name:size；
  registerFont 供测试注入合成字体。
- Workbench 加载 12~16 共 5 份 atlas，内存随字号线性增长；无任意字号、无 SDF。

### 1.6 RHI 边界

- RHI 模块：Foundation/RHI（ya-rhi）+ Backend/{Vulkan,OpenGL} + Backend/include（common）。
  Core 头：RenderImage / Texture / Image / Swapchain / CommandBuffer / …。
- 泄漏计数：Product 层 39 个文件、GUI/App 层 4 个文件直接 include/使用
  RenderImage/IImageView/ITexture/IRender/ICommandBuffer。
- GUI/App 的 4 个文件是 GUIPresentationTarget.* + GUIAppHost.cpp（都在呈现侧）。

### 1.7 模块 / 目标图

- ya-app-runtime（Framework/AppRuntime）：Bootstrap + WindowManager，deps =
  ya-foundation-core + ya-rhi。注释已声明"shared by GUI apps and the engine host"，
  是 AppKernel 的天然落点。
- ya-gui-framework：foundation + hierarchy + rhi + backend + gui-resources/draw2d/
  widgets/compose/tooling（纯 GUI 聚合，无 exe）。
- ya-gui-app-host：GUIAppHost。ya-host：引擎 App。ya-editor / ya-engine / ya-runtime：
  游戏/编辑器线。

## 2. 目标架构

    Foundation
      Core/Event           事件结构（已存在）
      Core/Application     AutomationRun / ControlServer（已存在，扩容通用命令）
      AppRuntime
        AppKernel          唯一主循环：事件源 + 帧节拍 + 逻辑回调 + 退出策略
                           不认识 RenderImage / Swapchain / WidgetTree
        IAppEventSource    SDL / 网络 / 回放文件 三种实现
        PresentSurface     呈现边界门面（可选 sink；无窗口=不挂）
    Framework/GUI
      WidgetTree / 控件 / snapshot / compose（纯 GUI，已存在）
      GUI 呈现器           WindowedPresenter / OffscreenPresenter（实现 PresentSurface）
    Product/Host
      App（引擎游戏/编辑器）—— 逐步收敛到 AppKernel（本次先不动）

关键原则：kernel 只持有 loop + event pump + timing + exit + automation glue，不持有
presentation。窗口 presenter、离屏 presenter、dedicated-server 空 sink 都是可插拔的
PresentSurface 实现，由产品线注入。

## 3. Phase 1 — 观测 + 自动化 harness

目标：让 agent 能在 headless 下驱动 GUI、看树、看像素、看差异。

### 3.1 输入驱动器（Foundation）

新增 Core/Application/GuiEventDriver（名字可再定，落在 Foundation，引擎/GUI 共用）：

    // 场景脚本：JSON lines，每行一个 step
    // {"frame": 1}
    // {"event": {"type":"mouse_move","x":120,"y":80}}
    // {"event": {"type":"mouse_press","button":0,"x":120,"y":80}}
    // {"event": {"type":"mouse_release","button":0,"x":120,"y":80}}
    // {"event": {"type":"key_press","key":"Enter"}}
    // {"drag": {"from":[100,100],"to":[200,200]}}
    // {"checkpoint": {"dump_tree":"tree.json","capture":"frame.png"}}
    struct IGuiEventSink { virtual EWidgetRouteResult dispatch(const Event&) = 0; };
    class GuiEventRecorder  { /* 包 dispatch：记 frame+event 到 JSONL */ };
    class GuiScenarioRunner { /* 注入 sink + 帧步进 + dump/capture 提供者，逐 step 执行 */ };

- 复用现有 Event 结构，不再每 app 写 smoke switch-case。
- drag 展开成 press → 若干 move → release；key_press 走 EKey::fromSDLKeycode 同源映射。
- 录制器把宿主事件泵的真实事件落成同一 JSONL，实现"先录后放"。

### 3.2 元素树 dump

新增 GUI/Widgets/WidgetTreeDump.h（框架级，不碰 RHI）：

    nlohmann::json dumpWidgetTree(const WidgetTree& tree);
    // 每节点：name, typeId, rect{pos,extent}, visibility, zOrder, hitFilter,
    //         focusPolicy, bFocused, bHovered, captured, children[]

同时提供 dump-tree 命令与 checkpoint 落盘；这是"纯体验问题可断言"的基础。

### 3.3 离屏 presenter（headless 前提）

把编辑器 GUIWorkbenchPanel 已验证的"合成到离屏 RenderImage"路径正规化：

    // Framework/GUI/App/OffscreenPresenter
    struct OffscreenPresenter {           // 未来实现 PresentSurface
        bool init(IRender&, Extent2D);
        std::shared_ptr<RenderImage> beginFrame();   // 离屏 target
        void presentAndReadback(const std::string& pngPath); // GPU 读回 + 写图
    };

- 不建 swapchain、不要求可见窗口；Render2DComposePass 直接合成到离屏 target。
- 这是 PresentSurface 的第一种实现，也是 AppKernel 无窗口 sink 的雏形。

### 3.4 黄金截图 diff

- capture 命令：离屏/窗口读回写 PNG（复用现有 writeRGBAtoBMP 字节序处理，改 PNG）。
- diff 命令：像素阈值比对 + 输出差异图（差异像素高亮），返回通过/失败。
- 基线小集合入库（Example/GUIWorkbench/Test/Golden/）。

### 3.5 CLI

在通用 AppAutomationControlServer 协议上新增共享命令，GUI 与引擎都挂：

    step_frame  inject_event  dump_tree  capture  diff  quit

GUI 侧把这些命令加进 GUIAppHost::run() 现有的 consumePendingRequests 分支；引擎侧
AppAutomationControlService::handleCall 保持引擎域命令，仅共享上面这些通用命令的传输与
JSON 规范（不进引擎域实现）。

### 3.6 回归场景

把上轮四个缺陷固化成 headless 场景（树断言 + 黄金图）：

- menus：点 File → hover Edit → 断言 open menu 首项 label == "Undo"（树 dump）。
- menu_width：dump 菜单页，断言面板 rect ≥ 最大标签宽 + padding（树 dump）。
- scroll_split：dump 页头与 split 首行，断言无重叠（rect 结构断言）。
- button_hover：press A → press B，断言 A._bHovered==false、B 状态正确（树 dump）。

### 3.7 文件与验收

- 新增/改动：Core/Application/GuiEventDriver.*、GUI/Widgets/WidgetTreeDump.*、
  GUI/App/OffscreenPresenter.*、GUIAppHost.cpp（通用命令 + headless 模式）、
  Example/GUIWorkbench/Test/（场景 + golden + 一个 ya-gui-scenario 测试目标）。
- 验收：xmake r GUIWorkbench --headless --scenario=menus.json 绿；4 个场景全部产出
  tree dump + 差异图；窗口模式与 headless 共用同一 snapshot/compose 路径。

## 4. Phase 2 — 单循环收敛（先迁 GUI 线）

### 4.1 AppKernel（Foundation/Core/Application）

注：内核不含呈现、不依赖 RHI；dedicated server / CLI 不应被迫链 GPU，因此放在
Foundation 而不是会拉 RHI 的 AppRuntime。窗口/离屏 presenter 放 GUI/App。

    // Framework/AppRuntime/AppKernel.h
    struct IAppEventSource { virtual std::vector<Event> pollEvents() = 0; };
    struct IAppLoopDelegate {
        virtual void onInit() = 0;
        virtual void onTick(float dt) = 0;      // 逻辑 + snapshot（不含 present）
        virtual bool shouldClose() = 0;
        virtual void onShutdown() = 0;
    };
    struct IAppFrameSink {                       // 可选；无窗口则不注入
        virtual bool acquireFrame() = 0;
        virtual void presentFrame() = 0;
        virtual Extent2D getExtent() = 0;
    };
    class AppKernel {
        // config: eventSource, loopDelegate, frameSink(optional), runPolicy
        int run();
        int iterate(float dt);                   // 供 step_frame / 测试
    };

内核只做：事件泵 → onTick → （可选 sink present）→ shouldClose 判定 → 退出。
PresentSurface 边界规则：kernel / sink 只见 PresentSurface，RenderImage / IImageView /
ICommandBuffer 只允许出现在 presenter 实现内部（GUI 侧收敛到 OffscreenPresenter /
WindowedPresenter 两个文件）。

### 4.2 GUIAppHost 迁移

- GUIAppHost 变成 AppKernel 的薄配置：SDLEventSource + IGUIAppDelegate 实现的 loop
  delegate + WindowedPresenter 或 OffscreenPresenter；删除自己的 pumpEvents/run 重复实现。
- 保留现有 FGUIAppHostConfig 语义，IGUIAppDelegate 接口不变（buildUI/updateUI/
  onRoutedEvent/shouldRequestClose），避免示例层改脸。

### 4.3 引擎 App 不动 + 文档

- 引擎 App/AppFrameLoop 本次不迁；AppKernel 接口按"引擎未来可迁"设计（事件源/sink/
  delegate 可插拔）。
- 在 .agent/plan 本文件 + 一份 docs/architecture（或 plan 内图）落主链路图与分层边界，
  回答"哪个是真正主循环"。

### 4.4 验收

- GUIWorkbench 窗口 / headless 双模式冒烟绿；新增"无 sink CLI 进程"冒烟证明 kernel 无
  呈现依赖；closure 检查 GUI/App 不再直接 include RHI 类型（除 presenter 两个文件）。

## 5. Phase 3 — SDF 字体

- 单张 SDF 图集（ASCII 种子），ensureGlyphs 遇缺失字形动态追加入扩容图集；
  Font::measureText / drawText 接口不动，字号通过 scale 实现。
- 删除逐字号 atlas 加载与 fontSizes 配置；registerFont 合成字体注入路径保留
  （widget/closure 测试依赖它）。
- 小字号（≤13）清晰度：屏幕空间 AA 或 1 档位图兜底，按 golden diff 结果定。
- 验收：任意字号文本 golden 稳定；内存不再随字号线性增长；closure 测试不因字体注入
  方式改变而改脸。

## 6. Phase 4 — 布局 / 文本增强

- UIContainer 增加主轴拉伸：flexGrow（末子节点 / 权重填满剩余空间），替换页头+填充区
  靠 magic padding 的写法；ScrollSplit / Editor 页面改成结构性布局。
- UIText 增加换行 + 省略号（maxLines / ellipsis）。
- grid / docking / 多窗口继续明确不做。
- 验收：布局回归场景从"坐标断言"升级为"结构断言"（无 magic padding）；文本换行/省略
  场景入 harness。

## 7. 验证基线

每个 Phase 结束，以下必须全绿：

    xmake r ya-gui-closure-test              # 82
    xmake r ya-gui-widgets-test              # 65
    xmake r ya-gui-workbench-workspace-test  # 8
    xmake r ya-testing                       # 444
    xmake r GUIWorkbench --smoke-actions --exit-after-frame=30
    xmake b ya-editor

Phase 1 起新增 ya-gui-scenario 目标，headless 跑 4 个场景并输出 tree dump + 差异图。

## 8. 风险与边界

- 单循环收敛只动 GUI 线；引擎 App 的循环、ImGui 编辑器壳、GameUIHost 本次都不动，避免
  三条线同时重构。
- RHI 不做全面重构；offscreen presenter + PresentSurface 门面是唯一新增的 RHI 面。
- 中文字形不做预载，动态追加进 SDF 图集（图集需扩容机制）。
- 黄金基线小集合入库；阈值与差异图为默认输出。
- harness 分层：输入驱动/CLI 基座在 Foundation，树 dump/goldens/场景在 GUI 模块，引擎域
  命令继续留在 AppAutomationControlService。

## 9. 推进方案（每步可验证）

1. P1.1 输入驱动 + 树 dump + 离屏 presenter（同批，共同构成 headless 闭环）→
   验收：ya-gui-scenario 能 headless 跑 4 个场景并 dump 树。
2. P1.2 黄金 diff + CLI 通用命令 → 验收：capture/diff 命令输出差异图。
3. P1.3 收口 smoke：删 example 的 --smoke-actions switch-case，改为 scenario 脚本。
4. P2.1 AppKernel + PresentSurface 门面 → 验收：无 sink CLI 冒烟。
5. P2.2 GUIAppHost 迁移到 AppKernel → 验收：窗口/headless 双模式 + closure 边界检查。
6. P3 SDF 字体 → 验收：内存平坦 + 文本 golden 稳定。
7. P4 布局/文本 → 验收：结构断言替代 magic padding，换行/省略场景入 harness。

每一步都是"改完即可跑验证"的最小垂直切片，不在中途引入第三条宿主语义。
