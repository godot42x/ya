# GUI Framework / Editor / Game UI 重构计划

## 目标

让同一套 retain-mode GUI framework 同时承载独立 GUI app、Game Runtime UI 和未来移除 ImGui 后的 Game Editor。目标参考 Slate/UMG：WidgetTree、布局、输入、focus、popup、drag/drop、snapshot/compose 由 framework 提供；Game UI 的外观由 project/application authored properties 控制，不强制依赖 UITheme；UITheme 保留为 Editor、tooling、standalone app 的可选默认样式机制。

UI document（独立文件格式）暂不做；Scene 的 Game UI entry 目前只承载 inline document。重构主线先收口 "GUI 替换 Editor UI"：同一套 framework + DSL 先吃自己的狗粮（Editor），再支撑 Game UI，类似 UMG 基于 Slate、Godot Node2D 基于 Control 树的关系。

## 架构决策

Framework 提供 retained WidgetTree、UIElement、Layout、Input、UIFrameBuilder、Brush、draw primitives、visual state、typed builders 和 invalidation contract；静态 DSL 作为强类型组合层建立在其上；Editor 提供 EditorTheme/semantic tokens；Project/Game 提供 authored colors、textures、brushes、documents 和动态 bindings。

外观解析优先级固定为：explicit runtime/authored property > widget-local authored default > optional scoped/theme style > framework neutral fallback。Game UI 不要求挂载 theme。

补充原则（2026-08-27）：`UICompoundWidget` 只是 native retained composition primitive，不是唯一 declarative/component model。未来 React-like / HTML-CSS-JS / script/document adapter 应建立在 runtime kernel 之上，而不是反过来让 kernel 绑定某一种 authoring 方式。

补充原则（2026-08-27）：runtime 架构按四层收口：

1. UI Runtime Kernel：`UIElement`、`WidgetTree`、layout / paint / input / focus / dragdrop / invalidate、snapshot / host bridge。
2. Native Retained Widget Layer：leaf / compound widget、原生 C++ 控件、Slate 风格静态 DSL。
3. Declarative Adapter Layer：React-like、HTML-CSS-JS、editor-authored document、script/project schema。
4. Product Layer：standalone app、game runtime UI、editor。

补充原则（2026-08-27）：`Reactive` 可以保留，但不应继续被视为 `Runtime/Widgets` kernel 本体。kernel 必须允许多种状态来源并存：imperative setter、widget transient state、behavior state、reactive binding、future adapter patch。

补充原则（2026-08-27）：behavior 是一级横切组合层。drag/drop、tooltip、shortcut、accessibility、editor interaction affordance 等能力应收敛到 behavior，而不是继续膨胀基础控件。

补充原则（2026-08-27）：`Declarative/Construct.h` 只是 native DSL surface，不是未来所有声明式 authoring 的统一底座；上层 adapter 可以投影到 runtime kernel，而不必共享 builder API。

补充原则（2026-08-26）：`UICompoundWidget` 与新底层的关系必须按迁移桥接契约收口：

1. `UICompoundWidget` 明确属于 Native Retained Widget Layer，不属于 Runtime Kernel。
2. Runtime Kernel 只负责 `UIElement` / `WidgetTree` / layout / input / focus / invalidate / snapshot / host bridge，不承载 editor/game 复合控件语义。
3. 横切能力优先进入 `UIBehavior`，而不是继续塞进 `UICompoundWidget` 基类，更不能反向污染 `UIButton` / `UIText` 等 leaf widget。
4. `UICompoundWidget` 只承担局部 composition root 职责：一次性 `construct()`、内部 retained 子树组装、局部状态/生命周期、必要时的复杂输入策略或自定义 paint；不引入第二套 tree、reconciler 或 adapter host。
5. 现有 editor/game 复合控件迁移时必须三分：无局部状态的收敛为普通 builder helper；有局部 retained 状态/生命周期的收敛为 `UICompoundWidget`；drag/drop、tooltip、shortcut、editor affordance 等横切能力收敛为 `UIBehavior`。
6. future React-like / HTML-CSS-JS / script/document adapter 直接投影到 Runtime Kernel，不得把 `UICompoundWidget` 作为唯一 component 宿主或 authoring 底座。

补充现状（2026-08-26）：仓库当前**还没有任何实际控件继承 `UICompoundWidget`**。现阶段只有 `UICompoundWidget` 基类与 `ui::compound<T>()` builder 入口落地；业务层仍主要分布在两类结构里：

- specialized native retained control：如 `UITreeView` / `UITableGrid` / `UIDockSpace` / `UIMenuBar`，直接继承 `UIElement`，自己承担数据投影、命中、paint、局部子树或 slot 编排；
- demo / editor ad-hoc widget：如 `FDemoDragItem` / `FDemoDropZone`，先用临时 `UIElement` 包装交互，再挂进 DSL 壳。

因此 G4.3 的目标不是“把现有复杂控件一把梭全部改成 `UICompoundWidget`”，而是先把它们分流到正确归宿。

### G4.3 第一版迁移判定表（2026-08-26）

| 现有对象 | 当前形态 | 目标归宿 | 说明 |
|---|---|---|---|
| `FDemoDragItem` / `FDemoDropZone` | GUIWorkbench demo 专用 `UIElement` | 普通 retained widget / builder 壳 + `UIBehavior` | 它们的长期价值是 drag source / drop target 交互，不是保留 demo 专用 widget 类型。 |
| `UISelectableRow` | leaf-like `UIElement` primitive | 保留 leaf primitive，逐步剥离横切能力到 `UIBehavior` | 行的职责应保持在 selection / activation / presentation surface；drag/drop 高亮、editor affordance 不应继续膨胀。 |
| `UITreeView` / `UITableGrid` | specialized native retained control | 继续保留 specialized control，不强制改写成 `UICompoundWidget` | 它们自己拥有数据投影、hit test、flatten/layout、paint 与选择状态；本质上不是“由现成控件简单拼装”的局部 composition root。 |
| `UIMenuBar` / `UIMenu` / `UIDialog` | retained control + popup/menu 生命周期 | 先保留 native control；复用交互逐步外提到 `UIBehavior` | 菜单/弹出有明确路由与生命周期要求，短期不应用 `UICompoundWidget` 包一层再套回基础控件。 |
| `UIDockSpace` / `UIDockFloatingWindow` / `UIDockFloatingHost` | projection-heavy retained control | 继续保留 specialized control，不强制迁到 `UICompoundWidget` | Dock 是 workspace/model → 投影视图树的复杂控制器，问题重点在 behavior 与 projection seam，不在 compound 化。 |
| Workbench / Editor 中纯组合 panel、toolbar group、inspector section | 多为 DSL + live attach/detach | 优先收敛为 builder helper；有局部状态时再升到 `UICompoundWidget` | 没有局部生命周期和自定义输入时，不需要为了“组件化”先引入 compound 基类。 |
| future editor composite widget（如带局部状态的 property section、search panel、tool palette） | 尚未统一 | `UICompoundWidget` 优先归宿 | 这类对象天然符合“一次 construct + retained 子树 + 局部状态/生命周期”的 compound 定位。 |

