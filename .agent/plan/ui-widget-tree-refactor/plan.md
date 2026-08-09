# Game UI WidgetTree 重构计划

> 建立日期：2026-08-09  
> 状态：待执行；架构语义已由用户确认。  
> 当前范围：只重构 Game UI；ImGui Editor Shell 保持不变。  
> 替代关系：本计划取代 `../game-ui-rendering/plan.md` 中“Node2D 与 Node3D
> 共用一棵 Scene Tree”的 Game UI 路线。  
> 继承关系：`../gui-framework-module-split/plan.md` 和
> `../module-boundary-cleanup/plan.md` 已完成的模块闭包、XMake、DLL/monolith
> 与 forwarding header 规则继续有效；本计划只修正 GUI 的运行时语义、数据源
> 和呈现链。

## 0. 已确认决策

| 编号 | 决策 | 计划中的执行含义 |
|---|---|---|
| UI-1 | `Window Tree` 不是合适语义；Window 本身也应是 Widget | 框架核心只定义 `WidgetTree`。未来每个 SDLWindow 可持有一棵树及一个 Window Root Widget，但本轮不引入完整 Window UI |
| UI-2 | 当前只做 Game UI，不替换 ImGui Editor | 新框架负责运行时 Game UI 和编辑器内的 Game UI 预览；Editor Shell 继续通过 ImGui 呈现 |
| UI-3 | 当前在游戏承载区域维护 WidgetTree | Product/Host 为当前游戏呈现区域维护一棵活动 `WidgetTree`，而不是让 Scene 或 SDLWindow 直接拥有 live UI tree |
| UI-4 | 不照抄 UE 的 `AddToViewport` API | 对外语义是“把 Widget 加入指定/当前 World 的 Game UI”；Game Host 再把它解析为对应 WidgetTree 的默认内容挂载点 |
| UI-5 | WidgetTree 是单父树 | Widget 只有进入 WidgetTree 后才参与 layout、input、paint 和 snapshot；detached Widget 可以存活，但不会被收集 |
| UI-6 | Scene 中可保存多个顶层 UI 条目 | Scene 只保存 `SceneWidgetEntry` authoring recipe，不保存 live Widget，也不引入公共 `UIRoot` |
| UI-7 | Scene UI 默认随场景激活/退出挂载和解除 | 默认挂到当前 Game UI 内容层；Project 可注入 controller 改写多 World、持久 UI、转场和分层策略 |
| UI-8 | Widget 使用共享引用生命周期 | unmount 不等于销毁；仍被业务持有的 detached Widget 继续存活。带活跃 Widget 实例的模块 DLL 不允许卸载 |
| UI-9 | 最终呈现方向仍为 World Image + UI 合成 | 分阶段完成；本轮先建立 Game WidgetTree 与 immutable snapshot，未来再让每个 SDLWindow 的 root tree 接管最终 presentation |
| UI-10 | Project 定义业务层，Framework 保留系统层 | HUD/Menu 等由 Project 组织；popup、tooltip、drag、focus/capture/IME 等基础层和路由能力由 Framework 持有 |

## 1. Review 结论与架构纠正

### 1.1 当前最主要的问题不是 API 名字，而是事实源错误

当前实现把 Game UI 当作 Scene Node2D：

```text
active Scene::_rootNode
  └── Node2D / UIPanelNode / UIButtonNode ...

App input
  -> UISceneRenderer::handleEvent(scene->getRootNode())

Render2D compose
  -> UISceneRenderer::render(scene->getRootNode())
```

这导致 GUI Framework 无法脱离 Game Scene 使用，也让 live Scene Tree 同时承担：

1. 游戏世界组织；
2. UI visual parent；
3. UI layout/paint 顺序；
4. UI 输入事实源；
5. UI 序列化文档。

这些职责并不高内聚。Game UI 应迁移到独立 `WidgetTree`；Scene 只通过 authoring
entry 和 Game Host adapter 与它发生关系。

### 1.2 `UIRoot` 不应成为公共对象

不新增以下模型：

```cpp
scene.ensureUIRoot();
uiRoot.create<T>();
```

原因：

- 动态 Widget 创建不应要求先访问 Scene；
- 两个独立 Panel 不需要人为共享一个业务 `UIRoot`；
- 多 World、多游戏呈现区域和未来多 Window 时，`UIRoot` 会同时混淆所有权、
  挂载点和布局根；
- root 是 `WidgetTree` 的结构实现，不是业务必须理解的对象。

`WidgetTree` 内部必须有 root widget，但它只承担树结构和系统层组织。业务代码默认
操作 Game UI service 或明确的父 Widget，不直接依赖 root 类型。

### 1.3 `Viewport` 不作为 GUI Framework 的公共挂载语义

这里存在三种不同的 viewport：

1. RHI graphics viewport；
2. RenderRuntime 的游戏画面区域；
3. ImGui Editor 中承载游戏画面的面板。

若公共 API 统一叫 `UIViewport::addWidget()`，会把三个概念混在一起。计划采用：

- Framework 层：`WidgetTree::attach(parent, widget)`；
- Game 层：把 Widget “加入某个 World 的 Game UI”；
- Host 层：把 World/active game presentation 解析到具体 `WidgetTree` 和默认内容层；
- Render 层：只接收提前生成的 UI snapshot，不理解 World 或 WidgetTree。

实现类内部可继续使用 `viewportRect`、`viewportExtent` 表达坐标区域，但不把
`UIViewport` 固化成框架公共对象。

### 1.4 Scene 中可编辑的 UI 不是 live visual tree

Scene 中的多个顶层 Panel 应保存为多个 `SceneWidgetEntry`：

