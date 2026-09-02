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

### 首版兼容政策（硬约束）

本计划的首版目标是一次性完成全量重构，**不保留任何 legacy 兼容语义**。这里的“无兼容”不仅指不对外承诺旧 API，还包括运行时、序列化和测试资产都不得继续解释旧模型：

- 旧 widget serialized file 不迁移、不转换、不兼容读取；未定版文件和旧 fixture 直接删除，首版只生成/读取新 schema。
- 旧 geometry 字段、旧 builder/attach 重载、child-owned self-positioned fallback、deprecated alias、双写、自动推断和静默降级均不得成为长期或隐式路径。
- 现阶段残留的 bridge 只能作为明确标注、可删除的内部施工措施；不得新增依赖，不得把 bridge 包装成兼容 API。每个 bridge 必须在计划中绑定删除 checkpoint。
- 任一阶段若只能依赖兼容 shim 才能继续，必须先暂停编码并修正 root-slot / typed-slot contract 与调用点；不得扩大兼容面或以兼容层掩盖架构缺口。
- 最终验收包含负向检查：代码、反射 schema、序列化读取分支、示例、测试 fixture 和公开头文件中均不存在旧布局真值或 legacy 兼容入口。

### Edge 查询与类型识别约束

- `UISlot` 的 ownership 仍属于 parent；为避免 child 每次通过 parent 扫描查询 edge，`UIElement` 持有一个 non-owning `UISlot*` 回指。插入 edge 后立即绑定，detach/reparent 销毁旧 edge 前先清空，创建新 edge 后重新绑定；same-parent reorder 只移动 `unique_ptr`，不得重建或清空 slot。
- `getSlot()` 必须是 O(1) 回指；`getSlotForChild()` 仅作为 parent 侧按 child 查询和诊断接口保留。slot 指针不得跨 detach/reparent 缓存到业务对象。
- slot 类型保持开放扩展，不引入 engine-owned `ESlotKind` 枚举；typed slot 查询只在 slot 边界使用 `UISlot::as<T>()`，避免把用户扩展绑定到核心枚举。widget 类型的 RTTI 清理另列 checkpoint，不与 edge ownership 混杂。

### Checkpoint 提交政策

- 一个 checkpoint 必须对应一个完整、可运行、可验证的架构目标，至少同时包含实现、测试和计划/进度映射；禁止以单行修复、占位、纯文档或重复拆分制造表面进度。
- 当前历史中已有若干过细的布局提交；后续不再新增同类碎片。发布/合并前应将本计划相关提交整理为语义 checkpoint；在未获明确授权前不直接改写共享分支历史。

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

统一布局流程：parent constraints -> layout.measure(child, slot) -> layout.arrange(parent, rect, slots) -> child.layoutAssigned(rect) -> paint / hit-test / snapshot。

### 1.1 各 layout 的默认 slot 语义（不传任何 slot 参数）

默认值只表达“该 parent layout 最自然、最安全的布局语义”，不能偷偷替调用方补强意图，也不能退化成 silent 0x0：

- UICanvasLayout -> UICanvasSlot
  - 默认：anchorMin=anchorMax={0,0}、offset=0、insets=0、pivot=0、alignment=Left/Top
  - 尺寸模式默认：widthSizeMode=Auto、heightSizeMode=Auto
  - 语义：child 以左上角为锚点，按自身 desired/intrinsic size 显示；显式 fill/size/anchor/... 才表达更强布局意图。
  - 禁止再把 Fixed/Fixed + 0x0 作为 canvas default；guardrail 只负责诊断错误调用，不承担长期默认语义。

- UIBoxLayout -> UIBoxSlot
  - 默认：sizeRule=Auto、weight=0、margin=0、crossAlignment=Stretch、参与布局
  - 语义：child 在主轴按 desired size 排布，在交叉轴默认拉伸到父内容区；显式 fill/grow/preferredSize/margin/... 才覆盖。

- UISingleChildLayout -> 复用 UIOverlaySlot
  - 默认：hAlign=Fill、vAlign=Fill、padding=0、preferredSize=0
  - 语义：single-content child 默认填满宿主内容区；需要居中/贴边/保持 desired size 时再显式改 slot。

- UIOverlayLayout -> UIOverlaySlot
  - 默认：hAlign=Fill、vAlign=Fill、padding=0、preferredSize=0
  - 语义：overlay child 默认占满父 rect；角标、浮层、角落按钮等通过显式对齐/内边距表达。

- UIScrollLayout -> 内容 child 复用 single-child / overlay slot
  - 默认：Fill/Fill
  - 语义：viewport 内的 content host 默认占满 viewport；真正内容尺寸仍来自 child desired/intrinsic，而不是靠 slot 默认缩成左上角一块。