### G4.3a 逐文件迁移清单（2026-08-26）

1. `Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp`
   - `FDemoDragItem` / `FDemoDropZone`：列为第一批行为化目标；后续应拆成“展示 widget + drag/drop behavior”，不再作为长期 demo widget 类型保留。
   - `FVectorDemoCanvas`：保留为 demo/raw retained 对照，不纳入 `UICompoundWidget` 迁移线。它体现的是自定义 paint primitive，不是局部组合根。
   - 页面中的纯组合区块（toolbar row、inspector section、property group）：优先继续收敛为 builder helper；只在引入局部 retained 状态/生命周期后才升级为 `UICompoundWidget`。
2. `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/SelectableRow.h/.cpp`
   - 保持 leaf primitive 定位；下一刀优先审视 drag/drop 高亮、draggable payload、editor affordance，能外提的先外提到 `UIBehavior`。
   - 不把 row presenter/model 责任反向吸回控件本体。
3. `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/TreeView.h/.cpp`
   - 继续视为 specialized native retained control；不进入第一批 compound 化。
   - 下一步关注点是把可复用的 reorder / drop affordance 从 widget 自身实现里抽出 behavior seam，而不是把 flatten / paint / hit-test 改写成 compound 组装。
4. `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/TableGrid.h/.cpp`
   - 继续视为 specialized native retained control；不进入第一批 compound 化。
   - 如后续出现复用型表格交互（selection helpers、editor affordance、drag handle），优先评估 behavior seam。
5. `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/MenuBar.h/.cpp`、`Menu.h/.cpp`、`Dialog.h/.cpp`
   - 继续保留 native control；菜单、弹出、dismiss、focus 清理等生命周期仍由 retained control + tree route 明确负责。
   - 若有可复用快捷键/tooltip/search/filter 交互，再单独抽 behavior，不先做 compound 包装。
6. `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.h/.cpp`、`DockFloatingWindow.h/.cpp`、`DockFloatingHost.h/.cpp`
   - 明确排除出第一批 `UICompoundWidget` 迁移对象。
   - Dock 的重心仍是 workspace/model projection、drop preview、floating 生命周期与 route/popup seam；这里需要的是 projection/behavior 收口，不是 compound 化。
7. `Engine/Source/Framework/GUI/Runtime/Widgets/include/GUI/Widgets/CompoundWidget.h` 与 `Runtime/Declarative/include/GUI/Declarative/Construct.h`
   - 当前只提供机制和入口，不代表现有复杂控件都应被倒入这条抽象。
   - 下一步应优先拿未来 editor composite widget 试点，而不是回头把 specialized control 大规模改写成 `UICompoundWidget`。

不在本计划中恢复 ImGui、强制 Game UI 使用 UITheme、一次性重写全部控件，或把 project-specific tokens 放入 framework。

### 默认构建路径：DSL 直接物化 live widget（2026-08-26）

**直接回答：静态 UI 不需要 `UIDescription`，也不需要 Description → apply → widget 这一层转发。**

Slate 的声明式写法 `SNew(SButton).Text("Save")` 在 `Construct()` 之后就是活的 `SWidget`。它的 `FArguments` 是**构造期一次性参数**，用完即弃，不是一棵可反复 diff 的快照树。我们当前的 typed builder 却把 `UIDescription` 当成唯一产物，于是连 Feature Gallery 的静态页也走：

```
builder → UIDescription（std::any payload + 双份 children）→ Reconciler.create/apply → UIElement
```

这是把 React 的 VDOM 误当成 Slate 的构建语法。静态树不再生效，转发层没有收益，却逼出 apply hook、typeId 分发、`requestRender`、强制 `_bAutoSize` 等一串补丁。

#### 三条路径，不许混

| 路径 | 产物 | 何时用 | 之后怎么更新 |
|---|---|---|---|
| **静态 DSL（默认）** | live `UIElement` 子树 | 结构在构建时已知：shell、Editor panel、菜单、Gallery 页 | 值走 `Reactive<T>`；已知结构变化走 live API（`attach`/`detach`/`setVisible`/Switcher） |
| **Document / script 边界** | 短生命周期 typed spec | JSON / inline document / Lua 不能直接 `new UIButton` | **实例化一次**成 live 树，然后与静态路径汇合；不每帧 rebuild |
| **变长 keyed 集合（可选）** | 列表/repeater **控件内部**的 child 生命周期 | 背包格子、动态 tab 这类长度未知的列表 | 数据源是 `ReactiveList`；行的创建/复用/销毁由该控件拥有，对标 `SListView`。不是整页 Description 重跑 |

`UIDescription` 只允许作为第二条路径的边界对象（以及第三条路径里列表控件的内部实现细节）。它**不是** GUI 系统的基础抽象，也不是静态 DSL 的返回类型。

#### 硬规则（防止再走错）

