# GUI App 自举：设计与实施计划

> 建立日期：2026-08-11  
> 状态：方向收敛完成（2026-08-11）；当前主线是先完成独立生命周期的
> standalone GUI app 最小闭环，`.yaui` / document authoring 暂不作为前置条件
> 承接：`../ui-widget-tree-refactor/plan.md` 已完成 Game UI WidgetTree 闭环；
> `ya-gui-minimal-host` 已验证 SDL 输入、snapshot、直出 swapchain 合成与 resize
> 重建。  
> 本文件：定义 GUI framework 走向工具型 GUI / editor 自举的设计方法与实施切片；
> 不回写历史迁移记录。

## 1. 决策

下一阶段的产品形态拆成三个明确角色：

```text
Shared app foundation
  automation service   startup/exit、frame stepping、smoke hooks、scripted driving
  reflection           type metadata、property access、tool inspection/adaptation

Engine / GUI libraries
  ya-gui-framework   GUI framework：WidgetTree、控件、snapshot、compose
  ya-gui-app-host    standalone native app host：窗口、SDL、RHI、present

Example / executable consumers
  GUIFrameworkSmoke（target 可继续命名 ya-gui-minimal-host）
    最小端到端冒烟与 host contract 样例，不承载真实工具功能
  GUIWorkbench（暂定）
    实际的 retain-mode 工具型 GUI app，直接消费两个 Engine library
```

也就是说：

- `Engine/Source/Framework/GUI/` **不产生 exe**。它只声明 GUI libraries：
  默认 shared linkage 下产出 dylib/DLL，monolith 配置下按项目规则退化为 static；
- 现有 `ya-gui-minimal-host` **保留为 exe，但迁入 `Example/`**。它的价值是最小闭包、
  自动化冒烟和 GPU 生命周期回归，不是未来 editor app 的开发场所；
- 可复用的是 Engine 内的 GUI libraries：`ya-gui-framework` 保持纯 GUI，
  `ya-gui-app-host` 提供 standalone app 的窗口与呈现宿主；
- `automation service` 与 `reflection` 视为 **game / gui 共用的 app foundation**，
  不归入 GUI 专属层；GUIWorkbench 需要时直接复用现有 Runtime/Application 与 Core
  侧能力，而不是在 GUI 子树再造一套 automation / property system；
- 真正的 `树 / 选择 / 属性 / 命令` GUI app 放在 `Example/GUIWorkbench`（目录名暂定），
  作为 framework 的第一个真实消费者；
- editor 后续只复用 framework、tool workspace 和 presentation contract，不依赖
  standalone app-host。
- 当前不要求 GUIWorkbench 读取、保存或生成 `.yaui`；先用代码构建 retain-mode
  `WidgetTree` 跑通完整生命周期与交互闭环，再决定 document 如何接入。

`GUIWorkbench` 不是“再做一个 demo”。它要成为 GUI framework 的产品级探针，用真实的
`树 / 选择 / 属性 / 命令` 工作流回答以下问题：

1. WidgetTree 是否足以承载工具 GUI，而不只是 game HUD；
2. GUI 的宿主、输入、资源、渲染边界能否脱离 Scene / GameUIHost；
3. editor 是否能复用 GUI 的 tree、snapshot、工具状态模型，而不复制交互逻辑。
4. 不依赖 document loader 时，retain-mode widget composition 是否已经足够表达工具 UI。

本阶段的结果不以“替换多少 ImGui 代码”衡量，而以能否建立一条可重复的迁移路径衡量。

## 2. 已有能力与缺口

### 2.1 已有、必须直接复用的能力

| 能力 | 当前锚点 | 这一阶段的使用方式 |
| --- | --- | --- |
| automation service / 自动化生命周期 | `Runtime/Application/` | smoke、帧步进、条件退出、未来 scripted driving 都继续走共享自动化入口；GUI host 默认长期运行，不在 host 内复制一套测试开关。 |
| 反射 / property metadata | `Core` 反射系统 | GUIWorkbench 的 inspector / command binding 后续优先复用现有反射读写能力，而不是在 widget 层新造字段描述系统。 |
| 单一 live visual tree、系统 layer、hit test、focus、pointer capture | `Framework/GUI/Runtime/Widgets/WidgetTree.*` | 每个 presentation context 一棵 `WidgetTree`；业务不直接维护第二棵 live tree。 |
| 布局后生成 immutable draw packet | `UIFrameSnapshot.h`、`WidgetTree::buildSnapshot` | 一帧内所有 widget mutation 在 snapshot 前完成；录制只读取 snapshot。 |
| 共享 2D 合成 | `Runtime/Compose/Render2DComposePass.*` | standalone 传 `PresentSrcKHR`；editor preview 保持现有离屏 target 的 layout 约定。 |
| 直出 swapchain、resize 重新导入 image | 当前 `MinimalHost/main.cpp` | 提取到 library 后由 Example smoke / workbench 共同验证。 |
| GUI closure 单元测试 | `Test/Source/WidgetTreeTest.cpp`、`UIFrameSnapshotTest.cpp` | 新增 framework 能力必须先在这个闭包内测试。 |

