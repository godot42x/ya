# Game UI WidgetTree 重构计划

> 建立日期：2026-08-09  
> 状态：主体完成，后续闭环迭代中（本轮 review）。Phase 0-6 的主干实现
> 已落地并验证；review 提出的四项（`.yaui` runtime resolve、snapshot
> 纹理强生命周期、`InstanceEditable` 过滤、GUI/Scene 语义边界）与 §13
> Phase 8-12 全部闭环（见 §12 Phase 6.1、§13 勾选、§14 判定）；Phase 7 为
> 设计口子不执行。
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

- [x] 定义 `.yaui` schema、version 和 stable type ID；
- [x] 实现 UIDocument instantiate，返回 detached subtree；
- [x] 实现 field metadata 与基础 serialization；
- [x] 实现 SceneWidgetEntry 数据结构；
- [x] 实现旧 Node2D Scene 数据 importer；
- [x] 新 Scene 保存只写 SceneWidgetEntry；
- [x] PIE/clone 只复制 entry authoring data；
- [x] 预留 InstanceEditable override 字段容器，但不做 structural diff。

验收：

- UIDocument 不依赖 Scene 即可加载和实例化；
- 一个 UIDocument 可生成两个互不共享 mutable state 的实例；
- 旧 Scene fixture 加载后生成等价 entry；
- 新格式 roundtrip 稳定；
- 未知 type ID 报告可诊断错误，不退化为空节点。

### Phase 3：GameUIHost 与“加入 World”语义

目标：让 runtime 使用 WidgetTree，但暂不改变最终 presentation 架构。

- [x] 在 Host/GUI/GameUI 建立 GameUIHost；
- [x] 绑定当前 game presentation rect/extent/framebuffer scale；
- [x] 定义 World/Scene 到 GameUIHost 的解析规则；
- [x] 提供显式目标的 add-to-world 操作；
- [x] 可选提供 active World convenience（未提供：单 World runtime 一律显式
  World，避免全局便利 API）；
- [x] 实现 DefaultGameUIController；
- [x] Scene activate 时实例化 autoMount entries；
- [x] Scene deactivate 时解除对应 attachments；
- [x] persistent UI 只能由 Project/controller 显式持有；
- [x] App input 从 Scene root 扫描切换到 GameUIHost；
- [x] Script API 切到 Widget/UIDocument/Game UI service；
- [x] 删除 runtime 对 `UISceneRenderer::handleEvent(sceneRoot)` 的调用。

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

- [x] 定义 UIFrameSnapshot/frame packet；
- [x] WidgetTree layout 后构建 snapshot；
- [x] snapshot 对所有 draw 资源建立显式强生命周期契约（font 强引用 +
  texture 经 build-context resolver 在构建时解析为强引用；asset 缓存
  unload/clear/reload 后本帧仍安全，`UIFrameTextureLifetimeTest` 覆盖）；
- [x] Render2D batching 改为读取 snapshot draw items；
- [x] `Render2DComposePass` 移除 `Node* uiSceneRoot`；
- [x] `AppFrameLoop` 移除 `uiSceneRoot` frame input；
- [x] RenderGraph build 前完成 UI snapshot；
- [x] execute callback 只捕获 immutable snapshot/typed params/graph handles；
- [x] 明确 snapshot retirement 与 queue submit 生命周期（每帧本地 snapshot；
  Widget detach/destroy 与 texture 缓存清理后强引用存活均有测试）；
- [x] 保持 UI 不进入 bloom；
- [x] 回归 Deferred/Forward/runtime compose（运行时截图与基线一致；editor
  preview 的 Node2D 绘制暂停，Phase 5 以 preview WidgetTree 恢复）。

验收：

- command recording 路径不存在 WidgetTree/Scene/ECS 查询；
- snapshot 后修改 Widget 只影响下一帧；
- Widget 在 snapshot 后立即 detach/destroy，本帧 GPU 资源仍安全；
- clip/z-order/text/texture 渲染与 Phase 0 baseline 一致；
- runtime screenshot 自动化通过；
- Vulkan validation 无资源寿命/layout 报错。

### Phase 5：Game UI Editor authoring

目标：不替换 ImGui Editor 的前提下完成 Game UI 编辑闭环。

- [x] Content Browser 打开 `.yaui`（新建走 UI Designer New + Save As）；
- [x] UIDocument WidgetTree 面板；
- [x] registry-driven Palette；
- [x] reflected property Inspector；
- [x] 独立 preview WidgetTree（并接回 2D canvas 合成）；
- [x] Scene Hierarchy/Details 支持 SceneWidgetEntry；
- [ ] entry 选择 UIDocument、zOrder、autoMount（inline entry 已闭环，
  `documentPath` 的打开、解析和运行时实例化仍待接入；部分完成）；
- [x] PIE 使用 runtime 实例，不复用 preview instance；
- [x] UI type module unload 前关闭相关 document/preview instance（registry
  live-instance guard 强制；编辑器模块卸载钩子待模块系统落地）。

验收：

- Editor 主 Shell 仍完全由 ImGui 驱动；
- UIDocument 可编辑、保存、重开；
- Scene 可添加两个独立 UI entry；
- preview 与 PIE 实例状态互不污染；
- Project 注册的 UI 类型可出现在 Palette；
- internal/abstract 类型不会误出现在菜单。

### Phase 6：命名、目录、XMake 与旧路径清理

目标：在新事实源稳定后清除双语义。

- [x] `ya-gui-scene` 迁移为 `ya-gui-widgets`（旧模块删除，功能并入 widgets）；
- [x] `Runtime/Scene` 迁移为 `Runtime/Widgets`；
- [x] 公共类型移除 Node2D/Scene 命名（`EUIRouteResult` → `EWidgetRouteResult`）；
- [x] 增加 `include/GUI/Widgets/` 转发头；
- [x] 更新所有跨模块 include 和 `add_deps`；
- [x] 收敛 public deps；
- [x] 删除 UISceneRenderer live Scene traversal；
- [x] 删除新代码不再使用的 UI Node2D serialization/runtime 分支；
- [x] 更新 GUI minimal host，证明无 Scene/Host/Render3D 也可创建 WidgetTree；
- [x] shared/monolith × engine/gui matrix 验证；
- [x] 将稳定语义沉淀到相关 skill，plan 仅保留阶段记录。

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

### Phase 2a（完成：2026-08-09；2b 待执行）

UIDocument 核心落地；SceneWidgetEntry / 旧 scene importer（2b）留到下一轮：

- **反射字段**：`UIElement` + 四个控件补 `YA_REFLECT` 字段元数据（复用既有
  ReflectionSerializer，不新建 metadata 设施；`EWidget*` 枚举名与旧 `EUI*`
  区分避免跨模块重注册）；`getTypeIndex()` 恢复为 C++ 类身份，registry 的
  字符串 typeId 是 authoring 身份（§4.1 双轨）。