1. **默认 API 必须能直接建树。** 形态是先组 builder，再 `UIElementRef root = ui::build(tree, parent, std::move(page));`。不要把整棵 DSL 包进 `ui::build(...)`。Builder 在表达式求值结束时物化 widget；callback 在 Construct 时绑到 live `UIButton&`，不经过 Description 中转。
2. **禁止把 Description 树当静态页的宿主协议。** Workbench / Editor 的 DSL 页不得再以 `FPageRenderFn → UIRenderController → reconcile` 为默认。现有这条路是过渡实现，下一刀切到直接构建。
3. **禁止继续给 `UIDescription` 加职责。** 不再往它里面堆 callback、dirty、auto-size 策略、每帧属性值。Apply hook 是 Description 路径的适配器，不是控件的主 API。
4. **Apply 层不是架构必需品。** 它存在只因为 Description 是 type-erased 的延迟构造规格（`typeId` + `std::any`）。直接构建路径调用的是 `UIText::setText` / `bindText` / `UIButton::_onClick = ...`，没有 apply。
5. **结构变化默认不是 reconcile。** 显示/隐藏已知面板用 visibility 或 Switcher；加一个已知子节点用 `attach`。只有「子节点集合由外部列表数据决定、需要按 key 复用行」才进入列表控件的内部 diff。不得把「任意 render() 重声明整棵树」做成应用层默认写法。
6. **`UIScreen` 不是构建单位。** 它是挂卸与输入路由（模态/zOrder）的 host 概念。一个 screen 内部的 UI 仍然是 live 子树 + 绑定，不是每帧 `render() → Description`。

#### 与上一轮「值/结构分工」的关系

值走 `Reactive<T>` 仍然成立。需要修正的是「结构变化走 `UIDescription` + reconcile」这句话：那是把 React 的全树快照 diff 说成了结构更新的唯一手段。Slate 的结构更新落在**容器 widget 自己身上**（`AddSlot`、`SListView::RequestListRefresh`、`SWidgetSwitcher`），不经过一棵与 live 树平行的 Description。

上一轮撤销「新增 invalidation owner」仍然有效：失效落点是 widget（值）或拥有变长子节点的那个容器（结构）。`requestRender` / `renderEx` 仍是反例。

#### 现有 Description / Reconciler 怎么处理

- Phase -1B 的 reconciler **保留为实验/边界实现**，契约测试继续锁住它已承诺的 keyed reuse 语义，但**停止扩展**：不接更多控件、不作为 Editor 迁移默认路径、不作为 Gallery 迁移默认路径。
- `UIDescription` 的双份 identity/children 是该路径内部的实现债；只有这条路径还活着时才修，不把它当成框架主模型去完善。
- Document/script 以后若需要 spec，优先复用「一次性 instantiate」而不是「每帧 reconcile」。G1 的 typeId 收口对 registry 工厂仍然有价值，因为它服务的是 document 实例化，不是静态 DSL。

## 当前缺口

1. FBrush 的 NinePatch/Border 目前仍降级为整图拉伸。
2. 大量控件直接暴露颜色/尺寸字段，runtime 修改没有统一的 changed-only setter/invalidation contract。
3. CheckBox、ComboBox、Slider、TextField、TreeView、TableGrid、Dialog、Menu、InputExtras 缺少统一 visual state contract。
4. DSL（Declarative）与 Game UI document 体系是两套平行系统：闭集 EWidgetKind vs UITypeRegistry typeId、typed static description vs dynamic/script schema，尚未收口；不能用 JSON fields 侵入 retained/static 热路径。
5. rounded rect、border、opacity、gradient、shadow、transform 等视觉原语不完整。
6. Editor 未来迁移需要 editor-grade dense controls、property editing、validation visual states。

## Phase -1：Retained 基础、静态强类型 DSL 与可选动态 Reconciler

第一阶段先稳定 retained runtime 和 Slate/EUI-NEO 风格的静态强类型 builder/DSL。Builder 的默认产物是 live WidgetTree，不是 Description。动态 component、脚本 UI、document instantiate 和 keyed 列表控件属于其上的可选扩展。目标不是 XML/CSS 外部语法，也不是把 JSON/反射字段做成核心属性载体。

基础形态：typed static builder/DSL → 直接物化 retained WidgetTree → layout/input/snapshot → compose。

Document/script 形态：外部 schema → 一次性 typed spec → registry 工厂实例化 → 同一棵 retained WidgetTree。

变长列表形态：`ReactiveList` → 列表/repeater 控件拥有行的创建与复用（内部可有 keyed diff，但不暴露为应用层 Description 树）。

示意 API：`auto page = ui::column("settings").children(...); ui::build(tree, parent, std::move(page));` 是基础静态 DSL。不存在「函数每次重新返回 builder / Description」的默认循环；值更新走绑定。

### 渐进式实施原则

- 不预先实现完整数据绑定、完整组件生命周期或完整脚本 DSL；每一层只实现当前垂直切片所需的最小能力。
- 每个阶段先写契约测试，再写实现，再接入一个真实控件，最后跑 windowed/offscreen/headless 回归。
- 旧 retain API 在迁移期继续可用；DSL 只能作为新增路径，不能先破坏 WidgetTree。
- 每一步都必须能独立编译、运行和回滚；不允许跨多个 Phase 的大批量重写。
- 任何新抽象必须先有一个真实 consumer 和测试，禁止为了未来可能需求提前建复杂框架。

### 必须先冻结的契约

- Widget identity：区分 type、stable key、display name；key 用于声明式重建和状态保留，不能只依赖 name。
- Static construction：每种控件的 builder/args/slot 是强类型；静态 builder 不依赖 component、script、JSON、反射或 UITheme。
- Ownership/lifecycle：声明式节点如何 mount、update、detach、reparent；哪些状态 keep-alive，哪些随节点销毁。
- Property mutation：颜色、brush、布局、文本、selection 等属性的 setter、事务和 invalidation 规则。
- Data flow：动态层 Reactive/observable 的读取依赖、写入通知、双向绑定、computed 派生值和 batch 更新；静态层不携带依赖追踪。
- Resource reference：纹理、字体、材质、图标的引用格式、resolve 时机、缺失 fallback 和 queue-submit 保活。
- Event contract：事件 callback 的生命周期、capture/focus、popup/modal、取消和异步回调安全边界。
- Serialization boundary：哪些字段属于 scene inline document authoring，哪些属于动态/script schema，哪些属于运行时状态，哪些不能序列化；反射只在边界适配。
- Layout vocabulary：Overlay、Grid、Wrap/Flow、Canvas/absolute、List virtualization 是否进入第一版 DSL。

### DSL 设计边界

