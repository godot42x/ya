# GUI Framework / Editor / Game UI 重构进度

## 2026-08-27 checkpoint：继续推进 G4.2，补 MenuBar label binding 与 hover/open lifecycle 并存契约

- `BindingContractTest.MenuBarLabelBindingSurvivesOpenMenuAndHoverRouting` 新增，补上 `UIMenuBarItem` 这条菜单入口控件的并存边界：外部 `bindLabel(Reactive<std::string>)` 继续作为权威 display source，而 hover 状态、menu open/close 生命周期、hover-routing 切换只负责 transient/lifecycle 行为，不能打断或覆盖 label binding。
- 这条回归覆盖了一个和 `UIText` 不同的场景：binding 生效时，menu item 仍然要能在点击后打开菜单、在 move/hover 路由中保持 open 状态，并允许 binding 值在菜单已打开时继续更新；解绑后则回到 authored fallback label，但不会顺手把当前 open menu 生命周期打断。
- 这一步没有触发实现修改，说明 `MenuBar` 当前已经满足“display binding 与 transient hover/open state 并存”的 contract；把它独立钉住后，后续若继续给 menu bar 增加快捷键、搜索、最近文件之类的 editor affordance，也不容易把交互生命周期和 label source 再搅回一起。
- 这样 `G4.2` 的覆盖面继续扩大：除了 Text/Button/SelectableRow/TreeView/TableGrid，现在菜单入口这条常见 editor chrome 路径也进入了自动化门禁。
- 验证：`xmake b ya-gui-widgets-test`、`./build/macosx/arm64/debug/ya-gui-widgets-test --gtest_filter='BindingContractTest.MenuBarLabelBindingSurvivesOpenMenuAndHoverRouting'`、`./build/macosx/arm64/debug/ya-gui-widgets-test --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：继续推进 G4.2，补 TableGrid selection binding 与 hover transient 并存契约

- `BindingContractTest.TableSelectionBindingCoexistsWithHoverTransientState` 新增，补上 `UITableGrid` 这条 data-driven 基础件的并存边界：外部 `bindSelection(Reactive<int>)` 负责权威选中行，控件内部 `_hoveredRow` 只负责 transient hover 反馈，两者互不覆盖。
- 这条回归同时锁住两类方向：一是 hover/move/clearTransientInputState 这类临时视觉状态不能偷偷改写 selection binding；二是外部 selection binding 的写回与用户点击选中仍然继续共享同一个 reactive 事实源。
- 这一步没有触发实现修改，说明 `UITableGrid` 当前已经满足这条 coexistence contract；但把它单独落成回归之后，后续若再给 table 加 editor affordance、drag handle 或 richer row chrome，就不会轻易把 transient state 和 selection source 搅回一起。
- 这也让 `G4.2` 的覆盖面从 `Text/Button/SelectableRow/TreeView` 再扩到另一个 data-driven 控件：现在至少 Text、Button、SelectableRow、TreeView、TableGrid 五条高频路径都有“多状态来源并存”的自动化门禁。
- 验证：`xmake b ya-gui-widgets-test`、`./build/macosx/arm64/debug/ya-gui-widgets-test --gtest_filter='BindingContractTest.TableSelectionBindingCoexistsWithHoverTransientState'`、`./build/macosx/arm64/debug/ya-gui-widgets-test --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：继续推进 G4.2，锁住 TreeView filter binding 与 expansion state 并存

- `BindingContractTest.TreeFilterBindingAndManualExpansionCoexistWithoutStickyReexpand` 新增，明确 `UITreeView` 上三类状态来源必须稳定共存：外部 `bindFilter()` reactive binding、控件自有的 per-node expand-state reactive、以及用户/宿主的 imperative `toggleExpanded()`。
- 这条回归锁住的语义是：filter 文本变化时，matching chain 应在第一次结构读取前展开；但在同一 filter 生命周期内，用户随后手动 collapse 仍然保持权威，不会被“每帧重新套用 filter expansion”偷偷顶回去；当 filter 再次变更时，新的匹配链才重新 one-shot 展开。
- 为了让这条契约真正成立，`UITreeView` 的 filter 读取从 `value()` 收口到 `get(Layout)`，使 filter binding 正式成为 layout 级依赖；同时 matching-chain expansion 前移到 `flattenVisible()` 的第一次结构读取路径，而不再只藏在 `paintSelf()` 里。否则 `filterRef->set(...)` 后树可能根本不脏，`buildSnapshot()` 会复用旧结构，绑定和结构状态实际上没有并起来。
- 这一步把 `G4.2` 从 Text/Button/SelectableRow 的状态并存进一步推进到了 data-driven widget：不仅有 display binding 和 transient state，也开始覆盖“外部结构 binding + 控件内部 retained state + imperative override”三者共存。
- 验证：`xmake b ya-gui-widgets-test`、`./build/macosx/arm64/debug/ya-gui-widgets-test --gtest_filter='BindingContractTest.TreeFilterBindingAndManualExpansionCoexistWithoutStickyReexpand'`、`./build/macosx/arm64/debug/ya-gui-widgets-test --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：清理阻塞 G4.2 回归的现有 GUI 编译问题

- 在补 `G4.2` 合约回归时，先撞到了工作区里已有的三处 GUI 编译阻塞，并顺手最小收口：`WidgetTree.h` 的 `_dragSourceKeepAlive` 重复声明被整理回单一定义；`DockFloatingHost.cpp` 与 `DockSpace.cpp` 的 workspace 回调 weak-self 绑定都从 `shared_from_this()` 的基类返回值改成了显式 typed cast。
- 这几处都不是新功能，而是现有 retained/dock 生命周期收口时留下的半成品：前两者会直接阻塞 `ya-gui-widgets-test` 编译，后两者还会让“workspace 持有回调，但 widget 已销毁”的防悬空策略在 Clang 下过不了类型检查。
- 这一步的目的只是恢复当前 GUI closure 的可验证状态，为后续继续补 binding/behavior coexistence contract 提供稳定门禁。
- 验证：`xmake b ya-gui-widgets-test`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：收口 G4.3，把 compound 迁移桥接契约落到事实源

- `G4.3 / G4.3a / G4.3b` 这一轮正式按完成态收口：计划里已经明确了 kernel / behavior / compound / future adapter 的四层边界，第一批对象的归宿判定表已经成形，而对应的首批实施顺序也已经兑现到实际改动上——`WorkbenchDemoPages.cpp` 的 demo drag/drop 已行为化，`UISelectableRow`、`UITreeView`、Dock 线都走了“保留 retained control / leaf primitive，本体减负、横切交互外提”的路线。
- `UICompoundWidget` 公开头现在也补上了这条边界：它属于 native retained widget layer，用于局部 composition root 与 retained state/lifecycle；它不是 runtime kernel，也不是 behavior registry、document host 或 future adapter 的统一宿主。
- `WorkbenchDemoPages.h` 同步注明：example 页面的纯组合区块继续优先走 builder helper，少量 bespoke retained widget 只是 demo / specialized control 示例，不代表 framework 要把所有复合 UI 都升成 `UICompoundWidget`。
- `todo.md` 因此把 `G4.3`、`G4.3a`、`G4.3b` 一并标为完成；后续若继续推进这条线，重点应转向真正的 future editor composite widget 试点或 `G5.2/G6.2` 的 adapter seam，而不是回头强推一轮“万物 compound 化”。
- 这一步只收口契约与迁移判定事实源，没有改变运行时行为。

## 2026-08-27 checkpoint：启动 G5.2，把 native DSL / future adapter 边界写回公开头

- 在 `Build.h`、`CompoundBuilder.h`、`Declarative.h` 的文件头注释里补回当前已经形成的架构事实：native retained DSL 是直接物化 live widget 的原生 authoring API，但不是 future React-like / HTML-CSS-JS / script/document adapter 的唯一入口。
- 这一步没有引入 adapter host，也没有恢复 description/reconciler 中间层；目标只是先把 `G5.2` 的最核心边界从 plan 文档落回公开头附近，避免后续新 consumer 误把 `Build.h` / `compound()` 当成所有声明式 authoring 的强制底座。
- 同时明确 `UICompoundWidget` 仍属于 native retained composition primitive：它可以作为局部组合根，但不承担 adapter host 或统一 component model 的职责。
- 验证：`xmake b ya-gui-declarative-contract-test`、`xmake r ya-gui-declarative-contract-test -- --gtest_filter='DeclarativeContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：收 G5.1 尾，把 declarative consumer 逐步切到细粒度头