### 2.2 当前缺口

- 当前 `ya-gui-minimal-host` 的窗口、Vulkan 初始化、swapchain 导入、帧循环都集中在
  Engine 内的 `main.cpp`；稳定生命周期尚未成为可复用 library，且该 exe 的归属也应移到
  Example。
- `WidgetTree` 有 focused widget，却没有统一的 focusable contract 与 Tab traversal；
  `UIButton` 也尚未把 press/release 与 tree capture/focus 串成完整语义。
- 现有控件主要是绝对布局基础件；工具 GUI 所需的 stack、split、scroll、selection
  仍没有经过通用性检验。
- 当前计划不再以 `UIDesignerPanel` / `.yaui` 为第一落点；document loader、
  serializer、palette 等能力都暂时不进入 Phase 1-3 的实施闭包。

## 3. 总体设计

### 3.1 三层，而不是一套“可切换 backend”外观层

```text
Shared app foundation
  automation service / reflection / command-line automation hooks
  为 game app 与 GUI app 提供共用生命周期辅助与类型系统，不认识 WidgetTree。

Framework GUI
  UIElement / WidgetTree / layout / event / UIFrameSnapshot / compose
  只认识视觉树、输入语义与绘制资源；不认识 Scene、Editor、Document 业务。

Standalone GUI app host
  SDL window / Vulkan presentation / event pump / frame boundary / resize rebuild
  只认识 native window 与 GUI presentation；不认识具体工具业务。

Tool workspace
  app state / selection / commands / view-model binding
  只保存工具业务状态；可按需借助共享 reflection 暴露属性访问；
  不拥有窗口、swapchain、command buffer。

Presentation adapter
  standalone: SDL window -> swapchain
  editor embed: ImGui canvas rect -> editor offscreen target
  只负责坐标映射、事件转发、snapshot 的 target 合成。
```

这里不增加 “ImGui backend / 自研 backend 通用 facade”：

- ImGui editor shell 与 WidgetTree 不是同一 widget API；
- 两边共享的应是工具状态与呈现契约，不是套一层名称统一但行为不同的控件接口；
- 第一轮 editor 接入通过已有 `UIFrameBuildContext`、`UIFrameSnapshot` 和 compose
  pass 对接，而不是要求 ImGui 成为自研 GUI 的 backend。

同时也不把 `automation service`、`reflection` 吞进 `ya-gui-app-host`：

- automation 是 app 级共享服务，不是 GUI 宿主私有功能；
- reflection 是类型系统与工具适配底座，不是 widget toolkit 的一部分；
- GUI app 只是这些共享能力的一个消费者，和 game app 处于并列关系。

### 3.2 事实源与单向数据流

工具 GUI 必须遵守如下数据流：

```text
OS / editor event
  -> presentation adapter: 坐标转换、输入归属判断
  -> WidgetTree::dispatchEvent
  -> widget action callback
  -> ToolWorkspace mutation
  -> 根据 mutation 更新 widget presentation state / invalidateLayout
  -> WidgetTree::buildSnapshot
  -> recordRender2DComposePass(snapshot)
```

约束：

- app state、selection、命令结果是 `ToolWorkspace` 的事实源；
- widget 只保存呈现状态和临时输入状态，不成为 document 的第二份真相；
- snapshot 是帧边界不可变包；command recording 不读取 WidgetTree、ToolWorkspace、
  Scene 或 EditorLayer；
- snapshot 中纹理/字体的强引用规则保持现状，不能为了工具 UI 降级资源存活期。

### 3.3 两种 presentation adapter 必须分开实现

**Standalone app**

- `WidgetTree` 占完整窗口 logical extent；
- `UIFrameBuildContext` 为 `uiScale={1,1}`、`offset={0,0}`；
- 目标是 imported swapchain image，compose pass 的 `finalLayout` 必须是
  `PresentSrcKHR`；
- resize 由 host 在帧边界处理：等待当前提交完成后，重建 command buffers、
  imported presentation images，更新 tree logical extent，下一帧再录制。

**Editor embed**

- `WidgetTree` 占 editor canvas 的 tree-local logical extent；
- `UIFrameBuildContext` 使用已有 canvas 的 framebuffer scale、zoom、pan 与 offset；
- 目标是 editor 的离屏 preview target，最终 layout 服从后续采样链；
- EditorLayer 仅在指针落入 canvas、且该 slice 拿到输入归属时转发事件；
- 不创建第二个 SDL window，不把 standalone host 嵌进 editor。

二者共用 WidgetTree、tool workspace、snapshot 和 compose contract；不共用窗口 /
swapchain owner。

### 3.4 抽象引入原则

先抽取已经在两个调用点出现的稳定责任，不能为了“未来 editor”先建立泛化层。