静态第一版吸收 Slate/EUI-NEO 的控件专属链式 builder、typed children、slot/layout props，**并在 Construct 时物化 live widget**。不吸收 React 的「render function 返回 VDOM 快照」作为默认数据流。保留现有 retained tree 和 immutable snapshot。

公共 API 采用控件专属 builder/factory：Text/Button/TextField/Container 各自暴露自己的语义字段，直接写到对应 `UIElement` 子类。不要让一个通用 Description 承载未来所有控件的属性，也不要让 builder 的唯一出口是 `operator UIDescription()`。

第一版不承诺 XML/CSS 语法、Virtual DOM 全量替代 WidgetTree、自动双向数据绑定、任意 lambda 捕获 live widget 裸指针，或在 command recording 期执行 component/render function。第一版也不承诺把 Description+reconcile 做成应用层主 API。

### Phase -1 实施顺序

#### -1A：契约冻结（只读设计与测试，不迁移业务）

1. 定义 retained typed args / builder / slot 模型和 stable key 规则。
2. 定义静态 builder 分层：公共 node contract、控件专属 builder、共享属性注入点。
3. 定义 builder props：layout、appearance、text/value、event、children、visibility。
4. 定义 mount/update/remove/reorder/detach 的生命周期和 callback 安全边界。
5. 定义静态配置、动态外部 state、widget transient state、derived state 的边界。

退出条件：设计文档、重复 key/缺失 key 诊断、最小契约测试全部明确；没有未决的 identity/lifecycle 语义问题。

#### -1B：最小 Reconciler（不接复杂数据绑定）

1. 实现 same-key reuse、insert、remove、reorder、detach。
2. 接入现有 WidgetTree，但只支持 Panel、Text、Button、Column/Row。
3. 增加 mount/update/remove/reorder 的 WidgetTreeDump 和 snapshot 测试。

退出条件：同一 description 重复应用不会重复创建同 key widget；删除和重排不会留下 parent/focus/capture 残留。

#### -1C：第一个有状态控件

1. 接入 TextField 或 ScrollViewport（二选一，优先 TextField）。
2. 验证 focus、caret 或 scroll offset 在 same-key update/reorder 后保留。
3. 验证条件子树移除时 transient state 和事件 capture 正确清理。

退出条件：状态保留和状态清理都有自动化断言，且 windowed/offscreen snapshot 一致。

#### -1D：最小动态数据流（建立在静态 DSL 之上）

1. render function 读取只读 state/props。
2. event callback 写回外部 model，由下一次 render 产生 description 更新。
3. Reactive 只接入已存在的依赖追踪和 dirty invalidation，不在此阶段实现完整 computed/bidirectional binding。
4. 增加 batch update，保证一次业务事务不会重复 reconcile 同一子树。

退出条件：state 变化只更新受影响的 props/子树；command recording 期不读取 live model；重复相同值不会产生多余 dirty transition。

### Phase -1 验收

- 静态 builder 可以脱离 `UIRenderController` / `UIDescription` 直接构建基础 WidgetTree。
- 值变化（绑定）不重建 widget、不走 apply/reconcile。
- 既有 reconciler 契约测试继续通过，但新的 Gallery/Editor 页默认不依赖它。
- DSL 不依赖 UITheme；没有 theme 也能 authored appearance。
- 至少完成一个 Button/TextField prototype（直接构建 + 绑定），并用 snapshot/scenario 验证 mount、事件、teardown。
- 在 Phase -1 完成前，不开始 Editor 全量迁移，不扩展大规模控件样式，不承诺外部 XML/脚本语法稳定。

### Phase -1 禁止事项

- 不实现 XML/CSS parser。
- 不实现完整 Virtual DOM 或全树替换。
- 不把 `UIDescription` 做成静态 DSL 的默认返回类型，不把 reconcile 做成 Gallery/Editor 的默认更新循环。
- 不把 DSL 节点以业务长期持有裸 `UIElement*` 的方式泄漏出 Construct/事件回调；事件期 `UIButton&` 参数可以，跨帧保存不可以。
- 不在同一阶段同时迁移 TreeView、DockSpace、Inspector 等复杂 Editor 控件。
- 不以“demo 能显示”为完成标准，必须通过生命周期、状态、snapshot 和 teardown 测试。

## DSL 在 Game UI 中的使用设计（案例驱动）

（yaui 文件格式已移除；本节以 UMG / Godot Control 树对照的具体案例定义 DSL 的应用层用法。）

### 概念映射

| UMG / Godot | YA 对应 | 说明 |
|---|---|---|
| UserWidget (WBP) / 场景里的 Control 根 | 一个 C++ 对象拥有 live 子树（不必是 `UIScreen::render()`） | 构建一次，之后靠绑定和 live API 更新 |
| CanvasPanel + 锚点 | `Panel`/`Container` + position/size（anchor 待 Phase 4） | 左上原点 |
| AddToViewport + ZOrder / CanvasLayer | `ScreenStack::push(zOrder)` | 栈顶决定输入归属；screen 是挂卸单位，不是每帧 render 单位 |
| Blueprint Property Binding / NativeTick 更新 | `Reactive<T>::set()` → 依赖 widget 失效 | 不经过 Description，不 `invalidate()` 整页 |
| NamedSlot / WidgetSwitcher | live slot 容器 + Switcher/visibility | document 骨架实例化一次，代码填 live 子树 |
| FocusPath / DirPad 导航 | EWidgetFocusPolicy 文档序焦点链 | gamepad 导航需新增（见缺口） |
| UMG Preview / Godot 编辑器 | UIDesignerPanel 编辑 **同一棵** live WidgetTree | 不是 reconciler 驱动的第二份树 |

### 现状盘点：两套平行系统

DSL 侧（`Framework/GUI/Runtime/Declarative/`，ya::ui）：
- `UIDescription` + 闭集 `EWidgetKind`（Column/Row/Panel/Text/Button/TextField）。
- 控件专属 builder（`ui::column("key").child(...)` / `.children(...)`）；动态层额外提供 `.compose(...)` / `.when(cond, ...)`。
- `UIReconciler`：keyed reconcile（same-key reuse / insert / remove / reorder / detach），身份 = (kind, stableKey)。
- `UIRenderController`：render function + invalidate + batch + flush（单向数据流）。
- 消费者：仅 DeclarativeContractTest，还没有任何宿主。