```text
Scene authoring data
  ├── SceneWidgetEntry -> Inline Widget/UIDocument A, zOrder=0
  ├── SceneWidgetEntry -> UIDocument B, zOrder=100
  └── SceneWidgetEntry -> Inline Widget C, autoMount=false
```

Scene 激活时由 `IGameUIController` 实例化并挂载；Scene 退出时解除默认挂载。
因此：

- Scene 文件可以直接 author Game UI；
- 运行时仍然只有 WidgetTree 是 layout/input/draw 的事实源；
- Project 可以替换默认 controller，决定 persistent HUD、loading screen、
  split-screen 和多 World 的策略；
- 不需要让 UIElement 继承 Scene Node。

### 1.5 draw collection 不能发生在命令录制期间

当前 `recordRender2DComposePass()` 接收 `Node* uiSceneRoot`，并在 command
recording 中遍历 live Scene/UI tree。该路径必须移除。

正确的数据流为：

```text
Game logic mutates live WidgetTree
  -> layout
  -> paint traversal
  -> build immutable UIFrameSnapshot
  -> build RenderGraph
  -> execute callback consumes snapshot only
  -> snapshot/resources live through queue submit
```

Input routing 仍访问 live WidgetTree；render execute callback 不能访问 WidgetTree、
Scene、ECS、registry、resource resolver 或动态 Widget 状态。

### 1.6 Window 是未来的 Widget 承载者，不是当前重构前提

最终可扩展为：

```text
SDLWindow
  └── WidgetTree
      └── WindowRootWidget
          ├── Window content
          │   └── Game presentation widget
          │       ├── World render image
          │       └── Game UI content
          └── Framework system overlays
```

但本轮只实现：

```text
current game presentation area
  └── GameUIHost
      └── WidgetTree
          └── internal root
              ├── project content layer
              └── framework system layers
```

`WindowManager` 本轮不持有 WidgetTree，不重构 ImGui 多窗口，不改变 SDLWindow
创建/销毁流程。所有接口只需避免阻断未来“一窗口一树”即可。

## 2. 目标语义模型

### 2.1 `UIElement`

公共 Widget 基类继续使用此前确认的 `UIElement` 命名：

- 只表达 visual/layout/input/paint 语义；
- 不继承 `Node`、`Node2D`，不持有 ECS entity；
- 可 detached 创建；
- 最多只有一个 visual parent；
- 只有绑定到某个 WidgetTree 后才进入 active visual tree；
- parent 关系不等于业务对象所有权；
- Project 可继承并注册自定义类型。

现有类型迁移：

| 当前类型 | 目标类型 |
|---|---|
| `Node2D` | `UIElement` |
| `UICanvasNode` | `UICanvas` 或内部 content root；避免把每个业务 UI 都强制包 Canvas |
| `UIPanelNode` | `UIPanel` |
| `UITextNode` | `UIText` |
| `UIButtonNode` | `UIButton` |
| `UIContainerNode` | `UIContainer` |

`Node2D` 名称保留给未来真正的 2D World/Scene 节点，不再承担 Game UI。

### 2.2 `WidgetTree`

`WidgetTree` 是每个 UI presentation context 的唯一 live visual tree，负责：

- internal root widget；
- single visual parent 约束；
- attach/reparent/detach；
- layout invalidation；
- paint order 和 clip hierarchy；
- focus、capture、hover path；
- hit testing 和 event routing；
- Widget ID/path 索引；
- immutable frame snapshot 构建；
- 模块活跃实例统计的接入点。

`WidgetTree` 不负责：

- Scene 生命周期；
- World 选择；
- RenderGraph 编排；
- SDLWindow 创建；
- Project 的 HUD/Menu 业务分层；
- UIDocument 的资产加载策略。

### 2.3 Tree 内部层级

Framework 创建内部 root 和稳定系统挂载点：

```text
internal tree root
  ├── game content layer
  │   └── Project-owned children, ordered by zOrder
  ├── popup/menu layer
  ├── tooltip layer
  └── drag/IME/debug layer
```

规则：

- “加入 World 的 UI”默认进入 `game content layer`；
- Project 可在 content layer 下建立 HUD/Menu/Modal 等任意业务层；
- Framework 系统层不写入 Scene authoring 数据；
- Project 不能通过普通 child 顺序覆盖 tooltip/drag 等系统层；
- 系统层是否拆成多个具体 Widget 由实现决定，不升级为独立 target。

### 2.4 `GameUIHost`

`GameUIHost` 是 Product/Host 的适配职责，不属于纯 GUI Framework。它负责：

- 当前游戏呈现区域对应的 WidgetTree；
- active World/Scene 到 WidgetTree 的解析；
- viewport rect、logical extent、framebuffer scale 到 UI 坐标的转换；
- SceneWidgetEntry 的自动实例化/挂载/解除；
- 调用 layout/update/snapshot；
- 将 input 路由到当前树；
- 把 UI snapshot 交给 RenderRuntime；
- 提供 Project 可替换的 `IGameUIController`。

初期只有一个活动 GameUIHost/WidgetTree。接口中不写死 singleton，未来允许：

- 多 SDLWindow 各有独立 WidgetTree；
- split-screen 每个 game presentation 拥有独立或共享 UI context；
- Editor 中多个独立 Game Preview。

### 2.5 `UIDocument`

可复用 UI 使用独立文档，建议扩展名 `.yaui`：

- 保存一棵 detached UIElement subtree；
- 保存稳定 type ID、属性、children 和文档版本；
- 可被 SceneWidgetEntry 引用；
- 可由代码动态实例化；
- 实例之间数据独立；
- 本轮不实现 prefab structural diff；
- 后续通过 `EditAnywhere` / `InstanceEditable` metadata 支持 entry 实例字段覆盖。

`UIDocument` 不复用 `.scene`，不保存 World entity，也不要求 Scene 存在。

