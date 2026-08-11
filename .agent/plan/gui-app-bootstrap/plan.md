# GUI App 自举：设计与实施计划

> 建立日期：2026-08-11  
> 状态：Phase 1-3 + .yaui 文档闭环完成（2026-08-11）；方向调整：不做
> ImGui UIDesigner 渐进试点，先完成自研 GUI app 最小闭环，再整体替换  
> 承接：`../ui-widget-tree-refactor/plan.md` 已完成 Game UI WidgetTree 闭环；
> `ya-gui-minimal-host` 已验证 SDL 输入、snapshot、直出 swapchain 合成与 resize
> 重建。  
> 本文件：定义 GUI framework 走向工具型 GUI / editor 自举的设计方法与实施切片；
> 不回写历史迁移记录。

## 1. 决策

下一阶段的产品形态拆成三个明确角色：

```text
Engine / GUI libraries
  ya-gui-framework   GUI framework：WidgetTree、控件、snapshot、compose
  ya-gui-app-host    standalone native app host：窗口、SDL、RHI、present

Example / executable consumers
  GUIFrameworkSmoke（target 可继续命名 ya-gui-minimal-host）
    最小端到端冒烟与 host contract 样例，不承载真实工具功能
  GUIWorkbench（暂定）
    实际的工具型 GUI app，直接消费两个 Engine library
```

也就是说：

- `Engine/Source/Framework/GUI/` **不产生 exe**。它只声明 GUI libraries：
  默认 shared linkage 下产出 dylib/DLL，monolith 配置下按项目规则退化为 static；
- 现有 `ya-gui-minimal-host` **保留为 exe，但迁入 `Example/`**。它的价值是最小闭包、
  自动化冒烟和 GPU 生命周期回归，不是未来 editor app 的开发场所；
- 可复用的是 Engine 内的 GUI libraries：`ya-gui-framework` 保持纯 GUI，
  `ya-gui-app-host` 提供 standalone app 的窗口与呈现宿主；
- 真正的 `树 / 选择 / 属性 / 命令` GUI app 放在 `Example/GUIWorkbench`（目录名暂定），
  作为 framework 的第一个真实消费者；
- editor 后续只复用 framework、tool workspace 和 presentation contract，不依赖
  standalone app-host。

`GUIWorkbench` 不是“再做一个 demo”。它要成为 GUI framework 的产品级探针，用真实的
`树 / 选择 / 属性 / 命令` 工作流回答以下问题：

1. WidgetTree 是否足以承载工具 GUI，而不只是 game HUD；
2. GUI 的宿主、输入、资源、渲染边界能否脱离 Scene / GameUIHost；
3. editor 是否能复用 GUI 的 tree、snapshot、工具状态模型，而不复制交互逻辑。

本阶段的结果不以“替换多少 ImGui 代码”衡量，而以能否建立一条可重复的迁移路径衡量。

## 2. 已有能力与缺口

### 2.1 已有、必须直接复用的能力

| 能力 | 当前锚点 | 这一阶段的使用方式 |
| --- | --- | --- |
| 单一 live visual tree、系统 layer、hit test、focus、pointer capture | `Framework/GUI/Runtime/Widgets/WidgetTree.*` | 每个 presentation context 一棵 `WidgetTree`；业务不直接维护第二棵 live tree。 |
| 布局后生成 immutable draw packet | `UIFrameSnapshot.h`、`WidgetTree::buildSnapshot` | 一帧内所有 widget mutation 在 snapshot 前完成；录制只读取 snapshot。 |
| 共享 2D 合成 | `Runtime/Compose/Render2DComposePass.*` | standalone 传 `PresentSrcKHR`；editor preview 保持现有离屏 target 的 layout 约定。 |
| 直出 swapchain、resize 重新导入 image | 当前 `MinimalHost/main.cpp` | 提取到 library 后由 Example smoke / workbench 共同验证。 |
| `.yaui`、registry、preview tree | `UIDocument`、`UITypeRegistry`、`UIDesignerPanel` | editor 首刀复用其文档和 snapshot 路径，不将 Scene authoring 扩散进 framework。 |
| GUI closure 单元测试 | `Test/Source/WidgetTreeTest.cpp`、`UIFrameSnapshotTest.cpp` | 新增 framework 能力必须先在这个闭包内测试。 |

### 2.2 当前缺口