| 责任 | 第一处实现 | 何时提升为 framework/app API |
| --- | --- | --- |
| Vulkan + SDL + swapchain 生命周期 | `ya-gui-minimal-host` 与 `GUIWorkbench` | 两个 exe 都需要，因此先抽为 Engine 的 `ya-gui-app-host`；editor 不依赖它。 |
| tool state / selection / command | `GUIWorkbench` 内部 `ToolWorkspace` | 独立 app 与 editor 试点确实共享后，再提取到可被 editor 引用的轻量模块。 |
| stack / split / scroll / focus traversal | `ya-gui-widgets` | 每个能力先有至少一个纯 WidgetTree 回归测试，再进入 example。 |
| 多窗口、dock、窗口管理 | 不在本计划 | 只有单窗口 app 与 editor embed 都暴露同一缺口时，另立计划。 |

## 4. 实施方法

每项能力都按同一循环推进，禁止跳过中间 gate：

1. **写契约**：先在头文件注释 / test 名里写清 owner、坐标系、状态和生命周期。
2. **纯树测试**：不启动 Vulkan，不依赖 Scene / Host / Editor，覆盖结构、输入、
   focus、layout、snapshot。
3. **example 垂直切片**：在 standalone app 里使用该能力，确保不是测试专用 API。
4. **GPU 冒烟**：验证 snapshot → compose → present，并检查 validation。
5. **再抽取**：只有第二个使用者实际需要时才移出 example。

一个提交只穿透一个垂直切片。例如“增加 split pane”应包括：

- 控件及其布局/拖拽状态；
- WidgetTree unit test；
- example 中真实左右 pane 使用；
- 30 帧自动冒烟；

而不顺带改 editor shell、文档格式或多窗口。

### 4.1 retain-mode 构树实践规则

GUIWorkbench 先明确采用“代码构树 + app state 驱动”的 retain-mode 做法。

规则如下：

- `buildUI()` 只负责创建稳定 widget 骨架：shell、pane、row、button、inspector field；
- `updateUI()` 只负责把 `ToolWorkspace` 当前状态映射到已有 widget 的显示状态；
- 业务状态变化先写 `ToolWorkspace`，再由 presenter 同步到 widget，不允许 widget 自己持有
  第二份业务对象；
- 优先复用既有 widget 实例并更新文本、颜色、选中态；只有结构真的变化时才重建局部 subtree；
- 局部 subtree 重建必须发生在帧边界，且旧 subtree 在本帧 snapshot 资源生命周期之外 detach；
- 不引入“声明式 diff engine”或虚拟树；当前阶段直接使用 `WidgetTree` 的 retain owner
  模型验证框架能力；
- 不为了减少样板代码而提前抽 DSL。先接受 `main.cpp` / builder 中有一定显式组装代码，
  等出现第二个真实 app 或 editor slice 后再判断是否需要提炼 builder helper。
- 若 inspector 需要字段编辑，优先通过共享 reflection 将 `ToolWorkspace` 暴露为
  property adapter；不要先定义一套 GUI 私有字段 schema。

### 4.2 Presenter 分层

为避免 app state 与 widget 相互污染，GUIWorkbench 内部分三层：

```text
ToolWorkspace
  保存 stable item id、selection、字段值、命令结果

WorkbenchPresenter
  读取 ToolWorkspace，写入 widget state；响应 widget action 后回写 ToolWorkspace

Widget shell
  只负责布局、绘制、临时 hover/focus/pressed 状态
```

约束：

- `ToolWorkspace` 不 include GUI 头；
- `Widget shell` 不直接读写 mock object graph；
- `WorkbenchPresenter` 是唯一允许同时认识 workspace 与 widget id/handle 的层。

### 4.3 第一刀选择原则

GUIWorkbench 不从“完整编辑器壳”开始，而从最薄的真实交互闭环开始。默认顺序：

1. 工具条 + 左侧 selectable list
2. list selection -> 中央 preview 高亮
3. inspector 字段编辑 -> preview / list 文本回流
4. split drag + scroll
5. keyboard traversal 与 command shortcut

只有前一刀已经稳定并有测试后，才进入下一刀；不要同时铺 tree、inspector、命令系统、
样式系统。

## 5. Phase 1：提取 GUI app-host library，并将 smoke exe 迁到 Example

### 5.1 目标

保留现有行为不变地拆开 `MinimalHost/main.cpp`：

- Engine GUI 子树只保留 library target，不再 include 或定义 binary target；
- `ya-gui-minimal-host` 仍是 exe，保留 click-counter 与自动退出参数，但 source /
  xmake target 迁入 `Example/GUIFrameworkSmoke`；
- 将窗口、RHI、swapchain、snapshot、compose、present 的稳定生命周期提取为
  `ya-gui-app-host` library；
- 让随后创建的 `Example/GUIWorkbench` 能调用同一个 host library，而不是复制
  `main.cpp`。

这次提取满足独立 target 的条件：有两个真实消费者（minimal smoke 与 GUIWorkbench）、
独立初始化/关闭和资源生命周期、可独立裁剪，且能明显减少重复的 SDL/Vulkan 宿主代码。

### 5.2 设计与落点

建议在 `Engine/Source/Framework/GUI/` 下增加稳定职责目录与 target：

