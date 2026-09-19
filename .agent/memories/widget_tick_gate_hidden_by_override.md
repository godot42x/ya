# widget 的 tick 门被子类 override 藏掉（behavior 静默不跑）

## 症状

把 tween 或 `UITickBehavior` 挂到一个控件上（`ya::ui::animate(compound)` 或直接
`addBehavior`），动画/刷新完全不发生，**没有任何日志、警告或断言**；而该控件自己的
`tick()` 照常执行——因为它是被别的理由（`enableTick()`）打开的同一扇门。

## 根因

`wantsTick()` 曾实现成「子类自己回答」的虚函数，而执行侧 `UIElement::tick()` 会无条件
转发 `_behaviors`。于是**门（wantsTick）与执行（tick）对同一件事给出两个答案**：
`UICompoundWidget` 的实现是 `return _bTickEnabled;`（只报自己的 flag），任何挂在 compound
上的 behavior 就永远没机会跑。第二个同族洞：15 个 editor tab / section `override tick(float)`
且不转发 `UIElement::tick(dt)`——门开了，behavior 还是拿不到帧。两者都静默。

## 修法（2026-09-20）

门归框架，且**非虚**：`UIElement::wantsTick()` = `_bTickEnabled || 任一 behavior 想跑`；
`_bTickEnabled` / `enableTick()` 从 `UICompoundWidget` 搬到 `UIElement`，compound 不再有
自己的一套。这样「每帧状态在自己类型里」（`enableTick()` + override `tick()`）与「把别人的
每帧工作挂上去」（`addBehavior`）是同一条协议的两种用法，谁也不能单方面关掉门。
测试：`WidgetTreeTest.BehaviorTicksOnAWidgetThatNeverOptedInItself`（挂 behavior、不
`enableTick`，门必须开）/ `EnableTickIsAvailableToAnyWidgetNotOnlyCompounds`（叶子控件用
同一入口）。原来两个测试替身用 `wantsTick()` override 模拟，现在改用 `enableTick()`。

**仍未做**：`tick()` 本身还是虚函数。谁 override 它又忘了转发 `UIElement::tick(dt)`，
挂在自己类型上的 behavior 依然静默丢帧。结构性修法是非虚 `tick(float)` 包装 + 虚
`onTick(float)`（tree 只调前者；前者先转发 behaviors 再调后者），代价是 15 个 editor
文件 + 3 个测试替身改名。记录在 `.agent/plan/gui-editor-structure/todo.md` C4e。

## 照这个形状继续找

凡是「tree 调虚函数、而基类实现里还要做事」的位置都是这个形状：控件 override 且不调基类，
框架那半就被吃掉，而且不报错。已知第二处：`onPointerEnter/onPointerLeave` 只跑控件自己的
虚函数，behavior 收不到（`.agent/plan/archive/gui-animation/plan.md` 末尾已记为下一刀）。