- `Construct.h` 已经退成兼容聚合头后，这一刀继续把明确只依赖 authoring 入口的 consumer 改为直接 include 更细粒度头：`GUIWorkbench.cpp`、`WorkbenchDemoPages.cpp`、`WorkbenchSurface.cpp`、`EditorSurface.cpp` 改为 `Build.h`，`Declarative.h` 改为显式聚合 `Build.h + CompoundBuilder.h`。
- 这样 `Construct.h` 的职责进一步收窄为“向后兼容入口”，而不再继续承担当前主路径 consumer 的直接依赖；plan 中 `G5.1` 所要求的五类 declarative 边界不仅落了文件，也开始落到真实 include 面。
- `todo.md` 同步把 `G5.1` 标为完成，表示当前阶段的 `Construct.h` 拆分主目标已经达到；后续若继续精修 declarative 线，应转入 `G5.2` 的 native DSL / future adapter 边界，而不是继续把 `G5.1` 长期挂成进行中。
- 验证：`xmake b ya-gui-declarative-contract-test`、`xmake r ya-gui-declarative-contract-test -- --gtest_filter='DeclarativeContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：继续推进 G5.1，把 authoring 入口从 Construct.h 拆到 Build / CompoundBuilder

- 新增 `Build.h` 与 `CompoundBuilder.h`，把 `text/button/panel/...`、`build()`、`compound<T>()` 这些 authoring 入口从 `Construct.h` 中抽出，和前一刀拆出的 `ControlBuilders.h` / `LayoutBuilders.h` 形成更清晰的 declarative 头部分层。
- `Construct.h` 现在退成纯兼容聚合头，只负责保留长期稳定 include 路径；具体 builder 类、compound 入口、attach build 入口都已经各自落到独立职责文件，不再继续把所有 declarative authoring 面堆在一个 god header 里。
- 这样 G5.1 至少已经把 plan 里要求的 `BuilderBase / ControlBuilders / LayoutBuilders / CompoundBuilder / Build` 五类边界都显式落到了文件层，后续如果继续精修，会更多是命名、include 依赖和 consumer 渐进收口，而不是继续做大块结构拆分。
- 验证：`xmake b ya-gui-declarative-contract-test`、`xmake r ya-gui-declarative-contract-test -- --gtest_filter='DeclarativeContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：继续推进 G5.1，把具体 builders 再拆成 control / layout 两层

- 在 `BuilderBase.h` 之上继续落第二刀：新增 `ControlBuilders.h` 与 `LayoutBuilders.h`，把 `Construct.h` 中的具体 builder 定义按职责拆开。前者承载 `Text/Button/TextField/CheckBox/Slider/ComboBox/Image`，后者承载 `Panel/Container/Split/Scroll/Overlay/SizeBox`。
- `Construct.h` 现在进一步收窄成 authoring-facing 聚合入口：只 re-export 两类 builder 头，并保留 `text/button/panel/...`、`compound<T>()`、`build()` 这些 DSL 入口函数，不再同时承担所有具体 builder 类定义。
- 这一步继续遵守既有决策：native retained DSL 仍然直接物化 live widget，没有恢复 description/reconciler 中间层；这里只是在 G5.1 下把 declarative 头部分层从“公共模板基座”继续推进到“具体控件 builder 分类”。
- 验证：`xmake b ya-gui-declarative-contract-test`、`xmake r ya-gui-declarative-contract-test -- --gtest_filter='DeclarativeContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：推进 G5.1，先把通用 builder 基座从 Construct.h 拆出

- 新增 Engine/Source/Framework/GUI/Runtime/Declarative/include/GUI/Declarative/BuilderBase.h，把 declarative DSL 的公共基座统一收口到一个独立边界：UIWidgetBuilder / UICompoundWidgetType concept、makeLiveWidget()、TUIWidgetBuilder、TUIWidgetChildrenBuilder、TUICompoundWidgetBuilder 不再继续堆在 Construct.h 顶部。
- Construct.h 现在只保留具体控件/布局 builder、ui::compound<T>() 与 ui::build() 这类 authoring-facing 入口；通用模板层与具体控件层不再混成一个越来越难读的 header，为后续继续拆 ControlBuilders / LayoutBuilders / Build 留出落点。
- 这一步还没有改变 DSL 语义，也没有引入新的 declarative 中间层；目标只是先收紧 G5.1 的最上层公共模板面，避免 Construct.h 继续同时承担 runtime glue、template base 与所有具体 builder 三种职责。
- 验证：xmake b ya-gui-declarative-contract-test、xmake r ya-gui-declarative-contract-test -- --gtest_filter='DeclarativeContractTest.*'、xmake b GUIWorkbench 通过。

## 2026-08-27 checkpoint：继续推进 G4.2，锁住 behavior state 与 presenter state 并存

- `BindingContractTest.BehaviorDropHighlightDoesNotOverwritePresenterSelectionState` 新增后，把 `UISelectableRow` 的两类状态来源明确钉住：presenter 写入的 `_bSelected` 与 behavior 写入的 `_bDropHighlighted` 可以并存，拖放高亮的进入/退出不能把 presenter selection 覆盖或清掉。
- 这条契约和前面的 `TextBindingSurvivesImperativeFallbackWriteUntilUnbound`、`WidgetEnabledGateRemainsAuthoritativeOverButtonDisplayBinding`、`DisablingPressedButtonStillClearsPressSessionOnRelease` 一起，把 `G4.2` 从“binding 迁移后还能工作”推进到了“多种状态来源在同一 widget 上如何稳定共存”的更具体层面。
- 当前已经有三类共存边界被自动化覆盖：
  1. reactive binding + imperative fallback (`UIText`)
  2. reactive display binding + tree gate / transient press (`UIButton`)
  3. behavior state + presenter selection (`UISelectableRow`)
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.BehaviorDropHighlightDoesNotOverwritePresenterSelectionState'`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：修复 mid-press disable 遗留 capture / pressed 状态的 G4.2 bug

- `BindingContractTest.DisablingPressedButtonStillClearsPressSessionOnRelease` 暴露了一个真实问题：按钮已经按下并持有 pointer capture 后，如果中途被 `setEnabled(false)`，随后的 release 会因为 disabled subtree gate 被路由层直接丢弃，导致 `_bPressed` 与 pointer capture 残留。
- `WidgetTree::dispatchCapturedPointerEvent()` 现在对这条边界做了显式收口：当 captured widget 在 release 到来前已经处于 disabled subtree 中时，tree 会先清掉 capture，再让该 widget 丢弃自己的 transient input state，然后返回 `NotHandled`。
- 这保证了 `disabled subtree is input-inert` 与 `transient press/capture state never survives a disabled transition` 两条契约可以同时成立，不需要把“禁用后的 release 仍然正常业务处理”误当成默认语义。
- 同时把这条回归补进 `BindingContractTest`，让 `G4.2` 不只覆盖“binding 与 fallback 并存”，也开始覆盖“tree gate 与 transient state 并存”的框架级边界。
- 验证：`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.DisablingPressedButtonStillClearsPressSessionOnRelease'`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：继续推进 G4.2，锁住 reactive binding 与 imperative fallback / tree gate 并存

- `BindingContractTest.cpp` 继续补 `G4.2` 契约，不再只覆盖 persistent edge 的 detach / reattach，而是明确 reactive binding 与其他状态来源并存时的稳定边界。
- `BindingContractTest.TextBindingSurvivesImperativeFallbackWriteUntilUnbound` 锁住 `UIText` 的 fallback 语义：`bindText()` 生效后，`setText()` 继续维护 authored fallback，但不会打断当前 binding；解绑后回到最近一次 imperative 文本。
- `BindingContractTest.WidgetEnabledGateRemainsAuthoritativeOverButtonDisplayBinding` 锁住 `UIButton` 的双层 enabled 语义：`bindEnabled()` 只负责显示态/样式选择，而 `UIElement::setEnabled(false)` 代表 tree-level 输入门禁，binding 的值变化不能把它重新打开。
- 这一步进一步落实了 plan 里“imperative setter、widget transient state、behavior state、reactive binding 可以并存”的方向：现在至少在 Text / Button 这两个高频基础件上，binding 与 imperative fallback 的边界已经被自动化固定下来。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：补 G4.2 契约回归，锁住 imperative setter / reactive binding 并存语义

- 在 `BindingContractTest.cpp` 新增两条 `G4.2` 回归，明确 binding contract 的目标不是“只留一种状态来源”，而是允许 imperative fallback 与 reactive binding 并存且边界稳定。
- `BindingContractTest.TextBindingSurvivesImperativeFallbackWriteUntilUnbound` 锁住 `UIText` 的双轨语义：`bindText()` 激活后，`setText()` 继续维护 authored fallback，但不打断当前 binding；解绑后则回到最近一次 imperative 文本。
- `BindingContractTest.WidgetEnabledGateRemainsAuthoritativeOverButtonDisplayBinding` 锁住 `UIButton` 的两层 enabled 语义：`bindEnabled()` 只负责显示态/样式选择，而 `UIElement::setEnabled(false)` 的 tree-level 输入门禁仍然是权威，不会被 reactive 值写回偷偷重新打开。
- 这一步把 `G4.2` 从“Reactive 已经搬家”往前推进到“不同状态来源如何稳定共存”的第一批自动化契约，后续可以继续围绕 behavior state / adapter patch / transient widget state 增补同类回归。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：Reactive include 迁移线收尾到只剩兼容桥