```text
Framework/GUI/App/
  GUIAppHost.h/.cpp        // standalone：window、IRender、frame lifecycle
  GUIPresentationTarget.*  // imported swapchain image/view、command buffers

Framework/GUI/App/xmake.lua
  ya-gui-app-host            // set_kind(ya_target_kind())

Example/GUIFrameworkSmoke/
  xmake.lua
  Source/main.cpp            // smoke app entry，只创建最小内容并调用 GUIAppHost

Example/GUIWorkbench/
  xmake.lua
  Source/main.cpp            // 真实工具 app entry
```

`ya-gui-app-host` 是 Engine 的动态库（或 monolith 下的 static library），只提供运行 GUI
app 所需的最小协议，不创建 editor/application framework：

```cpp
struct IGUIAppDelegate {
    virtual void buildUI(WidgetTree&) = 0;         // 初始化时挂载 widgets
    virtual void updateUI() {}                     // snapshot 前同步呈现状态
    virtual void onRoutedEvent(...) {}             // 可选：观察已路由事件
};
```

`GUIAppHost` 可以接受 `IGUIAppDelegate`，也可以先暴露 `getTree()` + `runFrame()`；
以 minimal host 和 GUIWorkbench 同时使用后最小的公开面为准。接口不应暴露 command
buffer、swapchain image 或 Vulkan 类型给 app delegate。

`GUIAppHost` 的职责：

- 初始化 / 销毁 VFS、SDL window、shader storage、IRender、builtin textures、fonts、
  Render2D；
- 将 SDL event 转成已有 Core `Event`，以 tree-local logical point 调用
  `WidgetTree::dispatchEvent`；
- 在 `begin()` 成功且 swapchain 稳定后，依次执行：
  `prepare pipeline -> beforeSnapshot -> buildSnapshot -> record compose -> end/present`；
- 管理 imported presentation images 和 command buffer 的资源保留；
- 只在帧边界处理 resize/recreate，绝不在事件分发或 command recording 中重建 GPU 资源。
- 接入共享 automation service，使 smoke、frame stepping、条件退出、未来 scripted input
  驱动都通过统一 app automation contract 生效；host 默认无限主循环，只有 automation 或
  app 自己请求关闭时才退出。

`GUIAppHost` 不做：

- 不认识 ToolWorkspace、UIDocument、Scene、EditorLayer；
- 不重新实现 automation runner、反射注册、property database；
- 不承担 widget 注册和业务控件创建；
- 不尝试为 OpenGL 补一套假实现。当前 direct presentation 需要 Vulkan swapchain
  image import；若 RHI 将来提供跨后端同等能力，再提升该边界。

### 5.2.1 文件级落点

Phase 1 优先按稳定职责切，不按“代码量平均分”拆：

```text
Engine/Source/Framework/GUI/App/
  GUIAppHost.h/.cpp
    对外 owner；持有 window、render services、WidgetTree、主循环

  GUIPresentationResources.h/.cpp
    imported swapchain image/view、framebuffer/command buffer retention

  GUIAppEventPump.h/.cpp
    SDL event -> Core Event -> tree-local logical point 路由

  App/xmake.lua
    ya-gui-app-host
```

允许第一提交先只落 `GUIAppHost.*` + `GUIPresentationResources.*`，如果 event pump
独立后反而打碎主链路，就先内聚在 `GUIAppHost.cpp`，等第二个消费者出现再拆。

### 5.2.2 host 公开面最小化

第一版 `GUIAppHost` 公开面只保留：

- 构造配置：window title、initial size、clear color、可选 automation session / run policy；
- `run(delegate)`；
- 可选的 `requestClose()` / `requestRebuildLayout()`；
- 只读查询：当前 logical extent、frame index。

第一版不要公开：

- Vulkan image / image view / command buffer；
- SDL window handle；
- “任意帧回调”式低层 hook；
- 给 app 直接插入 render pass 的扩展点。

这样可以避免 GUIWorkbench 反过来绑死宿主实现。

### 5.3 切分步骤

1. 新建 `ya-gui-app-host` target 与模块 charter：它对外提供 standalone native GUI
   host，禁止依赖 Scene/ECS/Render3D/Product Host/Editor。
2. 先将当前 `buildPresentationTargets`、frame loop 和 shutdown 迁到 host，保持
   `ya-gui-minimal-host` 的 Panel + click counter demo 与日志完全可运行。
3. 从 `Framework/GUI/xmake.lua` 删除 `MinimalHost` include；将 smoke 的 xmake /
   source 移到 `Example/GUIFrameworkSmoke`，由 `Example/xmake.lua` include。
4. 将 swapchain recreate 收敛成显式 `rebuildPresentationResources()`：
   先 `waitIdle`，再释放 command-buffer retention / image view，重建 buffers /
   imported images，调用 `tree.setLogicalExtent()`，跳过当前帧录制。
5. 将 `buildDemoContent` 留在 `Example/GUIFrameworkSmoke/Source/main.cpp`；此时
   `main.cpp` 应只包含 host
   配置、demo build、run。
6. 用同一 host library 新建 `Example/GUIWorkbench` 空壳 binary，先只显示 title /
   panel，以证明真实第二消费者不复制 host。
