# UIBehavior 能力化与热路径

状态：决策门已确认（2026-09-28），排在 `game-ui-script-framework` S4 之前；C1、C1b、C1c、C1d 完成，下一步 C2。
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

- 能力接口（`GUI/Widgets/UIBehavior.h`）：`IUITickable`、`IUIInputHandler`，
  各带 `kCapability`（`EUIBehaviorCapability`）。能力只能是通用派发点（帧 / 输入路由）；
  拖、放、tween 是行为种类，不是能力（B5）；控件自身事件（点击等）是控件上的委托，不是能力（B6）。接口构造函数私有、只对 `UIBehaviorWith` 开放：
  绕过声明直接继承接口是编译错误，不会出现「实现了接口却没登记」的静默失效。
- `UIBehavior` 只剩所有权、生命周期与种类键（`getOwner` / `onAttached` / `onDetached` / `getKind`）。
- `UIBehaviorWith<Self, Caps...>`（CRTP，`Self` 必须 final）：编译期得到能力掩码与种类键
  `type_index_v<Self>`（类型名 FNV 哈希，跨模块稳定、无 RTTI）；`capabilityInterface(c)` 用
  `static_cast` 给出接口子对象，只在 `addBehavior` / `removeBehavior` 时调用。
- `UIElement`：`_behaviors`（所有权与顺序）+ `FUIBehaviorIndex`（无行为的控件只有一个空指针）：
  `kinds`（与 `_behaviors` 平行的连续种类键）与按能力的类型化列表。`behaviorsOf<I>()` 是派发入口，
  同一能力内按挂上顺序；`findBehavior<T>()` 扫连续整数键，返回 `T*`。
- 拖放是两个 final 行为种类 `UIDragSourceBehavior`（声明 `IUIInputHandler`）/
  `UIDropTargetBehavior`（无能力），回调配置 + `onOwnerDetached`；控件不再派生它们，而是在
  install 函数里配置后 `addBehavior`。树通过自由函数（`acceptsDrop` / `previewsDrop` /
  `dropOnto` / `highlightDrop` / `hoverDrop` / `detectDrag`）以 `findBehavior` 取到唯一实例，
  `UIElement` 没有拖放虚函数。
- **同类行为唯一**（B4）：一个控件每种行为至多一个，`addBehavior` 按种类键拒绝第二个
  （返回 false + ERROR）。因此 `findBehavior<T>()` 结果唯一；GameRuntime 的 `scriptSelfOf`、
  `animate()` 与拖放自由函数用它。
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
| B5 | 拖 / 放是否为能力（2026-09-28 用户追加） | 否：删除 `IUIDragSource` / `IUIDropTarget`，拖放是具体行为种类；按类型查找用 CRTP 编译期种类键 `type_index_v<Self>`，不用 `typeid`（C1b） |
| B6 | 控件事件如何到达脚本（2026-09-28 用户追加） | 删除「动作」概念（`IUIActionHandler`、`UIButton::_action` 字符串、`emitAction` / 世界 sink / `onUiAction`）；`UIButton::onClicked` 是 `MulticastDelegate<void()>`，脚本直接监听（UMG 式），不做编辑器信号槽、不做 UI↔玩法中转总线（C1c） |
| B7 | Lua 监听的生命周期边界（2026-09-28 用户追加） | 委托只持弱令牌，Lua 引用归 owner 脚本实例（`LuaScriptInstance::listeners`，随 `self` 生灭）；owner 必须是活脚本实例的 `self`，不提供无 owner 的 add；Lua 写法与 Core 一致：`event:add(self, fn)` 返回 handle，`remove(handle)` / `removeAll(self)`（C1d） |

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

### C1b — 拖放是行为种类；编译期种类键（B5）

- 删除 `IUIDragSource` / `IUIDropTarget` 与对应能力值；`UIDragSourceBehavior` / `UIDropTargetBehavior`
  改 final，方法非虚，加 `onOwnerDetached`。
- TreeView / SelectableRow / DockArea / DockFloatingWindow 的派生行为改为 friend installer 配置 stock 行为。
- `UIBehaviorWith<Self, Caps...>` 写入 `type_index_v<Self>`；`FUIBehaviorIndex.kinds` 连续键；
  `findBehavior<T>()` / 判重按键比较，删 `typeid`；`findBehavior` 返回 `T*`（`UITween` 回指 animator
  改裸指针，animator 析构时清空）。