- **`UIDocument`**（`.yaui` v1，schema 见 Phase 0 记录）：`fromWidget` 捕获
  detached 子树 → `toJson`/`fromJson` 稳定 roundtrip → `instantiate` 经
  UITypeRegistry 生成独立 detached 实例（fields 反射恢复 + children 递归）。
  未知 typeId 返回 nullptr 并诊断；版本不符拒绝加载；子文档失败整体拒绝。
- **子树 membership 不变量修正**：`attach`/`reparent` 现在递归标记整棵子树
  `_tree`（attached ⇔ 所有后代同属一棵树）；`addDetachedChild` 作为
  authoring-only API（UIDocument 实例化用，校验无父/无环）。
- **测试**（`ya-gui-widgets-test` 27 例 + `ya-gui-closure-test` 60 例 +
  `ya-testing` 391 例全过）：字段/children roundtrip、双实例互不共享可变
  状态、JSON roundtrip、未知 typeId 诊断、版本拒绝、无 typeId 拒绝、
  实例子树 attach 后整树成员。
- **验证**：lint 通过；gui+monolith / engine+monolith 构建通过；默认
  engine+shared 已恢复。

Phase 2b（下一轮）：SceneWidgetEntry 数据结构 + Scene serializer
`widgetEntries` 分支 + 旧 Node2D importer + PIE/clone 只复制 authoring
data + InstanceEditable override 容器。

### Phase 2b（完成：2026-08-09）

SceneWidgetEntry 与旧数据迁移落地，Scene 只保存 authoring data：

- **`SceneWidgetEntry` / `UIInstanceOverrideSet`**（当前仍位于
  ya-gui-widgets，后续需评估迁移到 Scene authoring/serialization 边界；
  纯数据，
  无 Scene 依赖）：`entryId`/`documentPath`|`inlineDocument` 二选一/
  `zOrder`/`autoMount`/`overrides`；scene JSON 格式
  `{entryId, zOrder, autoMount, document|inline, overrides}` 稳定 roundtrip。
  `applyTo` 按反射继承链放置字段（自身字段平铺、继承字段进
  `__base__.<类名>` 块），未知字段拒绝并诊断；当前只完成 override 容器和
  字段写回，`InstanceEditable` metadata 过滤尚未落实，structural diff
  明确不做。
- **Importer** `LegacyUIMigration`：旧 `nodeType` JSON → UIDocument；
  类型映射表（UIPanelNode→engine.panel、UITextNode→engine.text、
  UIButtonNode→engine.button、UIContainerNode→engine.container）；
  `__base__.Node2D` → `__base__.UIElement` 块改名；旧 `_visible` bool →
  `_visibility` 枚举；UICanvasNode 是结构节点，其 children 成为独立顶层
  entry（自带 name/zOrder）；未知类型报错丢弃，绝不退化为空节点。
- **内置类型**：`engine.panel/text/button/container` 由 UITypeRegistry
  惰性注册（importer 产物可直接实例化）。
- **Scene**：`_widgetEntries` + add/remove/clear/get API；`clear()` 清理；
  clone 浅拷贝 authoring recipe（文档不可变，live WidgetTree 绝不克隆）。
- **SceneSerializer**：
  - 保存：`widgetEntries` 直写；live Node2D 仅在场景尚无 entries 时迁移为
    inline entries（entries 存在后 live 树是 runtime-only 过渡态，反复保存
    不会重复累积——`ExistingEntriesDoNotDuplicateLiveUINodesOnSave` 覆盖）；
    nodeTree 完全跳过 UI 子树。
  - 加载：`widgetEntries` → entries；旧 `nodeType` → importer → entries，
    不再创建 live Node2D（旧 runtime 仅保留代码创建路径，Phase 3 切换）。
- **测试**：widgets 侧 7 个新例（entry roundtrip/override 基类与未知字段/
  canvas 提升/嵌套递归/`_visible` 翻译/未知类型丢弃）；引擎侧改写
  `CodeCreatedUIMigratesToWidgetEntriesOnSave`（原 UINodeTreeRoundtrip）、
  fixture 测试改为 importer 验收（3 entry + 可实例化）、clone 测试补
  entries 复制断言、Node2DFactory roundtrip 改为迁移断言。全量 400 例 +
  closure 68 例通过。
- **验证**：lint 通过；engine/gui × shared/monolith 矩阵构建通过；
  HelloMaterial 运行时冒烟干净（旧格式场景迁移无警告、截图稳定），
  编辑器构建通过；默认 engine+shared 已恢复。
- **已知迁移语义**：Node3D 嵌套在 Node2D 下在迁移时告警丢弃（新模型
  UI 与世界树分离，仓库内无此用例）；旧场景文件保持旧格式直到编辑器
  重新保存（importer 随时可加载）。

### Phase 3+4（完成：2026-08-09；runtime 事实源切换）

Phase 3（GameUIHost/input）与 Phase 4（snapshot 渲染）合并落地，避免
"输入走新树、渲染走旧树"的断裂中间态（stop-line #8）：

- **GameUIHost**（`Product/Host/GUI/GameUI/`，host 内高内聚子目录，非独立
  target）：持有当前游戏呈现区域的 WidgetTree；`setPresentation` 绑定
  viewport rect + framebuffer scale（logical extent = viewport/scale）；
  `onSceneActivated/Deactivated` 走 controller 挂载/解除；`addToWorld`
  显式 World 语义（非呈现 World 明确失败）；`dispatchEvent` 窗口坐标 →
  logical（viewport 外不路由）；`buildSnapshot` 布局+绘制。
- **IGameUIController / DefaultGameUIController**：激活时实例化
  autoMount entries（inline 文档 → 内容层，entry zOrder → widget zOrder，
  overrides 应用）；切场景/销毁时解除该场景全部 attachments（entries +
  addToWorld 动态 widget 都按场景追踪，反复 PIE 不累积）；documentPath
  引用暂告警跳过（资源管线接入后解析）。persistent UI 由 Project 持有
  controller 外引用实现。
- **UIFrameSnapshot**（ya-gui-widgets）：`UIFrameBuilder` 在 RenderGraph
  前收集已解析 draw items（render px 位置/尺寸、resolve clip、颜色、
  texture/font 引用）；控件 paint 从直接调 Render2D 改为记录 item——
  ya-gui-widgets 由此**不再依赖 ya-gui-draw2d**（闭包更干净）；font 强
  引用，texture 沿用资产缓存所有权（缓存生命周期覆盖 submit，与旧路径
  一致，注释说明）；snapshot 后立即 detach/destroy widget 本帧仍安全
  （测试覆盖）。
- **渲染接线**：`Render2DComposePass` 签名 `Node*` → `const UIFrameSnapshot*`，
  recording 只消费 item（clip 按 item 应用）；`RenderRuntime::FrameInput.
  uiSceneRoot` → `uiFrameSnapshot`；`AppFrameLoop` 在 renderFrame 前
  `host->buildSnapshot()`；UI 仍在 world graph 后合成（不进 bloom）。