- 当前 `ya-gui-minimal-host` 的窗口、Vulkan 初始化、swapchain 导入、帧循环都集中在
  Engine 内的 `main.cpp`；稳定生命周期尚未成为可复用 library，且该 exe 的归属也应移到
  Example。
- `WidgetTree` 有 focused widget，却没有统一的 focusable contract 与 Tab traversal；
  `UIButton` 也尚未把 press/release 与 tree capture/focus 串成完整语义。
- 现有控件主要是绝对布局基础件；工具 GUI 所需的 stack、split、scroll、selection
  仍没有经过通用性检验。
- `UIDesignerPanel` 的文档、选择、树、Inspector 已经存在，但 UI 外壳和操作入口
  仍由 ImGui 承担；它不是可以直接搬进 standalone host 的工具模块。

## 3. 总体设计

### 3.1 三层，而不是一套“可切换 backend”外观层

```text
Framework GUI
  UIElement / WidgetTree / layout / event / UIFrameSnapshot / compose
  只认识视觉树、输入语义与绘制资源；不认识 Scene、Editor、Document 业务。

Standalone GUI app host
  SDL window / Vulkan presentation / event pump / frame boundary / resize rebuild
  只认识 native window 与 GUI presentation；不认识具体工具业务。

Tool workspace
  document model / selection / commands / view-model binding
  只保存工具业务状态；不拥有窗口、swapchain、command buffer。

Presentation adapter
  standalone: SDL window -> swapchain
  editor embed: ImGui canvas rect -> editor offscreen target
  只负责坐标映射、事件转发、snapshot 的 target 合成。
```

这里不增加 “ImGui backend / 自研 backend 通用 facade”：

- ImGui editor shell 与 WidgetTree 不是同一 widget API；
- 两边共享的应是工具状态、文档与呈现契约，不是套一层名称统一但行为不同的控件接口；
- 第一轮 editor 接入通过已有 `UIFrameBuildContext`、`UIFrameSnapshot` 和 compose
  pass 对接，而不是要求 ImGui 成为自研 GUI 的 backend。

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

- document、selection、命令结果是 `ToolWorkspace` 的事实源；
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
| tool document / selection / command | `GUIWorkbench` 内部 `ToolWorkspace` | UIDesigner 试点确实共享后，再提取到可被 editor 引用的轻量模块。 |
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

`GUIAppHost` 不做：

- 不认识 ToolWorkspace、UIDocument、Scene、EditorLayer；
- 不承担 widget 注册和业务控件创建；
- 不尝试为 OpenGL 补一套假实现。当前 direct presentation 需要 Vulkan swapchain
  image import；若 RHI 将来提供跨后端同等能力，再提升该边界。

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
7. 补一个最小 lifecycle smoke：`--exit-after-frame=30`，并保留人工 drag-resize
   验收。

### 5.4 验收

- Engine GUI 不再声明 executable target；
- `Example/GUIFrameworkSmoke` 的依赖闭包仍不含 Scene/ECS/Render3D/Product Host/Editor；
- 30 与 120 帧自动退出均无 validation error；
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

## 7. Phase 3：在 Example 实现真实 Tool GUI App

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

GUIWorkbench 采用一个 mock document workspace，而不是接 Scene：