- 继续清理后，仓库内对旧 `GUI/Widgets/Reactive.h` 的真实代码依赖已经清零；剩下的只是不影响语义的兼容桥本身以及迁移说明文字。
- 这意味着 `Runtime/Binding` 现在不仅是 `Reactive` 的事实源，也已经成为 framework / tests / workbench demo 的直接包含路径；旧 widgets 路径不再承担生产消费入口，只保留向后兼容职责。
- 因此，`G4.1` 可以按完成态理解：后续若要继续动 binding 线，重点已经从“搬家/改 include”切换到 `G4.2` 的契约细化，而不是继续追逐旧路径 consumer。

## 2026-08-27 checkpoint：收尾清理首批 behavior / binding 迁移遗留项

- `DockFloatingWindow` 的 tab-drag 入口不再保留只有一层转发意义的 `beginDockDrag()` wrapper；`UITabBar::_onTabDragBegin` 现在直接委托给 `FDockFloatingWindowPanelDragBehavior`，与 `DockSpace` 的 source behavior 入口保持一致。
- `DockSpace.cpp` 中关于“空白区域 drop 变成 floating window”的注释也同步改成当前事实：真正的 tear-off 发生在 drag session finish observer，而不是旧式 widget-level drop override。
- `todo.md` 同步收口：`G3.3`（drag/drop runtime 边界收敛）与 `G4.1`（Reactive 下沉到 Binding 层）在当前计划语义下已经完成，后续剩余工作进入 `G4.2` 的 binding contract 细化与更广泛 consumer 清理，不再继续把这两条保持为进行中。
- 验证：`xmake b ya-gui-widgets-test`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：收尾首批旧 Reactive include consumer 到 Binding 路径

- 在 `Reactive` 已下沉到 `Runtime/Binding`、`GUI/Widgets/Reactive.h` 已退成兼容桥之后，本刀继续把最后一批仍显式依赖旧 widgets 路径、且当前工作区干净的 consumer 切到 `GUI/Binding/Reactive.h`：`Button.h`、`MenuBar.h`、`SplitPane.h`、`Text.h`、`UIFrameSnapshotTest.cpp`、`WorkbenchDemoPages.cpp`。
- 这样 framework 的常用 leaf/container/control 与现有 binding tests/workbench demo 都已经直接消费新的 binding 路径；旧 `GUI/Widgets/Reactive.h` 继续保留，但当前只承担向后兼容桥职责，不再有真实代码 consumer。
- 这一步没有扩展 binding 语义，只是在 G4.1 的基础上继续降低旧 widgets 路径的直接消费者数量，让后续删除或进一步瘦身兼容桥时有更小、更可控的落点。
- 验证：`xmake b ya-gui-widgets-test`、`xmake b GUIWorkbench`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.*'`、`xmake r ya-gui-widgets-test -- --gtest_filter='UIFrameSnapshotTest.TreeView*'` 通过。

## 2026-08-27 checkpoint：补 Binding contract tests，锁住 persistent edge 的 detach / reattach 语义

- 新增 `Engine/Test/Source/BindingContractTest.cpp`，直接用新 public 路径 `GUI/Binding/Reactive.h` 建立 binding 层回归，而不是继续把所有 binding 语义都挂靠在旧 `UIFrameSnapshotTest.cpp` 上。
- `BindingContractTest.PersistentLayoutBindingOnDetachedWidgetDoesNotInvalidateTree` 锁住 persistent layout binding 在 detached widget 上的安全语义：ratio 写回可以继续打到 widget，但不得把已经脱树的控件重新拖回 live tree 的 layout invalidation。
- `BindingContractTest.PersistentLayoutBindingSurvivesDetachAndReattach` 锁住同一 persistent binding 在 detach / reattach 后仍然继续生效，证明 binding 语义属于 retained object，而不是“一次挂树生命周期内的临时副产品”。
- `Engine/Test/Test.xmake.lua` 已把这份新测试接进 `ya-gui-widgets-test` 与 `ya-gui-closure-test`，确保后续继续清理旧 `GUI/Widgets/Reactive.h` consumer 时，这条契约会一直被自动化守住。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='BindingContractTest.*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：Dock panel drag source / observer 收口到 behavior

- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.cpp` 新增局部 `FDockSpacePanelDragBehavior`，把 docked tab drag 的 payload 生成、preview observer、NoTarget tear-off 以及 `WidgetTree::beginDrag()` 全部从 `UITabBar::_onTabDragBegin` 的内联 lambda 收口到 behavior。`DockSpace` 本体只保留 dock tree 投影与 preview/dock model 语义。
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockFloatingWindow.cpp` 新增局部 `FDockFloatingWindowPanelDragBehavior`，把 floating tab drag 的 payload、跟随指针移动、chooser 持续显示、session finish 清理与 refresh 收口到 behavior；`beginDockDrag()` 现在只负责委托。
- 这一步把 Dock 线上剩余的 source/observer 半边也拉进了 behavior seam：到现在 Dock 的 drop target（DockSpace / FloatingWindow）和 drag source（DockSpace / FloatingWindow）都不再把 drag/drop runtime 协议散落在 widget-level override 或 ad-hoc lambda 里。
- 新增 `WidgetTreeTest.DockSpaceTabDragBehaviorStartsSessionAndTearsOffOnNoTarget` 与 `WidgetTreeTest.FloatingWindowTabDragBehaviorStartsDockPanelSession`，直接锁住 tab bar 触发的 source/observer 路径；并复跑 floating merge regression，确保 source 行为化没有破坏之前的 drop target seam。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.DockSpaceTabDragBehaviorStartsSessionAndTearsOffOnNoTarget'`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.FloatingWindowTabDragBehaviorStartsDockPanelSession'`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.DockPanelPayloadCanMergeIntoFloatingWindowThroughBehaviorTarget'`、`xmake b GUIWorkbench` 通过。

## 2026-08-27 checkpoint：Reactive 下沉到 Binding 层并保留 Widgets 兼容桥

- 新增 `Engine/Source/Framework/GUI/Runtime/Binding/Reactive.h/.cpp` 与 public 头 `include/GUI/Binding/Reactive.h`，把 `Reactive` / `ReactiveList` / `Computed` / paint dependency stack 的事实源从 `Runtime/Widgets` 下沉到更中性的 binding 层，兑现 G4.1 的第一步。
- `Runtime/Widgets/Reactive.h` 与 `include/GUI/Widgets/Reactive.h` 现在都退为兼容桥：旧 include 路径继续可用，但只转发到新的 binding 路径；旧 `Runtime/Widgets/Reactive.cpp` 退成空兼容 translation unit，避免和新的 binding 实现重复定义。
- `Runtime/Widgets/xmake.lua` 补进 `../Binding/*.cpp` 与 `../Binding/include`，保证 binding 层属于同一个 `ya-gui-widgets` closure，而不是新的独立宿主或平行框架。
- 首批 framework/core consumer 已切到 `GUI/Binding/Reactive.h`：`WidgetTree.h`、`UIElement.cpp`、`Style.h`、`TreeView.h`、`TableGrid.h`、`SplitPane.cpp`、`ScrollViewport.cpp`、`Container.cpp`、`GUIHeadlessHost.cpp`、`EditorSurface.h` 等；后续补刀已把剩余干净 consumer 一并迁完，旧路径当前只剩兼容桥自身。
- 验证：`xmake b ya-gui-widgets-test`、`xmake b GUIWorkbench`、`xmake r ya-gui-widgets-test -- --gtest_filter='UIFrameSnapshotTest.TreeView*'` 通过。

## 2026-08-27 checkpoint：DockFloatingWindow drop target 收口到 behavior

- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockFloatingWindow.cpp` 新增局部 `FDockFloatingWindowDropTargetBehavior`，把 floating window 对 dock-panel payload 的 accept / drop 协议收口到 behavior；浮窗本体继续保留 title drag、window move、resize 与 dock-drag observer 这类窗口语义。
- `DockFloatingWindow.h` 不再继续暴露 `canAcceptDrop/onDrop` 这组 widget-level override，保持“浮窗是 drop target”这一能力与“浮窗如何移动/缩放/重建内容”解耦，和 `TreeView` / `DockSpace` 的 seam 方向对齐。
- 新增 `WidgetTreeTest.DockPanelPayloadCanMergeIntoFloatingWindowThroughBehaviorTarget`，验证 docked panel 的 payload 经过 floating window 命中后，仍会正确通过 workspace 合并成新 tab；同时复跑 dock preview regression，确认这刀没有破坏 DockSpace 的点敏感 preview。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.DockPanelPayloadCanMergeIntoFloatingWindowThroughBehaviorTarget'`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview'`、`xmake b GUIWorkbench` 通过。

