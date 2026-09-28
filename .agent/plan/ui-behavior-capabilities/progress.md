# Progress

## 2026-09-28 — 立项

完成：盘点 `UIBehavior` 能力、`WidgetTree::tick` 与 `LuaScriptingSystem::call` 的每帧开销，写入 `plan.md` §1。

未开始：全部。

待确认：`plan.md` §3 决策门 B1–B3（已确认，见 plan.md）。

## 2026-09-28 — C1 能力接口与按能力索引（含 B4 同类唯一）

完成：
- `UIBehavior` 只剩 owner + 生命周期；5 个能力接口 + `UIBehaviorWith<Caps...>`；`UIElement` 按能力索引，
  输入 / 动作 / tick / 拖放全部走 `behaviorsOf<I>()`；`emitAction` 派发前复核处理者仍在索引里。
- `UIElement` 6 个拖放虚函数删除，改为 `acceptsDrop` 等自由函数；Dock 两个伪行为改为普通函数，
  删除两份手写 `findBehavior`；`UITabButton::onDragDetected` 删除。
- B4：`addBehavior` 同类判重、`findBehavior<T>()`；tween 拆为 `UITween` + `UIAnimatorBehavior`；
  `LuaWidgetScriptBehavior` 声明 `<IUITickable, IUIActionHandler>`，第二个 `script.lua` 告警忽略，
  `scriptSelfOf` 改用 `findBehavior`。
- 验证：`ya-testing` 1385 全绿；`ya-gui-closure-test` 610 全绿；GUIWorkbench `--smoke-actions` PASS；
  GreedSnake runtime / editor、HelloMaterial `--exit-after-frame=60` 无 Error。

保留：tick 仍全树遍历（C2）。

偏离：§2 原写「不提供通用按类型查找」，因 B4 保证唯一而改为提供 `findBehavior<T>()`。

下一步：C2 树 tick 登记表。

## 2026-09-28 — C1b 拖放是行为种类；编译期种类键（B5）

完成：
- 删除 `IUIDragSource` / `IUIDropTarget`，能力只剩 Tick / Input / Action；`UIDragSourceBehavior` /
  `UIDropTargetBehavior` final + `onOwnerDetached`，GUI 内派生行为改为 installer 配置。
- `UIBehaviorWith<Self, Caps...>` 记录 `type_index_v<Self>`；`FUIBehaviorIndex.kinds` 连续键，
  `findBehavior<T>()` 返回 `T*`、判重按键，删 `typeid`；`UITween` 回指 animator 为裸指针。
- 验证：`ya-testing` 1389 全绿；`ya-gui-closure-test` 613 全绿；GUIWorkbench `--smoke-actions` PASS；
  GreedSnake runtime / editor、HelloMaterial `--exit-after-frame=60` 无 Error。

保留：tick 仍全树遍历（C2）。

偏离：无。

下一步：C2 树 tick 登记表。

## 2026-09-28 — C1c 控件委托取代动作字符串（B6）

完成：
- `MulticastDelegate::broadcast` 快照 + handle 复核，监听者可在广播中增删（含自身）。
- `UIButton::onClicked` 多播取代 `_onClick` / `_action`；33 处赋值与构建器改 `addLambda`。
- 删除 `IUIActionHandler`（能力只剩 Tick / Input）、`emitAction` / `setActionSink`、世界动作 sink、
  `invokeWorld`、`onUiAction`、Lua `Button.action`。
- Lua `Button:onClick(target, fn)` + `UIConnection`，`LuaWidgetScripts` 登记并在按钮 / UI 脚本实例 / runtime
  结束时断开；GreedSnake 直接监听 Restart / Resume / Slow / Normal / Fast，`onDestroy` 断开。
- 验证：`ya-testing` 1389 全绿；`ya-gui-closure-test` 612 全绿；GUIWorkbench `--smoke-actions` PASS；
  GreedySnake runtime / editor、HelloMaterial `--exit-after-frame=60` 无 Error、无 `ui.get` 告警。

保留：输入 / tick 派发遍历 `behaviorsOf<>` 视图，处理者在派发中移除行为仍不安全（先前已有）；tick 侧由 C2 处理，
输入侧未在本计划内。LuaEvent（脚本层任意参数委托）用户确认暂不需要。

偏离：无。

下一步：C2 树 tick 登记表。
