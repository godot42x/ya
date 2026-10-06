# pointer reconcile 在处理处查物理鼠标（读活硬件）导致测试不确定

## 症状

`GUIWindowManagerTest` 里驱动 focus-loss / mouse-leave reconcile 的用例间歇失败：
同一份代码，有时 reconcile 收回 pointer session、有时不收，测试无法稳定复现。
真实用户操作下也偶发「焦点丢了一下一次点击就失效/双 press 被 cancel」。

## 根因

平台可能永远不投递 release（key focus 丢失、指针离开窗口），框架用
`WidgetTree::reconcilePointerButtons(buttonMask, ...)` 收回缓存 press。但
`GUIWindowManager` / `GUIAppHost` 在**处理事件时**现场查询物理鼠标状态
（`OsEventPump::queryGlobalMouse().buttonMask`）来填这个 mask。事件进队列到被处理
之间有时间差：用户（或测试）此刻可能已把按钮松开又按回去，处理处读到的
buttonMask 与事件发生那一刻的真实 held 集合不一致——reconcile 拿错事实。
对测试更致命：mask 取决于运行时机器上物理 / 注入鼠标的当下状态，断言无法确定。

「事件」是过去某刻的事实快照；「活硬件」是现在。处理历史事件却读现在的硬件，
等于把不确定的现在冒充确定的过去。

## 修法（2026-10-07，showcase-roadmap W3）

把 held 集合采样进事件本身：`WindowFocusLostEvent` / `WindowMouseLeaveEvent`
携带 `heldButtons`（构造时由 `OsEvent.cpp` 的 `heldMouseButtons()` 用
`SDL_GetGlobalMouseState` 采样，`getHeldButtons()` 读取），host 两侧
（`GUIWindowManager.cpp` / `GUIAppHost.cpp`）一律用 `event.getHeldButtons()` 调
`reconcilePointerButtons`，不再现场查询。测试
`FocusLostReconcilesAgainstTheButtonsTheEventReports` 从此确定。

## 同族未收口

- `EditorInputNode::reconcilePointerSessionsWithPlatform` 仍在处理处读活硬件；
  修它要改 `InputRouter` 的注入接口（让事件自带 held 集合），属于 editor 输入线。
- `GUIAppCrossWindowDragTest.SourceTaggedMoveHitsForeignWindowUsingGlobalMouse`
  仍依赖 warp 真实光标，间歇失败率 ~7/60，另线处理。