## 2026-08-26 checkpoint：DockSpace drop target 收口到 behavior

- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.cpp` 新增局部 `FDockSpaceDropTargetBehavior`，把 dock-panel payload 的 accept / drop / highlight-clear / hover-preview 协议收口到 behavior；`UIDockSpace` 本体继续保留 dock model 投影、preview 求解与 overlay 绘制。
- `UIDockSpace` 构造时默认挂上该 behavior，控件头文件不再继续暴露 `canAcceptDrop/onDrop/setDropHighlight/updateDropHover` 这一整组 widget-level override，保持 Dock 的业务语义和拖放协议 seam 解耦。
- 这一步与 `TreeView` 的 reorder 行为化对齐：`WidgetTree` 继续拥有 drag session / target routing，DockSpace 只通过 behavior 暴露 drop capability，point-sensitive preview 仍通过 framework 级 `updateDropHover` seam 驱动。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview'`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.BehaviorCanActAsDragSourceAndDropTargetWithoutDedicatedWidgetSubclass'`、`xmake r ya-gui-widgets-test -- --gtest_filter='ToolControlsTest.TreeView*'`、`xmake b GUIWorkbench` 通过。

## 2026-08-26 checkpoint：TreeView reorder drag/drop 外提到 behavior seam

- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/TreeView.cpp` 新增局部 `FTreeViewReorderDragBehavior` / `FTreeViewReorderDropBehavior`，把 TreeView reorder 的 press-capture、阈值起拖、payload 生成、drop hover、drop 回调全部收口到 behavior，而不是继续由 `UITreeView` 本体手写一整段私有协议。
- `UITreeView` 构造时默认挂上这两个 behavior；控件本体保留 tree row 的命中、selection、expand/collapse、paint 与 reorder 回调落点，不再 override `canAcceptDrop/onDrop/setDropHighlight`。
- `UIBehavior` / `UIDropTargetBehavior` 补 `updateDropHover` 委托，框架级 drop target seam 现在既能承载“静态高亮”，也能承载 TreeView / Dock 这类“跟随指针更新预览位置”的点敏感拖放目标。
- `TreeView.h` 增加 `setReorderable` / `setOnReorderHandler`，`Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp` 的 Gallery tree demo 已切到这组 runtime setter，不再继续从上层直接写 `_bReorderable` / `_onReorder`。
- 新增 `ToolControlsTest.TreeViewReorderUsesBehaviorBackedDragDrop`，验证 TreeView 行 reorder 仍保留原有 capture -> threshold -> drag session -> drop callback 契约；同时复跑 `SelectableRow` 与 framework behavior drag/drop 合同，确保这次 seam 扩张没有破坏前一批行为化结果。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='ToolControlsTest.TreeView*'`、`xmake r ya-gui-widgets-test -- --gtest_filter='ToolControlsTest.SelectableRow*'`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.BehaviorCanActAsDragSourceAndDropTargetWithoutDedicatedWidgetSubclass'`、`xmake b GUIWorkbench` 通过。

## 2026-08-26 checkpoint：drag/drop behavior 收口为 framework 级可复用基础件

- `Engine/Source/Framework/GUI/Runtime/Widgets/UIBehavior.h/.cpp` 新增 `UIDragSourceBehavior` 与 `UIDropTargetBehavior`，把通用 drag source / drop target 协议（press 状态、阈值起拖、payload factory、highlight/drop handler）收口到 framework 层。
- `UISelectableRow` 的内置拖放逻辑改为基于这两个 framework behavior 组装：row 不再自己持有一整段私有拖放协议实现，只保留 row 自身的 selection / activation / hover / pressed 语义。
- `Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp` 的 demo drag/drop 也迁到了这两个 framework behavior，上层只提供 tile 的 pressed/highlight 显示和 payload/drop 回调，不再各自复制一套协议代码。
- 这一步把前两刀的“局部 behavior”进一步上提成 runtime seam，本质上是在兑现 G3.3：`WidgetTree` 继续拥有 drag session / routing，widget 只暴露能力落点，可复用协议收口到 behavior。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='ToolControlsTest.SelectableRow*'`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.BehaviorCanActAsDragSourceAndDropTargetWithoutDedicatedWidgetSubclass'`、`xmake b GUIWorkbench` 通过。

## 2026-08-26 checkpoint：SelectableRow 将拖放协议外提到内置 behavior

- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/SelectableRow.h/.cpp` 现在把 drag source / drop target / highlight / drop callback 收敛到内置 `FSelectableRowDragDropBehavior`；row 本体保留 selection / activation / hover / pressed 这些 leaf 语义。
- 为了兼容现有 pointer-capture 语义，起拖阈值也放进了该 behavior 的 `handleInputEvent(MouseMoved)`，不再让 row 本体手动 `beginDrag()`；拖放协议继续存在，但不再和 leaf 表现层耦在一起。
- 外部调用面保持兼容：`_bDraggable` / `_dragPayload` / `_dragGhostLabel` / `_onDropped` 现阶段仍可继续被 Workbench/Tooling 写入，先完成“协议外提”，不在这一刀强推 API 迁移。
- 进一步收口调用面：`UISelectableRow` 新增 `setDraggable` / `setDragPayload` / `setDragGhostLabel` / `setOnDropHandler`；`WorkbenchSurface::rebuildItemRows()` 已改为走这些 runtime setter，不再继续直接写 row 的拖放字段。
- 新增 `ToolControlsTest.SelectableRowDraggableRowsUseBehaviorBackedDragDrop`，验证 draggable row 仍可在 capture 语义下起拖、跨 row drop，并保留原有选择/激活/hover 契约测试全绿。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='ToolControlsTest.SelectableRow*'`、`xmake b GUIWorkbench` 均通过。

## 2026-08-26 checkpoint：Workbench drag/drop demo 完成第一刀行为化

- `Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp` 已移除 `FDemoDragItem` / `FDemoDropZone` 两个 demo 专用拖放 widget，改为局部展示 widget `FDemoDragDropTile` + `FDemoDragSourceBehavior` / `FDemoDropTargetBehavior`。
- drag source 的 press / release / drag-detect 责任现在归 `UIBehavior`：按下时只维护 pressed visual 与 capture，真正超过阈值后仍由 `WidgetTree` 调 `onDragDetected()` 启动 drag session；行为里主动清 pressed/capture，避免旧 demo source 自己背拖放协议。
- drop target 的 accept / highlight / drop 回调也迁到 `UIBehavior`；展示 widget 只保留 label/highlight paint，不再自己实现 drop 协议。
- Gallery 的 drag/drop section 与独立 DragDrop 页都切到了这套局部 behavior，用法保持 `child(UIElementRef)` 挂进 DSL 壳，不扩 `Construct.h`。
- 验证：`xmake b ya-gui-widgets-test`、`xmake r ya-gui-widgets-test -- --gtest_filter='WidgetTreeTest.BehaviorCanActAsDragSourceAndDropTargetWithoutDedicatedWidgetSubclass'`、`xmake b GUIWorkbench`、`xmake r GUIWorkbench -- --start-page DragDrop --scenario Example/GUIWorkbench/Scenarios/dragdrop_interaction.jsonl --scenario-dump-dir /tmp/ya_dragdrop_scenario` 均通过。

## 2026-08-26 checkpoint：补齐 CompoundWidget 与新底层的迁移桥接契约

