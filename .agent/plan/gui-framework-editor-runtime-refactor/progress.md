# GUI Framework / Editor / Game UI 重构进度

## 2026-08-25 架构整体 review checkpoint

已对本计划与当前 DSL 实现重新对齐：

- Phase -1 的基础不再表述为 React-style DSL 优先，而是 retained runtime + Slate/EUI-NEO 风格静态强类型 builder/DSL。
- child/children(UIDescription...) 是静态核心；UIComponent/compose/when/reconcile 是上层动态扩展。
- UIDescription 保持 typed common/payload，不演化成 fields JSON bucket；UIDocument/脚本反射只在边界转换。
- 静态 builder 必须可脱离 UIRenderController 独立使用，standalone GUI、Game Runtime、Editor 不强依赖动态 component 层。
- G1 slice 2 的目标已改为 typed adapter handlers，而不是 DSL apply 全部走反射字段。

## 已完成

- [x] 完成当前 theme/style 系统 review。
- [x] 确认 UITheme 为可选 framework/tooling 机制。
- [x] 确认 Game UI 允许 project/application authored appearance。
- [x] 确认未来 Editor 移除 ImGui，统一使用 WidgetTree framework。

## 进行中 / 未开始

- [x] Phase -1A：retained 基础与静态强类型 DSL 最小契约与测试骨架。
- [x] Phase -1B：最小 reconciler。
- [x] Phase -1C：一个有状态控件。
- [x] Phase -1D：最小数据流。
- [ ] Phase 0：基线、契约与迁移护栏。
- [ ] Phase 1：visual property 与 invalidation。
- [ ] Phase 2：Brush 与基础视觉原语。
- [ ] Phase 3：统一控件 visual state。
- [ ] Phase 4：yaui authored appearance。
- [ ] Phase 5：Editor 控件与迁移准备。
- [ ] Phase 6：ImGui 移除与三宿主收敛。

## 当前结论

WidgetTree、布局、输入、snapshot、compose 和 host 边界可继续作为长期基础；主要重构对象是视觉属性契约、Brush 绘制能力、控件状态、文档序列化和 Editor 控件覆盖，而不是强制扩展 UITheme。

补充结论：整体采用 retained UI runtime → Slate 风格静态强类型 builder（**直接物化 live widget**）→ 可选的 document 实例化与列表控件内部 keyed 行。静态 builder 不经过 Description/reconciler。`UIDescription` 不是基础对象。

JSON bucket、反射字段查找、脚本 schema 只允许作为 UIDocument/Script 到 typed description 的边界适配，不能进入 retained runtime 或静态 DSL 热路径。

新增诊断边界：`UIElement::serializeFields()` 继续由反射负责 authored/UIDocument 持久化；`WidgetTreeDump::serializeNode()` 保留 WidgetTree 拓扑、slot/layout、focus/hover/capture 和其他 runtime 诊断。控件专属 runtime 诊断通过 `UIElement::appendRuntimeDiagnostics()` 扩展，不能混入 authored serialization。首批已迁移 UIButton、UIText、UITextField，JSON schema 保持不变。

## 下一步

当前新增决策：第一阶段先稳定 retained runtime 与静态强类型 DSL；DSL 默认直接构建 live 树。UIComponent/render function、reconciler 不是默认路径。不先做 XML，也不让动态层替代 WidgetTree。

已完成：Phase -1A 契约文档落盘，新增契约测试骨架 DeclarativeContractTest.cpp，并通过独立目标 ya-gui-declarative-contract-test（7/7）。

已完成：Phase -1B 最小 reconciler（same-key reuse / insert / remove / reorder / detach）。

已完成：Phase -1C 状态保留控件验证（TextField focus / Button remove 清理）。

实施策略进一步收敛为四个小步：-1A 契约冻结、-1B 最小 reconciler、-1C 一个有状态控件、-1D 最小数据流。每一步都必须独立编译、自动化测试和可回滚，再进入下一步。

其中 -1A 现在要先把 builder 的分层定清：公共 node contract、控件专属 builder、共享属性注入点，而不是先把一个通用 builder 字段集补到全量。

2026-08-24 checkpoint：完成 DSL builder 分层第一步。外层新增 UIContainerBuilder/UIPanelBuilder/UITextBuilder/UIButtonBuilder/UITextFieldBuilder，各自只暴露对应语义；reconciler 统一接收 UIDescription。独立目标 ya-gui-declarative-contract-test 构建通过，7/7 通过。