## 3. 创建、挂载与生命周期契约

### 3.1 Framework 基础操作

以下是语义示例，不要求逐字采用函数名：

```cpp
UIElementRef widget = uiFactory.create<MyWidget>(); // detached

WidgetAttachment attachment =
    tree.attach(parent, widget, {.zOrder = 100});

attachment.detach();
```

约束：

- detached 创建成功后没有 tree/parent，不参与 UI 行为；
- `attach` 校验 parent 属于目标 tree；
- 已挂载 Widget 再 attach 时必须显式 reparent，禁止隐式双父；
- detach 递归解除整棵 subtree 的 tree membership；
- detach 不销毁 Widget；
- Widget 被销毁前必须先失去 visual parent；
- tree 销毁时解除所有 membership 和 focus/capture 状态。

### 3.2 Game 层添加语义

不固化 `AddToViewport`。推荐的语义接口为：

```cpp
UIElementRef widget = ui.create<MyWidget>();
GameUIAttachment attachment =
    gameUI.addToWorld(world, widget, {.zOrder = 100});
```

可以提供“当前 active World”的便利调用，但它只能位于 Product/Host 或 Project
helper，不能成为 GUI Framework 的全局 singleton API：

```cpp
gameUI.addToActiveWorld(widget);
```

语义要求：

- 显式 World 版本是无歧义基础；
- active World 版本只在单 World runtime 中提供便利；
- Host 把 World 解析到 GameUIHost；
- 最终实际 attach 到该树的 `game content layer`；
- 若 World 没有活动 presentation，返回明确失败，不静默挂到其他树；
- Scene/World 退出时只自动解除由该生命周期创建的 attachment。

具体 C++ 名称在 Phase 3 开始前根据现有 `Scene`/`SceneManager` API 定稿；不得
为了追求 UE 外观引入仓库中不存在的 `World` 核心类型。

### 3.3 Scene 自动挂载

`SceneWidgetEntry` 建议数据：

```cpp
struct SceneWidgetEntry
{
    StableId                 entryId;
    UIDocumentRef            document;       // 或 inline definition
    UIElementDefinition      inlineWidget;
    int32_t                  zOrder = 0;
    bool                     autoMount = true;
    UIInstanceOverrideSet    overrides;      // 初期允许为空
};
```

要求：

- `document` 与 `inlineWidget` 二选一；
- `entryId` 在同一 Scene 中稳定，用于编辑器选择和 override；
- 不存 live pointer、tree pointer、parent pointer 或 transient focus 状态；
- 多个 entry 分别实例化，成为 content layer 的多个 children；
- 默认 controller 只处理 `autoMount=true`；
- persistent UI 不依附 SceneWidgetEntry 的自动生命周期；
- Scene clone/PIE 复制 authoring data，不复制 live WidgetTree。

### 3.4 共享引用与 DLL 卸载

采用 `shared_ptr` 等价的 `UIElementRef`，同时增加以下护栏：

- visual parent/tree 对 child 持强引用；
- child 对 parent 使用非 owning pointer 或 weak reference；
- Project 可额外持有强引用；
- unmount 后若 Project 仍持引用，Widget 保持 detached 存活；
- `UITypeRegistry` 记录 type owner module；
- 每个实例创建/销毁更新模块 live-instance count；
- module unregister 前必须先销毁或迁出其所有 Widget 实例；
- 带 live instance 的 DLL unload 明确失败并记录错误；
- registry/global UI state 必须由唯一 shared owner 持有，不能在多个 DLL 中各有一份。

## 4. 类型扩展与编辑器入口

### 4.1 显式类型注册

应用和插件通过稳定 type ID 注册 UI 类型：

```cpp
registry.registerType<MyInventoryPanel>({
    .typeId      = "project.inventory_panel",
    .displayName = "Inventory Panel",
    .category    = "Project/Game UI",
    .module      = moduleHandle,
});
```

不使用“扫描所有反射派生类自动生成创建菜单”作为公共机制，原因：

- DLL load/unload 顺序不可控；
- 重命名 C++ 类会破坏文档；
- 无法稳定管理 category、icon、abstract/internal type；
- 很难判断实例来自哪个 module；
- Editor 菜单和文档兼容需要稳定 ID。

反射继续负责字段读写、序列化和 Inspector metadata；registry 负责类型身份、
factory、编辑器展示和 module ownership。

### 4.2 自定义字段

第一阶段字段 metadata 至少区分：

- serializable；
- editor visible；
- runtime only；
- instance editable；
- required resource/reference；
- deprecated/migration alias。

`UIDocument` 保存类型默认字段；`SceneWidgetEntry` 的 override 仅允许覆盖
`InstanceEditable` 字段。本计划不实现 child 增删、重排等 prefab diff。

### 4.3 Editor 入口

Editor Shell 仍为 ImGui，只新增 Game UI authoring 工具：

1. Content Browser：创建/打开 `.yaui`；
2. UI Designer：显示 UIDocument 的 WidgetTree；
3. Palette：来自 `UITypeRegistry`；
4. Inspector：使用反射 metadata 编辑字段；
5. Preview：使用独立 preview WidgetTree，不污染 runtime tree；
6. Scene Hierarchy/Details：添加和编辑 `SceneWidgetEntry`；
7. PIE：Scene 激活后由默认 controller 创建 runtime 实例。

明确不做：

- 用新 UI 重写 Editor 主菜单、Dock、Property Panel；
- 把 ImGui widget 混进 Game WidgetTree；
- 让 Editor ImGui tree 成为 Game UI 的绘制事实源；
- 同一 UIElement 同时挂到 preview tree 和 runtime tree。

## 5. 输入、布局与绘制数据流

### 5.1 每帧时序