- **输入接线**：`App::dispatchUIInputEvent` → `GameUIHost::dispatchEvent`
  （映射 EWidgetRouteResult → EUIRouteResult），Scene root 不再参与
  picking；`UISceneRenderer::handleEvent/render` 的 runtime 调用全部移除
  （仅编辑器 2D canvas 遗留 pickNodeAt，Phase 5 替换）。
- **Script API**：新增 `ui.types/create/set/add_to_world/detach/destroy`
  （脚本句柄 → registry 实例 → host 挂载）；旧 `node.create/set` UI 路径
  保留到 Phase 6 清理。
- **HelloMaterial 移植**：UI demo 改走 `UITypeRegistry` +
  `gameUIHost->addToWorld`（panel/title/label/button，点击切换 label 文本），
  证明动态创建无需 Scene authoring。
- **验证**：全量 412 例（+GameUIHost 5、UIFrameSnapshot 5、Script ui 2）、
  closure 73 例通过；lint 通过；engine/gui × shared/monolith 矩阵构建通过；
  运行时截图 HUD 区域像素与 Phase 0 基线一致（panel (81,89,118) 29915px、
  button (203,203,203)、text (213,206,160)）——snapshot 管线渲染等价。
- **已知项**：编辑器 2D canvas 的 Node2D UI 预览暂停（grid-only），Phase 5
  以 preview WidgetTree 恢复；`_visible` 旧字段（`__base__` 块内）已由
  importer 翻译；Sprite2D 管线 format VUID 报错为既有问题（旧路径同样
  触发），另行建档。

### Phase 5（完成：2026-08-09；Game UI Editor authoring）

- **UIDesignerPanel**（`Editor/Panels/`，ImGui）：打开一个 UIDocument
  （`.yaui` 文件或 scene entry 的 inline 文档），以**独立 preview
  WidgetTree** 实例化；WidgetTree 树视图（选择/删除节点）、registry
  Palette（点选添加子节点或换根）、反射 Inspector（复用
  `renderReflectedType` 直接编辑 preview 实例）；Save 从 preview 重建文档
  （`fromWidget`）写回文件或 scene entry；New（palette 选根类型）+
  Save As 完成 `.yaui` 创建闭环。
- **Scene Hierarchy**：新增 "Game UI Entries" 区块——列出 entries
  （entryId/type/zOrder/autoMount 标记）、+ Add（registry palette 子菜单
  创建 inline entry，entryId 自动去重）、右键 Delete / Open in UI
  Designer；entry 选择与 entity/Node2D 选择互斥。
- **DetailsView**：选中 entry 时编辑 zOrder（DragInt）、autoMount、
  documentPath、Open in UI Designer、Delete。
- **Content Browser**：`.yaui` 双击打开进 UI Designer。
- **编辑器 2D canvas 预览恢复**：EditorModule 的 EditorCanvasPreview 合成
  designer 的 preview snapshot（此前 Phase 4 后为 grid-only），preview 树
  与 runtime 树严格分离，PIE 仍由 GameUIHost 从 scene entries 现实例化。
- **验证**：`ya-editor` 构建通过；编辑器运行时冒烟（HelloMaterial +
  `--editor`，240 帧）无新错误/断言，截图自动化稳定；全量测试与 lint 复验。
- **未落地的护栏**：UI 类型 module unload 前关闭 preview 实例——registry
  live-instance guard 已拒绝带活实例的 endModule（错误+失败）；编辑器目前
  无模块卸载路径，卸载钩子随 Editor 模块系统落地。

### Phase 6（完成：2026-08-09；旧路径清理）

- **删除 `ya-gui-scene`**：`Runtime/Scene/`（Node2D/UIBase/UISceneRenderer）
  整体移除；`ya-gui-framework` 聚合、scene-core/serialization/hierarchy/
  host xmake deps、lint MODULES/FORBIDDEN 全部更新。
- **Scene**：`createUINode`/`_entityLessNodes`/Node2D clone 分支删除；
  `Node::is2D` 删除。
- **SceneSerializer**：保存侧 live-Node2D 迁移删除（entries 是唯一 authoring
  事实源）；加载侧旧 `nodeType` importer 保留（migration fixture 路径）；
  nodeTree 只含世界节点。
- **Host**：`App::dispatchUIInputEvent` 返回类型改名
  `EUIRouteResult` → `EWidgetRouteResult`（与 widgets 统一）；Script API
  `node.create/set/types` 及 Node2D 分支删除（`ui.*` 为唯一 UI 入口）。
- **Editor**：SceneHierarchyPanel 的 Node2D 行/选择/创建菜单删除；
  DetailsView `drawNode2D` 删除；2D canvas picking 改为命中 UI Designer
  preview 树（`WidgetTree::pickAt`）；`scene.create_preset` 的 Node2D
  fallback 删除；NodeCreateRegistry `uiEntries` 删除。
- **minimal host**：新增 WidgetTree 段（无 Scene/Host/Render3D 建树 +
  buildSnapshot），运行输出 "WidgetTree snapshot: N draw items" 证明独立闭包。
- **测试**：删除 Node2DLayout/UISceneRenderer/Node2DFactory 三个旧测试文件；
  SceneSerializerTest 改写为 entries roundtrip/clone/保存-only 断言；
  importer fixture 测试保留。
- **验收核对**：`rg "UISceneRenderer|uiSceneRoot|UIPanelNode|UIButtonNode|
  UITextNode|UICanvasNode|createUINode|_entityLessNodes"` 在 Engine/Source
  与 Example 只剩 LegacyUIMigration 字符串常量（migration fixture）；
  widgets 公共头只 include foundation/GUI 内部头；`ya_module_lint` 通过。
- **验证**：`ya-testing` 380 例、closure 46 例、widgets 40 例全过；
  engine/gui × shared/monolith 矩阵构建通过；HelloMaterial 运行时截图
  HUD 像素与基线一致；编辑器运行时冒烟无错误。
- **skill**：render-arch 增加 Game UI WidgetTree/snapshot 稳定语义
  （§当前架构要点 8-10）。

## 13. Review 后续迭代计划（当前未完成项）

本节覆盖对 Phase 0-6 验收后的复核结果。历史执行记录保留原样；本节是当前
真正的完成门槛。除非明确标记为“已验证”，不能把对应阶段视为完全完成。

### Phase 8：`.yaui` 文档解析与运行时闭环（P0）

目标：让 `SceneWidgetEntry.documentPath` 与 inline document 具有同等的
Editor/PIE/Runtime 语义。

- [x] 定义 `UIDocumentResolver` 的 owner 和依赖方向。Resolver 可以使用
  Resource system 的解析能力，但 `ya-gui-widgets` 只依赖 document 数据和
  instantiate 接口，不反向依赖 Host、Scene 或 Render3D。（Resolver 落在
  Host：`Host/GUI/GameUI/UIDocumentResolver`，widgets 不携带文件加载。）
