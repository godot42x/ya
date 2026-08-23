# GUI Framework / Editor / Game UI 重构计划

## 目标

让同一套 retain-mode GUI framework 同时承载独立 GUI app、Game Runtime UI 和未来移除 ImGui 后的 Game Editor。目标参考 Slate/UMG：WidgetTree、布局、输入、focus、popup、drag/drop、snapshot/compose 由 framework 提供；Game UI 的外观由 project/application authored properties 控制，不强制依赖 UITheme；UITheme 保留为 Editor、tooling、standalone app 的可选默认样式机制。

## 架构决策

Framework 提供 WidgetTree、UIElement、Layout、Input、UIFrameBuilder、Brush、draw primitives、visual state 和 invalidation contract；Editor 提供 EditorTheme/semantic tokens；Project/Game 提供 authored colors、textures、brushes、documents 和 runtime bindings。

外观解析优先级固定为：explicit runtime/authored property > widget-local authored default > optional scoped/theme style > framework neutral fallback。Game UI 不要求挂载 theme。

不在本计划中恢复 ImGui、强制 Game UI 使用 UITheme、一次性重写全部控件，或把 project-specific tokens 放入 framework。

## 当前缺口

1. FBrush 的 NinePatch/Border 目前仍降级为整图拉伸。
2. 大量控件直接暴露颜色/尺寸字段，runtime 修改没有统一的 changed-only setter/invalidation contract。
3. CheckBox、ComboBox、Slider、TextField、TreeView、TableGrid、Dialog、Menu、InputExtras 缺少统一 visual state contract。
4. yaui 没有清晰区分 authored appearance 与 optional theme role。
5. rounded rect、border、opacity、gradient、shadow、transform 等视觉原语不完整。
6. Editor 未来迁移需要 editor-grade dense controls、property editing、validation visual states。

## Phase -1：React-style 函数式 DSL 与 Reconciler

第一阶段先做 DSL，但目标不是 XML/CSS 外部语法，而是 React 风格的 C++ 函数式 DSL：UI 函数返回 builder/description，reconciler 再把 description 应用到 retained WidgetTree。这样先验证声明式模型，再决定未来是否需要 yaui/XML 映射。

目标形态：UI function(state) -> UIBuilder/UIDescription -> keyed reconcile -> retained WidgetTree -> layout/input/snapshot。

示意 API：UIBuilder buildSettings(const SettingsState& state) { return ui::column("settings").child(ui::text("title").text(state.title)).child(ui::button("save").text("Save").onClick(...)); }

函数每次可以重新返回 builder，但 stable key 对应的 retained widget 必须复用，不能每次重建所有 live widget。

### 渐进式实施原则

- 不预先实现完整数据绑定、完整组件生命周期或完整 DSL 语法；每一层只实现当前垂直切片所需的最小能力。
- 每个阶段先写契约测试，再写实现，再接入一个真实控件，最后跑 windowed/offscreen/headless 回归。
- 旧 retain API 在迁移期继续可用；DSL 只能作为新增路径，不能先破坏 WidgetTree。
- 每一步都必须能独立编译、运行和回滚；不允许跨多个 Phase 的大批量重写。
- 任何新抽象必须先有一个真实 consumer 和测试，禁止为了未来可能需求提前建复杂框架。

### 必须先冻结的契约

- Widget identity：区分 type、stable key、display name；key 用于声明式重建和状态保留，不能只依赖 name。
- Ownership/lifecycle：声明式节点如何 mount、update、detach、reparent；哪些状态 keep-alive，哪些随节点销毁。
- Property mutation：颜色、brush、布局、文本、selection 等属性的 setter、事务和 invalidation 规则。
- Data flow：Reactive/observable 的读取依赖、写入通知、双向绑定、computed 派生值和 batch 更新。
- Resource reference：纹理、字体、材质、图标的引用格式、resolve 时机、缺失 fallback 和 queue-submit 保活。
- Event contract：事件 callback 的生命周期、capture/focus、popup/modal、取消和异步回调安全边界。
- Serialization boundary：哪些字段属于 yaui authoring，哪些属于运行时状态，哪些不能序列化。
- Layout vocabulary：Overlay、Grid、Wrap/Flow、Canvas/absolute、List virtualization 是否进入第一版 DSL。

### DSL 设计边界

第一版吸收 React 的 render function、keyed children、单向 state -> description 数据流，以及 EUI-NEO 的链式 builder；保留现有 retained tree 和 immutable snapshot。第一版必须支持 stable key、条件子树、列表 keyed children、属性绑定、事件绑定、slot/layout props 和显式 state ownership。

第一版不承诺 XML/CSS 语法、Virtual DOM 全量替代 WidgetTree、自动双向数据绑定、任意 lambda 捕获 live widget 裸指针，或在 command recording 期执行 render function。