```text
1. Game/Project mutates UIElement properties
2. GameUIHost dispatches queued UI input to live WidgetTree
3. WidgetTree processes focus/capture/hover and invalidation
4. WidgetTree performs layout using logical game presentation extent
5. WidgetTree builds immutable UIFrameSnapshot
6. RenderRuntime builds RenderGraph with the snapshot
7. RenderGraph execute records commands from snapshot only
8. snapshot resources remain alive until queue submit completes
```

输入与 update 的精确先后应与现有 App frame loop 对齐；核心要求是 snapshot
生成后，本帧 command recording 不再读取 live tree。snapshot 之后发生的 UI
修改进入下一帧。

### 5.2 坐标

- UI 继续使用左上角原点、Y 向下；
- WidgetTree 使用 logical pixels；
- GameUIHost 持有 presentation rect、logical extent、framebuffer scale；
- SDL event/window coordinates 先由 Host 转成 tree-local logical coordinates；
- hit test、clip、paint 使用同一转换结果；
- Editor Game Preview 的 offset/scale 由 Host adapter 处理，Widget 不感知 ImGui。

### 5.3 Event routing

WidgetTree 维护：

- topmost hit path；
- preview/capture phase；
- target phase；
- bubble phase；
- pointer capture；
- keyboard focus；
- hover enter/leave；
- popup dismissal；
- event consumed result。

Game input fallback 只在 UI 未消费时执行。Scene root 不再参与 UI picking。

### 5.4 `UIFrameSnapshot`

snapshot 至少包含：

- logical extent 与 framebuffer scale；
- 按最终 paint order 排列的 draw items；
- resolved transform；
- resolved clip rect/scissor；
- brush/texture/font/glyph binding；
- color、opacity、blend/pipeline key；
- batch boundaries；
- 保证 GPU 录制安全的资源强引用；
- 可选 debug source ID，但不保存 live Widget pointer 作为执行依赖。

snapshot 必须：

- 构图前完整生成；
- 对 command recording 只读；
- 不调用 Widget 虚函数；
- 不查询 AssetManager/Scene/ECS；
- 在 queue submit 完成前保持引用资源存活；
- 允许 Render2D/未来 UI renderer 做 batch，但 batch 不能反向访问 WidgetTree。

### 5.5 呈现阶段

本轮目标：

```text
World render output
  -> existing game presentation target
  -> compose UIFrameSnapshot over world result
  -> current presentation path
```

远期窗口目标：

```text
World render output image
  -> Game presentation widget
  -> per-SDLWindow WidgetTree snapshot
  -> final window compositor
  -> swapchain presentation
```

远期阶段才让 Window root tree 接管最终 presentation。本轮的接口和 snapshot
不得假设最终 target 永远是 world render target，但不提前实现完整窗口 compositor。

## 6. 模块、目录与依赖

### 6.1 模块职责

在不为了拆分而拆分的前提下，建议把现有 GUI 模块收敛为：

```text
ya-gui-resources
  Font/Texture/Brush/Glyph 等轻量 GUI 资源

ya-gui-draw2d
  snapshot draw item 的 batching 和 RHI 命令生成

ya-gui-widgets
  UIElement/WidgetTree/layout/input/type registry/UIDocument/snapshot builder

ya-gui-compose
  把 UIFrameSnapshot 合成到指定 render target

ya-gui-framework
  兼容聚合入口，不持有第二份 registry/global state

ya-host（内部 GameUI 子目录）
  GameUIHost/IGameUIController/World-or-Scene adapter/input and frame orchestration
```

说明：

- 当前 `ya-gui-scene` 的 Scene 语义已错误，应迁移/重命名为
  `ya-gui-widgets`；
- 初期不单独创建 `ya-game-ui` target，Game UI integration 先在 `ya-host`
  内以高内聚子目录组织；
- 只有 Game UI integration 后续满足 target 拆分标准至少两项时，再升级为
  独立 target；
- `ya-gui-widgets` 不依赖 Scene/ECS/Render3D/Host/Editor；
- `ya-gui-compose` 不依赖 WidgetTree 或 Scene，只依赖 snapshot 数据契约、
  draw2d、resources 和 RHI；
- Host 可以依赖 GUI + Scene + RenderRuntime，反向依赖禁止。

### 6.2 建议目录

遵循“源码与原始头文件放在一起，不引入 Public/Private”：

```text
Engine/Source/Framework/GUI/Runtime/
  Widgets/
    UIElement.h/.cpp
    WidgetTree.h/.cpp
    WidgetAttachment.h/.cpp
    UITypeRegistry.h/.cpp
    UIDocument.h/.cpp
    UIFrameSnapshot.h
    Layout/
    Input/
    Controls/
    include/GUI/Widgets/...
    xmake.lua

  Compose/
    Render2DComposePass.h/.cpp
    include/GUI/Compose/...

Engine/Source/Product/Host/GUI/GameUI/
  GameUIHost.h/.cpp
  IGameUIController.h
  DefaultGameUIController.h/.cpp
  SceneWidgetEntryRuntime.h/.cpp
```

目录拆分标准：

- owner/facade 留在 `Widgets/` 根；
- Layout/Input/Controls 只在职责稳定且文件数量足够时建目录；
- 不按每个控件创建 target；
- 不把连续 layout/paint 主流程切碎到多个互相跳转的同级文件；
- 若 `WidgetTree.cpp` 仍能清楚展示 update→layout→snapshot 主链，则允许保持大文件。

### 6.3 Forwarding header

跨模块只包含：

```cpp
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/UIDocument.h"
```

对应 `include/GUI/Widgets/*.h` 只转发原始头：

```cpp
#pragma once
#include "../../../UIElement.h"
```

规则：