- 计划层正式补齐 `UICompoundWidget` 与 Runtime Kernel / `UIBehavior` / future adapter 的关系，避免后续迁移只停留在“它不是唯一 component model”的抽象口号。
- 边界现明确为：Kernel 只保留 tree/layout/input/focus/invalidate/snapshot/host bridge；`UICompoundWidget` 属于 Native Retained Widget Layer，只承担局部 composition root、内部 retained 子树、局部状态/生命周期与必要时的复杂输入/自定义 paint。
- 横切能力统一优先收敛到 `UIBehavior`，不能继续把 drag/drop、tooltip、shortcut、editor affordance 既塞进 leaf widget，又塞进 compound 基类。
- editor/game 现有复合控件的迁移判定规则正式固定为三分：无局部状态 → builder helper；有局部 retained 状态/生命周期 → `UICompoundWidget`；横切交互能力 → `UIBehavior`。
- future React-like / HTML-CSS-JS / script/document adapter 的接入点也随之钉死：它们直接投影到 Runtime Kernel，不得把 `UICompoundWidget` 当成唯一宿主、唯一 component 模型或 adapter host。
- 现状盘点同时确认：截至 2026-08-26，仓库里还没有任何实际控件继承 `UICompoundWidget`；当前只有基类和 `ui::compound<T>()` 入口。第一版迁移判定表已补进 plan，用于把 demo widget、specialized control、future compound 三类分流。
- 进一步补齐了 G4.3a 的逐文件迁移清单：先从 `Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp` 的 drag/drop demo 行为化入手；`SelectableRow` 做 leaf 减负；`TreeView` / `TableGrid` / `Menu` / `Dock` 明确不进入第一批 compound 化。
- 日期说明：今天是 2026-08-26。`progress.md` 中既有的 `2026-08-27 checkpoint` 条目按当前时间属于未来计划/预填记录，不应被解读为今天已经完成的事实。

## 2026-08-27 checkpoint：UIBehavior 最小运行时 seam 落地（第一刀）

- 新增 UIBehavior 基类，先定义最小横切能力接口：attach / detach / tick / preview / target / bubble / invalidate-owner bridge。
- UIElement 新增 behavior 宿主能力：addBehavior / removeBehavior / hasBehavior / getBehaviors，并把 behavior 接入默认 tick、preview、target、bubble 路由。
- WidgetTree 在 attach / detach 生命周期中驱动 behavior 的 onAttached / onDetached，使 behavior 与普通 retained widget 共享同一棵 runtime tree 的生命周期。
- 新增 WidgetTreeTest 契约用例：
  - BehaviorLifecycleTickAndInvalidationFollowOwner
  - BehaviorParticipatesInPreviewTargetAndBubbleRouting
- 本切片目标是先打通 runtime seam，还没有开始把 Workbench drag/drop demo 迁到 behavior；那是下一刀。

## 2026-08-27 checkpoint：重排下一阶段主线（behavior / binding / declarative）

基于最近几轮对 compound widget、drag/drop、Reactive 分层、Slate-like native layer 与 future adapter 的讨论，当前主线重新收敛为：

- 第一优先级不是继续铺 Editor feature，而是先补 `UIBehavior` 最小模型，给 drag/drop、tooltip、shortcut、editor 交互 affordance 一个统一横切承载层。
- Workbench drag/drop demo 应优先迁到 `UIBehavior` + 普通 retained widget / 静态 DSL，不再把 `FDemoDragItem` / `FDemoDropZone` 作为长期结构。
- `WidgetTree` 保持 drag/drop gesture、session、routing owner；widget 只暴露 capability hook；behavior 承载复用逻辑。
- `Reactive` 后续应从 `Runtime/Widgets/Reactive.h` 拆到更中性的 Binding/Dataflow 子层；在此之前，不应再让 editor 功能继续反向固化现有内核边界。
- `Construct.h` 已出现 god file 风险，应在 behavior / binding seam 稳定后尽快拆分，而不是继续线性增大。
- `UICompoundWidget` 的定位正式固定为 native retained composition primitive，而不是唯一 declarative/component model；未来 React-like / HTML-CSS-JS / script/document adapter 应通过 adapter seam 接入 runtime kernel。

因此，下一轮具体执行顺序调整为：

1. `UIBehavior` 最小接口 + 宿主挂载能力 + contract test。
2. Workbench drag/drop demo 用 behavior 完成第一批验证。
3. `Reactive` 从 Widgets 降到 Binding/Dataflow。
4. 拆 `Declarative/Construct.h`。
5. 再恢复 GameEditor 剩余 ImGui feature migration。
6. 最后预留 future adapter seam。

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
- Render 页仍是 raw retained 对照。未迁：DragDrop / ScrollSplit / Gallery / Interactions / Dock / Editor。

验证：`ya-gui-declarative-contract-test` 21/21（含 DirectConstructContainerClipAndMainAxisAlignment）；headless
- `layout_spacing_interaction.jsonl`：spacing 8 → `$gt` 16，route `SpacingSlider`
- `menus_popup_interaction.jsonl`：File 打开后 hover 切 Edit（mouse_move 经 hover-transparent shield，lastRoute 为 hitTest/`MenuBar_Edit`），outside press 仍走 popup dismiss
- `theme.jsonl`：ThemeToggle 切 light，`assert_validation_clean`

scenario 点击按当前 chrome 对齐；旧坐标会打到左侧页签（例如 Layout 的 `{250,507}` 打到 `Tab_Unicode`）。

## 2026-08-27 checkpoint：RoundedRect + Modal 迁到 live construct

- `Construct.h` 补 `setAnchors` 与 panel `setCornerRadius`。`ui::build(tree, parent, builder)`：先组 builder，再单独 attach。
- RoundedRect 页一次 `ui::build`；卡片名 Round0/8/16/32、`RoundedNested` / `RoundedNestedInner` 保留。
- Modal 页壳迁到 `ui::build`（`OpenModal` 仍 `share()` 给 automation）；弹层仍在点击时 live 组装 `UIPopupOverlay`（与 Menus 的 `UIMenu::create` 同类）。

验证：`ya-gui-declarative-contract-test` 22/22（含 DirectConstructPanelCornerRadiusAndAnchors）；`modal_interaction.jsonl` headless 全 checkpoint 通过。OpenModal 点击对齐到 `{803,98}`。

下一步：DragDrop（页壳 DSL + `FDemoDragItem`/`FDemoDropZone` 经 `child(UIElementRef)`）；ScrollSplit（需要 split/scroll builder + Fill slot）。Gallery mega-page、Dock、Editor ImGui 仍不进入本切片。G2 `UIScreen` 继续搁置。

## 2026-08-27 checkpoint：DragDrop + ScrollSplit 迁到 live construct

- `Construct.h` 补 `ui::splitPane` / `ui::scroll` 与 container `childFill`（split 不是唯一 child，不能靠 stretch-last）。
- DragDrop 页壳一次 `ui::build`；`FDemoDragItem` / `FDemoDropZone` 经 `child(UIElementRef)` 挂入。名称保留 `Drag_asset.texture.diffuse` / `DropZone`。
- ScrollSplit 一次 `ui::build`：`DemoSplit` / `DemoScroll` / `DemoScrollList` / `ScrollRow{i}` / `DemoSplitRight` 保留。列表改为 40 行，使默认 1280x800 下 `maxOffset > 0`（原先 24 行在 Fill split 里装得下，wheel 断言永不触发）。
- Render 页仍是 raw retained 对照。未迁：Gallery mega-page / Interactions / Dock / Editor ImGui。

验证：`ya-gui-declarative-contract-test` 23/23（含 DirectConstructSplitScrollAndFillSlot）；headless
- `dragdrop_interaction.jsonl`：`Drag_asset.texture.diffuse` → `DropZone`，lastRoute `dragSession`
- `resize_scrollsplit_interaction_stress.jsonl`：四次 resize 间 divider 真的拖动（ratio 0.38 → ~0.69），wheel 打在 `DemoScroll` 内

scenario 点击按当前 chrome 对齐；旧 `{96,122}` / `{180,360}` 落在左侧页列表。800 宽会抬高 shell split 的 min-first clamp，回到 1280 后 DemoHost x 不再是初始的 ≈311。

下一步：Gallery mega-page（仍不要和 Dock / Interactions / Editor ImGui 绑在同一切片）。G2 `UIScreen` 继续搁置。

## 2026-08-27 checkpoint：Box 四边 margin + Overlay + SizeBox

- `FMargin`（四边 inset）收口 box slot / overlay slot / `UISingleChildLayout` padding。`glm::vec2` 仍表示左右/上下对称。`FMargin` 带四参数构造，避免 `{x,y}` 被当成 left/top。
- Box arrange 用主轴 before/after margin，不再 `margin * 2`。Construct：`column.child(node, FBoxSlotArgs{...})`；`childFill` 仍是 Fill 简写。
- `UIOverlay` + `UIOverlayLayout` / `UIOverlaySlot`：每个 child 在同一父 rect 内独立 Fill/Start/Center/End + padding。不是 `UIPopupOverlay`。DSL：`ui::overlay`。
- `UISizeBox`：单 child 约束盒（padding + 可选宽/高 override + min/max），复用 `UISingleChildLayout`。DSL：`ui::sizeBox`。
- dump：`layout.type` = `overlay` / `sizeBox`；box/overlay slot margin、padding 输出四边。

验证：`ya-gui-declarative-contract-test` 24/24（含 DirectConstructBoxSlotOverlayAndSizeBox）；`WidgetLayoutTest` 新增四边 margin / Overlay / SizeBox 全绿。
`WidgetLayoutTest.ScaledViewScalesFallbackGlyphsByOwnDesignSize` 仍失败（font fallback 缩放，与本切片无关）。