- UISplitLayout -> 各 pane child 复用 single-child / overlay slot
  - 默认：Fill/Fill
  - 语义：split 分配完 pane rect 后，每个 pane child 默认填满自己的分区。

- UITableLayout -> UITableSlot
  - 默认：row=0、column=0
  - 语义：仅作为构造期占位；table 业务不应依赖该默认 cell。后续 debug/validation 应对重复占用同一默认 cell 给出诊断，而不是把它当自然布局语义。

总原则：

- 默认 slot 负责“无参时仍有合理几何”；
- 显式 typed slot 负责“强布局意图”；
- guardrail 负责“发现看起来是误用的调用路径并报错”，而不是偷偷替用户完成布局设计。

## 2. UIElement authored geometry 删除清单

从 UIElement、反射、序列化、DSL、Designer 和所有调用点中删除：

~~~text
_anchorMin / _anchorMax / _position / _size / _minSize / _maxSize / _bAutoSize
setPosition / setSize / getPosition / getSize / computeAnchorRect
fillParent / setAnchors / fillWidth / fillHeight
reportStretchAnchorsIgnored
~~~

_layoutRect、setLayoutRect()、layoutAssigned() 保留，但仅作为布局结果通道。

当前进度：`_position/_size/_bAuthoredPosition/_bAuthoredSize` 及其 getter、`_anchorMin/_anchorMax`、`_bAutoSize` 与 no-arg layer attach 已完成删除；floating window 已改为 attach 生命周期写入 parent-owned slot。Declarative builder 仍暂时依赖 pending bridge，因为仓库中大量 `builder.release()` 后外部 attach 的调用点尚未全量迁移；必须先完成这些调用点迁移，才能安全删除该 bridge。

## 3. 目标 DSL 与内部模型

### 3.1 Public DSL：slot-first，而不是 ui::layout() 总入口

public authoring 以显式 typed slot 为唯一入口：不同 parent 直接使用自己的 xxxSlot()；ui::layout()、unified spec 和 modifier attachment 已物理删除，不保留兼容入口。

~~~cpp
ui::canvas("Root")
    .child(
        ui::text("Title").setText("Hello"),
        ui::canvasSlot().fill().insets(FMargin{8, 8, 8, 0}).height(28))
    .child(
        ui::image("Icon"),
        ui::canvasSlot().anchor({1, 1}, {1, 1}).alignment({1, 1}).insets(FMargin{0, 0, 8, 8}).size({120, 32}));

ui::column("Root")
    .child(ui::text("Title"))
    .child(ui::panel("Body"), ui::boxSlot().fill().margin(FMargin::all(8)));
~~~

原则：

- 布局意图就是 typed slot；
- slot 类型必须一眼可见；
- parent-specific contract 由 slot 类型本身表达，而不是由一个越来越大的万能 builder 再去做 capability 推断；
- .child(node) 只表示该 parent 的 default slot；.child(node, xxxSlot()) 表示显式 edge intent。

### 3.2 API 设计约束

- ui::canvasSlot()：anchor / offset / insets / alignment / pivot / widthSizeMode / heightSizeMode / preferredSize / minSize / maxSize / fixed size。
- ui::boxSlot()：fill/grow、margin、crossAlign、preferredSize、min/max、layout participation。
- ui::overlaySlot()：fill / align / inset(padding) / preferredSize。
- ui::tableSlot()：row / column / span / cell-specific contract。

- 只有语义真的跨 slot 完全一致的 helper 才允许抽公共薄层；不能再以“统一入口”为目标，把 typed slot 契约压扁成单一 ui::layout()。
- public 头文件中不再保留 ui::layout()、FUILayoutSpec 或 unified attachment operator。

Canvas 是 layout 类型，不以 panelSlot 命名；新增 UICanvasLayout + UICanvasSlot。所有 authored edge 字段都直接落到对应 typed slot，由宿主 layout 消费，不能写回 child。

SingleChild 不再是独立 public slot 类型，而是明确复用 UIOverlaySlot 的 public authoring：single-child host 一律用 ui::overlaySlot()。

### 3.3 编译期与可读性目标

- typed slot 仍是 runtime/layout 的唯一布局数据模型，public DSL 直接暴露它，而不是再包一层统一 spec。
- LSP/编译器的错误应该主要来自 slot 类型不匹配，而不是 capability 组合失败后再回头解释 ui::layout() 当前是哪个 parent 的语义。
- 人眼应该在 child(node, xxxSlot()) 这一行就能看出布局归属；不能把关键布局信息藏到过深链式层级里。
- 不保留 ui::layout() >> child 的 legacy 兼容分支；如果决定切到 slot-first，就按一步到位迁移，删除旧主路径。