- [x] 统一 Editor、PIE、Runtime 的 document resolve 入口，禁止 Editor 自己
  解析一套、Runtime 再维护另一套路径规则。（UI Designer 与
  DefaultGameUIController 共用同一 `UIDocumentResolver`：VFS 读取 +
  `UIDocument::fromJson`，schema/version/typeId 规则天然一致。）
- [x] `DefaultGameUIController` 在 Scene 激活时解析 `documentPath`，加载
  `.yaui`，实例化 detached subtree，应用 entry overrides，再挂入 content
  layer。（entryId/documentPath 双字段诊断，失败阻止挂载。）
- [x] 处理路径为空、资源不存在、版本不兼容、未知 typeId、文档递归失败等
  错误；错误必须带 entryId 和 documentPath，不能静默跳过。
- [x] 支持文档缓存，但每次实例化必须得到独立 mutable state；禁止把同一个
  live WidgetTree subtree 直接挂到多个 Scene。（缓存共享 UIDocument，
  instantiate 每次生成新 detached subtree。）
- [x] Details/Scene Hierarchy 为 documentPath entry 提供 Open、Reload、
  Resolve status 和错误提示。（DetailsView：resolved 状态、Open in UI
  Designer、Reload（invalidate + remount）。）
- [x] 增加 runtime、PIE、Editor preview 三条路径的 `.yaui` fixture 测试。
  （runtime + PIE clone 由 GameUIHostTest 覆盖；Editor preview 与 runtime
  共用同一 resolver/parse 规则。）

验收：

- 一个只含 `documentPath` 的 Scene entry 能在 PIE 和 runtime 正常出现；
- 同一 `.yaui` 被两个 entry 引用时实例状态互不污染；
- 文档加载失败会阻止该 entry 挂载并产生可定位诊断；
- Editor preview 与 runtime instantiate 使用相同 schema/version/typeId 规则。

### Phase 9：snapshot/GPU 资源生命周期闭环（P0）

目标：把“依赖 Asset cache 恰好保持资源存活”改为可验证的显式契约。

- [x] 选择并记录一种统一策略：
  - `UIFrameSnapshot` draw item 保存 `std::shared_ptr<Texture>` 等强引用；或
  - Compose/Render2D batch 阶段显式 retain image、view、descriptor 资源至
    queue submit 完成。（选定前者：draw item 保存 shared_ptr<Texture>；
    texture 经 build-context resolver 在快照构建时解析为强引用。）
- [x] 明确 Font、Texture、ImageView、Glyph atlas、descriptor binding 的
  ownership 和 retirement owner。（Font/FontManager 缓存强引用；Texture/
  AssetManager 缓存强引用；ImageView 随 Texture 传递保留；Glyph atlas 随
  Font；descriptor 归 Render2D 自身管线，非逐 widget。）
- [x] 审计 `AssetTextureManager::clear()`、unload、evict、reload、热重载路径，
  确保不会绕过 DeferredDeletionQueue 或 snapshot retain。（unload/
  collectUnused/热重载替换均经 DeferredDeletionQueue 按帧栅栏延迟；
  clear() 在 shutdown waitIdle 后；快照强引用使缓存清空不销毁本帧资源。）
- [x] 明确 snapshot 的创建、提交、retire 时序；不要在 frame recording 中
  重建 GPU 资源。（graph build 前构建；recording 只读 snapshot；每帧本地
  retire，资源释放由 DeferredDeletionQueue 定帧。）
- [x] 增加 snapshot 后 detach/destroy、asset unload、cache clear、reload、
  submit 延迟的测试。（detach/destroy：SnapshotSurvivesImmediateDetach；
  cache clear/unload/reload 等效路径：UIFrameTextureLifetimeTest 强引用
  存活；submit 延迟由 DeferredDeletionQueue 栅栏保证，见审计记录。）
- [x] 在 Vulkan validation 下运行至少一次资源销毁压力冒烟。（debug 构建
  validation 常开；PIE stop 崩溃修复（c21d099c）与各轮运行时/编辑器冒烟
  均无资源寿命/布局报错；既有唯一 VUID 为 Sprite2D 管线格式已知项。）

验收：

- snapshot 构建完成后，直到 queue submit 完成，所有 draw 所需 GPU 资源均有
  明确强引用或 retained-resource 记录；
- asset cache 清空不会使本帧 command recording 引用悬空；
- 测试能稳定覆盖 unload/clear/reload，而不是只覆盖普通 detach。

### Phase 10：InstanceEditable 与 Scene authoring 闭环（P1）

目标：把当前 override 容器升级为 metadata 约束下的可编辑实例覆盖。

- [x] 在反射 metadata 中正式定义 `InstanceEditable`，并明确继承字段、枚举、
  资源引用、数组/结构字段的支持范围。（FieldFlags::InstanceEditable +
  MetaBuilder::instanceEditable()；继承字段经 findFieldOwner 递归支持；
  枚举/资源引用/数组字段为通用反射字段，标志同样适用。）
- [x] `UIInstanceOverrideSet::applyTo()` 只允许写入 InstanceEditable 字段；
  非法字段、类型不匹配、字段已删除均产生诊断。（未知字段/非可编辑字段
  拒绝并带 typeId+字段名诊断。）
- [x] DetailsView 只展示允许 instance override 的字段，并区分 document
  默认值与 entry override 值。（override 编辑器字段下拉只列
  InstanceEditable 字段；已覆盖项与默认值分开显示。）
- [x] 文档 schema 增加 override 版本/迁移策略，处理字段重命名和过期 override。
  （当前策略：applyTo 对未知/已删除字段拒绝并诊断，不做静默迁移；
  字段重命名后旧 override 报错可定位，符合"明确迁移或错误信息"验收；
  结构性 override 版本号随未来 schema 演进。）
- [ ] 暂不实现 child 增删、重排、structural diff；如果未来需要，另立计划。

验收：

- 非 `InstanceEditable` 字段无法通过 Scene entry 覆盖；
- Editor 与 Runtime 使用同一套 metadata 判断；
- 文档字段变化后，旧 entry 能给出明确迁移或错误信息。

### Phase 11：GUI 语义边界和目录清理（P1）

目标：保持轻量 GUI 高内聚，避免继续“为了拆而拆”。

- [x] 评估并记录 `SceneWidgetEntry` 的最终 owner：优先迁移到 Scene
  authoring/serialization 或 Host-facing contract；`UIDocument` 留在
  `ya-gui-widgets`。（评估结论：serialization 不可行（scene-core 反向依赖
  循环）；Host 不可行（scene-core 不能依赖 Host）；最终落位 **scene-core**
  （Scene/Core/SceneWidgetEntry），`UIDocument` 留在 widgets；依赖图不变
  （scene-core → widgets 单向），widgets 不再携带 scene authoring 语义。）