2026-08-24 checkpoint：完成 Phase -1D 最小数据流。新增 UIRenderController，支持 render function、显式 invalidate、batch begin/end、单次 flush；新增两条测试验证外部 state 回写和批量更新，独立目标共 9/9 通过。

2026-08-24 checkpoint：Phase 0 外观基线第一批完成。新增 no-theme / authored-color / theme-only 三种 appearance mode 测试；验证无 theme 可渲染、显式 authored color 可独立生效、theme-only 可由 UITheme 提供外观。独立目标共 10/10 通过。

2026-08-24 checkpoint：补齐 declarative identity 诊断。UIReconciler 在 reconcile 前验证同一 parent 下重复 stable key，并返回包含路径与 key 的稳定错误；新增契约测试，独立目标共 11/11 通过。

2026-08-24 checkpoint：Phase 0 snapshot/host 基线取证。ya-gui-declarative-contract-test 12/12 通过，新增重复 build 的 snapshot 稳定性断言；ya-gui-headless-host-test 构建并运行 2/2 通过。ya-gui-closure-test 当前仍被既有 DockNodeTest.cpp 使用不存在的 FDockTreeModel::getRoot() 阻塞，未归因于本次改动。

2026-08-24 checkpoint：standalone windowed smoke 基线修复并通过。GUIWorkbench 原 smoke 硬编码 tabs[7]，新增 Gallery/Interactions/Dock/Theme 页面后 Editor 页已迁移到动态索引；改用 getEditorPageIndex() 后真实 Vulkan windowed smoke 通过，完成初始化、交互、Editor 切换、teardown。

2026-08-24 checkpoint：offscreen/headless parity 取证。GUIWorkbench windowed 与 headless 的 ScrollSplit snapshot JSON 均成功生成，但结构不一致：windowed 101 draw items，headless 53 draw items，structural/semantic digest 均不同。因此 validation.snapshot 继续保持未完成，下一步先修复 host 首帧/字体/shell 构建时序，再进入 offscreen-diff 绿灯。

2026-08-24 checkpoint：修复 FontManager::registerFont 的 DPI-qualified cache key。headless 预注册字体现在可被 getFont() 命中，ScrollSplit snapshot 从 53 draw items 恢复到 100（windowed 101），文本从 0 恢复到 47；剩余差异为 synthetic font metrics 与真实字体度量差异，exact digest parity 仍未通过。ya-gui-headless-host-test 2/2、ya-gui-declarative-contract-test 12/12 通过。

2026-08-24 checkpoint：为 FontManager 预注册字体增加 DPI cache 回归测试；ya-gui-declarative-contract-test 13/13 通过。后续 exact windowed/headless parity 仍需解决 synthetic font metrics 差异，当前不标记 validation.snapshot 完成。

2026-08-24 checkpoint：补齐 GUIWorkbench headless synthetic font sizes（9/10/11/12/13/14/15/16/20/24/32/40）。ScrollSplit windowed/headless snapshot 现在均为 101 draw items、48 text items；剩余仅为 synthetic font metrics 导致的几何/digest 差异，validation.snapshot 仍保持 in_progress。

2026-08-24 checkpoint：明确 cross-host parity 分层。新增 UIFrameSnapshotTest 回归断言：text 的字体度量变化会改变 structural digest，但不改变 semantic digest；semantic digest 作为 windowed/headless 结构门禁，windowed/offscreen 仍要求 exact parity。完整 ya-gui-closure-test 仍被既有 DockNodeTest.cpp 的 getRoot() 编译错误阻塞。

2026-08-24 checkpoint：收敛 macOS convergence 脚本的 parity 断言。gui_convergence_macos_validation.py 现在比较 windowed/headless 的 semanticDigest 与 draw-item 数量，不再错误要求 synthetic font 与真实字体的 structural JSON 完全相同；脚本通过 py_compile，已用现有 ScrollSplit evidence 验证 semantic parity PASS。

在实现 Phase 0 前，应先完成 Phase -1 的 DSL、stable key、状态保留、数据流和生命周期决策；Phase 0 的测试矩阵应覆盖 DSL reconcile，而不只是手写 retain builder。