7. 将现有 GUI smoke 的自动退出逻辑改为接共享 automation service：
   - 默认长期运行；
   - automation 配置存在时，才允许 `exit-after-frame` / frame budget / scripted stop；
   - 保留人工 drag-resize 验收。

### 5.3.1 具体操作顺序

建议按下面的物理操作顺序推进，减少中途 build 断面：

1. 新建 `Framework/GUI/App/xmake.lua`，先让 `ya-gui-app-host` 空 target 能被工程识别；
2. 将 `MinimalHost/main.cpp` 中“presentation resources + frame loop + shutdown”搬入
   `GUIAppHost.cpp`；
3. 保留 `MinimalHost/main.cpp`，先改成只负责 `buildDemoContent + host.run(...)`；
4. 在这一状态下先构建并跑通 `ya-gui-minimal-host`；
5. 再删除 `Framework/GUI/xmake.lua` 对 `MinimalHost` 的 include；
6. 新建 `Example/GUIFrameworkSmoke/xmake.lua` 和 `Source/main.cpp`，把上一步精简后的
   smoke entry 平移过去；
7. 更新 `Example/xmake.lua` 将 `GUIFrameworkSmoke` 纳入工程；
8. 确认 target 名仍可保持 `ya-gui-minimal-host`，避免测试命令与自动化脚本大面积改名；
9. 最后新增 `Example/GUIWorkbench` 空壳 target，验证第二消费者链接同一个 host。

### 5.3.2 Phase 1 风险点

Phase 1 最容易出问题的不是功能，而是生命周期边界：

- resize 时 rebuild 发生在错误时机，导致录制中途动 GPU 资源；
- imported image/view 或 command-buffer retention 提前释放；
- host 为了“通用”提前暴露过多低层句柄，导致 GUIWorkbench 反向依赖 Vulkan 细节；
- 仍把 `exit-after-frame` 写死在 GUI app 内，导致 host 不能默认长期运行，也无法与 game
  automation contract 对齐；
- 把 event pump 单独拆文件后，主时序变得难读。

出现这些问题时，优先保住 owner 主链路，而不是继续追求目录整齐。

### 5.4 验收

- Engine GUI 不再声明 executable target；
- `Example/GUIFrameworkSmoke` 的依赖闭包仍不含 Scene/ECS/Render3D/Product Host/Editor；
- 默认无 automation 参数时可长期运行；
- 接共享 automation 后，30 与 120 帧自动退出均无 validation error；
- window resize 后首个有效 frame 的 snapshot logical extent 与 swapchain extent 一致；
- command recording 只消费 `UIFrameSnapshot` 和 presentation target。
- `ya-gui-minimal-host` 与 `GUIWorkbench` 没有各自复制 SDL/Vulkan frame loop。

## 6. Phase 2：建立工具 GUI 的最小控件与输入规则

### 6.1 控件建设顺序

控件按“先解决布局与导航，再做复杂业务控件”的顺序进入 `ya-gui-widgets`：

1. **Focusable contract 与 Button 完整语义**
2. **Stack**
3. **Split pane**
4. **Scroll viewport**
5. **Selectable list/tree row**

不要一开始做通用 tree view、virtual list、rich text、theme 或 dock。

推荐提交粒度再细化为：

1. focus policy + traversal
2. button press / capture / keyboard activate
3. stack
4. split pane
5. scroll viewport
6. selectable row

### 6.2 Focus 与 keyboard 的明确规则

`WidgetTree` 已有 `_focused`，缺的是焦点资格与 traversal。建议：

- 在 `UIElement` 上增加明确的 focus policy（默认不可聚焦）；
- `WidgetTree` 以稳定 paint/tree 顺序收集 attached、visible、focusable widget；
- Tab / Shift+Tab 由 tree 在普通 key routing 之前处理，移动到下/上一个 focusable；
- Enter / Space 由当前 focused widget 解释；Button 的键盘激活与鼠标 click 走同一个
  action callback；
- pointer press 的控件自行请求 focus；press 时设置 pointer capture，release / detach
  时释放 capture；
- 禁止各控件自己遍历整个 tree 寻找下一个焦点。

这一步应先修齐 `UIButton` 的完整 press/capture/focus 行为，再推广到 selection row。

需要新增的纯树测试：

- Tab 与 Shift+Tab 的顺序、首尾回绕规则；
- detach focused / captured widget 后 transient state 被清理；
- press → 移出控件 → release 的 capture 行为；
- focused button 对 Enter / Space 的激活与鼠标 action 一致。

### 6.3 Layout primitive 规则

**Stack**

- 只负责顺序布局、gap、padding、主轴对齐；
- child 的 visual ownership 仍属于 WidgetTree，Stack 不引入第二种 child 容器；
- `Collapsed` child 不占空间，`Hidden` 保持布局占位，保持现有 visibility 语义；
- layout 只写 `_layoutRect`，不在 paint 阶段修改 child geometry。

**Split pane**

