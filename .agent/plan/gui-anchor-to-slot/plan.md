# Plan: gui-layout-unified — UIElement authored geometry 全量迁移到 Layout + Slot

> 本目录是阶段性工件，不默认代表当前主工作流。本计划完成后，稳定规则应回写到对应 skill。

## 0. 当前目标与边界

### 目标

完成一次无 legacy、无兼容层的 GUI 布局重构：

1. UIElement 不再持有 authored layout state。
2. 所有布局输入由 UILayout + UISlot 管理。
3. child 只提供 intrinsic measurement；parent layout 通过 constraints 和 slot 决定最终 rect。
4. UIElement::_layoutRect 仅保留为 layout pass 输出，供 paint / hit-test / snapshot 消费。
5. 消除 child-owned anchor/position/size API 和运行期“anchor 被忽略”兜底路径。

### 明确不做

- 不保留旧字段、旧 setter、旧序列化键或旧 DSL 兼容读取。
- 不把 slot 参数复制回 child 的 geometry 字段。
- 不继续维护 path-A/path-B 双轨布局协议；统一为 layout host + typed slot。
- .child(node) 若保留，只能表示新布局系统定义的 default slot，不是 legacy 兼容。

## 1. 最终架构契约

~~~text
UIElement
  ├─ identity / tree / style / paint / input
  ├─ measureContent(UIConstraints)
  └─ _layoutRect                         // arrange 输出，不是 authored input

UILayout: UIBox / UIOverlay / UISingleChild / UISplit / UIScroll / UIGrid / UICanvas
UISlot: UIBox / UIOverlay / UISingleChild / UISplit / UIScroll / UIGrid / UICanvas
~~~

职责边界：UISlot 是 parent-child edge 上的 authored intent；UILayout 负责 measure / arrange；UIElement::measureContent 提供 intrinsic size；_layoutRect 是最终几何结果。

统一布局流程：parent constraints → layout.measure(child, slot) → layout.arrange(parent, rect, slots) → child.layoutAssigned(rect) → paint / hit-test / snapshot。

## 2. UIElement authored geometry 删除清单

从 UIElement、反射、序列化、DSL、Designer 和所有调用点中删除：

~~~text
_anchorMin / _anchorMax / _position / _size / _minSize / _maxSize / _bAutoSize
setPosition / setSize / getPosition / getSize / computeAnchorRect
fillParent / setAnchors / fillWidth / fillHeight
reportStretchAnchorsIgnored
~~~

_layoutRect、setLayoutRect()、layoutAssigned() 保留，但仅作为布局结果通道。

## 3. 目标 DSL 与内部模型

### 3.1 Public DSL：layout spec 与 widget builder 分离

不为每种 parent 增加一套 fill/grow/place/align/cell 添加 API，也不把 UICanvasSlot 等 runtime 类型暴露给 DSL。布局 spec 与 widget builder 保持两个独立对象，通过 operator>> 组成临时 placed-child，再由 parent 的 operator[] 收集：

~~~cpp
ui::canvas("Root")[
    ui::layout()
        .fill()
        .offsets(FMargin{8, 8, 8, 0})
        .height(28)
        >> ui::text("Title").setText("Hello"),

    ui::layout()
        .anchor({1, 1}, {1, 1})
        .alignment({1, 1})
        .offsets(FMargin{0, 0, 8, 8})
        .size({120, 32})
        >> ui::image("Icon")
];

ui::column("Root")[
    ui::text("Title"),
    ui::layout().grow(1).margin(FMargin::all(8))
        >> ui::panel("Body")
];
~~~

operator[] 表示 parent 收集 children；operator>> 表示 layout intent 作用于 child。UILayoutIntent 只存在于 builder/materialization 阶段；parent 收到 placed-child 后解析并转换为对应 typed UISlot，随后 intent 被消费，不进入 live UIElement。

### 3.2 Modifier 组成

通用 modifier：margin、minSize、maxSize、width、height、align。

布局相关 modifier：grow/shrink/basis、anchor、offsets、row/column/span 等。它们可以继续链式组合；不再出现 child(node, huge slot object) 的长第二参数。

Canvas 是 layout 类型，不以 panelSlot 命名；新增 UICanvasLayout + UICanvasSlot。UICanvasSlot 至少包含 anchorMin/anchorMax、四边 FMargin offsets、alignment/pivot、width/height size mode、preferred/min/max size。所有字段由 UICanvasLayout 消费，不能写回 child。

Box 使用 grow/shrink/basis；Overlay 使用 fill/align/padding；SingleChild 使用 align；Grid 使用 row/column/span。fillWidth/fillHeight 不再作为通用概念。

### 3.3 约束与取舍