Game UI 侧（`Framework/GUI/Runtime/Widgets/` + `GameRuntime/GUI/GameUI/`，ya）：
- `UITypeRegistry`：稳定 typeId（"engine.panel"）+ factory + module lease；`UIDocument` inline 挂在 `SceneWidgetEntry` 上。
- `GameUIHost`：WidgetTree owner，viewport/presentation、event dispatch、snapshot build；`IGameUIController` 可替换挂载策略。

断点：EWidgetKind 闭集 vs typeId 平行；typed static description vs dynamic/script schema 两套边界；静态 DSL 尚未完全独立于动态 controller；DSL 也尚未接入 GameUIHost 帧循环与场景生命周期。

### 统一模型：三层收口

1. Retained runtime 收口：UIElement/WidgetTree/Layout/Input/Paint 是底层唯一运行时事实源；不依赖 JSON、脚本、反射、component 或 Description。
2. 静态 DSL 收口：每种控件保留 typed builder；**默认直接物化 live widget**。Builder 不是 Description 的语法糖。
3. 边界收口：UIDocument/JSON/脚本经 typed spec **实例化一次**进入同一棵树；变长列表由列表控件内部分配行。`UIReconciler` 不是应用层默认循环。
4. 生命周期/时机收口：screen 由 host 统一挂卸；绑定通知发生在 model `set()` 时；`buildSnapshot` 只读 live tree；command recording 只读 snapshot。静态构建不经过动态 flush。

---

### 案例A：动作游戏 HUD（动态 screen，最高频）

对应 UMG 的 WBP_HUD（血条 + 弹药 + 准星）+ 属性绑定。

按值/结构分工：HUD **构建一次**成 live 树。血条宽度、弹药文本、准星位置都是值变化，走 `Reactive<T>`，不重跑任何 render/Description。只有「RELOADING」这种可有可无的子树，用 visibility 或 Switcher 切换；不要为此引入整页 `render() + reconcile`。

```cpp
// ---- game state（普通 C++ struct / ECS 组件镜像，UI 不拥有它）----
// 值字段用 Reactive<T> 承载：set() 只标记读过它的 widget，不触发 re-render。
struct HudState {
    std::shared_ptr<Reactive<float>>       health   = std::make_shared<Reactive<float>>(1.0f);
    std::shared_ptr<Reactive<std::string>> ammoText = std::make_shared<Reactive<std::string>>("30 / 90");
    bool     bReloading = false;   // 结构开关：翻转才需要 invalidate()
    glm::vec2 crosshairPos = {400, 300};
};

// ---- screen = UMG 的 UserWidget ----
class HudScreen final : public UIScreen {          // 需新增 UIScreen
public:
    explicit HudScreen(HudState& state) : _state(state) {}

    UIDescription render() override {
        return ui::panel("hud-root")
            .child(ui::row("top-bar").setPosition({16, 12}).setSpacing(8)
                .child(ui::panel("health-bg").setSize({220, 18}).setColor({0, 0, 0, 0.5f})
                    .child(ui::panel("health-fill")               // 过渡期用 Panel 填色；
                        .setSize({220 * _state.health, 18})        // ProgressBar 列入缺口清单
                        .setColor({0.9f, 0.2f, 0.2f, 1})))
                .child(ui::text("ammo")
                    .bindText(_state.ammoText)   // 值绑定：换弹不重跑 render()
                    .setFontSize(20)))
            .child(ui::panel("crosshair")
                .setPosition(_state.crosshairPos).setSize({4, 4})
                .setColor({1, 1, 1, 0.8f}))
            .when(_state.bReloading, [] {
                return ui::text("reloading").setText("RELOADING...").setFontSize(28);
            });
    }
private:
    HudState& _state;   // 事件只写 state 再 invalidate，绝不持有 widget 指针
};
```

挂载与数据流（GameApp 侧）：

```cpp
// 场景激活时挂载（controller 策略内）：
auto hud = host.getScreenStack().push(0, HudScreen{_hudState});

// 值变化（ECS 掉血回调 / 网络回写）：不 invalidate，不重跑 render()。
void GameApp::onPlayerDamaged(float hp01) {
    _hudState.health->set(hp01);  // 只标记读过它的 widget paint-dirty
}

// 结构变化（进入/退出换弹态，增删 "reloading" 子树）：才需要 invalidate。
void GameApp::onReloadStateChanged(bool bReloading) {
    _hudState.bReloading = bReloading;
    hud->invalidate();            // 下一次 host.flushDirty() 重跑 render() + diff
}
```

UMG 的属性绑定在此对应 `Reactive<T>`：高频值更新（血条、弹药、准星）完全不进 reconcile，没有描述分配也没有 diff，失效落在读过该值的 widget 上；只有子树增删才付 render + reconcile 的成本。UMG NativeTick 式的每帧属性刷新在这里是纯绑定路径。

### 案例B：主菜单（静态布局 + 事件 + 面板切换）

对应 Godot 的 `MainMenu(Control) > VBoxContainer > [Title, BtnNewGame, BtnContinue, BtnQuit] + SettingsPanel`。

```cpp
class MainMenuScreen final : public UIScreen {
    UIDescription render() override {
        return ui::panel("menu-root").setColor({0.04f, 0.05f, 0.08f, 1})
            .child(ui::column("menu-col").setPosition({80, 120}).setSpacing(12)
                .child(ui::text("title").setText("YA").setFontSize(48))
                .child(ui::button("new-game").setText("New Game")
                    .setFocusPolicy(EWidgetFocusPolicy::Click)
                    .onClick([this] { _game.startNewGame(); }))
                .child(ui::button("continue").setText("Continue")
                    .setEnabled(_saves.hasSave())            // 无存档置灰
                    .onClick([this] { _game.loadLatest(); }))
                .child(ui::button("settings").setText("Settings")
                    .onClick([this] { _state.bShowSettings = true; invalidate(); }))
                .child(ui::button("quit").setText("Quit")
                    .onClick([this] { _game.quit(); })))
            .when(_state.bShowSettings, [this] { return renderSettingsPanel(); });
    }

    UIDescription renderSettingsPanel() {
        return ui::panel("settings").setPosition({240, 160}).setSize({400, 260})
            .setColor({0.1f, 0.1f, 0.12f, 0.95f}).setPadding({24, 24})
            .child(ui::column("settings-col").setSpacing(10)
                .child(ui::text("msaa-label").setText("MSAA"))
                .child(ui::comboBox("msaa-choice")          // 需新增：DSL comboBox builder
                    .options({"Off", "2x", "4x"}).selected(_state.msaaIndex)
                    .onSelect([this](int i) { _state.msaaIndex = i; invalidate(); }))
                .child(ui::button("close").setText("Close")
                    .onClick([this] { _state.bShowSettings = false; invalidate(); })));
    }
};
```