- 持有两块内容与一个可拖拽 divider；
- divider pointer capture 在自身 drag session 内完成；
- min/max size、初始 ratio、拖拽后 ratio 是控件状态；
- resize 只使 layout dirty，下一次 snapshot 前重新 layout；
- 不实现 dock、tab stack、浮动窗。

**Scroll viewport**

- 先支持一个 content child 和一个轴向；
- scroll offset 是 tree-local logical pixel，clamp 依赖 content desired size；
- paint 使用现有 snapshot clip stack；hit test 与事件点需要一致地转换到 content-local
  坐标，不能只裁绘制不裁交互；
- wheel 先由最内层可滚动 viewport 消费，未消费再向外冒泡。

**Selectable list/tree row**

- 控件只报告 stable item ID / activation / expansion action；
- 选择集合、当前 selection、业务对象查找都属于 ToolWorkspace；
- 第一版不做 virtualization；只有 example item 数量和 profiler 已证实需要时才单列优化。

### 6.4 样式与资源

Phase 2 只建立 example 的 `ToolStyle` 值对象：

- 默认字体 family/size；
- background、panel、selection、focus、disabled 等颜色 token；
- spacing、row height、splitter width。

它是 example/app 的显式构建参数，不创建 framework singleton theme。字体继续由 host
初始化，snapshot 继续强持有 font；图片资源继续通过 `UIFrameBuildContext::textureResolver`
在 snapshot build 时解析。

### 6.5 Phase 2 验收

- 新控件测试只链接 GUI closure；
- 每个控件都在 example 的真实 shell 中使用；
- keyboard、pointer capture、scroll clipping 的语义有回归测试；
- 没有将 ToolWorkspace 或 editor 类型 include 进 `ya-gui-widgets`。

### 6.6 Phase 2 实际开发顺序

先不要一口气把所有控件做完，建议按 GUIWorkbench 的使用压力来倒逼：

1. 先补 focus + button，使工具条和 list row 能成立；
2. 再做 stack，把顶部 / 左中右 shell 搭出来；
3. 再做 split pane，让工作区真正可用；
4. 然后做 selectable row，把 selection 模型落地；
5. 最后补 scroll viewport，让 item 数量增长后仍可操作。

也就是说，`scroll` 虽然抽象上比 `selectable row` 更基础，但如果当前 shell 还没有真实
长列表压力，就不应该提前花时间做复杂滚动语义。

## 7. Phase 3：在 Example 实现真实 retain-mode Tool GUI App

### 7.1 目标体验

新建 `Example/GUIWorkbench/`。它是 standalone binary，不沿用
`HelloMaterial` / `GreedySnake` 的 shared-project 形式：后两者由 `YARuntime` 承载，
而 GUIWorkbench 自己拥有窗口与 presentation。

建议构建形态：

```text
Example/GUIWorkbench/
  xmake.lua                 // target("GUIWorkbench"), kind("binary")
  Source/main.cpp           // 创建 GUIAppHost 与 GUIWorkbench delegate
  Source/GUIWorkbench.*     // ToolWorkspace、shell builder、actions
```

target 直接依赖 `ya-gui-app-host`（后者公开携带 GUI framework 所需闭包）；SDL 由
host library 的 public build dependency 统一提供。它不依赖 `ya-engine`、`YARuntime`、
Scene、Editor。
它可以按需依赖共享的 automation / reflection 所在 target，但这些依赖必须保持“app
foundation → GUI app consumer”的方向，不能反向把 GUI framework 注入 Runtime/Core。

GUIWorkbench 采用一个纯 app-state workspace，而不是接 Scene 或 `.yaui`：

```text
+-------------------------------------------------------------+
| Add | Remove | Rename | Reset Layout                        |
+--------------+------------------------------+---------------+
| item tree    | preview / canvas             | inspector     |
| tree/list    |                              | name          |
|              |                              | visible       |
|              |                              | color/size    |
+--------------+------------------------------+---------------+
```

最小操作闭环：

1. 点击或方向键改变左侧 selected item；
2. 中央 preview 高亮该 item；
3. 右侧 inspector 修改 mock object 的至少两个字段；
4. 修改立即更新 preview 和 tree label；
5. 顶部命令按钮执行 add / remove / rename / reset layout 之类 app 内命令，并有可见反馈。

### 7.1.1 首个最小 slice

GUIWorkbench 不要从完整三栏同时起步，首个可运行 slice 只要求：

- 顶部一排按钮；
- 左侧一个 selectable list；
- 中央一个根据 selection 改变颜色或边框的 preview panel。

这时 inspector 甚至可以先没有。只要以下链路成立就算 Phase 3 真正启动：

`button/list event -> ToolWorkspace mutation -> presenter 同步 widget state -> snapshot -> preview 更新`

在这个 slice 稳定前，不要急着引入右侧 inspector。

### 7.2 ToolWorkspace 的边界

example 内先定义轻量 `ToolWorkspace`，持有：

- mock object graph / stable object ID；
- `selectedId`；
- dirty 状态与 command result 文本；
- 不依赖 UIElement 的 command methods。
- 如需 inspector 字段枚举，优先从共享 reflection adapter 读 metadata；mock object
  graph 本身不直接 include widget 类型。