```text
+-------------------------------------------------------------+
| New | Open | Save | Reload                                  |
+--------------+------------------------------+---------------+
| document     | preview / canvas             | inspector     |
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
5. New / Open / Save / Reload 通过 command dispatcher 走 stub 行为并有可见反馈。

### 7.2 ToolWorkspace 的边界

example 内先定义轻量 `ToolWorkspace`，持有：

- mock document / stable object ID；
- `selectedId`；
- dirty 状态与 command result 文本；
- 不依赖 UIElement 的 command methods。

widget builder 或 presenter 负责将 workspace 映射为 widget 状态：

- `selectedId` 改变 → 更新 row selected state、inspector 字段、canvas highlight；
- command / inspector mutation → 更新 workspace，再仅对受影响控件更新显示；
- 改变 geometry 时调用 `tree.invalidateLayout()`；只改变颜色/文字但不影响 geometry 时
  可直接在下一帧 build snapshot。

第一版允许直接重建一个局部 content subtree，只要在帧边界进行，且旧 subtree 在 snapshot
资源生命周期之外被安全 detach；不要在 command recording 中重建。

### 7.3 归属规则

- `GUIAppHost`、presentation target、Vulkan/SDL 生命周期属于 Engine GUI app-host library；
- `ToolWorkspace`、mock document、工具 shell、命令 stub 属于 `Example/GUIWorkbench`；
- `MinimalHost` 只保留最小内容，不吸收 ToolWorkspace；
- 不要在此时将 GUIWorkbench 搬到 Product/Editor，也不要把 mock document 伪装成 `.yaui`。
`.yaui` 的 authoring 复用留给 editor pilot；这个 example 首先验证工具 UI 通用语义。

### 7.4 Phase 3 验收

- standalone 工具 GUI 能全程只依赖 GUI closure；
- 鼠标与 keyboard 都能完成 selection / command；
- 单元测试覆盖 workspace 的选择和命令状态转换，WidgetTree 测试覆盖通用输入语义；
- 交互演示不需要任何 ImGui fallback。

## 8. Phase 4：沉淀“第二个使用者证明过”的地基

这不是预先的大重构，而是在 standalone example 已稳定后，针对第二个实际调用点做提取。

优先复用候选：

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

## 9. Phase 5：UIDesigner 自举试点

> 方向调整（2026-08-11）：原方案是"在 ImGui UIDesigner 上切一个垂直 slice
> 试点"。已回退该试点（UIDesignerPanel.Slice / EditorLayer 路由 / compose
> 多 snapshot 等），改为：先把自研 GUI app 的 `.yaui` 文档闭环做完整
> （GUIWorkbench：New/Open/Save/SaveAs、palette、文档树、inspector、实时
> 预览、自动化冒烟），然后整体替换 ImGui UIDesignerPanel。
>
> 回退理由：在 ImGui 外壳里嵌自研 slice 会产生双写事实源与双份保存路径，
> 与"selection/inspector/save 各只有一个事实源"的目标相悖；自研 GUI 先
> 独立站稳（可完整编辑 `.yaui`）后，editor 接入只是 presentation adapter
> 的事，不需要过渡性的双 UI。

### 9.1 切口

首刀默认选择 `UIDesigner`，但不是把整个 ImGui panel 一次替换掉。

选择它的原因是它已有：

- 独立 preview `WidgetTree`；
- `.yaui` resolve / instantiate / serialize；
- selection、canvas picking、snapshot 与 editor compose 路径；
- 与运行时分离的 preview lifecycle。

### 9.2 迁移顺序

1. **先对齐模型**  
   将 designer 的 document、selection、save/reload 组织成可被 ToolWorkspace 风格
   presenter 消费的状态接口；不移动 ImGui UI。

2. **在既有 editor canvas 内渲染自研工具 slice**  
   用现有 `buildPreviewSnapshot(uiScale, offset)` 和
   `EditorCanvasPreview` compose 路径承载 tree / canvas / inspector 的一个局部；
   ImGui 仍只提供外层停靠窗口和该 canvas 的输入归属。

3. **迁移完整垂直 slice**  
   一个 slice 至少包含：树或列表、selection、对应 inspector、触发这些动作的命令。
   不能只换绘制，而把选择/编辑行为仍藏在 ImGui widget 中。

4. **删除对应 ImGui 实现**  
   只有自研 slice 已承接全部交互和状态更新后，删除同一功能的 ImGui 控件；
   不保留双写 selection 或双份 save/reload path。

### 9.3 输入与坐标约束

- EditorLayer 是 adapter：窗口/ImGui canvas point → tree-local logical point；
- tree 以左上角原点、Y down 运行，不让 widget 认识 ImGui 坐标；
- canvas 未取得输入归属时不得转发；取得后只让一个 UI 系统消费；
- editor preview 与 standalone 的差别只在 mapping / target，不在 widget 或 workspace
  业务语义。

### 9.4 试点完成条件

- 试点完整使用 WidgetTree 控件实现，不依赖 ImGui widget；
- selection、inspector、save/reload 各只有一个事实源；
- snapshot 仍在 RenderGraph build / command recording 之前形成；
- editor smoke 无输入串扰、无 Vulkan validation error；
- 能据此明确下一次迁移是扩大 UIDesigner，还是需要先解决多窗口 / docking 等新问题。

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

### 10.2 提交切片

推荐顺序：

1. `[gui/app] add standalone GUI app-host library`
2. `[gui/example] move GUI framework smoke executable out of Engine`
3. `[gui/widgets] add focus and button capture contract`
4. `[gui/widgets] add stack and split primitives`
5. `[gui/widgets] add scroll and selectable rows`
6. `[gui/example] add GUIWorkbench tool workspace shell`
7. `[gui/editor] migrate UIDesigner vertical slice`

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