关键语义：`_state.bShowSettings` 翻转时 `when()` 增删子树，reconciler 只对 settings 子树 mount/unmount，菜单其余节点原样保留（含焦点位置）。

### 案例C：背包（keyed 列表 + 条件子树）

React keyed children 的典型场景；Godot 里对应手动 add_child/remove_child 的地方。

```cpp
class InventoryScreen final : public UIScreen {
    UIDescription render() override {
        auto grid = ui::column("inv").setSpacing(8);
        for (const Item& item : _inventory.items()) {
            grid.child(ui::row(item.guid)              // key = guid（不是数组下标！）
                .child(ui::panel("icon").setSize({48, 48}).setColor(item.rarityColor()))
                .child(ui::column("meta")
                    .child(ui::text("name").setText(item.name))
                    .child(ui::text("count").setText("x" + std::to_string(item.count))))
                .when(item.bEquipped, [&] {
                    return ui::panel("equipped-badge").setSize({6, 6}).setColor({1, 0.8f, 0, 1});
                }));
        }
        return ui::panel("inv-root")
            .child(ui::text("weight")
                .setText(std::format("{:.1f} / {:.1f} kg", _inventory.weight(), _inventory.maxWeight())))
            .child(std::move(grid));
    }
};
```

拾取/丢弃/整理后 `items()` 变化：guid 不同的格子 insert/remove；guid 相同的格子只 diff 文本/徽标；格子的选中态、hover 态因 same-key reuse 保留。不写任何 add_child/remove_child。

### 案例D：暂停菜单（ScreenStack + 模态输入）

对应 UMG 的 AddToViewport(ZOrder) + SetGamePaused，Godot 的 CanvasLayer。

```cpp
// 栈：Content 层(autoMount entries) 之上有 ScreenStack，zOrder 分层
void GameApp::onPauseKeyPressed() {
    _stack.push(100, PauseMenuScreen{_game});   // push: HUD.onBlur()，Pause.onMounted()
    _game.setPaused(true);
}

class PauseMenuScreen final : public UIScreen {
    EInputBlocking getInputBlocking() const override { return EInputBlocking::Modal; } // 吃掉全部输入

    UIDescription render() override {
        return ui::panel("pause-dim")                    // 全屏半透明遮罩
            .setSize(_state.viewportSize).setColor({0, 0, 0, 0.6f})
            .child(ui::column("pause-col").setSpacing(10)
                .child(ui::text("paused").setText("PAUSED").setFontSize(36))
                .child(ui::button("resume").setText("Resume")
                    .onClick([this] { _stack.pop(); }))  // pop: unmount + HUD.onFocus()
                .child(ui::button("quit-menu").setText("Quit to Menu")
                    .onClick([this] { _game.backToMainMenu(); })));
    }
};
```

输入路由：`host.dispatchEvent` 先问栈顶 `getInputBlocking()`；Modal 返回 Exclusive（WASD 不再移动角色），Passthrough 允许穿透（UMG 的 bShouldShowCursor / Input Mode 对应物）。

### 案例E：document 骨架 + DSL slot（混合，队伍栏）

UMG 的 NamedSlot：策划用 UI Designer 排静态骨架（inline document），程序只填动态槽位。

```cpp
// 编辑器 authored 的 inline document：
//   engine.panel "party-frame"      ← 静态外观/布局（UI Designer 可视化编辑）
//     engine.container "slots"      ← 命名为 slot 的容器
//       engine.text "placeholder" ("Drop party widgets here")

class PartyHudMount final : public IGameUIMount {   // 需新增：entry 的代码挂载钩子
    void onEntryMounted(SceneWidgetEntry& entry, UIElementRef root) override {
        if (UIElement* slots = root->findChildByDisplayName("slots")) {
            _ctrl = std::make_unique<UIRenderController>(_host.getTree());
            _ctrl->attachTo(*slots);               // 需新增：reconciler 挂到 retained 子树
            _ctrl->setRenderFunction([this] { return renderParty(); });
        }
    }

    UIDescription renderParty() {
        auto row = ui::row("party");
        for (const PartyMember& m : _party.members()) {
            row.child(ui::column(m.guid)
                .child(ui::panel("hp").setSize({60 * m.hp01(), 6}).setColor(hpColor(m.hp01())))
                .child(ui::text("name").setText(m.shortName()).setFontSize(12)));
        }
        return row;
    }
};
```

### 一帧的完整时序

```
1. App::update(dt)
   ├─ ECS systems 写 _hudState（掉血/换弹/拾取）
   ├─ 事件回调: screen->invalidate()
   └─ host.flushDirty()               ← 唯一 flush 点（update 末尾）
        ├─ for dirty screen: desc = render()
        └─ reconciler.apply(desc)     （same-key changed-only diff）
2. 输入: host.dispatchEvent(...)      → ScreenStack 栈顶决定 modal/passthrough
3. host.buildSnapshot()               → layout + paint，immutable snapshot
4. Render: compose(snapshot)          （command recording 只读 snapshot）
```

### UIScreen / ScreenStack API（需新增）

```cpp
class UIScreen {                       // UMG UserWidget / Godot Control 根 的对应物
public:
    virtual ~UIScreen() = default;
    virtual UIDescription render() = 0;
    // 生命周期：UMG NativeConstruct/Destruct；Godot _ready/_exit_tree
    virtual void onMounted();          // 首次 reconcile 前
    virtual void onUnmounted();        // 从树移除后（释放 texture lease）
    virtual void onFocus();            // 成为栈顶（获得输入焦点）
    virtual void onBlur();             // 不再是栈顶
    virtual int  getZOrder() const;
    virtual EInputBlocking getInputBlocking() const;   // None/Passthrough/Modal
    void invalidate();                 // 置脏，下一次 flush 重渲染
};

class ScreenStack {
public:
    template<class TScreen, class... TArgs>
    std::shared_ptr<TScreen> push(int zOrder, TArgs&&... args);
    void pop();                        // unmount 栈顶，恢复下层焦点
    void popToRoot();
    [[nodiscard]] UIScreen* top() const;
};
```