2026-08-26 checkpoint：G1 类型收口切片 1 完成。DeclarativeNodeAdapter 的 create/sameKind/kindName 改为通过 UITypeRegistry 解析 typeId（Column/Row→engine.container；Panel→engine.panel；Text→engine.text；Button→engine.button；TextField→engine.text_field），DSL 创建的 widget 现在携带正确的 _typeId（此前为空，破坏 type-id 查询）。EWidgetKind 仍作 DSL 作者侧枚举，registry 成为类型单一事实源。行为不变：ya-gui-declarative-contract-test 31/31（含新增 DslCreatedWidgetsCarryRegistryTypeId）、ya-gui-headless-host-test 13/13、ya-gui-widgets-test 174/174、ya-game-editor 编译通过。
架构修订：G1 切片 2 不再把 UIDescription 改造成 typeId/stableKey/fields/children 的 JSON/反射字段桶。下一步改为：保留 typeId 收口，建立 typed static description/common/payload 的 adapter handlers；UIDocument/脚本反射只在动态边界转换到 typed description，静态 builder 与 retained runtime 不依赖反射。

2026-08-26 checkpoint：runtime diagnostics 第一刀完成。新增 `UIElement::appendRuntimeDiagnostics()` 虚拟扩展点，将 Button/Text/TextField 的控制态 dump 从中央 `WidgetTreeDump.cpp` 下沉到控件自身；保留其余控件兼容分支，确保渐进迁移。`ya-gui-widgets-test` 164/164 通过。

2026-08-26 checkpoint：G1 完成（刀1 enum→typeId 收口 + 刀2 typed builder/payload/apply hook 下沉 + G1收尾 typeId 常量单一事实源）。G1 门禁四项满足：重复应用不重建、未知 typeId 有诊断、静态 builder 不依赖反射、DeclarativeContractTest 31/31 全绿。

2026-08-26 checkpoint：G2 切片 1（UIScreen + ScreenStack 契约）落地。新增 UIScreen.h（抽象屏幕：render()/onMounted/onUnmounted/生命周期/onFocus/onBlur/getZOrder/getInputBlocking/invalidate/flush，内部持有 UIRenderController，挂载前 invalidate 延迟重放）、ScreenStack.h（push/pop/remove/top 按 zOrder 排序 + routeEvent 处理 Modal/PassThrough 输入路由）、实现进 Declarative.cpp。类型身份保留字符串 typeId（确认不用 type_index_v：字符串是 DSL/脚本/序列化的开放契约层，type_index 是 in-memory 身份层，注册表应桥接二者而非替换）。新增 ScreenStackContractTest.cpp 6 个用例（排序/弹栈/按身份移除/Modal 屏蔽/flush 增量重concile/挂载前 invalidate 重放），已注册进 ya-gui-declarative-contract-test。ya-gui-declarative-contract-test 37/37 通过。G2 尚未接入 GameUIHost 帧循环（flush 时机、场景生命周期），为下一切片。

下一步 G2-2：将 UIScreen/ScreenStack 接入 GameUIHost 帧循环——onSceneActivated 建栈、每帧 flushDirty 固定在 buildSnapshot 之前且 command recording 之外、Modal/Passthrough 输入路由接 GameUIHost::dispatchEvent。落地案例 A（HUD）/D（暂停菜单）并补 teardown 残留测试。

2026-08-26 战略转向（用户）：不做 GameUI，先做 GUI 最小闭环（吃自己狗粮），两条线：
  (1) 逐步用 DSL 替换 Feature Gallery，只留一页 raw retained builder 作为原始构建方式案例；
  (2) 逐步替换 Editor UI、剔除 ImGui，同时保证 ImGui 已实现的所有 feature 都能在本 GUI framework 复现。
这与 plan.md 既定的"吃自己狗粮（Editor）"终局一致，只是把优先级提前到 GameUI 之前。

