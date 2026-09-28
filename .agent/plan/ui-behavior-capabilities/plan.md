# UIBehavior 能力化与热路径

状态：决策门已确认（2026-09-28），排在 `game-ui-script-framework` S4 之前；C1 完成，下一步 C2。
来源：`game-ui-script-framework` 性能检查点时用户指出 `UIBehavior` 权责不分、实现性能低。

## 1. 现状（2026-09-28 核对）

- `UIBehavior` 一个基类承载五类互不相关的能力，全部是虚函数、默认空实现：
  生命周期（`onAttached` / `onDetached`）、tick（`wantsTick` / `tick`）、输入三相
  （`preview` / `handle` / `bubbleInputEvent`）、动作（`onAction`）、拖放（6 个）。
  存储是一个 `vector<UIBehaviorRef>`，子类信息丢失，只能继续往基类加接口。
- `UIElement` 另有 6 个拖放虚函数（`canAcceptDrop` / `canPreviewDrop` / `onDrop` /
  `setDropHighlight` / `updateDropHover` / `onDragDetected`），只遍历 `_behaviors` 转发；
  `WidgetTree` 调这一层。每加一种能力要改两个基类，实际等同写在 `UIElement` 上。
- `FDockSpacePanelDragBehavior` / `FDockFloatingWindowPanelDragBehavior` 不重写任何方法，
  只是借行为列表挂一个无状态函数，再用手写 `findBehavior<T>`（遍历 + `dynamic_cast`）找回。
- `WidgetTree::tick` 每帧递归整棵树；每个节点先 `isVisibleInTree()`（沿父链走到根），
  再 `wantsTick()`（遍历该节点全部行为）。开销 O(节点数 × 树深)，与是否有人要 tick 无关。
- `LuaScriptingSystem::call` / `invoke` 单次调用：复制 `scriptPath`；从 `sol::function`
  重新构造 `sol::protected_function`（注册表 ref / unref）；每次 `bindSelf`。debug 构建
  400 个空 onUpdate ≈ 600 µs/帧，主要在这里。

## 2. 设计（已确认）

**枚举的是能力，不是行为种类。** 能力集合封闭：每种能力对应 GUI 里的一个派发点，新增能力
本来就要改 GUI。行为种类开放：`LuaWidgetScriptBehavior` 在 GameRuntime、编辑器行为在
GameEditor，新增行为不改引擎 GUI 源码，GUI 也不认识它们。

- 能力接口（`GUI/Widgets/UIBehavior.h`）：`IUITickable`、`IUIInputHandler`、
  `IUIActionHandler`、`IUIDragSource`、`IUIDropTarget`，各带 `kCapability`
  （`EUIBehaviorCapability`）。接口构造函数私有、只对 `UIBehaviorWith` 开放：
  绕过声明直接继承接口是编译错误，不会出现「实现了接口却没登记」的静默失效。
- `UIBehavior` 只剩所有权与生命周期（`getOwner` / `onAttached` / `onDetached`）。
- `UIBehaviorWith<Caps...>`：编译期得到能力掩码；`capabilityInterface(c)` 用 `static_cast`
  给出接口子对象，只在 `addBehavior` / `removeBehavior` 时调用。派发无 RTTI。
- `UIElement`：`_behaviors`（所有权与顺序）+ 按能力的类型化索引（无行为的控件只有一个空
  指针）。`behaviorsOf<I>()` 是派发入口；同一能力内按挂上顺序。
- 拖放查询是基于索引的自由函数（`acceptsDrop` / `previewsDrop` / `dropOnto` /
  `highlightDrop` / `hoverDrop` / `detectDrag`），`UIElement` 不再有拖放虚函数。
- **同类行为唯一**（B4）：一个控件每种具体类型的行为至多一个，`addBehavior` 拒绝第二个
  （返回 false + ERROR）。因此 `findBehavior<T>()`（精确类型，`typeid`，非热路径）结果唯一；
  GameRuntime 的 `scriptSelfOf` 与 `animate()` 用它，取代手写 `dynamic_cast` 遍历。