未做：wrap / flow Grid / AspectRatio（后置）。未迁：Gallery mega-page / Interactions / Dock / Editor ImGui。G2 `UIScreen` 继续搁置。

下一步：Gallery mega-page（仍不要和 Dock / Interactions / Editor ImGui 绑在同一切片）。

## 2026-08-27 checkpoint：Gallery mega-page 迁到 live construct

- `buildGalleryDemo` 一次 `ui::build`：`panel("GalleryDemo").fillParent()` → `scroll("GalleryScroll")` → `column("GalleryForm")`。
- Construct 只补本页已有控件真正用到的 setter：Text `setFillBackground`、Button `bindEnabled` / `setContentPadding`、TextField `setOnTextChanged`、Split `bindSplitRatio`。不新增 TreeView / TableGrid / MenuBar / InputExtras builder。
- 复杂控件经 `child(UIElementRef)` 挂入：`UIMenuBar`、`UITreeView`、`UITableGrid`、`FVectorDemoCanvas`、DragFloat/SpinBox/Radio/ColorEdit/SearchCombo、DragSource/DropTarget。Table cell 按钮先 `demoButton` → `share()` → `addDetachedChild` + `getCellSlot()->setCell(3, 2)`。
- scenario 控件名全部保留。点击坐标按当前 chrome（1280×720，DemoHost x≈311）重对准；旧 x≈151 会打到左侧页列表。

验证：headless `--start-page Gallery`
- `gallery_vector.jsonl` / `gallery_table.jsonl` / `gallery_inputs.jsonl` / `gallery_drop.jsonl` 存在性断言
- `gallery_tree_edit.jsonl`：filter `Li` → `visibleRows:4`，折叠 Light → `visibleRows:2`
- `gallery_acceptance.jsonl`：滚到底后 DragFloat=1.5、SpinBox=4、ColorEdit channel 3、palette 开合、SearchCombo `To` 选挑

`gallery_p1.jsonl` 属于 Interactions 页，不在本切片。未迁：Interactions / Dock / Editor ImGui。G2 `UIScreen` 继续搁置。

下一步：Interactions（tooltip / wrap / disable / dialog），仍不要和 Dock / Editor ImGui 绑在同一切片。

## 2026-08-27 checkpoint：Interactions 迁到 live construct

- `buildInteractionsDemo` 一次 `ui::build`。名称保留：`TooltipBtn` / `WrappedText` / `DisableGroup` / `GroupBtnA` / `GroupBtnB` / `ToggleGroupBtn` / `OpenDialogBtn`。
- Construct 补 base `setTooltip`、Text `setWrap` / `setMaxWrapWidth`。DisableGroup 经 `share()` 给 toggle 回调 `setEnabled`。
- WrappedText 用 `FBoxSlotArgs{.crossAlignment = Start}`，否则 cross stretch 会把 wrap 宽度撑满 form（scenario 锁 `w:360`）。
- Dialog 仍在点击时 `UIDialog::create` + `open(tree)`（与 Menus / Modal 同类：事件期 live 组装）。

验证：`ya-gui-declarative-contract-test` 25/25（含 DirectConstructTooltipAndWrap）；headless `--start-page Interactions` `gallery_p1.jsonl` 全 checkpoint 通过（tooltip dwell、subtree disable `notHandled`、DialogOK 关弹层）。点击按当前 chrome 对齐；旧 tooltip `(100,115)` / GroupBtnA `(86,280)` 落在左侧页列表。

未迁：Dock / Editor ImGui。G2 `UIScreen` 继续搁置。

下一步：Dock（`UIDockSpace` / floating host 经 `child(UIElementRef)` + `attachToLayer`），不要和 Editor ImGui 绑在同一切片。

## 2026-08-27 checkpoint：Dock 迁到 live construct

- `buildDockDemo` 一次 `ui::build`：`column("DockDemo").fillParent().childFill(DemoDock)`。`UIDockSpace` / `UIDockFloatingHost` 不进 Construct。
- Panel body 走 DSL：`ui::panel(name+"_Body").setStyleKey("panel.canvas")` + fillParent label。名称保留 `DemoDock` / `DemoFloatingHost` / `Scene_Body` 等。
- Floating host 仍 `attachToLayer(Popup)`；model split 仍事件期之前的 live API。
- `beginDockDrag` 的 `lastPreview` 改为 `shared_ptr` 捕获（原先栈引用在 observer 回调里 UAF）。
- `dock_floating.jsonl`：re-dock 走浮动窗口的 `Tab_Scene`（title-empty 只移动窗口）；tear-off 后 Console 占中心叶，drop 必须打在 chooser 中心块（≈776,406），不能只丢在 leaf 内容上。

验证：`ya-gui-declarative-contract-test` DirectConstructPanelCornerRadiusAndAnchors 含 `setStyleKey`；headless `--start-page Dock`
- `dock_cardinal_split.jsonl` 结构锁
- `dock.jsonl`：Console tab 拖进 Scene，`!DockLeaf7`
- `dock_floating.jsonl`：tear-off → `FloatingWindow1` → tab 拖回 chooser → `!FloatingWindow1`

未迁：GameEditor ImGui。G2 `UIScreen` 继续搁置。Render 仍是 raw retained 对照。

下一步：Workbench 内置 Editor demo 迁到 live construct（不是 GameEditor ImGui）。

## 2026-08-27 checkpoint：Workbench Editor demo 迁到 live construct

- `FWorkbenchSurface::buildEditorDemo` 一次 `ui::build`。删掉 `buildToolbar` / `buildDocumentList` / `buildCanvas` / `buildInspector`。名称保留：`EditorDemo` / `Toolbar` / `Add` / `Remove` / `Rename` / `ResetLayout` / `MainSplit` / `ItemList` / `ItemScroll` / `RowList` / `RightSplit` / `PreviewCanvas` / `SelectionHighlight` / `PreviewName` / `Inspector` / `NameField` / `VisibleToggle` 等。
- Construct 补 base `setVisibility`、split `setPadding`（MainSplit `{0,42}` 给 toolbar 让位）。成员 `share()` 给 `syncPresentationState` / smoke。
- `rebuildItemRows()` 仍 live 重建 `UISelectableRow`（`Row_<id>` / `RowLabel_<id>`），不进 Construct。
- 不要把这条线和 GameEditor ImGui 替换绑在一起。

验证：`ya-gui-declarative-contract-test` 25/25（`DirectConstructSplitScrollAndFillSlot` 含 split `setPadding`）；headless `--start-page Editor` `editor_inspector_interaction.jsonl` 全 checkpoint 通过。点击按当前 chrome 对齐（1280×800，DemoHost x≈311）：Sphere 行 ≈(428,165)，VisibleToggle ≈(1169,210)，NameField 右侧 ≈(1240,157)。旧 `(150,187)` 落在左侧页列表。`--smoke-actions`（含 Editor Add / rename / drag reparent）PASS。

未迁：G2 `UIScreen` 继续搁置。Render 仍是 raw retained 对照。

2026-08-27 checkpoint：GameEditor chrome 整窗替换（不再走 ImGui 内嵌 panel）。

- 新增 `EditorSurface`：一棵填满窗口的 WidgetTree（菜单 / 工具栏 / DockSpace /
  Viewport live `UIImage` / Hierarchy TreeView / Inspector name+Transform /
  Frame Stats / Workbench 嵌入）。无项目时是 Project Browser。
- `replayUIFrameSnapshot` 把 snapshot 画进已打开的 presentation raster pass
  （swapchain format，`EditorToolSurface` slot）。`UIImage::setTexture` 采样离屏
  3D compose RT。
- `EditorModule::onPresentation` 不再 `onImGuiRender` / `GuiSystem::submit`；
  `EditorInputNode` 直接 `dispatchEvent`。Workbench 用 `buildUI(tree, parent)`。
- `ya-game-editor` / `GUIWorkbench` 编译通过。原 ImGui editor 文件仍编译，尚未删除。

剩余 ImGui 缺口（删文件 / 摘 `imgui-local` 之前必须迁完）：

- 反射 Inspector（`TypeRenderer` / `DetailsView`）
- Content Browser / FileExplorer / FilePicker
- ImGuizmo 3D 操纵器（当前用 Inspector 改 Transform + 选中包围盒 overlay）
- Runtime Tools / UI Designer / debug image viewer / Render Graph
- `GuiSystem` init 仍在（FilePicker 等残留）；摘掉前不要删 imgui 依赖

下一步：按面板把剩余 ImGui 功能迁进 `EditorSurface` dock，迁完再删
`EditorLayer::onImGuiRender` 与 imgui editor 文件。不要回到「单 panel 内嵌 ImGui」路径。