2026-08-26 checkpoint：GUI 最小闭环 切片 1（DSL 进 Feature Gallery 的机制 + 首张 DSL 页）。
- 使 UIReconciler / UIRenderController 支持"挂到指定父元素"的父作用域重载（reconcile 进 demo host 而非整层 Content，避免与 shell chrome 冲突）。
- FWorkbenchSurface 新增 addPage(name, FPageRenderFn) 重载 + 内部 _activeController（UIRenderController，按 demo host 父作用域 reconcile）；selectPage 切换 DSL 页时建/复位该 controller，updateUI 每帧 flush；clearDemoHost 仍负责拆子树。FPage 改为 tagged（imperative | declarative）。
- GUIWorkbench 新增 "DSL" 页，完全用 ya::ui::column/row/text/button builder 表达（含 button 计数交互），证明 shell 可托管 declarative 页，且零回归既有 14 个 scenario 页与自动化。
- GUIWorkbench 编译通过（仅预存 warning）；ya-gui-declarative-contract-test 37/37 通过（确认父作用域 reconcile 改动未回归）。
后续：逐页把既有 raw retained 页迁移到 DSL（优先纯 widget 组合、无自定义绘制/无 demoState 耦合的页）；再开 Editor ImGui 剔除线（先做 feature gap 审计）。

## 2026-08-26 架构决策：值/结构分工（Slate 口径）

起因：切片 1 为了让 DSL 页的按钮回调能触发重 reconcile，引入了 `FPageRenderExFn` / `isDeclarativeEx()` / `requestRender`，把 owner 的 `invalidate` 透传进 render 签名。review 时对代码逐条核对，得到的事实与之前的判断不同：

- `UIScreen` / `ScreenStack` 已在 G2 切片 1 落地，已经具备 render 根 + `invalidate()` + `flush()`，即所谓"dynamic invalidation owner"。所以问题不是缺抽象。
- workbench 绕过它的真实原因是 `UIScreen::onMounted` 只接受 `(tree, layer)`，无法挂到 `_demoHost` 这个具体元素；而 `UIRenderController` / `UIReconciler` 本身早已支持父作用域构造。
- 更根本的一层：Slate 没有 `UIScreen` 也没有 `requestRender`，因为它的值变化走 `TAttribute` + `SWidget::Invalidate`（落点是 widget），结构变化走容器 widget 自己的刷新（`SListView::RequestListRefresh`）。这套机制我们**已经完整实现**——`Reactive<T>` / `ReactiveList<T>` 是 push 失效 + pull 读取，依赖在 paint 走查中收集，每条边自带 Paint/Layout 级别；控件侧 `bindText` / `bindData` / `bindSelection` / `bindEnabled` / `bindSplitRatio` 都已就位。

决策：把 reconcile 的正当范围收缩到**只管结构**。

- 值变化（文本/颜色/尺寸/enabled/selection）走 `Reactive<T>` 绑定，不重跑 render function，不进 reconcile。
- 结构变化（变长子列表、条件子树、keyed reorder）才走 `UIDescription` + `UIReconciler` + `invalidate()`。
- 不新增 owner 抽象：结构 owner 的粒度等于 `UIRenderController` 的挂载点，screen 单位就是 `UIScreen`；撤销 `UISubtreeController` / `UIComponentHost` 提议。
- `FPageRenderExFn` / `isDeclarativeEx()` / `requestRender` 记录为反例并移除。

review 中另外查出的、与本决策独立的既存缺陷（待修）：

1. `UIDescription` 把 `key`/`displayName`/`children` 在顶层与 `common` 各存一份，`syncCommonIdentity()` 在每次 `appendChild` 全量深拷贝 children；reconciler 身份取顶层 `key` 但遍历取 `common.children`，读写分裂。另有多余的自我转换 `operator UIDescription()`，以及 `reconcileChildren` 开头一次无必要的整表拷贝。
2. `UIButtonBuilder::setText` 注释称 "appends/replaces" 但实现只追加，调用两次会产出两个同 key 的 `__label` 子节点，触发 duplicate-key 断言。
3. `ApplyUIButtonDescription` 在 `onClick` 缺省时不清空旧回调；并且无条件强制 `_bAutoSize = true`，DSL 按钮无法定尺寸。
4. `UIScreen::onUnmounted` 只 `_controller.reset()`，不 detach 已建子树；`ScreenStackContractTest` 只断言 `isMounted()` 标志，未覆盖 detach，契约测试第 9 条（unmount 后旧 callback 不再触发）在 screen 路径上未兜住。

## 2026-08-26 checkpoint：值/结构分工 切片 1（DSL 接绑定 + 移除 requestRender）