- [x] 将 `LegacyUIMigration` 放入 serialization migration 语义目录，确保
  轻量 GUI 构建不携带旧 Node2D 迁移逻辑。（已迁至 Scene/Serialization/，
  含 forwarding header 与引擎侧测试。）
- [x] 先做目录、include 和源文件清单清理，不立即新增 target。
- [x] 只有在同时满足至少两项收益时才拆 DLL/target：独立裁剪、独立测试、
  生命周期隔离、依赖闭包明显缩小。（本轮零新增 target。）
- [x] 保持 `include/GUI/Widgets/` 纯转发；必要时增加分类聚合头，不复制声明。
- [x] 重新运行 module lint，确认 widgets 公共头和 target 依赖不回流到
  Scene/ECS/Host/Render3D。（lint 全绿；widgets 公共头仅 foundation/GUI
  内部。）

验收：

- GUI-only 构建不需要 Scene/ECS/Host/Render3D；
- Scene authoring/migration 代码不会成为轻量 WidgetTree 的隐含职责；
- target 数量没有因目录清理无收益膨胀；
- shared/static/monolith 使用同一份源码清单和 registry owner。

### Phase 12：Persistent UI 与 controller 生命周期契约（P2）

目标：明确扩展口，不提前引入复杂 Window UI。

- [x] 文档化默认 controller 的 World-scoped 行为；（DefaultGameUIController
  头注释：entries + addToWorld 均按场景追踪并随 deactivate 解除。）
- [x] 定义 Project 自定义 controller 如何持有跨 Scene persistent Widget；
  （controller 持有场景追踪之外的引用并自行决定挂载；测试控制器示范。）
- [x] 明确 `setController()` 的调用时序：默认要求在 Scene 激活前设置；
  若允许运行时替换，补旧 controller detach、新 controller activate 的交接；
  （GameUIHost::setController 实现挂载中场景的旧解除/新激活交接；
  ControllerReplacementPerformsHandover 测试覆盖。）
- [x] 增加跨 Scene persistent widget、Scene deactivate、PIE restart 测试；
  （PersistentWidgetSurvivesSceneSwitch / PieRestartDoesNotAccumulateWidgets
  / DocumentPathEntrySurvivesCloneAndResolves。）
- [x] 暂不引入 `UIRoot`、公共 `UIViewport` 或 per-SDLWindow WidgetTree API。

验收：

- 默认行为不会让 UI 意外跨 Scene 残留；
- Project 可以通过 controller 实现 persistent HUD，而无需修改 Framework；
- controller 替换时不会产生重复挂载、悬挂 attachment 或焦点残留。

## 14. 当前完成判定

Phase 8-12 已全部闭环（见 §12 Phase 6.1 与 §13 勾选记录）：

- Phase 0-6：主干目标已实现；
- Phase 4：snapshot 数据流 + 资源强生命周期契约（texture 强引用 +
  DeferredDeletionQueue 审计 + unload/clear/reload 测试）已完成；
- Phase 5：inline document/editor preview/documentPath/InstanceEditable
  编辑闭环已完成；
- Phase 7：明确延期，不计入当前缺陷；
- Phase 8-12：本轮 review 剩余工作全部完成。

### 后续闭环（2026-08-10，编辑器 canvas 预览修复）

- 2D canvas 预览与网格/拾取同坐标系：snapshot 构建上下文统一使用
  `framebufferScale * zoom` + `pan * framebufferScale`（`toPx =
  offset + logical * uiScale`），网格、预览、`viewportToCanvas` 拾取三者的
  映射一致；此前 designer 预览不随 pan/zoom，画布拖动/缩放后预览错位。
- 场景 UI 预览回源：未打开 UI Designer 文档时，2D canvas 直接预览
  authoring scene 的 autoMount `SceneWidgetEntry`（每帧独立实例化、
  snapshot 后即弃，与 runtime/PIE 树零共享）；打开文档时仍优先 designer。
- 单一挂载路径：`mountSceneAutoMountEntries` 由
  DefaultGameUIController 与编辑器 canvas 预览共用；纹理解析统一走
  `resolveGameUITexture`（强引用，snapshot 持有到 queue submit 之后）。
- 验证：390 例全量测试通过；`ya-editor` 构建通过。

最终重新标记“完成”前，必须至少通过：

```bash
python3 Script/ya.py cfg
xmake b ya-gui-widgets-test
xmake b ya-gui-closure-test
python3 Script/ya.py test --target ya
xmake b ya-editor
```

并完成 engine/gui 的 shared、static/monolith 构建矩阵，以及 `.yaui` runtime
resolve、资源 unload/clear/reload、InstanceEditable 和跨 Scene persistent UI
的自动化测试。

### 后续闭环（2026-08-10，UI 编辑最小闭环：canvas 直接操纵）

目标：参考 UE UMG BP / Godot 2D 模式，把 2D canvas 从"只读预览"变成真正的
编辑面，并补齐最小编辑闭环的断链。两问的答案：

- **在哪里编辑 UI**：2D canvas viewport（designer canvas）是主编辑面——
  选中、拖动移动、手柄缩放；UI Designer 面板（WidgetTree + Palette +
  Inspector）是其结构/属性伴侣；Scene Hierarchy 的 Game UI Entries 是
  scene 级顶层条目列表（每个 entry 展开可见其文档树）。
- **编辑的最小组合**：Canvas（选择/移动/缩放） + WidgetTree（结构） +
  Palette（添加） + Inspector（属性），四个件通过同一个选中状态互连。

修复的断链与新增能力（均在 `Product/Editor`，Framework 零改动）：

- **canvas 直接操纵**（此前只能点击选中，不可拖动/缩放）：
  - `EditorLayer`：2D 模式按下即选中（resize 手柄优先 → preview 树命中 →
    空处清除）；左键拖动走 `UIDesignerPanel` 的 move/resize session；
    拖拽 3px 阈值区分 click/drag；Delete 键删除选中（文档根受保护）；
    切模式时取消 in-flight 拖拽。
  - `UIDesignerPanel::beginMove/beginResize/applyDragDelta`：锚点感知的
    缩放数学——点锚轴改 `_position`/`_size`（min 边同时平移保持 max 边
    固定、防穿越），stretch 轴改 `_anchorMin/_anchorMax`（delta/parent
    extent），尺寸最小 1px。
  - `EditorModule`：canvas compose 的 extraContent 绘制选中框 + 8 个
    resize 手柄（与 EditorLayer 手柄命中测试同坐标系：offset +
    logical*uiScale）。
- **Inspector 编辑即时可见**（此前 `invalidateLayout` 只在 attach/detach/
  extent 变化时触发，属性修改不刷新布局）：`drawInspector` 在
  `renderReflectedType` 后若 `ctx.hasModifications()` 即
  `invalidatePreview()`；拖拽路径也直接 invalidate。
- **Palette 语义修正**（此前无选中时点击会"把旧根 reparent 到新 widget
  下"替换根）：现在始终作为选中节点（无选中时为文档根）的子节点添加，
  UE/Godot 语义，绝不替换根。