## 下一轮重构前置：跨框架设计参考与 YA 组合模型

本节是下一轮 GUI 重构的前置依赖和设计参考。目标不是一比一复刻 Slate，而是在 retained widget tree 的基础上吸收各框架最有价值的边界。

### 参考框架与吸收点

- **Slate**：`SWidget` 是有身份的 runtime widget；`SCompoundWidget::Construct()` 负责一次性组装内部子树；`Tick` / 输入 / `Invalidate` 由实体 widget 驱动。YA 对应 `UIElement` + `UICompoundWidget`。
- **Flutter**：区分无状态组合和有状态 widget。YA 中静态 DSL helper 返回 builder；只有拥有状态、生命周期、Tick 或复杂输入的对象才升级为 `UICompoundWidget`。
- **SwiftUI**：强化 identity、lifetime、dependency 三者边界。`_stableKey` 只表达身份，不等于显示名；Reactive 依赖决定局部 paint/layout invalidate，不用整页重建替代增量更新。
- **Jetpack Compose**：吸收 state hoisting 与 Modifier/Behavior 思路。业务状态留在 model/presenter；交互能力（tooltip、drag/drop、focus、快捷键）应是可组合行为，不应污染 `UIButton` 等基础控件。
- **Qt/QML**：吸收 property binding、change notification 和 state 切换。authoring state、reactive state、transient input state 分层，样式解析只消费明确的 widget state。
- **Qt Model/View**：Editor 的树、表、列表、属性面板采用 model/view/delegate；数据集合不应被塞进每行 widget，也不应通过整页重跑刷新。
- **UMG**：DragDrop operation 独立于 source/target widget；任意 widget 可以参与 `OnDragDetected` / `OnDrop`，drop visual 由目标或行为决定，而不是 `UIButton` 固有能力。
- **Dear ImGui**：仅吸收工具层的快速原型、draw-list 和 automation 便利性；不作为最终 retained runtime/editor widget 架构。

### YA 的三种构造层级

1. **基础控件 builder**：`ui::button()`、`ui::text()`、`ui::panel()` 等，直接物化为 live `UIElement`。
2. **应用层 DSL helper**：函数返回 typed builder，只做静态组合；例如 `dragItem()` 默认返回 `button.child(text)`，不定义 demo 专用 `UIElement` 子类，也不重载 `paintSelf()`。
3. **`UICompoundWidget`**：Slate `SCompoundWidget` 的对应物。用于拥有独立状态、`construct()`、Tick、复杂输入/焦点策略、内部子树管理或可选自定义 paint 的复合控件。其 `construct()` 内部仍使用原始 DSL，最终仍是一棵 retained widget tree。

推荐树形关系：

```text
UIElement
├── 基础 leaf/container widgets
└── UICompoundWidget
    ├── framework composite controls
    ├── game UI components
    └── editor components
```

### 行为、状态与样式边界

- `UIElement` 继续拥有 identity、layout、input route、paint、focus/capture 和 invalidate 基础契约。
- `UICompoundWidget` 只增加局部 composition root 和生命周期，不引入第二套 tree/reconciler。
- DragDrop、Tooltip、快捷键、可访问性等横向能力抽象为 `UIBehavior` / `UIModifier`（名称待定），通过强类型对象挂载；禁止把 drag 专用字段直接加入 `UIButton`。
- DragDrop operation 独立存在；source/target 只是行为宿主，drop highlight 由行为或宿主控件自行决定。
- 状态分为：`AuthoringState`、`ReactiveState`、`TransientState`。Transient 状态（hover/pressed/focused/drag-over）不得改变基础控件职责边界。
- Theme/style 只负责 `StyleKey + WidgetState -> ResolvedAppearance`；业务颜色、纹理和特殊 drop visual 留在 project/game/editor 层。

### 下一轮实现前置依赖

- [ ] 定义 `UICompoundWidget` 的 `construct / onAttached / onDetached / tick` 生命周期和 content slot 契约。
- [ ] 定义 typed `UICompoundWidgetBuilder<T>`，使 compound widget 与普通 builder 共用 `ui::build`。
- [ ] 定义行为挂载模型（优先 `UIBehavior`/`withBehavior`，避免 Button 专用 drag API）。
- [ ] 明确 behavior 的 ownership、detach 清理、invalidate 传播和 tick 注册规则。
- [ ] 为 builder helper、compound widget、behavior 分别补 contract/unit tests。
- [ ] 将 `FDemoDragItem` / `FDemoDropZone` 作为迁移目标：简单版本改为 helper builder；只有出现独立状态或生命周期时才升级为 compound widget。
- [ ] Editor 列表/树/Inspector 在迁移前先定义 model/view/delegate 边界，避免把业务数据绑定到单个 widget 实例。

本节完成前，不应继续向 `UIButton`、`UIPanel` 等基础控件添加 drag/drop、editor 专用或项目专用字段。

## 2026-08-26 checkpoint：CompoundWidget 生命周期与统一 Tick 驱动

- 新增 `UICompoundWidget`：保留普通 `UIElement` 身份，在首次 attach 前调用一次 `construct()`，内部子树仍通过静态 DSL 组装。
- `UIElement` 增加 `prepareForAttach` / `onAttached` / `onDetached` / `tick` 生命周期钩子；普通基础控件默认 no-op，不承担 compound 语义。
- `WidgetTree::tick(dt)` 成为唯一 runtime 驱动入口；只调用 `wantsTick()` 的已挂载、可见 widget。windowed host 与 headless host 在 `updateUI()` / `buildSnapshot()` 前统一调用。
- detach 会递归触发 `onDetached()`；同树 reparent 不重复 construct/attach 生命周期；跨树移动按 detach → attach 处理。
- `ya-gui-widgets-test`：169/169，新增 `CompoundWidgetConstructsOnceAndTicksOnlyWhileAttached`，覆盖一次构造、attach/detach Tick 边界和重新 attach 不重复构造；`GUIWorkbench` build ok。

下一步：补齐 `UICompoundWidgetBuilder<T>` 与行为挂载模型（`UIBehavior` / `withBehavior`），再将 `FDemoDragItem` / `FDemoDropZone` 分别收敛为简单 builder helper 或真正有状态的 compound widget；不得把 drag 专用状态加入 `UIButton`。

## 2026-08-26 checkpoint：CompoundWidget typed builder 入口

- `Construct.h` 增加 `UICompoundWidgetType`、`TUICompoundWidgetBuilder<T>` 与 `ui::compound<T>(key, displayName, ...)`。
- compound builder 不走 `UITypeRegistry`，而是直接构造 typed `UICompoundWidget` 实例，并统一写入 `_stableKey` / `_name` / `_bAutoSize`，随后仍通过 `ui::build(tree, parent, builder)` 接入同一 retained tree。
- 这样应用层 / editor 层复合控件可以像基础控件一样进入静态 DSL，而不需要裸 `make_shared` + 手工 attach 作为旁路。
- `DeclarativeContractTest` 新增 `CompoundWidgetBuilderBuildsTypedLiveWidget`，验证 `share()` 返回 typed live widget、`ui::build` 后 identity 正确、`construct()` 只执行一次。

验证：`xmake r ya-gui-widgets-test -- --gtest_filter='DeclarativeContractTest.CompoundWidget*'` 2/2 通过。

下一步：给 compound/builder 接入 behavior 挂载模型，再把 Workbench/GameEditor 中适合的 helper/复合控件迁到 `ui::compound<T>`。

## 下一轮前置原则：Slate-like native layer 不是唯一编程模型

当前引入 `UICompoundWidget` / leaf/compound 风格分层，不应被理解为“框架未来只能是 Slate”。正确定位：**Slate-like 只是 native retained widget layer；不是唯一 authoring / declarative programming model。**

### 四层架构定位

1. **UI Runtime Kernel**
   - `UIElement`
   - `WidgetTree`
   - layout / paint / input / focus / dragdrop / invalidate
   - snapshot / resource / host bridge
2. **Native Retained Widget Layer**
   - leaf / compound widgets
   - 原生 C++ 控件
   - Slate 风格静态 DSL（`Construct.h` / `ui::build`）
3. **Declarative Adapter Layer**
   - React-like component adapter
   - HTML/CSS/JS document adapter
   - editor-authored UI document
   - script / project UI schema
4. **Product Layer**
   - standalone app
   - game runtime UI
   - editor

结论：只要未来新增 React/HTML 层时，只需要增加 adapter，而不需要推翻 `WidgetTree` / invalidation / layout tree / 事件分发，那么当前方向就是正确的。

### 明确写死与不写死的边界

允许现在固定的内容：