- 不复制声明；
- 不在 forwarding header 增加 adapter/helper/宏逻辑；
- 文件较多时提供 `GUI/Widgets/Controls.h`、`GUI/Widgets/Document.h` 等分类聚合；
- 只有完整引入成本足够小才提供 `GUI/Widgets/Lib.h`；
- Host 的 GameUI API 由 `include/Host/GUI/GameUI/` 自己暴露，不能从 GUI
  Framework 头反向 include Host。

### 6.4 XMake

`ya-gui-widgets`：

```lua
target("ya-gui-widgets")
    ya_module()
    add_includedirs("./include", { public = true })
    add_files("*.cpp", "Layout/*.cpp", "Input/*.cpp", "Controls/*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_deps("ya-foundation-core", { public = true })
    add_deps("ya-gui-resources")
```

实际依赖以 header audit 为准，不允许为了“先编过”把 RHI backend、Scene、
Render3D 或 Host 设成 public dependency。

shared/monolith：

- 沿用 `ya_linkage=shared|monolith`；
- shared 下 registry/state 只有一个 DLL owner；
- monolith 下同一 target 切 static，不维护第二份源码清单；
- `ya-engine` 与 `ya-gui-framework` 兼容 facade 不复制实现；
- type registration 在两种 linkage 下走同一注册入口。

## 7. 当前代码迁移映射

| 当前位置/行为 | 目标 |
|---|---|
| `GUI/Runtime/Scene/Node2D.*` | 拆出纯 `UIElement`/controls，移除 `Node` 继承和 Scene Tree 假设 |
| `GUI/Runtime/Scene/UISceneRenderer.*` | 职责拆为 WidgetTree layout/input 与 snapshot builder；不再接收 `Node*` |
| `Render2DComposePass(..., Node* uiSceneRoot, ...)` | 改为消费 `const UIFrameSnapshot&` 或共享只读 frame packet |
| `App::dispatchUIInputEvent()` 扫 active Scene root | 路由到 `GameUIHost::dispatchEvent()` |
| `AppFrameLoop::FrameInput.uiSceneRoot` | 改为 frame build 前生成的 `uiFrameSnapshot` |
| Scene serializer 中 Node2D 分支 | 迁移为 `SceneWidgetEntry` + UIDocument/inline definition |
| `ScriptApiCore` 的 `node.create/set/destroy` UI 路径 | 迁移为 UI document/runtime Widget API，不再伪装成 Scene Node |
| Editor Scene Hierarchy 的 Node2D 行 | 改为 SceneWidgetEntry 行；UIDocument 内部层级在 UI Designer 展示 |
| `WindowManager` 只管理 `IWindowProvider` | 本轮保持；未来阶段再增加 per-window WidgetTree/root owner |

迁移期间允许旧 Scene 文件导入，但兼容逻辑必须：

- 位于 serialization migration 层；
- 把旧 Node2D 数据转换为 SceneWidgetEntry/inline UIDocument definition；
- 不让新 runtime 同时维护旧 Scene UI tree 和新 WidgetTree 两套事实源；
- 有明确版本号、测试与删除条件；
- 新保存文件只写新格式。

## 8. 分阶段实施计划

### Phase 0：建立回归护栏和格式版本

目标：在修改行为前固定当前可观察结果。

- [x] 为现有 Node2D UI 增加 layout、z-order、clip、hit-test、event consumed 测试；
- [x] 增加 Scene UI roundtrip fixture，作为旧格式迁移输入；
- [x] 增加 runtime Game UI screenshot/automation baseline；
- [x] 记录 `ya-gui-scene`、`ya-gui-compose`、`ya-host` target deps/include；
- [x] 为 GUI closure lint 增加禁止 Scene/ECS/Render3D/Host include 的目标规则；
- [x] 定义新 UIDocument 与 SceneWidgetEntry format version。

验收：

- 当前测试全部通过；
- baseline 文件可在迁移后重放；
- 没有生产代码行为改动。

### Phase 1：建立 UIElement、WidgetTree 与显式类型注册

目标：先建立独立 visual tree，不接入 Scene 和最终渲染。

- [x] 从 Node2D 中提取 UIElement 的 layout/paint/input 属性；
- [x] UIElement 移除 Node/entity/Scene 依赖；
- [x] 实现 WidgetTree internal root 和 content/system layers；
- [x] 实现 single-parent attach/reparent/detach；
- [x] 实现 detached 生命周期；
- [x] 实现 invalidation、layout、hit path、focus/capture 基础；
- [x] 建立 UITypeRegistry 稳定 type ID 与 explicit registration；
- [x] 增加 module owner/live-instance tracking；
- [x] 将基础控件迁移到 UIElement hierarchy；
- [x] 保留旧 Node2D runtime 不接新入口，仅用于下一阶段数据迁移对照。

验收：

- WidgetTree 无 Scene/ECS/RHI/Render3D/Host 依赖；
- 两个独立 Panel 可分别 attach 到 content layer；
- detach 后业务强引用仍有效，但不再参与 hit-test/layout；
- double-parent、跨 tree 非显式 reparent 会失败；
- DLL unregister live-instance guard 有单测；
- GUI closure target 可独立链接。

### Phase 2：UIDocument 与旧 Scene UI 数据迁移

目标：把可复用 UI authoring 从 `.scene` 中分离。

- [ ] 定义 `.yaui` schema、version 和 stable type ID；
- [ ] 实现 UIDocument instantiate，返回 detached subtree；
- [ ] 实现 field metadata 与基础 serialization；
- [ ] 实现 SceneWidgetEntry 数据结构；
- [ ] 实现旧 Node2D Scene 数据 importer；
- [ ] 新 Scene 保存只写 SceneWidgetEntry；
- [ ] PIE/clone 只复制 entry authoring data；
- [ ] 预留 InstanceEditable override 字段容器，但不做 structural diff。

验收：