### 由案例倒推的缺口清单

| 缺口 | 案例 | 现状 |
|---|---|---|
| 静态 DSL 直接物化 live widget（`ui::build` / eager Construct） | Gallery / Editor | **已落地**（G1.5）；Description 路径已删除 |
| DSL builder 的 `Reactive<T>` 绑定 setter | A/B/E | `bindText` 已接到 Text builder；Button/Container 等未接。直接构建路径应调 live `bindXxx` |
| 列表/repeater 控件（变长 keyed 行，对标 SListView） | C | TreeView/TableGrid 已有 `bindData(ReactiveList)`；通用 DSL 列表未收口 |
| `UIScreen` + `ScreenStack` 挂卸/模态 | A/B/D | 已落地（G2 切片 1）；`onMounted` 无 parent-scoped 重载，`onUnmounted` 不 detach 子树 |
| ProgressBar（血条/读条） | A/E | 无，过渡期 Panel 填色 |
| `ui::comboBox/image/checkBox/slider` 直接构建包装 | B | **已接**（Widgets 页）；其余控件按页迁移再加 |
| viewport 尺寸/anchor | D | 仅 setSize 硬编码 |
| document 骨架实例化 + live slot 填充 | E | 无；**不要**做成「子 reconciler 每帧 apply」 |
| gamepad/键盘焦点链导航 | B/D | 只有 focusPolicy，无导航 |

### 增量接入步骤（G1-G5）

- G1 类型收口（已完成切片）：registry typeId + typed adapter 对 **document 实例化** 仍有价值；不再把「同 description 重复 apply」当成静态 DSL 的演进方向。
- G1.5 **静态 DSL 直接物化**（已完成）：`Construct.h` / `ui::build`；Workbench DSL 页直接构建 + 绑定。`UIDescription` / Reconciler / apply / `UIRenderController` 已删除。
- G2 Host（**搁置**）：`UIScreen`/`ScreenStack` 留给日后 HUD/暂停菜单等游戏表面。当前主线是 Gallery DSL + Editor 去 ImGui；两者都直接 `ui::build` 进已有 WidgetTree，不经过 screen。
- G3 列表控件：变长 keyed 行走 `ReactiveList` + 控件内部 reuse（复用 TreeView/TableGrid 经验），不用整页 reconciler 验收背包。
- G4 混合 slot：inline document **实例化一次**，slot 里填 live DSL 子树，不是子 reconciler 热循环。
- G5 Editor 狗粮：UIDesignerPanel 编辑同一棵 live WidgetTree。**禁止**「preview 改 reconciler 驱动」。

### 边界（不做）

- 不做 XML/CSS 外部语法；暂不做独立 ui document 文件格式。
- 不做自动双向绑定；不在 command recording 期读 live model 或跑 builder。
- 事件 lambda 不**捕获并跨帧持有**裸 live widget 指针。Construct/事件期的 `UIButton&` 参数可以；回调只能改 transient state 或外部 model/`Reactive`，不能假定下一帧会有 apply 来盖 props。
- 不把值变化塞进 Description/reconcile。
- 不把失效通道塞进 render function 签名。`requestRender`/`renderEx` 是反例，已移除，禁止回归。
- 不新增 dynamic invalidation owner，不新增与 `UIRenderController` 平行的 subtree controller。
- 不把 `UIDescription` 当作 GUI 基础对象。该路径已删除；document 走 `UIDocument::instantiate()`。
- 不把 Gallery/Editor 迁移接到 reconciler。该路径已删除。

## 稳定交付门禁

每个后续 Phase 都必须通过以下门禁后才能进入下一阶段：

1. Contract gate：API、所有权、invalidation、序列化语义有文档和单测。
2. Behavior gate：mount/update/remove/reorder、focus/capture/popup/drag 行为有自动化覆盖。
3. Render gate：snapshot JSON/digest、windowed GPU shot、offscreen parity 通过。
4. Resource gate：纹理、字体、brush、descriptor 保活到 submit 完成，teardown 无残留。
5. Compatibility gate：旧 retain API 和未迁移 host 继续通过原有测试。
6. Performance gate：记录 reconcile widgets、rebuilt widgets、draw items、paint/layout 时间；没有未经解释的回归。

任何门禁失败时，停止扩展功能，先修复当前 Phase；不允许带着已知生命周期或 cache 问题进入下一阶段。

## 推荐的完整迭代顺序

1. Phase -1A：identity/lifecycle/data-flow 最小契约。
2. Phase -1B：Panel/Text/Button + Row/Column 的最小 reconciler。
3. Phase -1C：TextField/ScrollViewport 的状态保留。
4. Phase -1D：只读 props、事件写回、Reactive dirty、batch update。
5. G1：registry typeId 收口（document 实例化；已完成切片）。
5b. G1.5：静态 DSL 直接物化 live widget；Workbench DSL 页离开 reconciler。
6. G2/G3：Screen 挂卸/输入路由补齐；列表控件 keyed 行（非整页 reconcile）。
7. Phase 0：基线矩阵、无 theme/authored/theme-only 测试和性能基线。
8. G4：document 骨架 + DSL slot 混合。
9. G5 + Phase 1：UIDesignerPanel 编辑 live WidgetTree（非 reconciler 化）；visual property setter/invalidation 收口。
10. Phase 2：Brush/NinePatch/Border/rounded primitives。
11. Phase 3：基础控件 visual state 完备。
12. Phase 5：Editor core controls 和 EditorTheme（editor-grade dense controls、property editing、validation）。
13. Phase 6：按 panel 逐步移除 ImGui，三宿主收敛。

每个编号都应作为可独立 review、测试、提交和回滚的增量，不把多个编号合并成一次“大重构”。

## Phase 0：基线、契约与迁移护栏