- 需要多份工作的行为在内部持有多份：tween 拆成 `UITween`（一个时钟 N 条轨道，不是行为）与
  `UIAnimatorBehavior`（每控件一个，按创建顺序 tick 其 tween，丢弃播完且无外部句柄的 tween）；
  `animate(widget, d)` 找到或创建 animator 再加一个 tween。一个控件至多一个 `script.lua`，
  多写的由 `LuaWidgetScripts::activate` 告警忽略。
- tick 登记（C2）：`IUITickable` 与控件自身 tick 在挂接时登记到树、脱离时注销；运行期开关
  取代每帧 `wantsTick()` 轮询；登记表在树结构变化时按前序重排，不对外承诺更多顺序。

## 3. 决策门

| 编号 | 问题 | 结论 |
| --- | --- | --- |
| B1 | 能力形态 | 能力接口 + 编译期声明（`UIBehaviorWith<Caps...>`），不用回调袋子、不用 `dynamic_cast` |
| B2 | tick 登记粒度 | 行为级（控件自身 tick 另算一个登记项） |
| B3 | 排期 | `game-ui-script-framework` S4 之前 |
| B4 | 同类行为是否可多个（2026-09-28 用户追加） | 不可；tween 改为 animator + 多 tween，规则对所有类型一律生效，并入 C1 |

## 4. Checkpoints

### C1 — 能力接口与按能力索引

- 接口、`UIBehaviorWith`、`UIElement` 索引；输入 / 动作 / tick / 拖放派发全部改走 `behaviorsOf<I>()`。
- 删除 `UIElement` 的 6 个拖放虚函数，改为自由函数；`WidgetTree`、`DockFloatingWindow` 与测试改调它们。
  测试里重写这些虚函数的 `UIElement` 子类改为挂放置目标行为。
- 迁移全部行为子类（GUI 内置、TreeView / SelectableRow / Dock、GameEditor、GameRuntime、测试）。
- 两个 Dock 伪行为改为普通函数，删除两份手写 `findBehavior`。`UITabButton::onDragDetected`
  的重写随虚函数删除（按钮上没有拖动源）。
- 同类行为唯一（B4）：`addBehavior` 判重、`findBehavior<T>()`；`UITweenBehavior` 拆为
  `UITween` + `UIAnimatorBehavior`，`UISwitch` / GUIWorkbench / 测试改用 `animate()`。
- 其余语义不变；tick 仍是全树遍历（C2 处理）。
- 验收：`ya-testing`、`ya-gui-closure-test` 全绿；GUIWorkbench `--smoke-actions` PASS；
  GreedSnake runtime / editor、HelloMaterial `--exit-after-frame=60` 无 Error。
  新增 `UIBehaviorCapabilityTest`：能力按声明进入索引、移除后退出、同能力按挂上顺序派发、
  派发中被移除的动作处理者不再被调用、同类第二个被拒；
  `GuiAnimationTest.TweensOnOneWidgetShareItsSingleAnimator`。

### C2 — 树 tick 登记表

- 登记：挂接时加入、脱离时移除、运行期开关（`setTickEnabled`）增删；控件自身 `enableTick` 同一张表。
- 可见性：只 tick `isVisibleInTree()` 的登记项，按 `getVisibilityRevision()` 缓存结果。
- 顺序：树结构变化后按前序重排。
- 验收：无人 tick 时 `WidgetTree::tick` 与节点数无关（基准）；现有 tick / 动画 / 对话框测试全绿。

### C3 — Lua `call()` 热路径

- `bindChunk` 时回调存为 `sol::protected_function`；路径只在出错时取；`bindSelf` 只在 `self`
  首次绑定或宿主变化时执行。
- 验收：`TickOrderTest.*`、`GameUIScriptTest.*` 全绿；400 个世界脚本空 onUpdate 显著低于 600 µs/帧（同构建前后对比）。