widget builder 或 presenter 负责将 workspace 映射为 widget 状态：

- `selectedId` 改变 → 更新 row selected state、inspector 字段、canvas highlight；
- command / inspector mutation → 更新 workspace，再仅对受影响控件更新显示；
- 改变 geometry 时调用 `tree.invalidateLayout()`；只改变颜色/文字但不影响 geometry 时
  可直接在下一帧 build snapshot。

第一版允许直接重建一个局部 content subtree，只要在帧边界进行，且旧 subtree 在 snapshot
资源生命周期之外被安全 detach；不要在 command recording 中重建。

### 7.2.1 mock object graph 最小模型

第一版 mock data 不要模拟完整文档系统，只保留最小必要字段：

```cpp
struct WorkbenchItem {
    ItemId id;
    std::string name;
    bool visible;
    UIColor color;
    float size;
    std::vector<ItemId> children;
};
```

外加：

- root item id；
- `selectedId`；
- 一段 command feedback 文本。

这样足以验证 list / preview / inspector 三者联动，不会把时间提前花在 document schema 上。

### 7.2.2 presenter 更新策略

Presenter 默认采用“显式同步”而不是“全量重建”：

- 选中项改变：只更新旧 row、新 row、preview highlight、inspector 当前值；
- 名称改变：只更新对应 row label 与 preview label；
- 可见性或颜色改变：只更新 preview / inspector，不触发整树 layout；
- item 增删：允许局部重建列表 subtree，并在必要时 `invalidateLayout()`。

只有当控件能力还不足以做细粒度更新时，才退回局部 subtree 重建；但这应被视为阶段性措施，
不是长期常态。

### 7.3 归属规则

- `GUIAppHost`、presentation target、Vulkan/SDL 生命周期属于 Engine GUI app-host library；
- `ToolWorkspace`、mock object graph、工具 shell、命令 stub 属于 `Example/GUIWorkbench`；
- `MinimalHost` 只保留最小内容，不吸收 ToolWorkspace；
- 不要在此时将 GUIWorkbench 搬到 Product/Editor，也不要把 mock state 伪装成 `.yaui`。
`.yaui` 的 authoring / loader / serializer 留给后续单独计划；这个 example 首先验证
工具 UI 通用语义与 retain-mode 组装方式。

### 7.4 Phase 3 验收

- standalone 工具 GUI 能全程只依赖 GUI closure；
- 鼠标与 keyboard 都能完成 selection / command；
- 单元测试覆盖 workspace 的选择和命令状态转换，WidgetTree 测试覆盖通用输入语义；
- 交互演示不需要任何 ImGui fallback。

### 7.5 Phase 3 建议里程碑

建议把 GUIWorkbench 拆成 4 个可提交的小里程碑：

1. `shell skeleton`
   - 空 host + 顶部栏 + 左中布局
   - 只有静态文本和按钮

2. `selection loop`
   - selectable list
   - preview 随 selection 改变

3. `inspector loop`
   - 右侧 inspector
   - 修改字段回流到 preview 与 list

4. `interaction hardening`
   - keyboard traversal
   - scroll / split drag
   - 命令反馈与 resize 冒烟

## 8. Phase 4：沉淀“第二个使用者证明过”的地基

这不是预先的大重构，而是在 standalone example 已稳定后，针对第二个实际调用点做提取。

优先复用候选：

- game / gui 共用的 automation smoke hooks、frame stepping contract；
- inspector / command binding 所需的 reflection adapter；
- `ToolWorkspace` 的 selection / command contracts；
- 工具 shell builder 使用的 stack/split/list primitives；
- standalone 与 editor embed 都要做的 event-to-logical-point 映射 helper；
- 默认 ToolStyle 之外确有第二个 consumer 的 style token。

提取门槛：

1. 两个调用点的 owner、生命周期、坐标系相同；
2. 抽取后不要求 framework 依赖 Product/Editor；
3. 可以写纯单元测试或 closure lint 证明边界；
4. 抽取能删掉实际重复代码，而不只是“未来也许会用”。

任何不满足上述门槛的逻辑留在 example 或 editor adapter。

## 9. 当前明确不做的事

为保证主链收敛，以下内容不进入本计划当前实施闭包：

- `.yaui` loader / serializer / palette / authoring workflow；
- 以 `UIDesignerPanel` 为核心的 editor 渐进迁移；
- “ImGui backend / 自研 GUI backend 可切换”外观层；
- 多窗口、dock、tabbed workspace、浮动窗；
- 为 OpenGL 补 direct presentation 宿主。
- 重新发明一套 GUI 私有 automation service 或 GUI 私有 property/reflection 系统。

这些能力都可能在后续计划中出现，但前提是：

1. standalone GUIWorkbench 已证明 retain-mode WidgetTree 足够承载真实工具 GUI；
2. app-host、snapshot、输入、布局 primitive 的生命周期边界已经稳定；
3. 需要复用的能力已经由第二个调用点证明，而不是提前抽象。

## 10. 验证与提交策略

