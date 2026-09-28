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