- `UITextDescription` 新增 `textBinding`（`shared_ptr<Reactive<std::string>>`），`UITextBuilder::bindText()` 写入；`ApplyUITextDescription` **无条件**应用该绑定，使描述成为绑定的单一事实源（省略 `bindText` 即清空旧绑定）。因为 apply 会强制 `_bAutoSize = true`，`UIText::paintSelf` / `computeDesiredSize` 对绑定文本按 Layout 级别 `get()`，所以 `Reactive::set()` 会正确触发 measure+arrange，而不是只重画。
- GUIWorkbench "DSL" 页改为绑定驱动：按钮标签是 `bindText` 的 Text 子节点（利用 button 的 content-slot 模型，无需给 button 加 magic text 字段），`onClick` 只 `set()` 计数字符串。页面从此**不再有任何失效通道**——`render()` 只在切页时跑一次。
- 移除 `FPageRenderExFn` / `isDeclarativeEx()` / `requestRender` 整条路径；`FPage` 回到 build/render 两态。`FPageRenderFn` 的注释写明"宿主刻意不提供 per-page 重渲染通道；需要它说明该值走错了机制"。

验证：`ya-gui-declarative-contract-test` 36/37、`ya-gui-widgets-test` 174/175、`ya-gui-headless-host-test` 2/2、GUIWorkbench 编译通过。

唯一失败项 `DeclarativeContractTest.TypedContainerPropertiesPreserveIdentityAndInvalidationScope` **不由本轮改动引起**，来源是工作区未提交的 `ApplyUIContainerDescription` 里 `container._bAutoSize = true`：容器改为按子节点测量后，`stretchLastChild` 没有富余空间（52 变 30），`clipChildren` 也按测得的 80 而非 authored 50 裁剪。已用临时注释该行 + 重跑该用例验证（PASS），随后原样恢复文件。

这暴露了一个待决问题：三个 apply hook（Container/Text/Button）都为了"让 DSL 页可见"无条件强制 `_bAutoSize = true`，代价是 DSL 无法表达 authored 尺寸，并且直接和既有 authored-size 契约测试冲突。正确解法应是把 auto-size 变成**live widget 构建语义**（未设 size 时才 auto），而不是在 apply 里硬写——这也是 Description 转发层制造补丁的证据。

## 2026-08-26 架构决策：静态 DSL 直接物化 live widget

用户追问：为什么需要 DSL → Description → apply → widget 这一层转发，而不是直接构建实际 UI？

结论：**不需要。** Description 转发不是架构必需品，是把 React VDOM 误当成 Slate 构建语法的结果。

冻结的边界：

- 默认路径：typed builder 在 Construct 时物化 `UIElement`，callback/绑定当时就落到 live widget。没有 apply。
- `UIDescription` 只允许作为 document/script 的一次性实例化规格，或列表控件内部的 keyed 行 diff。它不是 GUI 基础对象，不是静态 DSL 的返回类型。
- 已知结构变化走 live API（attach/detach/visibility/Switcher）；变长集合走列表控件 + `ReactiveList`。禁止把「render() 重声明整棵树」做成应用默认。
- `UIScreen` 是挂卸/输入路由单位，不是每帧 Description owner。
- Reconciler 保留契约测试，停止扩展；Gallery/Editor 迁移不得默认走它。
- G5「preview reconciler 化」撤销。

下一刀（G1.5）：`ui::build` + Workbench DSL 页改直接构建，去掉对该页的 `UIRenderController`。

## 2026-08-26 checkpoint：G1.5 静态 DSL 直接物化

- 新增 `GUI/Declarative/Construct.h`：`ui::column/row/text/button/panel/textField` 在 Construct 时 `UITypeRegistry::createInstance` + `addDetachedChild`，`ui::build(builder, tree, parent)` 一次 `attach`。未设 `size` 时 `_bAutoSize = true`，设了就关掉。没有 Description，没有 apply。
- Description 工厂退到 `ui::desc::*`，只给既有 reconciler 契约测试用。
- Workbench 去掉 `FPageRenderFn` / `_activeController` / 每帧 flush。DSL 页改走普通 `FPageBuilder` + `ui::build`；按钮计数仍是 `Reactive` 绑定。
- 新增契约：DirectConstructAttachesLiveWidgetsWithoutDescription、DirectConstructBindTextUpdatesWithoutRebuild、DirectConstructDetachStopsButtonClicks。

验证：`ya-gui-declarative-contract-test` 39/40（3 条新测试全绿）；唯一失败仍是既有的 `TypedContainerPropertiesPreserveIdentityAndInvalidationScope`（`ApplyUIContainerDescription` 强制 `_bAutoSize`，与本刀无关）。GUIWorkbench 编译通过。