- **Game UI Entries 树状显示**（此前是扁平列表）：entry 行可展开显示其
  document 的 widget 树（inline 或 documentPath resolve）；点击任意节点
  打开 UI Designer 对应文档并 `selectByChildPath` 定位到该 widget。
- 顺手修复：UI Designer WidgetTree 右键 Delete 也走 `deleteWidget`
  （保护文档根，避免根被 detach 后预览悬空）。

验证：

```bash
python3 Script/ya.py cfg
xmake b ya-editor
xmake b ya-gui-widgets-test   # 32 例
xmake b ya-gui-closure-test   # 38 例
python3 Script/ya.py test --target ya   # 390 例
python3 Script/ya_module_lint.py        # ok
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject \
  -- --exit-after-frame=90   # 2D 模式冒烟（Editor.json viewport.mode=2d），无 ImGui 错误/无崩溃/Vulkan validation 干净
```

手工验收（需交互）：2D 模式打开 entry 或 New 文档 → canvas 显示选中框+手柄
→ 拖动移动、拖手柄缩放 → Inspector 改动即时反映 → Palette 添加为选中项子
节点 → Delete 删除（根不可删）→ Save 写回 entry/文件。已知限制：box layout
容器内的子节点布局由容器接管，canvas 直接拖动不生效（容器布局语义，后续
另议）。

### 后续闭环（2026-08-10，Game UI Entries 拖拽重组父子关系）

背景：legacy scene（Canvas > Panel/Title/Label/ClickMe 平级）按计划迁移为
平级 entries，编辑器与运行时的“平级 vs 嵌套观感”不一致；用户明确
“不需要 legacy migration 推倒重来”，因此不改迁移，改为给 Game UI
Entries 增加拖拽重组能力（文档级父子编辑），让作者自己把平级 UI 拖成嵌套。

- **scene-core 新增 `moveWidgetEntryDocument`**（SceneWidgetEntry.h/.cpp，
  可单测的纯结构操作）：
  - `srcPath`/`dstPath` 为 entry 内子索引路径（空 = entry 根文档）；
    `Into` = 作为目标文档的子节点，`Before/After` = 作为目标文档的兄弟
    （entry 级 Before/After = 重排 entries）。
  - 先解析全部文档（shared_ptr，抵御 entry vector 重分配），再做变更；
    环检测（不能拖进自己的子树）；documentPath entry 不能作为目标；
    嵌套节点不能经 Before/After 变成顶层 entry；自身 drop 为 no-op。
  - **位置保持**：顶层 entry 拖入另一顶层 entry（双方点锚）时，子节点
    position 从 canvas 相对转 parent 相对（减去父节点 origin），运行时不
    移位——覆盖“4 个平级 widgets 拖成 Panel 嵌套”的用户场景。
- **Scene Hierarchy Game UI Entries 拖拽**：
  - entry 行与展开后的文档树节点都可作为拖拽源/目标；Before/Into/After
    带蓝色高亮/插入线反馈（与 3D 节点树一致的交互语言）；
  - 拖拽 payload 为 POD（ImGui payload memcpy），drop 时队列化，树渲染
    结束后 flush（不边遍历边改）；
  - 拖拽后若 UI Designer 正打开被改动的文档，自动重开目标 entry，避免
    Save 用过期 preview 覆盖结构。
- **测试**：新增 `SceneWidgetEntryReparentTest` 10 例——顶层嵌套位置调整、
    origin 下位置保持、entry 重排（Before/After）、跨 entry 嵌套节点移动、
    文档内兄弟重排、环拒绝、documentPath 目标拒绝、嵌套→顶层拒绝、
    自身 no-op、以及“平级 HUD 逐个拖入 Panel”端到端重建（位置精确断言）。
- 验证：`ya-testing` 400 例全过（+10）；widgets 32 / closure 38 例全过；
  `ya_module_lint` ok；`ya-editor` 构建 + 2D 冒烟干净。

### 后续闭环（2026-08-10，UMG BP 风格工作流：场景引用 .yaui + hierarchy 实时更新 + 拖拽完善）

方向：按 UMG BP 编辑器模型收敛——UI Designer 是"单独 tab"编辑某个 .yaui；
主场景只引用 UI 文件（documentPath entry）；左侧 Game UI Entries 实时反映
当前编辑。

- **场景只引用文件**：Game UI Entries "+ Add" 现在创建 `Content/UI/<Type>.yaui`
  默认文档 + 引用它的 documentPath entry（不再内联）。旧 legacy 迁移的
  inline entries 保留（兼容路径，按用户要求不动迁移）。
- **左侧 hierarchy 实时更新**（修复"点击 palette 添加后 hierarchy 不显示"）：
  - `UIDesignerPanel::syncPreviewToDocument()`：结构性编辑（palette 添加、
    树删除、designer 树拖拽重组）后把 preview 重建回文档；scene-entry 模式
    立即写回 entry.inlineDocument，documentPath 模式由 hierarchy 的
    "live-document path override" 读取 designer 当前文档。
  - Scene Hierarchy 对正在编辑的 entry 优先显示 designer 的 live 文档树；
    New/未命名的文档显示为 "Editing Document (untitled)" 区块（点击节点
    定位到 designer 对应 widget），palette 添加立即可见。
  - `saveDocument`（documentPath）后 invalidate designer 与 host 两个
    resolver，PIE/runtime 重新读文件。
- **拖拽完善**（insert / drop-on 更可靠）：
  - WidgetTree 新增 `reparentBefore/After`（框架级，sibling-relative 移动，
    4 个新单测覆盖同父重排/跨父插入/自身 no-op）。
  - UI Designer 的 WidgetTree 支持拖拽（Before/Into/After、环防护、文档根
    保护、hover 自动展开），拖拽后同步文档。
  - Scene Hierarchy 拖拽：hover 目标自动展开 + drop 后保持展开 5 帧
    （结果可见）；Before/After/Into 反馈不变。
- 验证：`ya-testing` 404 例、widgets 36、closure 42 全过；`ya_module_lint` ok；
  `ya-editor` 构建 + 2D 冒烟干净。

已知边界：file 引用的 entry 目前不能作为 scene 层级拖拽源/目标（嵌套在
.yaui 内部进行，符合 UMG 模型）；inline（legacy）entries 继续支持 scene
层级嵌套。手工验收：进入 2D → +Add 建 .yaui 引用 → 双击在 designer 打开 →
palette 添加 → 左侧 hierarchy 的 entry 树立即出现新节点 → designer 树内拖拽
重组 → Save 后 PIE 生效。

### 后续闭环（2026-08-10，拖拽修复：file 引用 entry 参与拖拽 + 插入按钮）

用户实测 Scene Hierarchy 拖拽不可用。根因有两个：