- UIDocument 不依赖 Scene 即可加载和实例化；
- 一个 UIDocument 可生成两个互不共享 mutable state 的实例；
- 旧 Scene fixture 加载后生成等价 entry；
- 新格式 roundtrip 稳定；
- 未知 type ID 报告可诊断错误，不退化为空节点。

### Phase 3：GameUIHost 与“加入 World”语义

目标：让 runtime 使用 WidgetTree，但暂不改变最终 presentation 架构。

- [ ] 在 Host/GUI/GameUI 建立 GameUIHost；
- [ ] 绑定当前 game presentation rect/extent/framebuffer scale；
- [ ] 定义 World/Scene 到 GameUIHost 的解析规则；
- [ ] 提供显式目标的 add-to-world 操作；
- [ ] 可选提供 active World convenience；
- [ ] 实现 DefaultGameUIController；
- [ ] Scene activate 时实例化 autoMount entries；
- [ ] Scene deactivate 时解除对应 attachments；
- [ ] persistent UI 只能由 Project/controller 显式持有；
- [ ] App input 从 Scene root 扫描切换到 GameUIHost；
- [ ] Script API 切到 Widget/UIDocument/Game UI service；
- [ ] 删除 runtime 对 `UISceneRenderer::handleEvent(sceneRoot)` 的调用。

验收：

- 动态创建 Widget 无需 Scene authoring；
- 加入指定 World 后成为默认 content layer child；
- 两个 SceneWidgetEntry 分别挂载并按 zOrder 命中；
- 切 Scene 不残留 auto-mounted Widget；
- persistent Widget 可跨 Scene 保留；
- UI 未消费事件时 gameplay fallback 正常；
- Editor Game Preview 坐标转换正确。

### Phase 4：Immutable UIFrameSnapshot 与渲染接线

目标：命令录制不再读取 live tree。

- [ ] 定义 UIFrameSnapshot/frame packet；
- [ ] WidgetTree layout 后构建 snapshot；
- [ ] snapshot 持有 draw 所需资源强引用；
- [ ] Render2D batching 改为读取 snapshot draw items；
- [ ] `Render2DComposePass` 移除 `Node* uiSceneRoot`；
- [ ] `AppFrameLoop` 移除 `uiSceneRoot` frame input；
- [ ] RenderGraph build 前完成 UI snapshot；
- [ ] execute callback 只捕获 immutable snapshot/typed params/graph handles；
- [ ] 明确 snapshot retirement 与 queue submit 生命周期；
- [ ] 保持 UI 不进入 bloom；
- [ ] 回归 Deferred/Forward/runtime/editor preview compose。

验收：

- command recording 路径不存在 WidgetTree/Scene/ECS 查询；
- snapshot 后修改 Widget 只影响下一帧；
- Widget 在 snapshot 后立即 detach/destroy，本帧 GPU 资源仍安全；
- clip/z-order/text/texture 渲染与 Phase 0 baseline 一致；
- runtime screenshot 自动化通过；
- Vulkan validation 无资源寿命/layout 报错。

### Phase 5：Game UI Editor authoring

目标：不替换 ImGui Editor 的前提下完成 Game UI 编辑闭环。

- [ ] Content Browser 新建/打开 `.yaui`；
- [ ] UIDocument WidgetTree 面板；
- [ ] registry-driven Palette；
- [ ] reflected property Inspector；
- [ ] 独立 preview WidgetTree；
- [ ] Scene Hierarchy/Details 支持 SceneWidgetEntry；
- [ ] entry 选择 UIDocument、zOrder、autoMount；
- [ ] PIE 使用 runtime 实例，不复用 preview instance；
- [ ] UI type module unload 前关闭相关 document/preview instance。

验收：

- Editor 主 Shell 仍完全由 ImGui 驱动；
- UIDocument 可编辑、保存、重开；
- Scene 可添加两个独立 UI entry；
- preview 与 PIE 实例状态互不污染；
- Project 注册的 UI 类型可出现在 Palette；
- internal/abstract 类型不会误出现在菜单。

### Phase 6：命名、目录、XMake 与旧路径清理

目标：在新事实源稳定后清除双语义。

- [ ] `ya-gui-scene` 迁移为 `ya-gui-widgets`；
- [ ] `Runtime/Scene` 迁移为 `Runtime/Widgets`；
- [ ] 公共类型移除 Node2D/Scene 命名；
- [ ] 增加 `include/GUI/Widgets/` 转发头；
- [ ] 更新所有跨模块 include 和 `add_deps`；
- [ ] 收敛 public deps；
- [ ] 删除 UISceneRenderer live Scene traversal；
- [ ] 删除新代码不再使用的 UI Node2D serialization/runtime 分支；
- [ ] 更新 GUI minimal host，证明无 Scene/Host/Render3D 也可创建 WidgetTree；
- [ ] shared/monolith × engine/gui matrix 验证；
- [ ] 将稳定语义沉淀到相关 skill，plan 仅保留阶段记录。

验收：

- `rg "UISceneRenderer|uiSceneRoot|UIPanelNode|UIButtonNode"` 只剩明确的
  migration fixture 或为零；
- GUI Framework public header 不 include Scene/Node/ECS/Render3D/Host；
- 所有 public header 都由所属 target 的 forwarding include root 暴露；
- `xmake show -t` 无错误 public dependency；
- 不存在为本次迁移临时增加后未证明价值的小 target。

### Phase 7：未来 per-window WidgetTree 与最终 presentation

状态：设计口子，本轮不执行。

进入条件：

- Game WidgetTree/snapshot 已稳定；
- 有真实多窗口或替换 Editor Shell 的需求；
- world output 已能作为稳定 image 输入参与 GUI compose；
- 已评估 SDLWindow、swapchain、ImGui viewport 的迁移成本。