### 3.4 slot-first 类型流

~~~text
ui::canvasSlot()   → UICanvasSlot authoring builder
ui::boxSlot()      → UIBoxSlot authoring builder
ui::overlaySlot()  → UIOverlaySlot authoring builder
ui::tableSlot()    → UITableSlot authoring builder

parent.child(child, slotBuilder)     → typed UISlot + UIElementRef
parent.child(child)                  → parent default slot
~~~

错误配置在重载解析/模板约束处失败：

~~~cpp
ui::column("Root")
    .child(ui::icon("Icon"), ui::canvasSlot()); // 编译错误

ui::canvas("Root")
    .child(ui::panel("Cell"), ui::tableSlot()); // 编译错误
~~~

该语法直接把 public DSL 和 runtime typed slot 对齐，不再保留一层独立 layout-spec 主路径。

### 3.5 不同 layout 的案例与类型隔离（历史草案，已由 typed slot 实现取代）

以下内容仅保留为历史设计记录；当前 public DSL 以 child(node, typedSlotBuilder) 为准。

#### Canvas

~~~cpp
ui::canvas("Root")
    .child(ui::text("Title")
        .layout(ui::layout()
            .fill()
            .insets(FMargin{8, 8, 8, 0})
            .height(28)))
    .child(ui::image("Icon")
        .layout(ui::layout()
            .anchor({1, 1}, {1, 1})
            .alignment({1, 1})
            .insets(FMargin{0, 0, 8, 8})
            .size({120, 32})));
~~~

Canvas capability 允许：fill、anchor、offset、insets、alignment/pivot、width/height、preferred/min/max。

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

### 当前审计结论（2026-08-31）

- CP3 的 DSL/capability/canvas 主体能力已完成，`canvasSlot()`、旧 panel-slot builder 与旧 canvas-panel 命名均已删除。
- CP5 的 layout-host、root/layer、popup、designer、Workbench、Editor 与测试调用点已完成迁移；运行期不再保留 child-owned self-positioned fallback。
- root/layer 路径需要分两步收口：先把 `TreeRoot` 自身改为正式 canvas host、用 root->layer slot 表达 system layer fill；再迁移 layer 下业务 child 的默认 attach 语义。不能直接把 layer 升成 canvas host，否则会把 `attachToLayer()` 现有几何语义静默打坏。
- 进一步审计结论：在 layer 尚未成为 typed layout host 之前，**不能**先给 `attachToLayer()` 暴露统一 `layout spec` 入口。否则 API 会看起来统一，但 layer->child edge 仍只能生成 base slot，intent 无法被正确消费，等于制造新的“能写不能兑现”的过渡层。
- 新审计结论：layer 已是 canvas host 后，运行时不得再通过 `UIElement::setPosition/setSize` 隐式桥接；layer child 必须在 attach 时传入显式 `FCanvasSlotArgs`，后续直接更新该 edge。
- 当前实现不存在 `applyLayoutSpecToSlot(box)` 回写 child geometry 的路径；Box 尺寸意图统一落在 parent-owned `UIBoxSlot`。
- 当前实现曾存在另一条明确偏差：capability 编译期约束已覆盖 single-child / overlay 宿主，但 unified `ui::layout()` 的运行时 slot 消费未完全覆盖，导致“能编译但 intent 可能静默丢失”。该问题现已纠正并补测试验证。
- 纠偏结论：single-child 不是一种独立 slot 数据模型；Button/SizeBox/Split/Scroll 等宿主复用 `UIOverlaySlot` 的 align/preferred-size edge 数据，`UISingleChildLayout` 只保留父级 measure/arrange 策略。这样用户扩展新 slot 类型无需修改核心枚举。
- 新审计结论：`PopupOverlay` 需要的是**独立 full-screen host 语义**，但不必为此再发明一套平行 slot 类型。更合理的收口是让 popup 自己拥有 shield/full-screen contract，同时复用通用 `UICanvasSlot` 承载 content edge；Menu / Dialog 通过覆盖 content slot args 表达“固定尺寸定位”与“居中 Auto 尺寸”。
- 审计补充：`CP5` 末尾不应把“测试里仍出现 `setPosition/setSize`”本身视为误差。剩余大量调用其实是在定义 absolute 几何、layer-child attach 语义或测试夹具初始条件；真正需要清理的是那些**runtime 已完全由 parent-owned slot 决定**、child 再写 `size/anchor/position` 只剩历史噪声的 dead write。
- 新审计结论：`reparent` 不能把“edge 属于 parent->child”误解成“同父重排时也应该销毁 edge”。跨父迁移当然要重建 slot，但 `reparentBefore/After` 在**同一个 parent** 下只是调整顺序，必须移动原 slot，而不是重建默认 slot，否则 box/canvas/overlay/table 的 edge state 会在 reorder 时蒸发。
- 新审计结论：`Grid/Table` 当前还**没有** declarative builder 正式暴露 unified `ui::layout()` 附着面，因此眼前更大的风险不是 runtime 掉 intent，而是 capability 常量先把未来承诺说宽了。`UITableSlot` 目前只有 `cell(row,col)` 事实契约，在它真正长出 align/margin/sizeMode 等 slot 数据前，grid capability 应保持 `cell-only`，避免再次制造“声明先于兑现”的假统一。
- 新审计结论：`[]` 应继续只作为 child attach 的语法糖，`TUILayoutAttachment` 只是 builder 层临时运输 `spec + child` 的壳，不应变成运行时 ownership 模型。正确的收口不是“让 widget 持有 slot”，而是让 **parent 在创建 edge 时立即初始化 slot**，从而把 declarative `[]`、`child(slotArgs)`、`ui::build(..., spec)`、`attachLayout(...)` 收到同一条 parent-owned slot 初始化路径。
- 当前只剩 CP7 最终门禁：按验收矩阵执行 closure、宿主构建、snapshot/offscreen parity 与全仓负向审计。