- 验收：同 C1；`UIBehaviorCapabilityTest.DragAndDropAreKindsNotCapabilities`、
  `KindLookupStaysAlignedAfterRemoval`；`GuiAnimationTest.HeldTweenOutlivesItsWidget`。

### C1c — 控件委托取代动作字符串（B6）

- Core `MulticastDelegate::broadcast` 可重入：遍历快照，调用前复核 handle 仍在；广播中新增的监听下次生效。
- `UIButton`：删 `_action` / `_onClick`，加 `onClicked` 多播；`setOnClick` 与全部直接赋值改 `addLambda`。
- 删 `IUIActionHandler`、Action 能力、`WidgetTree::emitAction` / `setActionSink`、`GameUIHost` 世界 sink、
  `LuaScriptingSystem::invokeWorld`、Lua `onUiAction` 与 `Button.action`。
- Lua `Button:onClick(target, fn)` → `fn(target, button)`，返回 `UIConnection:disconnect()`；
  登记在 `LuaWidgetScripts`：按钮销毁、所属 UI 脚本实例释放 / 热重载换 `self`、runtime 析构时自动断开
  （runtime 先于 Lua state 析构）；世界脚本自行在 `onDestroy` 断开。
- GreedSnake：UI 文档删 `_action`，`Snake.lua` 在 `onInit` 直接监听按钮。
- 验收：同 C1；`WidgetTreeTest.ButtonClickReachesEveryListenerWhileListenersChange`；
  `GameUIScriptTest.ScriptsListenToButtonsDirectly` / `ClickListenersEndWithTheScriptOrTheRuntime` /
  `SpawnedButtonCanBeListenedToBeforeItIsAttached`。

### C1d — Lua 监听归属脚本实例（B7）

- Core `MulticastDelegate::addWeakLambda(weak_ptr owner, fn)`：`caller = owner.get()`（`removeAll` 可用），
  owner 失效后 `broadcast` 跳过并清除、`size()` 不计。
- ECS `ECS/Systems/LuaEvent.h`：`LuaListenerScope`（持 Lua 回调与 `self`，`bindScriptTable` 随每个新 `self`
  创建并写入 `self.__listeners`，`releaseLuaHandles` 释放）；`FLuaListenerToken`（委托里唯一的东西：弱 scope +
  slot，最后一份拷贝析构时释放 slot）；`TLuaEvent<Args...>`（`add` / `remove` / `removeAll`，按签名注册 Lua 类型，
  `void()` 为 `Event`）；`makeLuaEvent(owner, &T::member, name)`。
- GameRuntime：Button 句柄 `onClicked` 属性；删除 C1c 的 `Button:onClick` / `UIConnection` /
  `LuaWidgetScripts` 连接登记。GreedSnake 改 `onClicked:add(self, ...)`，删除手动断开。
- 验收：同 C1；`DelegateTest.*`；`GameUIScriptTest.ScriptsListenToButtonsDirectly`（`removeAll` / `remove`）、
  `ListenersEndWithTheirScriptInstance`（拒无 owner、控件脚本销毁、热重载、世界脚本销毁）。

### C2 — 树 tick 登记表

- 登记：挂接时加入、脱离时移除、运行期开关（`setTickEnabled`）增删；控件自身 `enableTick` 同一张表。
- 可见性：只 tick `isVisibleInTree()` 的登记项，按 `getVisibilityRevision()` 缓存结果。
- 顺序：树结构变化后按前序重排。
- 验收：无人 tick 时 `WidgetTree::tick` 与节点数无关（基准）；现有 tick / 动画 / 对话框测试全绿。

### C3 — Lua `call()` 热路径

- `bindChunk` 时回调存为 `sol::protected_function`；路径只在出错时取；`bindSelf` 只在 `self`
  首次绑定或宿主变化时执行。
- 验收：`TickOrderTest.*`、`GameUIScriptTest.*` 全绿；400 个世界脚本空 onUpdate 显著低于 600 µs/帧（同构建前后对比）。
- 范围（2026-10-02）：只管生命周期回调路径 `call(ELuaScriptCallback)`。具名调用（脚本互调、`onShow` /
  `onHide`）已合并为一个原语，归 `rpg-prototype`（评审步骤 2），本节不改它。