- 建立 standalone、Game Runtime、headless snapshot 回归矩阵。
- 增加无 theme、authored properties、theme-only 三种模式测试。
- 定义 visual property 的 Paint/Layout/SubtreePaintContext 影响级别。
- 定义 common visual states 和 inline document authored appearance 版本策略。
- 建立 scenario、snapshot JSON、GPU parity 验收。

验收：无 theme 仍能稳定生成 snapshot；直接属性变更不会绕过 paint cache。

## Phase 1：Visual property 与 invalidation

- 为颜色、brush、font size、padding、border、尺寸提供 changed-only setter/getter。
- runtime 禁止依赖裸字段写入；reflection/deserialization 走事务式写入。
- 统一 UIElement visual mutation 辅助路径。
- 迁移 Panel、Image、Text、Button、CheckBox、Slider、TextField、ComboBox、SelectableRow。

验收：改颜色/brush 下一帧可见；相同值无多余 dirty transition；layout-affine 修改不扩大为全树重绘。

## Phase 2：Brush 与基础视觉原语

- 实现真正 NinePatch 和 Border；支持 UV/crop/flip 与资源丢失 fallback。
- 增加 rounded rectangle、border thickness、opacity/alpha 合成规则。
- 评估 gradient、shadow、inner shadow 是否进入 framework primitive。
- 验证资源保活到 queue submit 完成。

验收：button、panel、dialog、tooltip、window 只用 brush 即可正确绘制；不同尺寸和 DPI 下九宫格角点不变形；windowed/offscreen parity 一致。

## Phase 3：统一控件 visual state

统一 normal、hovered、pressed、focused、disabled、selected、checked、dragging、read-only、validation/error 状态。

- 为 CheckBox、ComboBox、Slider、TextField、TreeView、TableGrid、Menu、Dialog、Popup、SelectableRow 补齐 appearance 数据。
- 分离行为状态和视觉状态；状态变化统一通过 VisualFlag 或 invalidation。
- 定义 disabled ancestor 绘制策略。

验收：Editor 和 Game UI 使用同一控件 API 配置状态外观；每个状态有 scenario 和 snapshot 验收。

## Phase 4：Game UI authored appearance 与资源边界（原 yaui 步骤，已收口为 inline document）

- 区分 geometry、behavior、authored appearance 字段。
- 将必要 brush、颜色、font、可选 style role 纳入稳定 schema（inline document，无独立文件格式）。
- 统一纹理、字体 resolver 和资源缺失 fallback。
- 增加 document version migration；theme role 不作为文档有效性的前置条件。

验收：Editor 编辑的 inline document 在 Runtime 中外观一致；无 theme 也可实例化；资源缺失有稳定诊断。

## Phase 5：Editor 控件与迁移准备

- 完善 DockSpace、Tab、FloatingWindow、Menu、Popup、TreeView、TableGrid、List、Property row、Search/Filter。
- 增加 enum、numeric、vector、color、asset picker、reference picker。
- 增加 read-only、mixed value、modified、validation error、tooltip 状态。
- 完善 keyboard navigation、focus scope、shortcut routing、selection model、undo/redo 边界。
- 设计 EditorTheme，但只作为 Editor layer 内容。

验收：核心 Editor shell 不依赖 ImGui；WidgetTree 能承载 editor panel、dock、inspector、viewport overlay。

## Phase 6：ImGui 移除与三宿主收敛

- 逐 panel 迁移 Editor。
- 移除 ImGui event/style/render backend 依赖。
- Editor 使用 EditorTheme，Game Runtime 使用 project-authored appearance，standalone app 可使用 UITheme/Workbench。
- 清理旧 GUI、旧兼容路径和重复控件实现。

验收：Editor 无 ImGui 编译/运行时依赖；三种宿主均使用同一套 WidgetTree/Render2D compose；均通过 headless、snapshot、GPU smoke 和 teardown 验收。

## 验收矩阵与提交策略

每阶段覆盖 standalone windowed、offscreen/headless、Game Runtime viewport、Editor embedded/offscreen、theme mounted、no theme、direct authored color/brush、runtime state transition、resize/DPI、资源缺失、detach/reparent/reload、GPU teardown。

推荐入口：python3 Script/ya.py cfg；python3 Script/ya.py test --target ya --filter Suite.Test；xmake b/r ya-gui-closure-test；xmake b/r GUIWorkbench。真实渲染必须执行 GPU shot，不能只依赖 tree assertion。

每个 Phase 单独提交，格式为 [gui] phase N: description。Phase 内顺序为 contract/tests、framework implementation、control migrations、documentation/baseline。

## 风险

- NinePatch 会改变 draw item 数量和 baseline digest。
- setter 收口会暴露业务层直接写字段的隐式依赖。
- inline document authored appearance schema 变更需要 scene migration（无独立文件格式后migration 跟随 scene 版本）。
- DSL typeId 收口会动 EWidgetKind 的所有契约测试，需保持 DeclarativeContractTest 行为等价迁移。
- Editor 迁移同时涉及 input、focus、undo/redo 和 resource picker。
- theme 与 authored appearance 优先级必须先固定，否则控件行为会不一致。

## 8. 计划修订记录

- 2026-08-25：整体 review 后，Phase -1 改为先稳定 retained runtime 与 Slate/EUI-NEO 风格静态强类型 builder/DSL。
- 2026-08-25：明确动态 component/script/reconcile 是静态 DSL 之上的可选层；standalone GUI、Game Runtime、Editor 的基础构建不依赖动态层。
- 2026-08-25：撤销将 UIDescription 统一成 JSON/反射 fields bucket 的 G1 slice 2 方案，改为 typed common/payload + typed adapter handlers；JSON/反射仅作 document/script 边界适配。
- 2026-08-26：确立值走 `Reactive<T>`、撤销「新增 dynamic invalidation owner」、`requestRender`/`renderEx` 记录为反例并移除。
- 2026-08-26：进一步收口默认构建路径。静态 DSL 必须直接物化 live widget；`UIDescription` + apply + reconcile 不是架构必需品，只保留给 document/script 一次性实例化，以及列表控件内部的 keyed 行生命周期。禁止再把 Description 当 GUI 基础对象扩展；禁止 Gallery/Editor 默认走 reconciler。修正「结构变化 = 整页 Description diff」的表述——已知结构走 live API，变长集合走列表控件。