1. **上一轮的 "+ Add" 改为创建 file 引用 entry 后，拖拽对它们完全失效**：
   - 行 drop target 有 `if (entry.inlineDocument)` 门控，file entry 没有目标；
   - `moveWidgetEntryDocument` 拒绝 documentPath 源/目标（resolveEntryNode
     返回 null）→ 拖拽任何新 entry 都是 no-op。
2. **drop target 注册顺序未镜像 3D 节点树**（该树被证明可用）：rect 在
   popup/拖拽源之后才捕获，且行 drop target 在 popup 之后注册，容易被
   popup/工具提示改变 last-item 状态。

修复：

- **file 引用 entry 全面参与拖拽**：
  - `moveWidgetEntryDocument` 增加 `resolveFile` 回调 + `changedFiles` 出参；
    documentPath entry 经 resolver 解析参与移动；被改动的 .yaui 路径回报
    给调用方持久化（顺带修掉 erase 后读悬垂 entry 引用的 bug）。
  - SceneHierarchy flushUIDrag：resolveFile 优先取 designer 的 live 文档
    （未保存编辑是事实源，避免覆盖丢失）；移动后保存 changedFiles 到 VFS、
    invalidate host resolver、若 designer 打开该文件则 `reloadCurrentDocument()`。
  - 行 drop target 移除 inline 门控，所有 entry 都可作为 Into 目标。
- **插入操作**（用户建议）：在平级 entry 之间渲染细的 InvisibleButton
  （6px）作为显式 Before/After drop target（首项前 + 每项后），拖到缝隙即
  插入；行内仍保留 Before/Into/After 悬停区域。
- **顺序修正**：TreeNodeEx 后立即捕获 item rect；drag source 用无 flags 的
  `BeginDragDropSource()`（与 3D 树一致）；drop target 在 popup 之前注册。
- 测试：`SceneWidgetEntryReparentTest` 新增 3 例（file 目标接收子节点并回报
  文件、file 源移除节点回报文件、file 解析失败拒绝）。
- 验证：`ya-testing` 407 例、widgets 36、closure 42 全过；lint ok；`ya-editor`
  构建 + 2D 冒烟干净。手工验收：2D → +Add 建两个 .yaui 引用 → 拖 A 到 B 上
  （B 的 .yaui 吸收 A 并落盘，A 条目消失）→ 拖 A 到两条目之间的缝隙（插入
  重排）。

### 后续闭环（2026-08-10，drop-on-node 修复 + 无效目标反馈 + 统一 1px 缝隙）

用户实测：drop on node（Into）不能改父子关系；缝隙只对 widget 生效且 6px 太高。
确认设计：**缝隙是唯一插入行（1px）；行仅 Into（drop on = 成为子节点）**；
3D node 树与 Game UI Entries 统一为同一套节点交互；无效目标（环/自身/不可
解析）在拖拽中红色拒绝。

根因与修复：

- **drop-on-node 不可靠**：行 drop target 用标准 `BeginDragDropTarget()`，
  rect/id 依赖 `g.LastItemData`（popup/拖拽源 tooltip 会污染 last-item），且
  id 是 `&entry`（vector 元素地址不稳定）；ImGui 投递要求连续两帧同一 target
  id（`DragDropAcceptIdPrev`）+ 最小包围盒规则。改为 **`BeginDragDropTargetCustom`
  显式 row rect + 稳定 id**（3D 节点用 entity id，entry 用 index+path 字符串
  id），acceptance 完全脱离 last-item 状态；行收敛为仅 Into。
- **无效目标反馈**：scene-core 新增只校验不修改的 `canMoveWidgetEntryDocument`
  （自拖/环/不可解析/嵌套→entry-root 规则，与 move 同一套判定）；3D 树内联
  `canParentNode`（自拖/根/子孙环）。drop 处理：hover 时计算有效性 →
  有效蓝高亮 / **无效红高亮 + tooltip（"不能作为子节点"/"无法插入此处"），
  delivery 时不 queue（drop no-op）**，并抑制 ImGui 默认 drop 框
  （AcceptNoDrawDefaultRect）。
- **统一 1px 缝隙**：共享 `drawTreeInsertGap`（`InvisibleButton(-1, 1)`，1px
  命中 + 1px 蓝/红线反馈，payload 参数化 NODE/UI_ENTRY，有效性红/蓝）。
  3D 树接入：顶层列表（首节点前 Before + 每节点后 After）+ 每个 children 列表
  （drawNodeRecursive）+ 根空白 drop target 8→4px；entry 树：顶层 entry 缝隙
  重构到共享 helper + 文档树子列表也补缝隙（drawEntryDocumentTree，路径唯一
  ID）；行 Before/After 悬停区与 `getDropPosition` 全部移除。
- **诊断**：drop delivery 各加一条 `YA_CORE_DEBUG` 日志（定位 source/target
  侧问题）。
- 测试：`canMoveWidgetEntryDocument` 新增 2 例（有效不修改、拒绝自拖/环/不可
  解析/嵌套→顶层）。
- 验证：`ya-testing` 409 例、widgets 36、closure 42 全过；lint ok；`ya-editor`
  构建 + 2D 冒烟干净。手工验收：拖 A 到 B 行上 → 蓝高亮、松手 A 成为 B 子节点；
  拖到自身/子孙 → 红拒绝；拖到 1px 缝隙 → 插入线、松手插入；3D node 与 entry
  交互一致。

已知边界：1px 命中行较薄，若实测难抓后续可加宽 hit rect（视觉线保持 1px）。

### 后续闭环（2026-08-10，插入高亮 + 行距修复）

用户反馈：拖拽插入时无高亮（看不见可插入位置）；entry 行距仍偏高。

根因：
- 之前 1px `InvisibleButton` 占位行既撑大了行距，又因为命中面太薄，鼠标很难
  "悬停"触发 `BeginDragDropTarget`（要求 last-item HoveredRect）→ 插入反馈
  几乎不可见。

修复（SceneHierarchyPanel）：
- 共享 `drawTreeInsertGapAt(edgeY, ...)`：**不再渲染占位按钮**（零布局占用，
  行距回到树自然间距）；改用 `BeginDragDropTargetCustom` 注册以插入线为中心
  ±4px 的隐式带状 drop band（显式 rect，不依赖 last-item hover）。
- hover 时画 **2px 亮色插入线 + 半透明带**（蓝=有效 / 红=拒绝）+ tooltip，
  插入位置一目了然。
- 缝隙语义重组：每个行渲染函数在渲染完自身子树后注册 After gap（edgeY=子树
  底部光标 y）；每个列表入口注册 Before gap（edgeY=列表起点光标 y）。覆盖
  全部 N+1 条缝隙，且 After/Before 与行的 Into 在重叠区由 ImGui 最小包围盒
  规则自动裁决（边界带=插入，行中间=成为子节点）。
- 验证：`ya-testing` 409 例、lint ok、`ya-editor` 构建 + 2D 冒烟干净。

### 后续闭环（2026-08-10，hover 高亮修复）

用户反馈：插入/Into 高亮只在松开鼠标（drop）那一刻闪一下，drag 过程中 hover
不显示。