### CP1 — 冻结新布局协议
- 新增 UIConstraints、measure/arrange contract、size mode 和布局结果定义。
- 明确各 layout 的 default slot 与 measure 优先级。
- 冻结 UILayoutIntent modifier 词汇、组合规则和 parent 解析规则。
- 增加 layout contract 文档和纯 CPU 几何测试。

### CP2 — 移除 UIElement authored geometry（已完成）
- 删除字段、setter、反射字段、序列化字段和 computeAnchorRect。
- 将 leaf/container desired-size 逻辑迁移到 measureContent / layout。
- 删除 reportStretchAnchorsIgnored 及相关诊断字段。

### CP3 — 新增 UICanvasLayout / UICanvasSlot（已完成）
- 实现 `offset(glm::vec2)`、四边 `insets(FMargin)`、anchor span、alignment/pivot、preferred/min/max 和 Auto/Fixed/Stretch 语义。
- Panel 改为 Canvas layout host。
- 不创建绑定视觉控件的 FCanvasPanelSlot 命名。
- public DSL 使用 canvasSlot().anchor()/offset()/insets()/fill()。

### CP4 — 统一所有 layout host 的 slot 消费（已完成）
- Box、Overlay、SingleChild、Split、Scroll、Grid、Canvas 全部通过 typed slot arrange。
- child(node, typedSlotBuilder) 在 materialization 时直接初始化 typed slot；不写 child geometry。
- reparent/detach 时销毁并重建 slot。

### CP5 — 全仓 API / runtime 迁移（已完成）
- 迁移所有旧 geometry setter/getter；布局修改只能显式读取并更新 parent-owned slot，builder 的 `.setSize/.setPosition` 仅作为构造期 edge intent。
- 迁移 WidgetTree、DockSpace、Popup、UIDesigner、Workbench、Editor 和测试中的直接字段写入。
- PopupOverlay 单独定义 full-screen host 与 content slot，不假设等同于普通 Panel。

### CP6 — 反射、文档和资源格式迁移（已完成）
- 删除旧 JSON 字段和旧 schema。
- 更新 UIDocument、Designer inspector、脚本绑定和快照 dump。
- 旧文档不做兼容读取；若需要迁移工具，只做一次性离线转换器，不进入 runtime。

### CP7 — 验证
- 编译期断言：child builder 不存在任何 child-owned geometry modifier；parent child() 只接受匹配 host SlotArgs 的 typed slot。
- DSL 可读性样例覆盖：layout spec 与 widget 平行可见，fill/grow/anchor 可继续链式调整。
- 几何测试：intrinsic measure、constraints、Canvas `offset`/`insets`、anchor span、alignment、min/max、reparent。
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
- Canvas、Box、Overlay、Grid slot 不可混用；single-child layout 使用 Overlay slot edge，不再存在独立 SingleChild slot。
- reparent 后不会泄漏旧 parent 的布局状态。
- measure → arrange → layoutRect → snapshot 是唯一几何链路。
- closure tests、GUIWorkbench、GameEditor、headless 和 macOS convergence gate 全部通过。