- retained widget tree
- identity / lifecycle
- layout / paint / input / focus / dragdrop / invalidate 基础设施
- host 驱动的 tick 与 frame build

禁止现在写死的内容：

- 把 Slate 风格静态 builder 当成唯一 authoring 入口
- 把状态管理强绑定到 widget 实例字段
- 把 theme/style 写死成只服务 native C++ theme key
- 把 `construct()` 流程写成唯一上层 component 模型
- 把事件 / 属性 / 样式解析直接耦合到具体控件类，而不是抽象成 runtime capability

架构原则：

> `UICompoundWidget` 是 native retained composition primitive，不是唯一 declarative programming model。

### 对 React-like / HTML/CSS/JS 的兼容目标

- React-like：`component -> intermediate tree -> retained widget tree`，state change 通过 diff/patch/invalidate 投影到底层 runtime。
- HTML/CSS/JS：`DOM tree + style tree/cascade + layout tree + event bridge -> retained runtime projection`。
- 这两类上层共享 runtime kernel，但**不要求**复用 native builder 语法。

### 当前最容易把未来锁死的 5 个点

- 把 `Construct.h` 当所有声明式 UI 的唯一底座。
- 把 `Reactive<T>` 变成 UI 更新的唯一状态入口。
- 把 drag/drop、tooltip、shortcut 继续塞进 `UIButton` / `UIText` 这类基础控件。
- 把 `UICompoundWidget` 膨胀成“框架之框架”（behavior registry / DOM host / JS 宿主都塞进去）。
- 把 style system 只做成 native theme key 映射，而没有中间样式层。

## 下一轮前置原则：Reactive 从 Widgets kernel 降级为 Binding/Dataflow 子层

`Reactive` 作为能力应保留在 GUI runtime 附近，但**不应继续被视为 Widgets kernel 本体的一部分**。runtime kernel 必须允许多种状态来源并存，而不是要求“凡是更新 UI 都必须包成 `Reactive<T>`”。

### 状态来源必须并存

- widget internal transient state
- external imperative mutation
- reactive / binding-driven state
- future adapter patch（React/HTML/document/script）

### 目录重构目标

建议长期目录：

```text
Framework/GUI/Runtime/
  Core/
    UIElement.h
    WidgetTree.h
    WidgetAttachment.h
    UIFrameSnapshot.h
    Invalidation.h
    FocusPath.h
    DragDrop.h

  Layout/
    UILayout.h
    UISlot.h
    UIBoxLayout.h
    UISplitLayout.h
    UIScrollLayout.h
    UIOverlayLayout.h

  Binding/
    Reactive.h
    ReactiveList.h
    Computed.h
    BindingContext.h
    DependencyTracker.h

  Style/
    Theme.h
    Style.h
    Brush.h
    WidgetState.h

  Behavior/
    UIBehavior.h
    DragDropBehavior.h
    TooltipBehavior.h
    ShortcutBehavior.h

  Widgets/
    CompoundWidget.h
    Controls/

  Declarative/
    Construct.h
    Builders/
    Build.h

  Adapters/
    Document/
    ReactLike/
    HtmlCss/
```

### 重构顺序

1. **第一阶段：模块拆层，不改行为**
   - `Reactive.h` 从 `Runtime/Widgets/` 移到 `Runtime/Binding/`
   - theme/style 相关逐步收到 `Style/`
   - dragdrop session/runtime 类型逐步收到 `Core/` 或 `Behavior/`
   - 先修 include 边界与模块语义，不先改业务行为

2. **第二阶段：抽薄 kernel 依赖面**
   - `UIElement` 依赖 binding/invalidation 抽象接口，而不是模板细节
   - 抽出稳定 invalidation contract（binding / widget / paint dirty / layout dirty 解耦）
   - 让不用 `Reactive<T>` 的上层也能驱动 widget

3. **第三阶段：引入 Behavior**
   - 定义 `UIBehavior`
   - 优先迁 dragdrop / tooltip / shortcut 这类横切能力
   - 禁止继续往基础控件加项目/交互专用字段

4. **第四阶段：稳定 native retained layer**
   - `UICompoundWidget`
   - `ui::compound<T>()`
   - helper builder 与 compound widget 并存

5. **第五阶段：准备 adapter 接口**
   - 先定义 adapter host / node / renderer / patch 接口
   - 不急着直接做 React/HTML，但先保证不会被 native DSL 锁死

### 额外原则

- `Construct.h` 已经有长成 god file 的风险；后续应拆成 `BuilderBase.h / ControlBuilders.h / LayoutBuilders.h / CompoundBuilder.h / Build.h`。
- React/HTML adapter 未来复用的是 runtime kernel、behavior、style resolution，而不要求共享同一个 native builder API。
- 判断标准：未来接入 React/HTML 时，如果只是“新增 adapter 层”，而不是“重写 WidgetTree”，则本轮分层成功。

## 当前主线重排（2026-08-27）

上一轮已经把工作重点从“继续迁页面功能”切回到了“先稳底层架构边界”。当前不要继续横向铺控件/Editor 功能，而是先锁定 runtime kernel / compound / behavior / binding / declarative 五层职责。

### 当前确认的底层结论

- `WidgetTree + UIElement` 是 **UI Runtime Kernel**。
- `UICompoundWidget` 是 **native retained composition primitive**，不是唯一声明式模型。
- `ui::compound<T>()` 是 native DSL 入口之一，但不是未来 React/HTML 的唯一 authoring 入口。
- `Reactive` 应降级为 binding/dataflow 子层，不继续被视为 Widgets kernel 本体。
- GameEditor 的 `EditorSurface` 方向保持，不回退到“ImGui 内嵌 panel”路线。

### 当前真正应该做的事情（按优先级）

1. **先落地 `UIBehavior` 最小模型**
   - 目标：把 drag/drop、tooltip、shortcut、gesture、accessibility 这类横切能力从基础控件中解耦出来。
   - 最小闭环只做：attach / detach 生命周期、可选 tick、可选输入 hook、可触发 invalidate。
   - 原则：禁止继续往 `UIButton` / `UIText` / `UIPanel` 直接加项目/交互专用字段。

2. **用 Workbench drag/drop demo 验证 behavior 模型**
   - 删掉 `FDemoDragItem` / `FDemoDropZone` 这类 demo 专用 widget subclass。
   - 改成应用层 helper 返回普通 builder（例如 `button.child(text)` / `panel.child(text)`）。
   - drag source / drop target 通过 behavior 挂载，而不是让基础控件知道 drag/drop 细节。
   - 这是 behavior 第一块试验田；比直接在 GameEditor 上试更安全。

3. **把 `Reactive` 从 `Widgets/` 降到 `Binding/`**
   - 第一刀先做模块拆层与 include 边界修正，不急着重写语义。
   - 目标是让 `UIElement` 依赖 binding/invalidation contract，而不是某个具体模板细节。
   - 必须保证底层允许 imperative mutation / behavior state / adapter patch / reactive binding 并存。

4. **拆 `Construct.h`，控制 declarative god file 风险**
   - 优先拆为：`BuilderBase.h / ControlBuilders.h / LayoutBuilders.h / CompoundBuilder.h / Build.h`。
   - 不要求一次全拆完，但后续新增 builder / adapter 接口不能继续堆回单头文件。

5. **暂缓继续深挖 GameEditor 剩余 ImGui 功能迁移**
   - `EditorSurface` 方向保持；不回退。
   - 但在 behavior / binding / declarative 边界稳定前，不继续大面积迁 `TypeRenderer / FilePicker / ImGuizmo / Runtime Tools`。
   - 当前策略：结构保留，功能迁移放缓。

6. **最后才准备 React-like / HTML/CSS/JS adapter 接口**
   - 先定义 adapter host / node / renderer / patch 的最小接口。
   - 成功标准：未来接入 React/HTML 时，是新增 adapter，而不是重写 `WidgetTree`。

### 当前执行主线（收敛版 todo）

- [ ] 定义 `UIBehavior` 最小接口与宿主挂载能力。
- [ ] 用 `UIBehavior` 重构 Workbench drag/drop demo。
- [ ] 将 `Reactive` 从 `Runtime/Widgets/` 拆到 `Runtime/Binding/`。
- [ ] 拆分 `Construct.h`。
- [ ] 之后再继续推进 GameEditor 剩余 ImGui 功能迁移。
- [ ] 最后预留 React/HTML adapter 接口。

### 当前不应该做的事情

- 不继续给基础控件直接加 drag/drop/tooltip/editor 专用状态。
- 不把 `UICompoundWidget` 当成唯一 component 模型。
- 不继续把新增 declarative 能力堆进 `Construct.h` 单头文件。
- 不在 behavior / binding 未稳定前继续大面积铺 Editor feature migration。