- 统一 parent[children] 结构，降低 DSL 层级和 parent-specific 添加方法数量。
- typed slot 仍是 runtime/layout 的唯一布局数据模型。
- operator>> 产生的 placed-child 在 parent materialization 时负责 intent → slot 的解析。
- 明显的结构不兼容应在 builder concept/静态检查中拒绝；无法在不制造大量 scoped API 的前提下静态表达的细节冲突，必须给出 materialization 期的确定性诊断。
- 未包装的 widget builder 直接出现在 parent[...] 中时，使用新布局系统定义的 default slot，不是 legacy 兼容。

### 3.4 operator DSL 的类型流

~~~text
ui::layout().grow(1)                 → FLayoutSpec<BoxCapability>
ui::layout().anchor(...)             → FLayoutSpec<CanvasCapability>
ui::layout().cell(0, 1)              → FLayoutSpec<GridCapability>

FLayoutSpec<C> >> WidgetBuilder      → FPlacedChild<C, WidgetBuilder>
ParentBuilder.operator[](child)      requires C compatible with ParentLayout
FPlacedChild                          → typed UISlot + UIElementRef
~~~

错误配置在 parent 的 operator[] 约束处失败：

~~~cpp
ui::column("Root")[
    ui::layout().anchor({0, 0}, {1, 1}) >> ui::icon("Icon") // 编译错误
];

ui::canvas("Root")[
    ui::layout().cell(0, 1) >> ui::panel("Cell")           // 编译错误
];
~~~

该语法不让 widget builder 持有 layout，也不要求 public DSL 显式构造 slot；layout spec、placed-child、typed slot 是三个清晰阶段。

### 3.5 不同 layout 的案例与 capability 隔离（历史草案，已被 3.1–3.4 的 operator DSL 取代）

以下内容保留作 capability 设计备忘，但 public DSL 以 3.1–3.4 的 parent[layoutSpec >> widget] 为准。

#### Canvas

~~~cpp
ui::canvas("Root")
    .child(ui::text("Title")
        .layout(ui::layout()
            .fill()
            .offsets(FMargin{8, 8, 8, 0})
            .height(28)))
    .child(ui::image("Icon")
        .layout(ui::layout()
            .anchor({1, 1}, {1, 1})
            .alignment({1, 1})
            .offsets(FMargin{0, 0, 8, 8})
            .size({120, 32})));
~~~

Canvas capability 允许：fill、anchor、offsets、alignment/pivot、width/height、preferred/min/max。

#### Box / Row / Column

~~~cpp
ui::column("Root")
    .child(ui::text("Title").layout(ui::layout().autoSize().margin(FMargin::all(4))))
    .child(ui::panel("Body").layout(ui::layout().grow(1).margin(FMargin::all(8))))
    .child(ui::text("Footer").layout(ui::layout().alignSelf(EUIBoxSlotCrossAlignment::End)));
~~~

Box capability 允许：autoSize、grow、shrink、basis、margin、cross-axis align。它不允许 anchor、row/column、h/v overlay alignment 等 Canvas/Grid 专用参数。

#### Overlay

~~~cpp
ui::overlay("Root")
    .child(background.layout(ui::layout().fill()))
    .child(badge.layout(ui::layout().align(EUIOverlayAlignment::End,
                                           EUIOverlayAlignment::Start)
                                  .padding(FMargin::all(4))));
~~~

Overlay capability 允许：fill、水平/垂直 alignment、padding；它不允许 grow、anchor、grid cell。

#### Single-child / SizeBox / Scroll

~~~cpp
ui::sizeBox("Root")
    .width(320)
    .height(180)
    .child(content.layout(ui::layout().align(Center, Center)));

ui::scroll("Viewport")
    .child(content.layout(ui::layout().fill()));
~~~

Single-child capability 允许：fill 或双轴 align，以及 host 约束下的 preferred/min/max；不允许多 child 的 grow、overlay stack 或 grid cell。

#### Grid

~~~cpp
ui::grid("Root")
    .child(title.layout(ui::layout().cell(0, 0).columnSpan(2)))
    .child(value.layout(ui::layout().cell(1, 1)));
~~~

Grid capability 允许：row、column、rowSpan、columnSpan、cell alignment；不允许 Canvas anchor 或 Box grow。

#### 类型隔离规则

~~~text
ui::layout()                         → CommonIntent
ui::layout().grow(1)                 → CommonIntent + BoxCapability
ui::layout().anchor(...)             → CommonIntent + CanvasCapability
ui::layout().cell(0, 1)              → CommonIntent + GridCapability

column[layoutSpec >> widget]  requires BoxCapability-compatible intent
canvas[layoutSpec >> widget]  requires CanvasCapability-compatible intent
grid[layoutSpec >> widget]    requires GridCapability-compatible intent
~~~