未来任务：

- SDLWindow owner 持有 WidgetTree；
- WindowRootWidget 成为每窗口树根；
- Game presentation 作为 root 的一个 child；
- world render image 作为 Widget 内容；
- popup/tooltip/drag 跨窗口策略；
- 每窗口 input/focus/IME；
- per-window snapshot 与 swapchain presentation；
- 分阶段迁移或替换 ImGui Editor Shell。

本阶段未开始前，禁止为了“以后可能需要”把 WindowManager、ImGui backend 和
GameUIHost 一次性重写。

## 9. 验证矩阵

### 9.1 单元测试

- Widget single parent/reparent/detach；
- detached Widget 不参与 tree；
- zOrder 和 stable sibling order；
- clip hierarchy；
- layout invalidation；
- hit-test path；
- focus/capture/hover；
- popup system layer；
- UIDocument roundtrip/instantiate；
- SceneWidgetEntry roundtrip；
- old Scene UI migration；
- explicit type registry；
- module live-instance unload guard；
- snapshot immutability；
- snapshot resource lifetime。

### 9.2 目标构建

每阶段按影响范围选择，完成阶段至少覆盖：

```bash
python3 Script/ya.py cfg
xmake b ya-gui-closure-test
xmake b ya-gui-minimal-host
xmake b ya-editor
python3 Script/ya.py test --target ya
```

遵守 xmake 3.0.8 一次 `xmake b` 只构建一个显式 target 的限制。

### 9.3 自动化与人工冒烟

```bash
python3 Script/ya.py run \
  --project Example/HelloMaterial/HelloMaterial.yaproject

python3 Script/ya.py run-editor \
  --project Example/HelloMaterial/HelloMaterial.yaproject
```

检查：

- runtime World + Game UI；
- 动态 add/remove；
- 两个独立顶层 Panel；
- Scene 切换；
- persistent UI；
- PIE 进出；
- Editor preview 缩放；
- UI 不进 bloom；
- UI 点击不穿透；
- screenshot/automation stable；
- Vulkan validation。

### 9.4 构建产品矩阵

```text
engine + shared
engine + monolith
gui    + shared
gui    + monolith
```

GUI profile 必须继续排除 ECS、Game Scene、RenderGraph、Render3D、Physics 和
完整 Material/Resource 链。

## 10. 停止线

出现以下情况必须暂停当前阶段并重新 review：

1. `UIElement` 或 `WidgetTree` 需要 include Scene/ECS/Render3D/Host 才能工作；
2. RenderGraph execute callback 仍需要 live Widget pointer；
3. 为了 Scene authoring 又把 live Widget 塞回 Scene Tree；
4. 为了模仿 UE API 引入含义不清的公共 `UIViewport`；
5. Project 业务层和 Framework popup/focus 等系统层再次混为一体；
6. 同一 Widget 能同时存在于两棵树或拥有两个 visual parent；
7. DLL 可以在仍有该模块 Widget 实例时卸载；
8. 迁移期存在两套同时参与 layout/input/render 的 UI 事实源；
9. 目录/target 数量增加，但无法说明独立裁剪、生命周期、测试或依赖收益；
10. 为未来 Window UI 提前重写 ImGui Editor 或 WindowManager。

## 11. 完成定义

本计划完成需要同时满足：

- Game UI 的唯一 live 事实源是 WidgetTree；
- Scene 只保存 SceneWidgetEntry authoring data；
- 动态 Widget 可 detached 创建并加入指定/当前 World；
- 两个独立顶层 UI subtree 可被同一 tree 收集；
- Widget 单父、共享引用和 DLL 生命周期契约有测试；
- App input 不扫描 Scene root；
- RenderGraph command recording 不遍历 live tree；
- UIFrameSnapshot 资源活到 submit 完成；
- UIDocument 和 Project 自定义类型形成编辑闭环；
- ImGui Editor Shell 未被强制迁移；
- GUI-only 闭包继续独立；
- forwarding header、XMake deps、shared/monolith matrix 全部通过；
- 旧 Node2D Game UI 语义和兼容路径按计划清理完成。

## 12. 阶段执行记录

> 每个阶段完成后在此追加验证证据与决策，plan 主体只保留目标语义，不随
> 实现细节膨胀。

### Phase 0（完成：2026-08-09）

回归护栏与格式版本已建立，无生产代码行为改动：

- **clip 回归**：`Render2D::pushClipRect` 的嵌套交集数学提取为纯函数
  `Render2D::intersectClipRect`（行为不变），新增 6 个单测（包含/钳制/部分
  相交/不相交/零宽细条/链式幂等）。边缘相切得到零宽细条而非全空——零宽
  scissor 与不相交等价，测试按此语义断言。
- **event consumed / layout / z-order / hit-test**：既有 `Node2DLayoutTest`
  （22 例）与 `UISceneRendererTest`（5 例）已覆盖 Stop 独占消费、Pass 穿透、
  Hidden 子树裁剪、zOrder 优先命中等，Phase 0 无需新增。
- **旧格式迁移 fixture**：新增
  `Engine/Test/Fixture/Data/OldFormatUIScene.scene.json`（Node2D 树 + 
  `__base__` 反射字段的旧 scene 格式），`SceneSerializerTest.
  OldFormatUISceneFixtureLoads` 从磁盘加载并断言基类/派生字段完整恢复。
  该 fixture 是 Phase 2 importer 的固定迁移输入。
- **截图基线**：HelloMaterial（含 HUD：Canvas/Panel/Title/Button）以
  `--screenshot-target presentation` 捕获
  `Engine/Saved/Automation/ui-widget-baseline/hello-ui-baseline.png`
  （sha256 `356e1765b5e027b81f3a338e389c7640ec164ecdfe1f485df74a9233be195304`；
  30 warmup + 5 stable 帧校验）。重放命令：
  `xmake r ya-runtime --project Example/HelloMaterial/HelloMaterial.yaproject
  --exit-after-frame 60 --screenshot <out> --screenshot-target presentation`