### Phase -1 实施顺序

#### -1A：契约冻结（只读设计与测试，不迁移业务）

1. 定义 UIBuilder/UIDescription 节点模型和 stable key 规则。
2. 定义 builder props：layout、appearance、text/value、event、children、visibility。
3. 定义 mount/update/remove/reorder/detach 的生命周期和 callback 安全边界。
4. 定义 state ownership：外部 state、widget transient state、derived state 的边界。

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

#### -1D：最小数据流

1. render function 读取只读 state/props。
2. event callback 写回外部 model，由下一次 render 产生 description 更新。
3. Reactive 只接入已存在的依赖追踪和 dirty invalidation，不在此阶段实现完整 computed/bidirectional binding。
4. 增加 batch update，保证一次业务事务不会重复 reconcile 同一子树。

退出条件：state 变化只更新受影响的 props/子树；command recording 期不读取 live model；重复相同值不会产生多余 dirty transition。

### Phase -1 验收

- React-style render function 连续返回 builder 不会重复创建同 key widget；key 相同的控件能保留 focus、text edit、scroll、popup 等允许保留的状态。
- state 变化只更新受影响的 props/子树；不会在 command recording 期读取 live model。
- DSL 不依赖 UITheme；没有 theme 也能描述和渲染 authored appearance。
- 至少完成一个 Button/TextField/列表 prototype，并用 snapshot/scenario 验证 mount、update、remove、reorder、focus 和 resource fallback。
- 在 Phase -1 完成前，不开始 Editor 全量迁移，不扩展大规模控件样式，不承诺外部 XML 语法稳定。

### Phase -1 禁止事项

- 不实现 XML/CSS parser。
- 不实现完整 Virtual DOM 或全树替换。
- 不把 DSL 节点直接绑定为裸 UIElement 指针并由业务长期持有。
- 不在同一阶段同时迁移 TreeView、DockSpace、Inspector 等复杂 Editor 控件。
- 不以“demo 能显示”为完成标准，必须通过生命周期、状态、snapshot 和 teardown 测试。

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
5. Phase 0：基线矩阵、无 theme/authored/theme-only 测试和性能基线。
6. Phase 1：visual property setter/invalidation 收口。
7. Phase 2：Brush/NinePatch/Border/rounded primitives。
8. Phase 3：基础控件 visual state 完备。
9. Phase 4：yaui authored appearance 与 descriptor 映射。
10. Phase 5：Editor core controls 和 EditorTheme。
11. Phase 6：按 panel 逐步移除 ImGui。

每个编号都应作为可独立 review、测试、提交和回滚的增量，不把多个编号合并成一次“大重构”。

## Phase 0：基线、契约与迁移护栏

- 建立 standalone、Game Runtime、headless snapshot 回归矩阵。
- 增加无 theme、authored properties、theme-only 三种模式测试。
- 定义 visual property 的 Paint/Layout/SubtreePaintContext 影响级别。
- 定义 common visual states 和 yaui authored appearance 版本策略。
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

## Phase 4：yaui authored appearance 与资源边界

- 区分 geometry、behavior、authored appearance 字段。
- 将必要 brush、颜色、font、可选 style role 纳入稳定 schema。
- 统一纹理、字体 resolver 和资源缺失 fallback。
- 增加 document version migration；theme role 不作为文档有效性的前置条件。

验收：Editor 保存的 yaui 在 Runtime 中外观一致；无 theme 也可实例化；资源缺失有稳定诊断。

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
- 清理旧 GUI、旧 yaui 兼容路径和重复控件实现。

验收：Editor 无 ImGui 编译/运行时依赖；三种宿主均使用同一套 WidgetTree/Render2D compose；均通过 headless、snapshot、GPU smoke 和 teardown 验收。

## 验收矩阵与提交策略

每阶段覆盖 standalone windowed、offscreen/headless、Game Runtime viewport、Editor embedded/offscreen、theme mounted、no theme、direct authored color/brush、runtime state transition、resize/DPI、资源缺失、detach/reparent/reload、GPU teardown。

推荐入口：python3 Script/ya.py cfg；python3 Script/ya.py test --target ya --filter Suite.Test；xmake b/r ya-gui-closure-test；xmake b/r GUIWorkbench。真实渲染必须执行 GPU shot，不能只依赖 tree assertion。

每个 Phase 单独提交，格式为 [gui] phase N: description。Phase 内顺序为 contract/tests、framework implementation、control migrations、documentation/baseline。

## 风险

- NinePatch 会改变 draw item 数量和 baseline digest。
- setter 收口会暴露业务层直接写字段的隐式依赖。
- authored appearance schema 变更需要 yaui migration。
- Editor 迁移同时涉及 input、focus、undo/redo 和 resource picker。
- theme 与 authored appearance 优先级必须先固定，否则控件行为会不一致。