下一步：Description / Reconciler / `UIRenderController` / apply hook 已无生产消费者，可以按「替换后删除」拆掉（保留 `UIDocument` 自己的 instantiate）。

## 2026-08-26 checkpoint：删除 Description / Reconciler / apply

生产路径已全部走 `ui::build`，按「替换后删除」拆掉 VDOM 转发层：

- 删除 `UIDescription`、`UIReconciler`、`UIRenderController`、`UIDeclarativeNodeAdapter`、各控件 `ApplyUI*`、registry `applyNode` / `std::any` apply hook。
- `Declarative.h` 只聚合 `Construct.h` + `ScreenStack.h`。`UIScreen` 只保留 mount/unmount/z-order/input blocking，没有 `render()` / `invalidate()` / `flush()`。
- 契约测试改为 live construct：typeId、snapshot 稳定、enabled/focusPolicy、button label、TextField focus、container authored size/clip、bindText、detach callback。去掉 compose/when/reconcile/controller 用例。
- `UIDocument::instantiate()` 与 registry `createInstance` / typeId / module lease **保留**。

验证：`ya-gui-declarative-contract-test` 19/19；`ya-gui-widgets-test` 159/159；`ya-gui-headless-host-test` 2/2；GUIWorkbench 编译通过。

已知缺口（G2，**搁置**）：`UIScreen::onUnmounted` 仍不 detach 子树。2026-08-26 用户确认：当前主线是 DSL Feature Gallery + 逐步替换 Editor ImGui，不接 GameUIHost / HUD / 模态栈。`UIScreen`/`ScreenStack` 保留为空壳，不扩展、不接入宿主，直到需要多块独立游戏 UI 表面。

## 2026-08-26 checkpoint：Widgets 页迁到 live construct

- `Construct.h` 增加 `checkBox` / `slider` / `comboBox` / `image`，以及 `share()` / `fillParent()`。
- GUIWorkbench Widgets 页改为一次 `ui::build`；控件 `_name` 保持 scenario 查找名（Counter / CheckA / BrightnessSlider / ApiCombo / NotesField）。
- Render 页仍是 raw retained 对照。

验证：`ya-gui-declarative-contract-test` 20/20；`widgets_interaction.jsonl` headless 全 checkpoint 通过。scenario 点击坐标按当前 workbench chrome（左侧页列表 ~238px）对齐到控件中心，页内结构未改。

## 2026-08-26 checkpoint：Layout + 简单 Gallery 页迁到 live construct

- Layout 页改为一次 `ui::build`；`DemoHBox` / `DemoVBox` / `SpacingSlider` 名称保留。`Construct.h` 补 `setClipChildren` / `setMainAxisAlignment` / Text `setHAlign`/`setVAlign`。HBox 经 `share()` 在 slider 回调里改 spacing。
- Menus / Theme / Unicode / 中文测试 同样改为 `ui::build`。Theme 页仍不给 `ThemeShowPanel` 上 authored color，toggle 继续换 tree-level UITheme。Popup 菜单仍在点击时 `UIMenu::create`（事件期 live API）。
- Render 页仍是 raw retained 对照。未迁：DragDrop / Modal / ScrollSplit / Gallery / Interactions / Dock / RoundedRect / Editor。

验证：`ya-gui-declarative-contract-test` 21/21（含 DirectConstructContainerClipAndMainAxisAlignment）；headless
- `layout_spacing_interaction.jsonl`：spacing 8 → `$gt` 16，route `SpacingSlider`
- `menus_popup_interaction.jsonl`：File 打开后 hover 切 Edit（mouse_move 经 hover-transparent shield，lastRoute 为 hitTest/`MenuBar_Edit`），outside press 仍走 popup dismiss
- `theme.jsonl`：ThemeToggle 切 light，`assert_validation_clean`

scenario 点击按当前 chrome 对齐；旧坐标会打到左侧页签（例如 Layout 的 `{250,507}` 打到 `Tab_Unicode`）。

下一步：RoundedRect（panel `setCornerRadius` + 自定义 anchor）；然后 DragDrop / Modal / ScrollSplit。Gallery mega-page、Dock、Editor ImGui 仍不进入本切片。G2 `UIScreen` 继续搁置。
