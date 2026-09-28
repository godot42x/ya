# UIBehavior 能力拆分与热路径

状态：立项（2026-09-28），未开始；§3 决策门待用户确认。
来源：`game-ui-script-framework` 性能检查点时用户指出 `UIBehavior` 权责不分、实现性能低；
当时只修了该计划引入的三处每帧开销，本计划承接剩下的结构问题。

## 1. 现状（2026-09-28 核对）

- `UIBehavior` 一个基类承载五类互不相关的能力，全部是虚函数、默认空实现：
  生命周期（`onAttached` / `onDetached`）、tick（`wantsTick` / `tick`）、输入三相
  （`preview` / `handle` / `bubbleInputEvent`）、动作（`onAction`）、拖放（6 个）。
  子类只用其中一两类（`UIDragSourceBehavior`、`UIDropTargetBehavior`、`UITickBehavior`、
  `UITweenBehavior`、`LuaWidgetScriptBehavior`），但每条派发路径都要遍历所有行为、逐个虚调用。
- `WidgetTree::tick` 每帧递归整棵树；每个节点先调 `isVisibleInTree()`（沿父链走到根），
  再 `wantsTick()`（遍历该节点全部行为）。开销 O(节点数 × 树深)，与是否有人要 tick 无关。
- `LuaScriptingSystem::call` / `invoke` 单次调用：复制 `scriptPath`；从 `sol::function`
  重新构造 `sol::protected_function`（注册表 ref / unref）；每次调用 `bindSelf`（实体宿主每次
  重写 `self.entity`）。debug 构建 400 个空 onUpdate ≈ 600 µs/帧，主要在这里。

## 2. 方向（建议，待确认）

- 能力按需登记，而不是基类虚函数探测：行为在 `onAttached` 时向树登记它要的能力
  （tick、动作、拖放、输入），`onDetached` 注销；派发只遍历登记表。
  `UIBehavior` 只剩生命周期。
- 树 tick 注册表：按控件登记，tick 时只遍历登记项，按可见性过滤（可复用
  `WidgetTree::getVisibilityRevision()` 缓存每个登记项的可见结果），树序稳定。
- `call()` 热路径：`bindChunk` 时把回调存成 `sol::protected_function`；路径只在出错时取；
  `bindSelf` 只在 `self` 首次绑定或宿主变化时执行。

## 3. 决策门

| 编号 | 问题 | 建议 |
| --- | --- | --- |
| B1 | 能力形态：登记回调（`std::function`）还是能力接口（`IUITickable` 等，登记指针） | 能力接口 + 登记指针：可调试、无捕获生命周期问题 |
| B2 | tick 登记粒度：控件还是行为 | 行为：一个控件上多个行为各自开关 |
| B3 | 排期：插在 `game-ui-script-framework` S4 之前，还是 S7 之后 | S7 之后：S4–S7 不依赖它，先闭合验收用例 |

## 4. 验收

- 行为语义不变：现有 GUI、GameUIScript、TickOrder 测试全绿；GUIWorkbench `--smoke-actions` PASS。
- 基准（同一 debug 构建前后对比）：无人 tick 时 `WidgetTree::tick` 与节点数无关；
  400 个世界脚本空 onUpdate 显著低于 600 µs/帧。