### 10.1 每阶段验证

```bash
python3 Script/ya.py cfg
xmake b ya-gui-minimal-host
xmake run ya-gui-minimal-host --exit-after-frame=30
xmake run ya-gui-minimal-host --exit-after-frame=120
python3 Script/ya.py test --target ya
```

按改动补充：

- 修改 shader / 2D pipeline 时先运行 `xmake ya-shader`；
- 修改 GUI closure 依赖时运行项目的 `ya_module_lint`；
- 接入 editor 试点后补跑 `xmake b ya-editor` 与现有 editor 自动化冒烟；
- interactive 验收必须手动检查点击、Tab/Shift+Tab、Enter/Space、scroll、split drag、
  window resize。

GUIWorkbench 建立后补充：

```bash
xmake b GUIWorkbench
xmake run GUIWorkbench --exit-after-frame=30
```

同时需要一条非自动退出验收：

```bash
xmake run ya-gui-minimal-host
```

确认默认情况下应用持续运行，直到收到窗口关闭或 automation stop。

若 target 最终使用 `ya-gui-workbench` 命名，则同步更新这里和自动化脚本；在决定前不要同时保留
两套叫法。

### 10.2 提交切片

推荐顺序：

1. `[gui/app] add standalone GUI app-host library`
2. `[gui/example] move GUI framework smoke executable out of Engine`
3. `[gui/widgets] add focus and button capture contract`
4. `[gui/widgets] add stack and split primitives`
5. `[gui/widgets] add scroll and selectable rows`
6. `[gui/example] add GUIWorkbench tool workspace shell`
7. `[gui/example] harden GUIWorkbench interaction shell`

每个提交都应包含对应测试；不要把 host 提取、控件设计、example 业务和 editor 迁移混在
同一个提交里。

## 11. 明确的下一刀

从 Phase 1 开始：

1. 以当前 `MinimalHost/main.cpp` 为基线，新增 `ya-gui-app-host` library；
2. 将 `ya-gui-minimal-host` 迁到 `Example/GUIFrameworkSmoke`，保持 click-counter demo
   和现有 GPU 路径不变；
3. 在 `Example/GUIWorkbench` 创建真正的 standalone GUI app，并让它消费 host library；
4. 再进入 focus/button contract，而不是直接开始堆 tree、inspector、dock。

这样 Engine GUI 保持纯 library 产品线；两个 Example exe 分别承担回归与真实产品验证，
共享 Engine library，而不会复制 host 或把工具业务污染进 framework。

## 12. 当前推荐的实际开工顺序

如果现在立刻开工，建议按这个顺序排任务：

1. `ya-gui-app-host` target 建立
2. `MinimalHost` 主循环提取
3. `GUIFrameworkSmoke` 迁入 `Example/`
4. `GUIWorkbench` 空壳 target
5. focus policy + button 完整语义
6. stack
7. selectable list 的第一版
8. GUIWorkbench 的 selection loop
9. split pane
10. inspector loop
11. scroll viewport
12. interaction hardening + resize / validation 回归

背后的方法论是：

- 先打通“宿主生命周期”
- 再补“最少控件能力”
- 再做“最小真实交互闭环”
- 最后才做“交互完善与可复用沉淀”

这样每一步都能被下一个真实消费者验证，而不是先造一批抽象再等待使用者出现。

## 13. 对共享 automation / reflection 的当前落地口径

这两项能力在本计划中的定位再明确一次：

- `automation service`：属于 app foundation。它服务于 game app、GUIWorkbench、未来
  editor smoke 与 scripted driving；GUI host 只接入，不拥有。
- `reflection`：属于 Core / 工具共享底座。它服务于 inspector、属性面板、命令绑定、
  类型浏览；GUI framework 不定义第二套字段描述协议。

对 automation 再补一条更具体的执行约束：

- `exit-after-frame` 不能继续作为 GUI app 私有 hardcode 行为存在于 `main.cpp` 或
  `GUIAppHost` 内部状态机里；
- 自动退出、帧预算、脚本驱动、稳定帧判定都应来自共享 automation 配置 / service；
- 没有 automation 配置时，GUI app 的默认语义就是长期运行。

因此后续实现遵守以下边界：

1. `ya-gui-framework`
   - 不依赖 automation / reflection
   - 只提供 widget、layout、snapshot、compose

2. `ya-gui-app-host`
   - 可桥接 automation 入口
   - 不持有业务反射 schema
   - 不把 automation / reflection 重新包装成 GUI 私有 API
   - 不内建 hardcoded frame budget / auto-exit policy

3. `GUIWorkbench`
   - 直接消费 host + framework
   - 需要自动化就接共享 automation service
   - 需要 inspector / property editing 就接共享 reflection adapter

4. 后续 game / gui 统一的方法论
   - 共享的是 app lifecycle tooling、automation、reflection、命令式状态模型
   - 不共享的是具体宿主：game 走 runtime/game host，GUI 走 gui app host

这样能保证未来收敛的是“共享 app 基础设施”，而不是把 game host 和 GUI host 硬糊成一层。