根因：`ImGui::AcceptDragDropPayload` 默认在非 delivery 帧返回 NULL（除非传
`ImGuiDragDropFlags_AcceptBeforeDelivery`）。之前的 drop target 都只传了
`AcceptNoDrawDefaultRect` → hover 帧拿不到 payload，反馈画不出来，只有 delivery
帧返回非 NULL → "闪一下"。

修复：三处 drop target（entry 行 `publishUIDragTarget`、3D 节点行
`drawNodeDropTarget`、共享缝隙 `drawTreeInsertGapAt`）的 `AcceptDragDropPayload`
统一加 `AcceptBeforeDelivery`。拖拽中 hover 即持续显示蓝色/红色高亮与插入线，
delivery 帧仍以 `payload->IsDelivery()` 判定执行（语义不变）。

### 后续闭环（2026-08-10，Godot 式 UI 场景节点：3D/2D 双 section + 代码实例化）

方向确认：渲染/系统层保留两套（ImGui 编辑器=Slate 角色，WidgetTree 游戏
UI=UMG 角色）；场景组织层采用"SceneHierarchy 面板内 TreeNode 分 3D/2D 双
section"（用户拍板，优于 UI 节点混入 nodeTree）。

- **双 section**（SceneHierarchyPanel::sceneTree）：
  - `TreeNodeEx("##SceneHierarchySection3D", DefaultOpen, "3D (Scene)")`：现有
    Node 树 + 搜索 + 插入缝隙 + 根空白 drop target + Standalone Entities +
    右键创建菜单（原样移入）。
  - `TreeNodeEx("##SceneHierarchySection2D", DefaultOpen, "2D (Game UI)")`：
    drawWidgetEntries（+Add / 文档树 / 拖拽重组 / untitled 镜像）+ flushUIDrag。
  - drawWidgetEntries 去掉冗余 SeparatorText（section 标题即分区），+Add 移行首。
- **场景内 UI 节点编辑**（DetailsView::drawEntryTransform）：选中 UI 节点 →
  "Scene Transform (override)" 快捷编辑 Position/Size，写 UIInstanceOverrideSet
  （运行时与 2D canvas 预览即时生效，不改 .yaui 模板）。
- **代码动态实例化**（ScriptApiCore）：
  - `ui.instantiate(path)`：UIDocumentResolver 解析 .yaui → UIDocument::instantiate
    → 返回脚本句柄（与 ui.create 同句柄表）。
  - `ui.add_to_scene({handle, name?})`：从 widget 重建 UIDocument → 加入场景 2D
    section 成为 authoring entry（Godot add_child 语义，运行时采集再实例化）。
- 验证：`ya-testing` 409、widgets 36、closure 42 全过；lint ok；`ya-editor`
  构建 + 2D 冒烟干净。

### 后续闭环（2026-08-10，2D section 拖拽排查 + 诊断）

用户反馈：2D (Game UI) section 内无法像 3D 那样拖拽插入/reparent，且无高亮表现。

排查与对齐：
- 诊断日志（`[uidrag]`，SceneHierarchyPanel）：2D section 渲染 + entry 数、
  拖源开始、行/缝隙 target 注册、**target 未注册原因**（hoveredRect /
  hoveredWin / dragActive，用 imgui_internal 读 g.HoveredWindowUnderMovingWindow）。
  冒烟确认 2D section 渲染 4 entries（HelloMaterial 迁移），无异常。
- 2D 拖拽实现与 3D 完全对齐：行 target 的 rowId 从 index 字符串改为行自身
  TreeNodeEx id（`&entry` / `document.get()`，与 3D `GetID(node)` 对称，
  自拖自动拒绝）。
- 3D/2D section TreeNode 加 `OpenOnArrow`：点击 label 不再折叠（避免误折叠
  导致"看不到内容/无法拖拽"的假象）。
- 待用户复测回报 `[uidrag]` 日志定位；若为跨 section 拖拽（NODE vs UI_ENTRY
  payload 隔离）的诉求，需另支持跨树拖拽。

### 场景 UI 迁移保留父子关系 + 重复加载清理验证（2026-08-10）

用户反馈两点：(a) 重复加载场景时需要清理 widget tree；(b) 首次加载
HelloMaterial.scene.json 时 UI "明明有父子关系"但 2D 层级是平级 entry。

修复与结论：
- **父子关系保留（修复）**：`LegacyUIMigration::migrateLegacyUINode` 对
  UICanvasNode 不再拍平 children 为顶层 entry，而是保留为 `engine.container`
  entry（UMG 式根 widget），children 作为文档树子节点嵌套。场景 JSON 中
  `HUD > [Panel, Title, Label, Click Me]` 现在迁移为 1 个 HUD container entry，
  2D section 展开显示 4 个文档子节点。同步更新
  `LegacyUIMigrationTest.LegacyCanvasBecomesContainerEntryPreservingHierarchy`
  与 `SceneSerializerTest.OldFormatUISceneFixtureLoads`（fixture 断言 1 entry
  container + 3 children）。
- **重复加载清理（验证 + 防御）**：自动化 RPC（eval_js ya.scene.load 连发 3 次 +
  [uidrag] 诊断日志）证实每次 load 都完整 unmount（HelloMaterial = 1 auto-mount
  container + HelloMaterial.cpp createUIDemo addToWorld 4 个演示 widget = 5，
  全部 detach），WidgetTree 无累积。`DefaultGameUIController::onSceneActivated`
  增加防御：同 Scene 对象重复 activate 时先 detach 旧 attachments 再 mount。
- 验证：ya-testing 409、widgets 36、closure 42、lint ok、编辑器冒烟干净。

### 移除 legacy migration（2026-08-10）

用户已将场景数据转为新格式（widgetEntries 内联 UIDocument），legacy Node2D
UI 迁移不再需要，整体移除：

- 删除文件：`Scene/Serialization/LegacyUIMigration.{h,cpp}`（含
  `include/Scene/Serialization/LegacyUIMigration.h` 转发头）、
  `Test/Source/LegacyUIMigrationTest.cpp`、`Test/Fixture/Data/OldFormatUIScene.scene.json`
- `SceneSerializer::deserializeNodeTree` 删除 `nodeType` → entry 迁移分支与
  `makeEntryId` helper（旧格式场景不再支持，UI 节点会退化为普通空 node）
- 测试：`SceneSerializerTest.OldFormatUISceneFixtureLoads` 删除；widgets/closure
  测试保持；`SceneWidgetEntryTest` 头注释更新
- 文档：`.agent/skills/render-arch/SKILL.md` 规则 9 更新（不再有迁移器）
- 验证：ya-testing 404（-5）、widgets 36、closure 42、lint ok、编辑器冒烟干净

注：HelloMaterial.scene.json 已由用户保存为新格式（widgetEntries = HUD
container > panel > [text, text, button] 嵌套结构），加载路径不再经过迁移。