因此错误配置在 child() 的编译期约束处失败：

~~~cpp
ui::column("Root")[
    ui::layout().anchor({0, 0}, {1, 1}) >> ui::icon("Icon") // 编译错误
];

ui::canvas("Root")[
    ui::layout().cell(0, 1) >> ui::panel("Cell")             // 编译错误
];
~~~

这不是为每种 parent 发明一套 child API，而是统一 layout() modifier 入口、通过 capability type 约束 parent 的 child()。

## 4. 实施 checkpoint

### CP1 — 冻结新布局协议
- 新增 UIConstraints、measure/arrange contract、size mode 和布局结果定义。
- 明确各 layout 的 default slot 与 measure 优先级。
- 冻结 UILayoutIntent modifier 词汇、组合规则和 parent 解析规则。
- 增加 layout contract 文档和纯 CPU 几何测试。

### CP2 — 移除 UIElement authored geometry
- 删除字段、setter、反射字段、序列化字段和 computeAnchorRect。
- 将 leaf/container desired-size 逻辑迁移到 measureContent / layout。
- 删除 reportStretchAnchorsIgnored 及相关诊断字段。

### CP3 — 新增 UICanvasLayout / UICanvasSlot
- 实现四边 offsets、anchor span、alignment/pivot、preferred/min/max 和 Auto/Fixed/Stretch 语义。
- Panel 改为 Canvas layout host。
- 不创建绑定视觉控件的 FCanvasPanelSlot 命名。
- public DSL 使用 layout().anchor()/offsets()/fill()，不暴露 canvasSlot() 工厂。

### CP4 — 统一所有 layout host 的 slot 消费
- Box、Overlay、SingleChild、Split、Scroll、Grid、Canvas 全部通过 typed slot arrange。
- layoutSpec >> widget 形成 placed-child，在 materialization 时转换为 typed slot；不写 child geometry。
- reparent/detach 时销毁并重建 slot。

### CP5 — 全仓 API / runtime 迁移
- 迁移所有 setPosition/setSize/fillParent/setAnchors。
- 迁移 WidgetTree、DockSpace、Popup、UIDesigner、Workbench、Editor 和测试中的直接字段写入。
- PopupOverlay 单独定义 full-screen host 与 content slot，不假设等同于普通 Panel。

### CP6 — 反射、文档和资源格式迁移
- 删除旧 JSON 字段和旧 schema。
- 更新 UIDocument、Designer inspector、脚本绑定和快照 dump。
- 旧文档不做兼容读取；若需要迁移工具，只做一次性离线转换器，不进入 runtime。

### CP7 — 验证
- 编译期断言：child builder 不存在任何 child-owned geometry modifier；parent[layoutSpec >> widget] 只接受兼容 capability。
- DSL 可读性样例覆盖：layout spec 与 widget 平行可见，fill/grow/anchor 可继续链式调整。
- 几何测试：intrinsic measure、constraints、Canvas 四边 offsets、anchor span、alignment、min/max、reparent。
- snapshot parity、GUIWorkbench、GameEditor、headless host 全部验证。

### CP8 — 构建与提交
- 每个 checkpoint 是一个完整可验证架构目标。
- 代码、测试、plan/progress/feature matrix 在同一 checkpoint 提交。
- 使用 [gui/layout] message。

## 5. 设计来源的吸收原则

- Flutter：typed parent-data / scoped slot intent；不同 parent 不接受不相容 slot。
- Compose：parent constraints → child intrinsic measurement → parent placement。
- Qt：preferred/min/max/expansion policy 分离，不把所有尺寸语义塞进一个 position/size 字段。
- YA 自身：保留 retained-mode WidgetTree、不可变 snapshot、parent-owned UISlot 和 layout invalidation。

YA 不复制任何单一框架的 widget API；只吸收共同不变量：

~~~text
layout intent belongs to the edge
measurement belongs to content
placement belongs to parent layout
final geometry belongs to the layout result
~~~

## 6. 验收

- UIElement 没有 authored anchor/position/size/min/max/autosize 字段。
- 全仓不存在 UI 布局用途的旧 geometry 字段、setter 和 builder modifier。
- 所有布局输入可从 typed slot + layout diagnostics 观察。
- 不存在 path-A/path-B 双轨或“anchor 被忽略”运行期兜底。
- Canvas、Box、Overlay、Grid、SingleChild slot 不可混用。
- reparent 后不会泄漏旧 parent 的布局状态。
- measure → arrange → layoutRect → snapshot 是唯一几何链路。
- closure tests、GUIWorkbench、GameEditor、headless 和 macOS convergence gate 全部通过。