- **deps 基线**：`baseline/show-ya-gui-scene.txt`、`show-ya-gui-compose.txt`、
  `show-ya-host.txt`（`xmake show -t` 快照）。关键事实：
  `ya-gui-scene` public 依赖 foundation-core + hierarchy，private 依赖
  draw2d/resources/backend/vulkan；`ya-gui-compose` public 依赖
  foundation-core + rhi，private 依赖 gui-scene/draw2d/resources；
  `ya-host` 依赖完整 engine 闭包（含 gui-scene）。
- **lint**：`ya_module_lint.py` 已对 `ya-gui-scene/compose` 禁止
  Scene/ECS/Resource/Render3D/Physics/Gameplay/Host/Editor include；
  Phase 0 验证通过（exit 0）。新 `ya-gui-widgets` 模块的 lint 条目随
  Phase 1 加入。

#### 格式版本（Phase 0 定义）

- `.yaui` 文档版本 `1`：`{ "version": 1, "typeId": "<stable-type-id>",
  "fields": {<InstanceEditable + serializable 字段>}, "children": [<子文档>] }`。
  `typeId` 来自 UITypeRegistry；children 为嵌套 UIDocument 片段；不保存
  live pointer / tree 状态。
- `SceneWidgetEntry` 格式版本 `1`：scene JSON 中 `"widgetEntries": [{
  "entryId": "<stable-id>", "document": "<path.yaui>" | "inline": {…},
  "zOrder": int, "autoMount": bool, "overrides": {} }]`。`document` 与
  `inline` 二选一；`overrides` 只允许 InstanceEditable 字段（Phase 2 落实）。
- 迁移期旧 scene 文件保留 `nodeType` 路径，新保存只写 `widgetEntries`。

### Phase 1（完成：2026-08-09）

`ya-gui-widgets` 模块建立，与旧 `ya-gui-scene` 并存、互不接线：

- **新模块** `Engine/Source/Framework/GUI/Runtime/Widgets/`：
  - `UIElement`：从 Node2D 提取的 layout/paint/input 属性（anchor 数学、
    visibility 三轴、zOrder、Pass/Stop hit filter），无 Node/entity/Scene；
    `UIElementRef` 共享引用，visual parent 非 owning；析构时解除子节点
    回链 + 附树销毁断言（`_tree != nullptr` 即 bug）。
  - `WidgetTree`：internal root + 四个稳定系统层（Content/Popup/Tooltip/
    DragIme），root/layer 为 `HitTestInvisible` 结构元素；attach 校验
    单父、parent 属于本树、防环；`reparent` 为显式跨树移动；detach 递归
    解除子树 membership 并清理 focus/capture/hover；layout/hit 走
    zOrder 稳定序；指针 capture 绕过 hit 走位（`bViaCapture`），键盘路由
    到 focus widget；tree 析构递归断链。
  - `UITypeRegistry`：稳定字符串 typeId + 显式工厂注册；`beginModule`/
    `endModule` 模块所有权；`createInstance` 挂 module lease，实例存活时
    `endModule` 失败（DLL unload guard 单测覆盖）。
  - Controls：`UIPanel`/`UIText`/`UIButton`/`UIContainer`（含 9-slice、
    autosize、box layout、clip push/pop）迁移到 UIElement 层级。
  - forwarding headers：`include/GUI/Widgets/*.h` + `Controls/*.h` + 聚合
    `Controls.h`。
- **XMake**：`ya-gui-widgets` public 依赖仅 foundation-core（公共头只
  include Core/Types、Core/Event、glm、AssetRef）；draw2d/resources 为
  private 依赖；`ya-gui-framework` 聚合加入新模块。
- **闭包证明**：新增 `ya-gui-widgets-test`（只链 `ya-gui-widgets` + gtest，
  与 `ya-gui-closure-test` 并列），21 个 WidgetTree/Registry 单测覆盖：
  双 Panel 挂载、detached 不参与、双父/跨树 attach 失败、显式 reparent
  （同树+跨树）、防环、detach 保活与递归断链、系统层不可 detach、zOrder
  命中、四层叠放顺序、Pass/Stop 路由、Hidden 裁剪、focus 键盘路由、
  capture 点击（drag-release 语义）、模块 live-instance guard、registry
  显式注册/未知类型诊断。
- **lint**：`ya-gui-widgets` 禁止 Host/Editor/Scene/ECS/Resource/Render3D/
  Physics/Gameplay/RHI include；`ya_module_lint.py` 通过。
- **验证**：`ya-gui-closure-test` 54 例、`ya-testing` 385 例全过；
  gui+monolith 与 engine+monolith 构建通过；`Script/ci.sh` 两个 closure
  测试纳入 engine-shared 与 gui-shared。
- **决策记录**：
  - `attach`/`reparent` 不覆盖 `_zOrder`——zOrder 是 widget 自身属性，单
    一事实源（计划 §3.1 的 `{.zOrder = 100}` 示例为语义示意，不逐字采用）。
  - 枚举/context 命名用 `EWidget*`/`Widget*Context` 前缀，与旧 `EUI*`
    并存避免跨模块重定义；Phase 6 删旧模块后恢复短名。
  - Phase 1 不引入反射字段：UIDocument 序列化（Phase 2）再决定 schema，
    type 身份由 registry 持有，与 §4.1 一致。
  - detached 子树内 parent 回链在父销毁时由 ~UIElement 统一断开，避免
    悬垂（测试 `TreeDestructionReleasesMembershipSafely` 覆盖）。
